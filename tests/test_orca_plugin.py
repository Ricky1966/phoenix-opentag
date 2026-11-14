"""Tests for orca-plugin/phoenix_spoolman.py with a stand-in `orca` module and local HTTP servers."""
import importlib.util
import json
import sys
import tempfile
import threading
import time
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
    orca.panels = []

    class Panel:
        def __init__(self, **kw):
            self.kw, self.posts, self.open, self.shown = kw, [], True, 0

        def post(self, data):
            self.posts.append(data)

        def is_open(self):
            return self.open

        def show(self):
            self.shown += 1

        def close(self):
            self.open = False

    def create_dock_panel(**kw):
        orca.panels.append(Panel(**kw))
        return orca.panels[-1]

    orca.selected = {"names": ["Polymaker PolyTerra PLA @Phoenix 0.4"], "filament_type": "PLA", "nozzle_temperature": "215"}

    def full_config_value(key):
        if key not in orca.selected:
            raise KeyError(key)
        return orca.selected[key]

    orca.bundle = types.SimpleNamespace(
        prints=lambda: [],
        filament_presets=["PLA"],
        current_filament_preset_names=lambda: orca.selected["names"],
        current_filament_presets=lambda: [],
        full_config_value=full_config_value,
        _hidden=1,
    )
    orca.host = types.SimpleNamespace(
        ui=types.SimpleNamespace(
            message=lambda text, **kw: orca.messages.append((text, kw)),
            create_dock_panel=create_dock_panel,
        ),
        preset_bundle=lambda: orca.bundle,
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
        self.assertEqual(
            self.orca.registered,
            [self.plugin.ActiveSpool, self.plugin.SpoolCheck, self.plugin.ActiveSpoolPanel, self.plugin.InspectPresetApi],
        )
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



class SpoolNameTests(unittest.TestCase):
    def test_vendor_not_duplicated_in_warning(self):
        plugin, _ = load_plugin()
        summary = {
            "vendor": "Sovol",
            "name": "Sovol PETG Nero",
        }
        selected = {
            "name": "Polymaker PolyLite PETG @Phoenix 0.4"
        }
        result = plugin.compare_product(summary, selected)
        self.assertEqual(
            result,
            "the spool is Sovol PETG Nero but the selected profile is Polymaker PolyLite PETG"
        )



class SpoolNameOutputTests(unittest.TestCase):
    def setUp(self):
        self.plugin, _ = load_plugin()
        self.spool = {
            "id": 6,
            "filament": {
                "name": "Sovol PETG Nero",
                "vendor": {"name": "Sovol"},
                "material": "PETG",
            },
        }

    def test_describe_spool_no_duplicate_vendor(self):
        result = self.plugin.describe_spool(self.spool)
        self.assertIn("Spool #6: Sovol PETG Nero", result)
        self.assertNotIn("Sovol Sovol", result)

    def test_gcode_comment_no_duplicate_vendor(self):
        lines = self.plugin.stamp_lines(
            self.spool, {"filament_type": "PETG"}, []
        )
        self.assertIn("; active_spool = Sovol PETG Nero", lines)
        self.assertNotIn("; active_spool = Sovol Sovol PETG Nero", lines)


class PanelTests(unittest.TestCase):
    def setUp(self):
        self.plugin, self.orca = load_plugin()
        self.servers = Servers()
        self.addCleanup(self.servers.close)
        self.addCleanup(self.plugin.PANEL.close)
        self.cap = self.plugin.ActiveSpoolPanel()
        self.cap.config = json.dumps({"moonraker_url": self.servers.url, "spoolman_url": self.servers.url,
                                      "timeout_seconds": 2, "poll_seconds": 1})

    def wait_for(self, predicate, seconds=5):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if predicate():
                return True
            time.sleep(0.02)
        return False

    def test_execute_opens_one_docked_panel(self):
        self.assertEqual(self.cap.execute().status, "success")
        panel = self.orca.panels[0]
        self.assertEqual(panel.kw["dock"], "right")
        self.assertIn("orca.postMessage({ type: 'ready' })", panel.kw["html"])
        self.cap.execute()  # a second run shows the same panel instead of opening another
        self.assertEqual(len(self.orca.panels), 1)
        self.assertEqual(panel.shown, 1)

    def test_ready_starts_polling_and_posts_the_active_spool(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(lambda: panel.posts))
        state = panel.posts[0]
        self.assertTrue(state["ok"])
        self.assertEqual(state["spool"]["id"], 4)
        self.assertEqual(state["spool"]["vendor"], "Polymaker")
        self.assertEqual(state["spool"]["color"], "C7BE5C")
        self.assertEqual(state["spool"]["tags"], ["E004010866073472"])

    def test_it_follows_a_change_of_spool(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(lambda: panel.posts))
        self.servers.active = None
        panel.kw["on_message"]({"type": "refresh"})
        self.assertTrue(self.wait_for(lambda: panel.posts[-1]["spool"] is None))

    def test_errors_are_shown_not_raised(self):
        self.cap.config = json.dumps({"moonraker_url": "http://127.0.0.1:9", "timeout_seconds": 1})
        payload = self.plugin.panel_payload(self.cap._config())
        self.assertFalse(payload["ok"])
        self.assertIn("Cannot reach", payload["error"])

    def test_unexpected_error_is_a_state_too(self):
        def refused(*args, **kwargs):
            raise RuntimeError("blocked by the audit hook")

        with mock.patch.object(self.plugin.urllib.request, "urlopen", refused):
            payload = self.plugin.panel_payload(self.cap._config())
        self.assertFalse(payload["ok"])
        self.assertIn("RuntimeError", payload["error"])

    def test_closing_the_panel_stops_the_poller(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(lambda: panel.posts))
        thread = self.plugin.PANEL.thread
        panel.kw["on_close"]()
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertFalse(self.plugin.PANEL.is_open())
        self.cap.execute()  # reopens a fresh panel after a close
        self.assertEqual(len(self.orca.panels), 2)

    def test_unloading_stops_the_poller(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(lambda: panel.posts))
        thread = self.plugin.PANEL.thread
        self.cap.on_unload()
        thread.join(3)
        self.assertFalse(thread.is_alive())

    def test_reopened_page_gets_the_last_state_at_once(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(lambda: panel.posts))
        before = len(panel.posts)
        panel.kw["on_message"]({"type": "ready"})  # page reloaded: asks again
        self.assertGreater(len(panel.posts), before)

    def test_messages_from_the_page_are_validated(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        for junk in (None, "ready", 5, {"type": "evil"}, {}):
            panel.kw["on_message"](junk)  # no exception, no polling started
        self.assertIsNone(self.plugin.PANEL.thread)

    def test_bad_colour_never_reaches_the_page(self):
        spool = {"id": 1, "filament": {"color_hex": "red; background:url(x)"}}
        self.assertEqual(self.plugin.spool_summary(spool)["color"], "")
        self.assertEqual(self.plugin.spool_summary({"id": 1, "filament": {"color_hex": "#abcdef"}})["color"], "ABCDEF")

    def message_after(self, kind="selection"):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(lambda: panel.posts))
        before = len(panel.posts)
        panel.kw["on_message"]({"type": kind})
        self.assertGreater(len(panel.posts), before)
        return panel.posts[-1]

    def test_panel_reports_a_matching_selected_profile(self):
        state = self.message_after()
        self.assertEqual(state["selected"], {"name": "Polymaker PolyTerra PLA @Phoenix 0.4", "type": "PLA", "nozzle": 215.0})
        self.assertEqual(state["problems"], [])

    def test_panel_flags_a_mismatch_when_the_selection_changes(self):
        self.orca.selected.update(names=["Generic ASA"], filament_type="ASA", nozzle_temperature="260")
        state = self.message_after()
        self.assertEqual(state["selected"]["type"], "ASA")
        self.assertEqual(len(state["problems"]), 2)
        self.assertIn("the spool is PLA but the selected profile is ASA", state["problems"][0])

    def test_panel_copes_with_per_extruder_lists_and_missing_values(self):
        self.orca.selected.update(filament_type=["PLA", "PETG"], nozzle_temperature=["205", "230"])
        self.assertEqual(self.plugin.read_selected_filament()["type"], "PLA")
        self.assertEqual(self.plugin.read_selected_filament()["nozzle"], 205.0)
        del self.orca.selected["nozzle_temperature"]
        selected = self.plugin.read_selected_filament()
        self.assertIsNone(selected["nozzle"])
        self.assertEqual(self.plugin.compare_summary({"material": "PLA", "nozzle": 210}, selected, 25), [])

    def test_unreadable_preset_api_gives_no_verdict_but_still_shows_the_spool(self):
        def broken():
            raise RuntimeError("no host yet")

        self.orca.host.preset_bundle = broken
        state = self.message_after()
        self.assertIsNone(state["selected"])
        self.assertIsNone(state["problems"])
        self.assertTrue(state["ok"])
        self.assertEqual(state["spool"]["id"], 4)

    def test_no_verdict_without_an_active_spool(self):
        self.servers.active = None
        state = self.message_after("refresh")
        self.assertTrue(self.wait_for(lambda: self.orca.panels[0].posts[-1]["spool"] is None))
        self.assertIsNone(self.orca.panels[0].posts[-1]["problems"])

    def test_same_material_but_different_product_gets_a_note_not_an_error(self):
        # the spool is Polymaker PolyTerra PLA; the profile is another PLA
        self.orca.selected.update(names=["Spectrum HT-PLA @Phoenix 0.4"])
        state = self.message_after()
        self.assertEqual(state["problems"], [])
        self.assertEqual(len(state["notes"]), 1)
        self.assertIn("the spool is Polymaker PolyTerra PLA but the selected profile is Spectrum HT-PLA", state["notes"][0])

    def test_product_check_follows_the_names(self):
        note = self.plugin.compare_product
        polyterra = {"vendor": "Polymaker", "name": "PolyTerra"}
        self.assertIsNone(note(polyterra, {"name": "Polymaker PolyTerra PLA @Phoenix 0.4"}))
        self.assertIsNone(note(polyterra, {"name": "polyterra pla"}))      # product alone is enough
        # same vendor but another product: the product is what counts when the spool has one
        self.assertIsNotNone(note(polyterra, {"name": "Polymaker PolyLite PLA @Phoenix 0.4"}))
        self.assertIsNotNone(note(polyterra, {"name": "Spectrum HT-PLA @Phoenix 0.4"}))
        # a spool whose name is only generic words is checked on the vendor
        spectrum = {"vendor": "Spectrum Filament", "name": "The Filament PLA"}
        self.assertIsNone(note(spectrum, {"name": "Spectrum HT-PLA @Phoenix 0.4"}))
        self.assertIsNotNone(note(spectrum, {"name": "Polymaker PolyTerra PLA @Phoenix 0.4"}))
        # the printer part after "@" is ignored, and unknown names give no verdict
        self.assertIsNotNone(note(polyterra, {"name": "Generic PLA @Polymaker PolyTerra"}))
        self.assertIsNone(note(polyterra, {"name": ""}))
        self.assertIsNone(note(None, {"name": "x"}))
        self.assertIsNone(note({"vendor": "", "name": ""}, {"name": "anything"}))

    def test_a_clean_match_has_no_notes(self):
        self.assertEqual(self.message_after()["notes"], [])

    def test_no_notes_without_a_verdict(self):
        self.orca.host.preset_bundle = lambda: (_ for _ in ()).throw(RuntimeError("no host yet"))
        self.assertEqual(self.message_after()["notes"], [])

    def test_inspect_shows_the_selected_filament_first(self):
        text = self.plugin.InspectPresetApi().execute().message
        self.assertLess(text.index("current_filament_preset_names()"), text.index("Members:"))
        self.assertIn("read_selected_filament()", text)

    def test_inspect_preset_api_lists_public_members(self):
        result = self.plugin.InspectPresetApi().execute()
        self.assertEqual(result.status, "success")
        self.assertIn("filament_presets", result.message)
        self.assertNotIn("_hidden", result.message)
        self.assertEqual(len(self.orca.messages), 1)


if __name__ == "__main__":
    unittest.main()
