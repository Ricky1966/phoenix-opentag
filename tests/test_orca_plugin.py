"""Tests for orca-plugin/phoenix_spoolman.py with a stand-in `orca` module and local HTTP servers."""
import importlib.util
import json
import sys
import tempfile
import threading
import types
import unittest
from unittest import mock
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

        @classmethod
        def skipped(cls, message=""):
            return cls("skipped", message)

    class Base:
        config = "{}"

        def get_config(self):
            return self.config

    orca.ExecutionResult = Result
    orca.PluginResult = types.SimpleNamespace(RecoverableError="recoverable", FatalError="fatal")
    orca.script = types.SimpleNamespace(ScriptPluginCapabilityBase=Base)
    orca.slicing = types.SimpleNamespace(
        SlicingPipelineCapabilityBase=Base,
        Step=types.SimpleNamespace(psGCodePostProcess="post", posSlice="slice"),
    )
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
        self.assertEqual(self.orca.registered, [self.plugin.ActiveSpool, self.plugin.SpoolCheck])
        self.assertEqual(self.cap.get_name(), "Active spool")
        self.assertEqual(self.plugin.SpoolCheck().get_name(), "Spool check")

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


GCODE = """; HEADER_BLOCK_START
; generated by OrcaSlicer
; HEADER_BLOCK_END
G1 X10 Y10
; CONFIG_BLOCK_START
; filament_type = %s
; nozzle_temperature = %s
; CONFIG_BLOCK_END
"""


class SpoolCheckTests(unittest.TestCase):
    def setUp(self):
        self.plugin, self.orca = load_plugin()
        self.servers = Servers()
        self.addCleanup(self.servers.close)
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.path = Path(self.dir.name) / "out.gcode"
        self.cap = self.plugin.SpoolCheck()
        self.set_config()

    def set_config(self, **extra):
        config = {"moonraker_url": self.servers.url, "spoolman_url": self.servers.url, "timeout_seconds": 2}
        config.update(extra)
        self.cap.config = json.dumps(config)

    def run_check(self, filament="PLA", nozzle="205", step="post"):
        self.path.write_text(GCODE % (filament, nozzle))
        ctx = types.SimpleNamespace(step=step, gcode_path=str(self.path))
        return self.cap.execute(ctx), self.path.read_text()

    def test_matching_profile_is_stamped_ok(self):
        result, text = self.run_check()
        self.assertEqual(result.status, "success")
        self.assertIn("matches", result.message)
        for expected in ("; active_spool_id = 4", "; active_spool = Polymaker PolyTerra PLA",
                         "; active_spool_tag = E004010866073472", "; spool check: OK"):
            self.assertIn(expected, text)
        self.assertTrue(text.startswith(GCODE % ("PLA", "205")))  # only appended, never rewritten

    def test_material_mismatch_blocks_the_export(self):
        result, text = self.run_check(filament="PETG")
        self.assertEqual(result.status, "recoverable")
        self.assertIn("the spool is PLA but the profile sliced is PETG", result.message)
        self.assertIn("on_mismatch", result.message)
        self.assertIn("; MISMATCH: the spool is PLA but the profile sliced is PETG", text)

    def test_mismatch_modes(self):
        for mode in ("block", "warn"):  # "warn" is the old name of "block"
            self.set_config(on_mismatch=mode)
            self.assertEqual(self.run_check(filament="ABS")[0].status, "recoverable")
        self.assertEqual(self.plugin.DEFAULT_CONFIG["on_mismatch"], "block")
        self.set_config(on_mismatch="ignore")
        result, text = self.run_check(filament="ABS")
        self.assertEqual(result.status, "success")
        self.assertIn("; MISMATCH", text)

    def test_temperature_tolerance(self):
        self.assertEqual(self.run_check(nozzle="225")[0].status, "success")  # +20 within 25
        result, _ = self.run_check(nozzle="260")
        self.assertEqual(result.status, "recoverable")
        self.assertIn("nozzle 260 C sliced", result.message)

    def test_multi_extruder_values_use_the_first(self):
        self.assertEqual(self.run_check(filament="PLA;PETG", nozzle="205,230")[0].status, "success")

    def test_comparison_is_case_insensitive(self):
        self.assertEqual(self.run_check(filament="pla")[0].status, "success")

    def test_missing_settings_block_does_not_fail(self):
        self.path.write_text("G1 X1\n")
        result = self.cap.execute(types.SimpleNamespace(step="post", gcode_path=str(self.path)))
        self.assertEqual(result.status, "success")
        self.assertIn("; spool check: OK", self.path.read_text())

    def test_unreachable_moonraker_skips_without_blocking(self):
        self.set_config(moonraker_url="http://127.0.0.1:9", timeout_seconds=1, on_mismatch="block")
        result, text = self.run_check()
        self.assertEqual(result.status, "skipped")
        self.assertIn("; spool check skipped: Cannot reach", text)

    def test_unexpected_error_never_blocks_the_export(self):
        def refused(*args, **kwargs):
            raise RuntimeError("blocked by the audit hook")

        with mock.patch.object(self.plugin.urllib.request, "urlopen", refused):
            result, text = self.run_check()
        self.assertEqual(result.status, "skipped")
        self.assertIn("; spool check skipped:", text)
        self.assertIn("RuntimeError", text)

    def test_idna_codec_is_preloaded(self):
        self.assertIn("encodings.idna", sys.modules)

    def test_no_active_spool(self):
        self.servers.active = None
        result, text = self.run_check()
        self.assertEqual(result.status, "success")
        self.assertIn("; no spool was active", text)

    def test_other_pipeline_steps_do_nothing(self):
        before = GCODE % ("PLA", "205")
        result, text = self.run_check(step="slice")
        self.assertEqual((result.status, text), ("success", before))

    def test_unwritable_file_is_a_recoverable_error(self):
        ctx = types.SimpleNamespace(step="post", gcode_path=str(Path(self.dir.name) / "missing" / "x.gcode"))
        self.assertEqual(self.cap.execute(ctx).status, "recoverable")


if __name__ == "__main__":
    unittest.main()
