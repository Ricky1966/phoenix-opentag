"""Tests for orca-plugin/phoenix_spoolman.py with a stand-in `orca` module and local HTTP servers."""
import asyncio
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

        def save_config(self, config):
            try:
                json.loads(config)
            except (TypeError, ValueError):
                return False
            self.config = config
            return True

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
    orca.permission_requests = []

    def request_permissions(
        fs_read=(),
        fs_readwrite=(),
        network_http=(),
        network_socket=(),
        process=(),
        threading=False,
        network=(),
    ):
        orca.permission_requests.append({"threading": bool(threading), "network": list(network)})

    orca.request_permissions = request_permissions
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

    def current_filament_presets():
        names = orca.selected.get("names", [])
        if isinstance(names, str):
            names = [names]

        result = []
        config_names = (
            "filament_type",
            "nozzle_temperature",
            "filament_vendor",
            "filament_diameter",
            "filament_density",
        )

        for index, name in enumerate(names):
            if not name:
                result.append(None)
                continue

            def config_keys():
                return [key for key in config_names if key in orca.selected]

            def config_value(key, index=index):
                if key not in orca.selected:
                    raise KeyError(key)
                value = orca.selected[key]
                if isinstance(value, (list, tuple)):
                    if index >= len(value):
                        raise KeyError(key)
                    return value[index]
                return value

            result.append(types.SimpleNamespace(
                name=name,
                config_keys=config_keys,
                config_value=config_value,
            ))

        return result

    orca.bundle = types.SimpleNamespace(
        filaments=types.SimpleNamespace(
            preset_names=lambda: [],
            find_preset=lambda name: None,
        ),
        prints=lambda: [],
        filament_presets=["PLA"],
        current_filament_preset_names=lambda: orca.selected["names"],
        current_filament_presets=current_filament_presets,
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
                elif self.path == "/api/v1/spool":
                    body, code = [outer.spool], 200
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
        self.assertEqual(self.orca.permission_requests, [{"threading": True, "network": ["http://100.93.111.109"]}])
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



class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.plugin, _ = load_plugin()
        self.config = {
            "spoolman_url": "http://127.0.0.1:7912",
            "timeout_seconds": 2,
        }

    def test_inventory_reads_spoolman_directly(self):
        spools = [{"id": 1}, {"id": 6}]
        with mock.patch.object(
            self.plugin, "get_json", return_value=spools
        ) as request:
            result = self.plugin.fetch_spool_inventory(self.config)

        self.assertEqual(result, spools)
        request.assert_called_once_with(
            "http://127.0.0.1:7912/api/v1/spool", 2.0
        )

    def test_inventory_rejects_invalid_response(self):
        with mock.patch.object(
            self.plugin, "get_json", return_value={"error": "invalid"}
        ):
            with self.assertRaises(self.plugin.PhoenixError):
                self.plugin.fetch_spool_inventory(self.config)




    def test_inventory_payload_returns_spools(self):
        spools = [{
            "id": 6,
            "archived": False,
            "remaining_weight": 529.8,
            "filament": {
                "name": "Sovol PETG Nero",
                "vendor": {"name": "Sovol"},
                "material": "PETG",
            },
        }]
        with mock.patch.object(
            self.plugin, "fetch_spool_inventory", return_value=spools
        ):
            result = self.plugin.inventory_payload(self.config)

        self.assertEqual(result["type"], "inventory")
        self.assertTrue(result["ok"])
        self.assertEqual(len(result["spools"]), 1)
        self.assertEqual(result["spools"][0]["id"], 6)
        self.assertEqual(result["spools"][0]["remaining"], 529.8)
        self.assertFalse(result["spools"][0]["archived"])

    def test_inventory_payload_handles_spoolman_error(self):
        with mock.patch.object(
            self.plugin,
            "fetch_spool_inventory",
            side_effect=self.plugin.PhoenixError("Spoolman offline"),
        ):
            result = self.plugin.inventory_payload(self.config)

        self.assertEqual(result["type"], "inventory")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"], "Spoolman offline")




class PanelCommandQueueTests(unittest.TestCase):
    def test_attach_worker_requires_open_panel(self):
        plugin, _ = load_plugin()
        loop = object()
        queue = object()

        self.assertFalse(plugin.PANEL.attach_command_worker(loop, queue))
        self.assertIsNone(plugin.PANEL.command_loop)

        plugin.PANEL.panel = types.SimpleNamespace(is_open=lambda: True)
        self.assertTrue(plugin.PANEL.attach_command_worker(loop, queue))
        self.assertIs(plugin.PANEL.command_loop, loop)
        self.assertIs(plugin.PANEL.command_queue, queue)

    def test_detach_only_matching_worker(self):
        plugin, _ = load_plugin()
        panel = plugin.PANEL
        panel.panel = types.SimpleNamespace(is_open=lambda: True)

        old_loop = object()
        new_loop = object()
        queue = object()

        self.assertTrue(panel.attach_command_worker(old_loop, queue))
        self.assertTrue(panel.attach_command_worker(new_loop, queue))

        self.assertFalse(panel.detach_command_worker(old_loop))
        self.assertIs(panel.command_loop, new_loop)

        self.assertTrue(panel.detach_command_worker(new_loop))
        self.assertIsNone(panel.command_loop)
        self.assertIsNone(panel.command_queue)

    def test_enqueue_without_worker_returns_false(self):
        plugin, _ = load_plugin()
        self.assertFalse(plugin.PANEL.enqueue_command(
            {"type": "select_preset", "name": "Test"}
        ))


class FilamentAssociationTests(unittest.TestCase):
    def test_associations_are_saved_and_preserved(self):
        plugin, _ = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.config = json.dumps({"poll_seconds": 7})

        self.assertEqual(cap.save_filament_association(42, "PLA A"), ["PLA A"])
        self.assertEqual(cap.save_filament_association(42, "PLA B"), ["PLA A", "PLA B"])
        self.assertEqual(cap.save_filament_association(42, "PLA A"), ["PLA A", "PLA B"])

        saved = json.loads(cap.config)
        self.assertEqual(saved["poll_seconds"], 7)
        self.assertEqual(saved["filament_preset_map"], {"42": ["PLA A", "PLA B"]})

        reopened = plugin.ActiveSpoolPanel()
        reopened.config = cap.config
        self.assertEqual(reopened.filament_associations(), {"42": ["PLA A", "PLA B"]})

    def test_invalid_association_is_rejected(self):
        plugin, _ = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        with self.assertRaises(ValueError):
            cap.save_filament_association(0, "PLA")
        with self.assertRaises(ValueError):
            cap.save_filament_association(42, "")

    def test_failed_save_raises_error(self):
        plugin, _ = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        with mock.patch.object(cap, "save_config", return_value=False):
            with self.assertRaises(plugin.PhoenixError):
                cap.save_filament_association(42, "PLA")


class AutomaticPresetSelectionTests(unittest.TestCase):
    def test_auto_selection_uses_begin_update_without_commit(self):
        import asyncio
        from types import SimpleNamespace

        plugin, orca = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.config = json.dumps({
            "filament_preset_map": {"42": ["PLA A"]}
        })
        calls = []

        class Transaction:
            def set(self, *args):
                raise AssertionError("Preset parameters must not change")

            def commit(self):
                raise AssertionError("Transaction must not be committed")

        def begin_update(slot, name):
            calls.append((slot, name))
            return Transaction()

        orca.host.preset = SimpleNamespace(
            filament=SimpleNamespace(begin_update=begin_update)
        )

        async def scenario():
            with mock.patch.object(plugin.PANEL, "start"):
                task = asyncio.create_task(cap.execute())
                await asyncio.sleep(0)
                plugin.PANEL.last = {
                    "ok": True,
                    "spool": {"id": 4, "filament_id": 42},
                }
                self.assertTrue(plugin.PANEL.enqueue_command({
                    "type": "auto_select",
                    "spool_id": 4,
                    "filament_id": 42,
                }))
                await asyncio.sleep(0.02)
                plugin.PANEL.close()
                await task

        asyncio.run(scenario())
        self.assertEqual(calls, [(1, "PLA A")])


class PresetChoiceTests(unittest.TestCase):
    def test_multiple_associations_require_choice(self):
        import asyncio
        from types import SimpleNamespace

        plugin, orca = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.config = json.dumps({
            "filament_preset_map": {"42": ["PLA A", "PLA B"]}
        })
        calls = []
        orca.host.preset = SimpleNamespace(
            filament=SimpleNamespace(
                begin_update=lambda *args: calls.append(args)
            )
        )

        async def scenario():
            with mock.patch.object(plugin.PANEL, "start"):
                task = asyncio.create_task(cap.execute())
                await asyncio.sleep(0)
                plugin.PANEL.last = {
                    "ok": True,
                    "spool": {"id": 4, "filament_id": 42},
                }
                self.assertTrue(plugin.PANEL.enqueue_command({
                    "type": "auto_select",
                    "spool_id": 4,
                    "filament_id": 42,
                }))
                await asyncio.sleep(0.02)
                posts = list(orca.panels[0].posts)
                plugin.PANEL.close()
                await task
                return posts

        posts = asyncio.run(scenario())
        choices = [
            m for m in posts
            if m.get("type") == "preset_selection"
        ]
        self.assertEqual(choices[-1]["status"], "choose")
        self.assertEqual(choices[-1]["names"], ["PLA A", "PLA B"])
        self.assertEqual(calls, [])

    def test_manual_selection_targets_requested_slot(self):
        import asyncio
        from types import SimpleNamespace

        plugin, orca = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.config = json.dumps({
            "filament_preset_map": {"42": ["PLA A", "PLA B"]}
        })
        calls = []
        orca.host.preset = SimpleNamespace(
            filament=SimpleNamespace(
                begin_update=lambda *args: calls.append(args)
            )
        )

        async def scenario():
            with mock.patch.object(plugin.PANEL, "start"):
                task = asyncio.create_task(cap.execute())
                await asyncio.sleep(0)
                plugin.PANEL.last = {
                    "ok": True,
                    "spool": {"id": 4, "filament_id": 42},
                }
                plugin.PANEL.slots = [
                    {"slot": 1}, {"slot": 2}
                ]
                plugin.PANEL.available_presets = [
                    {"name": "PLA A", "compatible": True},
                    {"name": "PLA B", "compatible": True},
                ]
                self.assertTrue(plugin.PANEL.enqueue_command({
                    "type": "select_associated",
                    "filament_id": 42,
                    "preset_name": "PLA B",
                    "slot": 2,
                }))
                await asyncio.sleep(0.02)
                plugin.PANEL.close()
                await task

        asyncio.run(scenario())
        self.assertEqual(calls, [(2, "PLA B")])


class AssociationWorkerTests(unittest.TestCase):
    def test_associate_persists_and_selects_preset(self):
        import asyncio
        from types import SimpleNamespace

        plugin, orca = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.config = json.dumps({"filament_preset_map": {}})
        calls = []

        orca.host.preset = SimpleNamespace(
            filament=SimpleNamespace(
                begin_update=lambda *args: calls.append(args)
            )
        )

        async def scenario():
            with mock.patch.object(plugin.PANEL, "start"):
                task = asyncio.create_task(cap.execute())
                await asyncio.sleep(0)
                plugin.PANEL.last = {
                    "ok": True,
                    "spool": {"id": 4, "filament_id": 42},
                }
                plugin.PANEL.available_presets = [
                    {"name": "PLA A", "compatible": True}
                ]
                cap._on_message({
                    "type": "associate",
                    "filament_id": 42,
                    "preset_name": "PLA A",
                })
                await asyncio.sleep(0.05)
                posts = list(orca.panels[0].posts)
                plugin.PANEL.close()
                await task
                return posts

        posts = asyncio.run(scenario())
        self.assertEqual(
            json.loads(cap.config)["filament_preset_map"],
            {"42": ["PLA A"]},
        )
        self.assertEqual(calls, [(1, "PLA A")])
        self.assertTrue(any(
            m.get("type") == "association_result" and m.get("ok")
            for m in posts
        ))


class AssociationRemovalTests(unittest.TestCase):
    def test_removal_preserves_other_associations(self):
        plugin, _ = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.config = json.dumps({
            "poll_seconds": 7,
            "filament_preset_map": {
                "1": ["PLA A", "PLA B"],
                "6": ["PETG A"],
            },
        })

        self.assertEqual(
            cap.remove_filament_association(1, "PLA A"),
            ["PLA B"],
        )
        self.assertEqual(
            cap.remove_filament_association(1, "PLA B"),
            [],
        )

        saved = json.loads(cap.config)
        self.assertEqual(saved["filament_preset_map"], {
            "6": ["PETG A"],
        })
        self.assertEqual(saved["poll_seconds"], 7)

    def test_removal_persists_after_reload(self):
        plugin, _ = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.save_filament_association(42, "PLA A")
        cap.remove_filament_association(42, "PLA A")

        reopened = plugin.ActiveSpoolPanel()
        reopened.config = cap.config
        self.assertEqual(reopened.filament_associations(), {})

    def test_failed_removal_preserves_saved_config(self):
        plugin, _ = load_plugin()
        cap = plugin.ActiveSpoolPanel()
        cap.save_filament_association(42, "PLA A")
        original = cap.config

        with mock.patch.object(cap, "save_config", return_value=False):
            with self.assertRaises(plugin.PhoenixError):
                cap.remove_filament_association(42, "PLA A")

        self.assertEqual(cap.config, original)


class FilamentSlotIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.plugin, self.orca = load_plugin()
        self.addCleanup(self.plugin.PANEL.close)

    def test_reads_all_selected_slots(self):
        names = ["PLA Slot 1", "PETG Slot 2", "ASA Slot 3"]
        self.orca.selected["names"] = names
        slots = self.plugin.read_filament_slots()
        self.assertEqual([s["slot"] for s in slots], [1, 2, 3])
        self.assertEqual([s["name"] for s in slots], names)

    def test_panel_exposes_mappings_and_compatible_presets(self):
        panel = self.plugin.PANEL
        panel.last = {"type": "state", "ok": True, "spool": None}
        panel.slots = self.plugin.read_filament_slots()
        panel.available_presets = [
            {"name": "PLA A", "compatible": True},
            {"name": "PLA B", "compatible": False},
        ]
        panel.config = {"filament_preset_map": {"42": ["PLA A"]}}

        message = panel.message()
        self.assertEqual(message["preset_options"], ["PLA A"])
        self.assertEqual(
            message["filament_preset_map"], {"42": ["PLA A"]}
        )
        self.assertEqual(len(message["slots"]), 1)


class OrcaPresetCollectionTests(unittest.TestCase):
    def test_list_filament_presets(self):
        plugin, orca = load_plugin()

        config = {
            "filament_type": "PETG",
            "nozzle_temperature": "240,250,250,250,250,250",
            "filament_vendor": "Sovol",
            "filament_diameter": "1.75",
            "filament_density": "1.27",
        }

        preset = types.SimpleNamespace(
            is_user=lambda: True,
            is_visible=True,
            is_compatible=True,
            config_keys=lambda: list(config),
            config_value=config.get,
        )

        presets = {
            "Sovol PETG Nero @Phoenix 0.4": preset,
        }

        orca.bundle.filaments = types.SimpleNamespace(
            preset_names=lambda: list(presets) + ["Missing"],
            find_preset=presets.get,
        )

        result = plugin.list_orca_filament_presets()

        self.assertEqual(result, [{
            "name": "Sovol PETG Nero @Phoenix 0.4",
            "user": True,
            "visible": True,
            "compatible": True,
            "type": "PETG",
            "nozzle": 240.0,
            "vendor": "Sovol",
            "diameter": 1.75,
            "density": 1.27,
        }])



class PanelTests(unittest.TestCase):
    def setUp(self):
        self.plugin, self.orca = load_plugin()
        self.servers = Servers()
        self.addCleanup(self.servers.close)
        self.addCleanup(self.plugin.PANEL.close)
        self.cap = self.plugin.ActiveSpoolPanel()
        self.cap.config = json.dumps({"moonraker_url": self.servers.url, "spoolman_url": self.servers.url,
                                      "timeout_seconds": 2, "poll_seconds": 1})

        self._panel_threads = []
        self._panel_errors = []
        self._async_execute = self.cap.execute
        self.cap.execute = lambda: self.start_panel_worker()
        self.addCleanup(self.cleanup_panel_worker)

    def start_panel_worker(self):
        if self.plugin.PANEL.is_open():
            self.plugin.PANEL.panel.show()
            return self.orca.ExecutionResult.success(
                "The active spool panel is already open."
            )

        def run():
            try:
                asyncio.run(self._async_execute())
            except BaseException as exc:
                self._panel_errors.append(exc)

        thread = threading.Thread(
            target=run,
            name="test-phoenix-panel",
            daemon=True,
        )
        self._panel_threads.append(thread)
        thread.start()

        if not self.wait_for(
            lambda: self.plugin.PANEL.command_loop is not None
            or not thread.is_alive()
        ):
            raise AssertionError("Async panel worker did not start")

        if self._panel_errors:
            raise self._panel_errors[-1]

        return self.orca.ExecutionResult.success("Active spool panel opened.")

    def cleanup_panel_worker(self):
        self.plugin.PANEL.close()
        for thread in self._panel_threads:
            thread.join(5)
            if thread.is_alive():
                raise AssertionError("Async panel worker did not stop")

    def wait_for(self, predicate, seconds=5):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if predicate():
                return True
            time.sleep(0.02)
        return False


    def test_panel_shutdown_terminates_async_worker(self):
        self.cap.execute()

        panel = self.orca.panels[0]
        worker_loop = self.plugin.PANEL.command_loop

        self.assertIsNotNone(worker_loop)
        self.assertTrue(worker_loop.is_running())

        worker_thread = next(
            thread for thread in self._panel_threads
            if thread.is_alive()
        )

        panel.kw["on_close"]()

        worker_thread.join(3)

        self.assertFalse(
            worker_thread.is_alive(),
            "Async Script worker did not terminate after shutdown"
        )
        self.assertIsNone(self.plugin.PANEL.command_loop)
        self.assertIsNone(self.plugin.PANEL.command_queue)

    def test_panel_cancellation_terminates_async_worker(self):
        self.cap.execute()

        panel = self.orca.panels[0]
        worker_thread = next(
            thread for thread in self._panel_threads
            if thread.is_alive()
        )

        self.assertIsNotNone(self.plugin.PANEL.command_loop)

        self.cap.on_cancelled()

        worker_thread.join(3)

        self.assertFalse(
            worker_thread.is_alive(),
            "Async Script worker did not terminate after cancellation"
        )
        self.assertIsNone(self.plugin.PANEL.command_loop)
        self.assertIsNone(self.plugin.PANEL.command_queue)
        self.assertFalse(self.plugin.PANEL.is_open())

    def test_offline_panel_hides_stale_match(self):
        import re
        import shutil
        import subprocess

        if not shutil.which("node"):
            self.skipTest("Node.js unavailable")

        match = re.search(
            r"<script>(.*?)</script>", self.plugin.PANEL_HTML, re.S
        )
        self.assertIsNotNone(match)

        setup = r"""
const elements = {};
function element() {
  return {
    hidden: false, textContent: '', value: '', style: {},
    children: [],
    replaceChildren(...items) { this.children = items; },
    append(...items) { this.children.push(...items); },
    addEventListener() {},
    classList: { add() {} }
  };
}
const document = {
  getElementById(id) {
    return elements[id] || (elements[id] = element());
  },
  createElement() { return element(); }
};
const orca = {
  onMessage(fn) { this.receive = fn; },
  postMessage() {}
};
const setInterval = () => 0;
"""

        checks = r"""
function check(condition, message) {
  if (!condition) throw new Error(message);
}

orca.receive({
  type: 'state', ok: true,
  spool: {
    id: 6, vendor: 'Sovol', name: 'Sovol PETG Nero',
    material: 'PETG', color: '000000',
    diameter: 1.75, remaining: 527, tags: []
  },
  selected: { name: 'Sovol PETG Nero @Phoenix 0.4' },
  problems: [], notes: []
});

const previous = elements.when.textContent;
check(previous.startsWith('Updated '), 'Missing online timestamp');
check(!elements.verdict.hidden, 'Match should be visible online');

orca.receive({
  type: 'state', ok: false, error: 'No route to host'
});

check(elements.verdict.hidden, 'Stale match remains visible');
check(
  elements.when.textContent.includes(previous.slice(8)),
  'Last successful timestamp was lost'
);
check(
  elements.when.textContent.startsWith('Offline'),
  'Offline status missing'
);
check(!elements.card.hidden, 'Last known spool was lost');

orca.receive({
  type: 'state', ok: true, spool: null,
  selected: null, problems: null
});
check(elements.verdict.hidden, 'Old verdict remains without spool');
"""

        result = subprocess.run(
            ["node"],
            input=setup + match.group(1) + checks,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


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
        self.assertTrue(self.wait_for(
            lambda: any(m.get("type") == "state" for m in panel.posts)
        ))
        state = next(m for m in panel.posts if m.get("type") == "state")
        self.assertTrue(state["ok"])
        self.assertEqual(state["spool"]["id"], 4)
        self.assertEqual(state["spool"]["vendor"], "Polymaker")
        self.assertEqual(state["spool"]["color"], "C7BE5C")
        self.assertEqual(state["spool"]["tags"], ["E004010866073472"])

    def test_it_follows_a_change_of_spool(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})
        self.assertTrue(self.wait_for(
            lambda: any(
                m.get("type") == "state" and
                (m.get("spool") or {}).get("id") == 4
                for m in panel.posts
            )
        ))
        self.servers.active = None
        panel.kw["on_message"]({"type": "refresh"})
        self.assertTrue(self.wait_for(
            lambda: any(
                m.get("type") == "state" and
                m.get("ok") and m.get("spool") is None
                for m in panel.posts
            )
        ))

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

    def test_inventory_and_both_pollers_stop_on_close(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})

        self.assertTrue(self.wait_for(
            lambda: any(
                m.get("type") == "state" and m.get("ok")
                for m in panel.posts
            )
        ))
        self.assertTrue(self.wait_for(
            lambda: any(
                m.get("type") == "inventory"
                and m.get("ok")
                and m.get("spools", [{}])[0].get("id") == 4
                for m in panel.posts
            )
        ))

        active_thread = self.plugin.PANEL.thread
        inventory_thread = self.plugin.PANEL.inventory_thread

        self.assertTrue(active_thread.is_alive())
        self.assertTrue(inventory_thread.is_alive())

        panel.kw["on_close"]()
        active_thread.join(3)
        inventory_thread.join(3)

        self.assertFalse(active_thread.is_alive())
        self.assertFalse(inventory_thread.is_alive())


    def test_refresh_updates_active_spool_and_inventory(self):
        self.cap.execute()
        panel = self.orca.panels[0]
        panel.kw["on_message"]({"type": "ready"})

        self.assertTrue(self.wait_for(
            lambda: {"state", "inventory"}.issubset(
                {m.get("type") for m in panel.posts}
            )
        ))

        self.servers.spool = dict(
            self.servers.spool, remaining_weight=321.0
        )
        before = len(panel.posts)
        panel.kw["on_message"]({"type": "refresh"})

        self.assertTrue(self.wait_for(
            lambda: any(
                m.get("type") == "state"
                and m.get("ok")
                and (m.get("spool") or {}).get("remaining") == 321.0
                for m in panel.posts[before:]
            )
        ))
        self.assertTrue(self.wait_for(
            lambda: any(
                m.get("type") == "inventory"
                and m.get("ok")
                and any(
                    s.get("remaining") == 321.0
                    for s in m.get("spools", [])
                )
                for m in panel.posts[before:]
            )
        ))


    def test_immediate_reopen_restores_both_pollers(self):
        self.cap.execute()
        first = self.orca.panels[0]
        first.kw["on_message"]({"type": "ready"})

        self.assertTrue(self.wait_for(
            lambda: {"state", "inventory"}.issubset(
                {m.get("type") for m in first.posts}
            )
        ))

        first.kw["on_close"]()
        self.assertEqual(self.cap.execute().status, "success")
        second = self.orca.panels[1]
        second.kw["on_message"]({"type": "ready"})

        self.assertTrue(self.wait_for(
            lambda: {"state", "inventory"}.issubset(
                {
                    m.get("type") for m in second.posts
                    if m.get("ok")
                }
            )
        ))
        self.assertEqual(len(self.orca.panels), 2)


    def test_reopen_while_inventory_request_is_blocked(self):
        entered = threading.Event()
        release = threading.Event()
        original = self.plugin.inventory_payload

        def slow_inventory(config):
            if not entered.is_set():
                entered.set()
                release.wait(10)
            return original(config)

        old_thread = None
        try:
            with mock.patch.object(
                self.plugin, "inventory_payload",
                side_effect=slow_inventory
            ):
                self.cap.execute()
                first = self.orca.panels[0]
                first.kw["on_message"]({"type": "ready"})
                self.assertTrue(entered.wait(2))

                old_thread = self.plugin.PANEL.inventory_thread
                first.kw["on_close"]()

                self.assertEqual(self.cap.execute().status, "success")
                second = self.orca.panels[1]
                second.kw["on_message"]({"type": "ready"})

                self.assertIsNot(
                    self.plugin.PANEL.inventory_thread, old_thread
                )
                self.assertTrue(self.wait_for(
                    lambda: any(
                        m.get("type") == "inventory" and m.get("ok")
                        for m in second.posts
                    )
                ))
        finally:
            release.set()
            if old_thread is not None:
                old_thread.join(3)


    def test_reopen_creates_independent_session_events(self):
        self.cap.execute()
        first = self.orca.panels[0]
        first.kw["on_message"]({"type": "ready"})

        state = self.plugin.PANEL
        old_stop = state.stop
        old_wake = state.wake
        old_inventory_wake = state.inventory_wake

        first.kw["on_close"]()
        self.assertTrue(old_stop.is_set())

        self.assertEqual(self.cap.execute().status, "success")
        second = self.orca.panels[1]
        second.kw["on_message"]({"type": "ready"})

        self.assertIsNot(state.stop, old_stop)
        self.assertIsNot(state.wake, old_wake)
        self.assertIsNot(state.inventory_wake, old_inventory_wake)
        self.assertFalse(state.stop.is_set())
        self.assertTrue(old_stop.is_set())


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
        active_thread = self.plugin.PANEL.thread
        inventory_thread = self.plugin.PANEL.inventory_thread

        self.assertIsNotNone(active_thread)
        self.assertIsNotNone(inventory_thread)

        for junk in (None, "ready", 5, {"type": "evil"}, {}):
            panel.kw["on_message"](junk)  # invalid messages must be ignored

        self.assertIs(self.plugin.PANEL.thread, active_thread)
        self.assertIs(self.plugin.PANEL.inventory_thread, inventory_thread)
        self.assertTrue(active_thread.is_alive())
        self.assertTrue(inventory_thread.is_alive())

    def test_spool_summary_includes_nested_filament_id(self):
        summary = self.plugin.spool_summary({
            "id": 7,
            "filament": {"id": 42}
        })
        self.assertEqual(summary["id"], 7)
        self.assertEqual(summary["filament_id"], 42)

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


