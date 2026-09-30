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
MEMORY_BYTES = 320
DEFAULT_BACKUP_DIR = "~/phoenix-tag-backups"


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


def save_backup(info: dict, backup_dir: Path) -> Path:
    """Salva la memoria attuale del tag (dal `write-info`) prima di sostituirla.

    Il firmware non ha rollback: se la scrittura si interrompe, il contenuto vecchio
    e' perso. Il file contiene i 320 byte esatti del tag, ripristinabili con
    `--image <file> --expect-uid <UID> --replace` se erano un'immagine valida.
    Non sovrascrive mai un backup esistente e non lascia file parziali.
    """
    memory_hex = info.get("memory_hex")
    if not isinstance(memory_hex, str) or not re.fullmatch(r"(?:[0-9A-Fa-f]{2}){%d}" % MEMORY_BYTES, memory_hex):
        raise WriterError("Il lettore non ha fornito il contenuto del tag: nessuna copia di sicurezza possibile, "
                          "scrittura annullata e tag non modificato")
    memory = bytes.fromhex(memory_hex)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    path = None
    try:
        backup_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        for attempt in range(1, 100):
            suffix = "" if attempt == 1 else "-%d" % attempt
            candidate = backup_dir / ("%s-%s%s.bin" % (info["uid"], stamp, suffix))
            try:
                fd = os.open(str(candidate), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            except FileExistsError:
                continue
            path = candidate
            break
        if path is None:
            raise OSError("troppi file con lo stesso nome")
        with os.fdopen(fd, "wb") as handle:
            handle.write(memory)
            handle.flush()
            os.fsync(handle.fileno())
        if path.read_bytes() != memory:
            raise OSError("il file salvato non coincide con la memoria del tag")
    except OSError as exc:
        if path is not None:
            try:
                path.unlink()
            except OSError:
                pass
        raise WriterError("Copia di sicurezza non riuscita (%s): scrittura annullata, tag non modificato" % exc) from exc
    return path


def write_image(client: SerialWriter, image: bytes, expected_uid: str, replace: bool, backup_dir: Path) -> dict:
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
    backup = None
    if not info.get("blank"):
        # Prima del commit, cosi' un errore qui lascia il tag intatto.
        try:
            backup = save_backup(info, backup_dir)
        except WriterError:
            client.request("write-cancel")
            raise
    result = client.request("write-commit " + ticket + (" replace " if replace else " new ") + image.hex())
    if result.get("verified") is not True or result["status"] not in ("verified", "unchanged"):
        hint = " Copia di sicurezza del contenuto precedente: %s" % backup if backup else ""
        raise WriterError("Scrittura non confermata: " + json.dumps(result) + hint)
    if backup:
        result = dict(result, backup=str(backup))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="porta by-id dell'ESP32, mai una MCU Klipper")
    parser.add_argument("--image", type=Path, help="immagine NFC-V .bin; omettere per sola ispezione")
    parser.add_argument("--expect-uid", help="UID esatto del tag autorizzato alla scrittura")
    parser.add_argument("--replace", action="store_true", help="autorizza la sostituzione completa dei dati esistenti")
    parser.add_argument("--klipper-config", default=os.path.expanduser("~/printer_data/config"))
    parser.add_argument("--backup-dir", type=Path, default=Path(os.path.expanduser(DEFAULT_BACKUP_DIR)),
                        help="dove salvare la memoria del tag prima di sostituirla (predefinita: %s)" % DEFAULT_BACKUP_DIR)
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
            result = write_image(client, image, args.expect_uid, args.replace, args.backup_dir)
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
