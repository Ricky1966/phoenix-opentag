#!/usr/bin/env python3
"""Demone Phoenix OpenTag: dal lettore ESP32 + PN5180 a Spoolman e Moonraker.

Riceve le letture del firmware (src/transport.cpp) in due modi, anche insieme:

  --listen HOST:PORT   HTTP POST /tag con il JSON del tag (modalita' wifi del firmware)
  --serial PATH        righe "PHOENIX-TAG {json}" sulla seriale USB (modalita' usb)

Un tag SLIX2 vuoto (riga "PHOENIX-BLANK {json}" o POST /blank) non crea nulla: viene
solo proposto a Spoolman con una scansione di formato "openprinttag-blank", se la
stampante e' ferma (non in stampa, in pausa o con macro/caricamento in corso).

Per ogni tag:

  1. POST /api/v1/tag/scan su Spoolman: risolve l'UID nella bobina associata e
     notifica i browser collegati;
  2. se l'UID non e' associato a nulla e i dati OpenPrintTag bastano, crea
     fornitore, filamento e bobina (riusando quelli esistenti) e associa il tag;
  3. imposta la bobina attiva in Moonraker (POST /server/spoolman/spool_id),
     ma non durante una stampa o una pausa.

Solo libreria standard: gira con il python3 di sistema della CB1.
"""

from __future__ import annotations

import argparse
import glob
import hmac
import json
import logging
import math
import os
import re
import select
import termios
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

LINE_PREFIX = "PHOENIX-TAG "
BLANK_PREFIX = "PHOENIX-BLANK "
BLANK_FORMAT = "openprinttag-blank"  # `format` of the Spoolman scan that offers to write a blank tag
READER_ID_RE = re.compile(r"^[A-Za-z0-9._:-]{1,64}$")
DEDUP_SECONDS = 5.0
MAX_BODY = 4096
# Primo token di material_name riconosciuto come tipo di materiale in Spoolman.
KNOWN_MATERIALS = {
    "PLA", "PETG", "PET", "ASA", "ABS", "TPU", "PA", "PA6", "PA11", "PA12", "PC", "PP",
    "PVA", "HIPS", "PVB", "PCTG", "PEEK", "PEI", "PEKK", "CPE", "FLEX",
}

log = logging.getLogger("phoenix-opentag")


class ApiError(RuntimeError):
    def __init__(self, method: str, url: str, status: int, body: str) -> None:
        super().__init__(f"{method} {url} -> {status}: {body[:300]}")
        self.status = status
        self.body = body


class Api:
    """Client JSON minimale. Non invia Origin: Spoolman rifiuta le origini sconosciute."""

    def __init__(self, base: str, timeout: float = 5.0) -> None:
        self.base = base.rstrip("/")
        self.timeout = timeout

    def call(self, method: str, path: str, body: Any = None, query: dict | None = None) -> Any:
        url = self.base + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(url, data=data, method=method)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read()
        except urllib.error.HTTPError as e:
            raise ApiError(method, url, e.code, e.read().decode(errors="replace")) from None
        return json.loads(raw) if raw else None


def exact(value: str) -> str:
    """Filtro Spoolman per corrispondenza esatta (i valori tra virgolette non sono parziali)."""
    return '"' + value.replace('"', "") + '"'


def midpoint(low: Any, high: Any) -> int | None:
    values = [v for v in (low, high) if isinstance(v, int) and v > 0]
    return round(sum(values) / len(values)) if values else None


