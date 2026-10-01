"""Tests for orca-plugin/phoenix_spoolman.py with a stand-in `orca` module and local HTTP servers."""
import importlib.util
import json
import sys
import threading
import types
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def fake_orca():
    orca = types.ModuleType("orca")
    orca.messages = []

    class Result:
        def __init__(self, status, message):
            self.status, self.message = status, message

        @classmethod
        def success(cls, message="", data=""):
            return cls("success", message)

        @classmethod
        def failure(cls, status, message, data=""):
            return cls(status, message)

    class Base:
        config = "{}"

        def get_config(self):
            return self.config

    orca.ExecutionResult = Result
    orca.PluginResult = types.SimpleNamespace(RecoverableError="recoverable")
    orca.script = types.SimpleNamespace(ScriptPluginCapabilityBase=Base)
    orca.base = type("PackageBase", (), {})
    orca.plugin = lambda cls: cls
    orca.registered = []
    orca.register_capability = orca.registered.append
    orca.host = types.SimpleNamespace(
        ui=types.SimpleNamespace(message=lambda text, **kw: orca.messages.append((text, kw)))
    )
    return orca


def load_plugin():
    sys.modules["orca"] = fake_orca()
    path = Path(__file__).resolve().parents[1] / "orca-plugin" / "phoenix_spoolman.py"
    spec = importlib.util.spec_from_file_location("phoenix_spoolman", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, sys.modules["orca"]


SPOOL = {
    "id": 4, "remaining_weight": 912.4, "tags": [{"uid": "E004010866073472"}],
    "filament": {"name": "PolyTerra PLA", "material": "PLA", "color_hex": "c7be5c", "diameter": 1.75,
                 "settings_extruder_temp": 205, "settings_bed_temp": 55, "vendor": {"name": "Polymaker"}},
}


class Servers:
    """Fake Moonraker and Spoolman on ephemeral ports."""

    def __init__(self):
        self.active = 4
        self.spool = SPOOL
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/server/spoolman/spool_id":
                    body, code = {"result": {"spool_id": outer.active}}, 200
                elif self.path == "/api/v1/spool/4":
                    body, code = outer.spool, 200
                else:
                    body, code = {"error": "nope"}, 404
                raw = json.dumps(body).encode()
                self.send_response(code)
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = "http://127.0.0.1:%d" % self.server.server_address[1]

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class PluginTests(unittest.TestCase):
    def setUp(self):
        self.plugin, self.orca = load_plugin()
        self.servers = Servers()
        self.addCleanup(self.servers.close)
        self.cap = self.plugin.ActiveSpool()
        self.cap.config = json.dumps({"moonraker_url": self.servers.url, "spoolman_url": self.servers.url,
                                      "timeout_seconds": 2})

    def test_registers_one_script_capability(self):
        self.plugin.PhoenixSpoolmanPlugin().register_capabilities()
        self.assertEqual(self.orca.registered, [self.plugin.ActiveSpool])
        self.assertEqual(self.cap.get_name(), "Active spool")

    def test_shows_active_spool(self):
        result = self.cap.execute()
        self.assertEqual(result.status, "success")
        for expected in ("Spool #4", "Polymaker PolyTerra PLA", "material PLA", "#C7BE5C", "1.75 mm",
                         "nozzle 205 C", "bed 55 C", "Remaining: 912 g", "E004010866073472"):
            self.assertIn(expected, result.message)
        self.assertEqual(self.orca.messages[0][0], result.message)

    def test_no_active_spool(self):
        self.servers.active = None
        self.assertIn("No spool is active", self.cap.execute().message)

    def test_unreachable_moonraker_is_a_recoverable_error(self):
        self.cap.config = json.dumps({"moonraker_url": "http://127.0.0.1:9", "timeout_seconds": 1})
        result = self.cap.execute()
        self.assertEqual(result.status, "recoverable")
        self.assertIn("Cannot reach", result.message)
        self.assertEqual(self.orca.messages, [])

    def test_unknown_spool_reports_http_error(self):
        self.servers.active = 99
        result = self.cap.execute()
        self.assertEqual(result.status, "recoverable")
        self.assertIn("HTTP 404", result.message)

    def test_broken_saved_config_falls_back_to_defaults(self):
        self.cap.config = "{not json"
        self.assertEqual(self.cap._config(), self.plugin.DEFAULT_CONFIG)
        self.assertEqual(self.cap.get_default_config(), self.plugin.DEFAULT_CONFIG)

    def test_sparse_spool_still_described(self):
        self.assertEqual(self.plugin.describe_spool({"id": 1}), "Spool #1: (unnamed)")


if __name__ == "__main__":
    unittest.main()
