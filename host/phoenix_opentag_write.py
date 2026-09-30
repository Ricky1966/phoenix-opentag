#!/usr/bin/env python3
"""Manual, one-shot SLIX2 writer. Stop the reader daemon before opening its port."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import select
import time

from phoenix_opentag_daemon import klipper_serial_ports, open_serial

PREFIX = b"PHOENIX-WRITE "


class WriterError(RuntimeError):
    pass


class SerialWriter:
    def __init__(self, fd: int, timeout: float = 30):
        self.fd, self.timeout, self.pending = fd, timeout, b""

    def request(self, command: str) -> dict:
        if "\n" in command or "\r" in command or len(command) > 768:
            raise WriterError("Comando non valido")
        payload = (command + "\n").encode("ascii")
        deadline = time.monotonic() + self.timeout
        while payload:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise WriterError("Timeout invio: esito sconosciuto, non ritentare automaticamente")
            try:
                count = os.write(self.fd, payload)
            except BlockingIOError:
                continue
            if count == 0:
                raise WriterError("Seriale chiusa")
            payload = payload[count:]
        deadline = time.monotonic() + self.timeout
        while True:
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                if line.startswith(PREFIX):
                    try:
                        reply = json.loads(line[len(PREFIX):])
                    except (ValueError, UnicodeError) as exc:
                        raise WriterError("Risposta writer non valida") from exc
                    if not isinstance(reply, dict) or not isinstance(reply.get("status"), str):
                        raise WriterError("Risposta writer non valida")
                    return reply
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise WriterError("Timeout: esito sconosciuto se il commit era stato inviato. Non ritentare automaticamente.")
            chunk = os.read(self.fd, 4096)
            if not chunk:
                raise WriterError("Seriale scollegata: verificare il tag prima di riprovare")
            self.pending += chunk
            if len(self.pending) > 8192:
                raise WriterError("Risposta seriale troppo lunga")


def write_image(client: SerialWriter, image: bytes, expected_uid: str, replace: bool) -> dict:
    if not 10 <= len(image) <= 320:
        raise WriterError("Serve un'immagine NFC-V completa di 10–320 byte, non il solo CBOR")
    expected_uid = expected_uid.replace(":", "").upper()
    if not re.fullmatch(r"E00401[0-9A-F]{10}", expected_uid):
        raise WriterError("UID atteso non valido (ordine E00401..., 16 cifre hex)")
    info = client.request("write-info")
    if info["status"] != "ready":
        raise WriterError("Tag non pronto: " + json.dumps(info))
    if info.get("uid") != expected_uid:
        client.request("write-cancel")
        raise WriterError("UID diverso da quello richiesto: " + str(info.get("uid")))
    if not info.get("blank") and not replace:
        client.request("write-cancel")
        raise WriterError("Tag già scritto: per sostituirlo serve --replace")
    ticket = info.get("ticket", "")
    if not isinstance(ticket, str) or not re.fullmatch(r"[0-9a-fA-F]{16}", ticket):
        raise WriterError("Ticket non valido")
    result = client.request("write-commit " + ticket + (" replace " if replace else " new ") + image.hex())
    if result.get("verified") is not True or result["status"] not in ("verified", "unchanged"):
        raise WriterError("Scrittura non confermata: " + json.dumps(result))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="porta by-id dell'ESP32, mai una MCU Klipper")
    parser.add_argument("--image", type=Path, help="immagine NFC-V .bin; omettere per sola ispezione")
    parser.add_argument("--expect-uid", help="UID esatto del tag autorizzato alla scrittura")
    parser.add_argument("--replace", action="store_true", help="autorizza la sostituzione completa dei dati esistenti")
    parser.add_argument("--klipper-config", default=os.path.expanduser("~/printer_data/config"))
    args = parser.parse_args()
    if args.image and not args.expect_uid:
        parser.error("--image richiede --expect-uid")
    if args.replace and not args.image:
        parser.error("--replace richiede --image")
    if os.path.realpath(args.serial) in klipper_serial_ports(args.klipper_config):
        parser.error("Questa porta è utilizzata da Klipper")
    fd = None
    try:
        image = args.image.read_bytes() if args.image else None
        if image is not None and not 10 <= len(image) <= 320:
            raise WriterError("Dimensione immagine non valida")
        fd = open_serial(args.serial, 115200)
        client = SerialWriter(fd)
        if image is not None:
            result = write_image(client, image, args.expect_uid, args.replace)
        else:
            result = client.request("write-info")
            client.request("write-cancel")
        print(json.dumps(result, indent=2))
        print("Rimuovi il tag dal lettore per riprendere le letture normali.")
        return 0 if result.get("status") in ("ready", "verified", "unchanged") else 1
    except (OSError, WriterError) as exc:
        print(str(exc))
        return 1
    finally:
        if fd is not None:
            os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