def normalize_event(event: dict) -> dict:
    """Accept validated v1 payloads and the deployed 0.4 legacy field names."""
    event = dict(event)
    if "schema_version" in event and event["schema_version"] != 1:
        raise ValueError("schema_version non supportata")
    aliases = {
        "material_name": "material", "brand_name": "brand",
        "filament_diameter": "diameter", "country_of_origin": "country",
        "nominal_netto_full_weight": "weight",
        "min_print_temperature": "nozzle_min", "max_print_temperature": "nozzle_max",
        "min_bed_temperature": "bed_min", "max_bed_temperature": "bed_max",
    }
    for source, target in aliases.items():
        if source in event:
            event[target] = event[source]
    if "color_rgba" in event:
        color = event["color_rgba"]
        if not isinstance(color, str) or not re.fullmatch(r"#[0-9a-fA-F]{8}", color):
            raise ValueError("color_rgba non valido")
        event["color"] = color[1:7]
    uid = event.get("uid")
    if not isinstance(uid, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{16}|(?:[0-9a-fA-F]{2}:){7}[0-9a-fA-F]{2})", uid):
        raise ValueError("uid NFC-V non valido")
    event["uid"] = uid.replace(":", "").upper()
    token = event.get("event_id")
    if token is not None and (not isinstance(token, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", token)):
        raise ValueError("event_id non valido")
    for field in ("material", "brand", "country"):
        if field in event and (not isinstance(event[field], str) or len(event[field]) > 128):
            raise ValueError(field + " non valido")
    if "color" in event and not re.fullmatch(r"[0-9a-fA-F]{6}", str(event["color"])):
        raise ValueError("color non valido")
    for field in ("density", "diameter", "weight", "nozzle_min", "nozzle_max", "bed_min", "bed_max"):
        if field not in event:
            continue
        value = event[field]
        if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or value < 0:
            raise ValueError(field + " non valido")
        if field in ("density", "diameter") and value == 0:
            raise ValueError(field + " deve essere positivo")
    return event


class Daemon:
    def __init__(self, args: argparse.Namespace) -> None:
        self.spoolman = Api(args.spoolman + "/api/v1")
        self.moonraker = Api(args.moonraker)
        self.create = not args.no_create
        self.activate = not args.no_activate
        self.default_reader = args.reader_id
        self.lock = threading.Lock()
        self.last_event = None
        self.last_result = None
        self.last_time = 0.0

    # ───────────────────────── gestione tag ─────────────────────────

    def handle(self, event: dict) -> dict:
        event = normalize_event(event)
        uid = event["uid"]
        reader = event.get("reader_id")
        if not isinstance(reader, str) or not READER_ID_RE.fullmatch(reader):
            reader = self.default_reader

        with self.lock:
            if getattr(self, "writer", None) and self.writer.busy:
                return {"error": "writer session active; automatic tag handling suspended"}
            key = json.dumps(event, sort_keys=True)
            now = time.monotonic()
            if key == self.last_event and self.last_result is not None and now - self.last_time < DEDUP_SECONDS:
                return dict(self.last_result, duplicate=True)
            # Invalidate even when the intervening event fails: A-B-A is a new activation.
            self.last_event, self.last_result = key, None
            result = self._process(uid, reader, event)
            self.last_time = time.monotonic()
            self.last_result = result
            return result

    def _printer_busy(self) -> str | None:
        """Why the reader must stay disarmed, or None. Fails closed when Moonraker is unreachable."""
        try:
            status = self.moonraker.call(
                "GET", "/printer/objects/query", query={"print_stats": "", "idle_timeout": ""}
            )["result"]["status"]
            if status["print_stats"]["state"] in ("printing", "paused"):
                return "stampante in stato " + status["print_stats"]["state"]
            # Klipper reports "Printing" while any G-code runs: filament load/unload, heating, macros.
            if status["idle_timeout"]["state"] == "Printing":
                return "stampante occupata (macro o caricamento filamento in corso)"
        except (ApiError, OSError, KeyError, ValueError, TypeError) as e:
            log.error("Moonraker: %s", e)
            return "Moonraker non raggiungibile"
        return None

    def handle_blank(self, event: dict) -> dict:
        """A blank tag is on the reader: offer it to Spoolman, which asks the user before writing."""
        event = normalize_event(event)
        uid = event["uid"]
        reader = event.get("reader_id")
        if not isinstance(reader, str) or not READER_ID_RE.fullmatch(reader):
            reader = self.default_reader
        with self.lock:
            if getattr(self, "writer", None) and self.writer.busy:
                return {"ignored": "writer session active"}
            reason = self._printer_busy()
            if reason:
                return {"ignored": reason, "uid": uid}
            scan = self.spoolman.call("POST", "/tag/scan", {
                "uid": uid, "reader_id": reader, "name": "Phoenix OpenTag", "format": BLANK_FORMAT,
            })
            return {"uid": scan["uid"], "spool_id": scan["matched_spool_id"], "blank": True, "offered": True}

    def handle_blank_logged(self, event: dict, source: str) -> dict:
        try:
            result = self.handle_blank(event)
        except (ApiError, OSError, ValueError, KeyError) as e:
            log.error("[%s] tag vuoto %s: %s", source, event.get("uid"), e)
            return {"error": str(e)}
        log.info("[%s] tag vuoto: %s", source, json.dumps(result))
        return result

    def _scan(self, uid: str, reader: str) -> dict:
        return self.spoolman.call("POST", "/tag/scan", {
            "uid": uid, "reader_id": reader, "name": "Phoenix OpenTag", "format": "openprinttag",
        })

    def _process(self, uid: str, reader: str, event: dict) -> dict:
        scan = self._scan(uid, reader)
        result: dict[str, Any] = {
            "uid": scan["uid"],
            "spool_id": scan["matched_spool_id"],
            "filament_id": scan["matched_filament_id"],
            "created": False,
            "active": False,
        }

        if result["spool_id"] is None and result["filament_id"] is None:
            if not self.create:
                result["note"] = "tag non associato (creazione disattivata)"
            else:
                missing = [k for k in ("material", "density") if not event.get(k)]
                if missing:
                    result["note"] = "tag non associato, mancano " + ", ".join(missing) + " per creare la bobina"
                else:
                    result["spool_id"] = self._create_spool(scan["uid"], event)
                    result["created"] = True
                    # Rinotifica: ora il tag risolve alla nuova bobina (Spoolman non la deduplica).
                    self._scan(uid, reader)
        elif result["filament_id"] is not None:
            result["note"] = "tag associato a un filamento, non a una bobina"

        if result["spool_id"] is not None and self.activate:
            result["active"], note = self._activate(result["spool_id"])
            if note:
                result["note"] = note
        return result

    # ───────────────────────── creazione in Spoolman ─────────────────────────

    def _vendor_id(self, brand: str) -> int:
        for v in self.spoolman.call("GET", "/vendor", query={"name": exact(brand)}):
            if v["name"].casefold() == brand.casefold():
                return v["id"]
        vendor = self.spoolman.call("POST", "/vendor", {"name": brand})
        log.info("Creato fornitore %s (id %s)", brand, vendor["id"])
        return vendor["id"]

    def _filament_id(self, event: dict) -> int:
        name = str(event["material"]).strip()
        first = name.split()[0].upper() if name.split() else ""
        material = first if first in KNOWN_MATERIALS else None
        brand = str(event.get("brand") or "").strip()
        vendor_id = self._vendor_id(brand) if brand else None
        color = str(event.get("color") or "").upper() or None
        diameter = float(event.get("diameter") or 1.75)

        query: dict[str, Any] = {"name": exact(name)}
        if vendor_id is not None:
            query["vendor.id"] = vendor_id
        for f in self.spoolman.call("GET", "/filament", query=query):
            if (
                f["name"] == name
                and (f.get("vendor") or {}).get("id") == vendor_id
                and (f.get("color_hex") or "").upper() == (color or "")
                and abs(f["diameter"] - diameter) < 0.01
            ):
                return f["id"]

        body: dict[str, Any] = {
            "name": name,
            "vendor_id": vendor_id,
            "material": material,
            "density": float(event["density"]),
            "diameter": diameter,
            "color_hex": color,
            "weight": event.get("weight") or None,
            "settings_extruder_temp": midpoint(event.get("nozzle_min"), event.get("nozzle_max")),
            "settings_bed_temp": midpoint(event.get("bed_min"), event.get("bed_max")),
            "comment": "Creato da Phoenix OpenTag",
        }
        filament = self.spoolman.call("POST", "/filament", {k: v for k, v in body.items() if v is not None})
        log.info("Creato filamento %s (id %s)", name, filament["id"])
        return filament["id"]

    def _create_spool(self, uid: str, event: dict) -> int:
        filament_id = self._filament_id(event)
        spool = self.spoolman.call("POST", "/spool", {
            "filament_id": filament_id,
            "comment": f"Creata da Phoenix OpenTag, tag {uid}",
        })
        try:
            self.spoolman.call("POST", f"/spool/{spool['id']}/tag", {"uid": uid, "format": "openprinttag"})
        except ApiError as e:
            # 409: il tag e' stato associato altrove nel frattempo. La bobina resta, senza tag.
            log.error("Associazione tag %s alla bobina %s fallita: %s", uid, spool["id"], e)
            raise
        log.info("Creata bobina %s (filamento %s) con tag %s", spool["id"], filament_id, uid)
        return spool["id"]

    # ───────────────────────── Moonraker ─────────────────────────

    def _activate(self, spool_id: int) -> tuple[bool, str | None]:
        try:
            status = self.moonraker.call("GET", "/printer/objects/query", query={"print_stats": ""})
            state = status["result"]["status"]["print_stats"]["state"]
            if state in ("printing", "paused"):
                return False, f"bobina non attivata: stampante in stato {state}"
            self.moonraker.call("POST", "/server/spoolman/spool_id", {"spool_id": spool_id})
        except (ApiError, OSError, KeyError, ValueError) as e:
            log.error("Moonraker: %s", e)
            raise OSError("bobina non attivata: Moonraker non raggiungibile, riprovare") from e
        log.info("Bobina attiva in Moonraker: %s", spool_id)
        return True, None

    def handle_logged(self, event: dict, source: str) -> dict:
        try:
            result = self.handle(event)
        except (ApiError, OSError, ValueError, KeyError) as e:
            log.error("[%s] tag %s: %s", source, event.get("uid"), e)
            return {"error": str(e)}
        if not result.get("duplicate"):
            log.info("[%s] %s", source, json.dumps(result))
        return result


# ───────────────────────── ingresso HTTP ─────────────────────────

def make_handler(daemon: Daemon) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def _reply(self, code: int, payload: dict) -> None:
            raw = json.dumps(payload).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self) -> None:  # noqa: N802
            if self.path == "/health":
                self._reply(200, {"status": "ok"})
            else:
                self._reply(404, {"error": "not found"})

        def do_POST(self) -> None:  # noqa: N802
            is_writer = self.path.startswith("/writer/")
            if is_writer:
                writer = getattr(daemon, "writer", None)
                if writer is None:
                    self._reply(404, {"error": "writer disabled"})
                    return
                # Browser requests go through Spoolman, never directly to this API.
                auth = self.headers.get("Authorization", "")
                if self.headers.get("Origin") or not hmac.compare_digest(auth, "Bearer " + writer.token):
                    self._reply(403, {"error": "writer authorization failed"})
                    return
            if self.path not in ("/tag", "/blank") and not is_writer:
                self._reply(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length") or 0)
            except ValueError:
                self._reply(400, {"error": "Content-Length non valido"})
                return
            if not 0 < length <= MAX_BODY:
                self._reply(400, {"error": "corpo mancante o troppo grande"})
                return
            try:
                event = json.loads(self.rfile.read(length))
                if not isinstance(event, dict):
                    raise ValueError
            except ValueError:
                self._reply(400, {"error": "JSON non valido"})
                return
            if is_writer:
                try:
                    result = writer.command(self.path.removeprefix("/writer/"), event)
                    self._reply(200, result)
                except (ValueError, OSError, RuntimeError) as exc:
                    self._reply(409, {"error": str(exc)})
                return
            source = f"http {self.client_address[0]}"
            if self.path == "/blank":
                result = daemon.handle_blank_logged(event, source)
            else:
                result = daemon.handle_logged(event, source)
            self._reply(502 if "error" in result else 200, result)

        def log_message(self, fmt: str, *args: Any) -> None:
            log.debug("http: " + fmt, *args)

    return Handler


# ───────────────────────── ingresso seriale ─────────────────────────

def klipper_serial_ports(config_dir: str) -> set[str]:
    """Porte seriali usate da Klipper ([mcu] serial: ...) nei .cfg della configurazione."""
    ports = set()
    for path in glob.glob(os.path.join(config_dir, "**", "*.cfg"), recursive=True):
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = re.match(r"\s*serial\s*[:=]\s*(\S+)", line)
                    if m:
                        ports.add(os.path.realpath(m.group(1)))
        except OSError:
            continue
    return ports


def open_serial(path: str, baud: int) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}")
    attrs[0] = termios.IGNPAR                                    # iflag
    attrs[1] = 0                                                 # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL      # cflag, senza HUPCL
    attrs[3] = 0                                                 # lflag: modo raw
    attrs[4] = attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def serial_loop(daemon: Daemon, path: str, baud: int) -> None:
    while True:
        try:
            fd = open_serial(path, baud)
        except OSError as e:
            log.warning("Seriale %s non disponibile (%s), riprovo tra 5 s", path, e)
            time.sleep(5)
            continue
        log.info("Seriale %s aperta a %d baud", path, baud)
        buf = b""
        writer = getattr(daemon, "writer", None)
        if writer:
            writer.online = True
        try:
            while True:
                if writer and writer.tick(fd):
                    buf = b""  # writer consumed its own response, discard pre-session partial logs
                ready, _, _ = select.select([fd], [], [], 1.0)
                if not ready:
                    continue
                chunk = os.read(fd, 1024)
                if not chunk:
                    raise OSError("dispositivo scollegato")
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", errors="replace").strip()
                    if line.startswith(LINE_PREFIX):
                        try:
                            event = json.loads(line[len(LINE_PREFIX):])
                        except ValueError:
                            log.warning("Riga non valida: %s", line)
                            continue
                        if isinstance(event, dict):
                            result = daemon.handle_logged(event, "usb")
                            token = event.get("event_id")
                            if "error" not in result and isinstance(token, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,64}", token):
                                ack = ("ack " + token + "\n").encode("ascii")
                                while ack:
                                    _, writable, _ = select.select([], [fd], [], 1.0)
                                    if not writable:
                                        raise OSError("timeout conferma USB")
                                    ack = ack[os.write(fd, ack):]
                    elif line.startswith(BLANK_PREFIX):
                        try:
                            event = json.loads(line[len(BLANK_PREFIX):])
                        except ValueError:
                            log.warning("Riga non valida: %s", line)
                            continue
                        if isinstance(event, dict):
                            daemon.handle_blank_logged(event, "usb")
                    elif line:
                        log.debug("esp32: %s", line)
                if len(buf) > 8192:
                    buf = b""
        except OSError as e:
            log.warning("Seriale %s: %s, riapro tra 5 s", path, e)
        finally:
            if writer:
                writer.online = False
            os.close(fd)
        time.sleep(5)


# ───────────────────────── main ─────────────────────────

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--listen", default="0.0.0.0:7913", help="HOST:PORT per HTTP, 'off' per disattivarlo")
    p.add_argument("--serial", help="porta seriale dell'ESP32, meglio /dev/serial/by-id/...")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--spoolman", default="http://localhost:7912")
    p.add_argument("--moonraker", default="http://localhost:7125")
    p.add_argument("--reader-id", default="phoenix-opentag", help="se il firmware non lo invia")
    p.add_argument("--klipper-config", default=os.path.expanduser("~/printer_data/config"))
    p.add_argument("--no-create", action="store_true", help="non creare bobine per tag sconosciuti")
    p.add_argument("--no-activate", action="store_true", help="non impostare la bobina attiva")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    if not READER_ID_RE.match(args.reader_id):
        p.error("--reader-id: solo [A-Za-z0-9._:-], 1-64 caratteri")
    if args.listen == "off" and not args.serial:
        p.error("serve almeno uno tra --listen e --serial")

    daemon = Daemon(args)
    writer_token = os.environ.get("PHOENIX_WRITER_TOKEN", "")
    if writer_token:
        if len(writer_token) < 32 or not args.serial:
            p.error("PHOENIX_WRITER_TOKEN requires at least 32 characters and --serial")
        from phoenix_writer_bridge import WriterBridge
        daemon.writer = WriterBridge(daemon.lock, writer_token, os.path.expanduser(
            os.environ.get("PHOENIX_TAG_BACKUP_DIR", "~/phoenix-tag-backups")))

    if args.serial:
        taken = klipper_serial_ports(args.klipper_config)
        if os.path.realpath(args.serial) in taken:
            p.error(f"{args.serial} e' una porta usata da Klipper: indica la seriale dell'ESP32")
        threading.Thread(target=serial_loop, args=(daemon, args.serial, args.baud), daemon=True).start()

    if args.listen != "off":
        host, _, port = args.listen.rpartition(":")
        server = ThreadingHTTPServer((host or "0.0.0.0", int(port)), make_handler(daemon))
        log.info("HTTP in ascolto su %s", args.listen)
        server.serve_forever()
    else:
        threading.Event().wait()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