class InventoryOfflineUITests(unittest.TestCase):
    def test_offline_cache_and_reconnection(self):
        import re
        import shutil
        import subprocess

        if not shutil.which("node"):
            self.skipTest("Node.js non disponibile")

        plugin, _ = load_plugin()
        match = re.search(
            r"<script>(.*?)</script>", plugin.PANEL_HTML, re.S
        )
        self.assertIsNotNone(match)

        setup = """
const elements = {};
function element() {
  return {
    hidden: false, textContent: '', value: '', style: {},
    children: [],
    replaceChildren(...items) { this.children = items; },
    append(...items) { this.children.push(...items); },
    add(item) { this.children.push(item); },
    addEventListener() {},
    classList: { add() {} }
  };
}
const document = {
  getElementById(id) {
    return elements[id] || (elements[id] = element());
  },
  createElement() { return element(); }
};
function Option(text, value) {
  this.textContent = text;
  this.value = value;
}
const orca = {
  onMessage(fn) { this.receive = fn; },
  postMessage() {}
};
const setInterval = () => 0;
"""

        checks = """
function check(ok, message) {
  if (!ok) throw new Error(message);
}

orca.receive({
  type: 'inventory', ok: true,
  spools: [{
    id: 6, vendor: 'Sovol', name: 'Sovol PETG Nero',
    material: 'PETG', remaining: 527, archived: false
  }]
});

check(elements['inventory-list'].children.length === 1,
      'Inventario non visualizzato');
check(elements['inventory-status'].textContent.includes('Updated'),
      'Orario iniziale mancante');

orca.receive({
  type: 'inventory', ok: false, error: 'No route to host'
});

check(elements['inventory-list'].children.length === 1,
      'Inventario conservato perso');
check(elements['inventory-status'].textContent.startsWith('Offline'),
      'Indicazione offline mancante');
check(elements['inventory-status'].textContent.includes('1 cached spools'),
      'Conteggio bobine conservate mancante');
check(!elements['inventory-error'].hidden,
      'Errore di rete non visualizzato');

orca.receive({
  type: 'inventory', ok: true,
  spools: [{
    id: 6, vendor: 'Sovol', name: 'Sovol PETG Nero',
    material: 'PETG', remaining: 510, archived: false
  }]
});

check(elements['inventory-error'].hidden,
      'Errore ancora visibile dopo riconnessione');
check(elements['inventory-status'].textContent.includes('Updated'),
      'Stato online non ripristinato');
check(elements['inventory-list'].children[0].children[1]
      .textContent.includes('510 g'),
      'Peso aggiornato non visualizzato');
"""

        result = subprocess.run(
            ["node"],
            input=setup + match.group(1) + checks,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
