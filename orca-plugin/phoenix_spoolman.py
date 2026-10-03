# /// script
# requires-python = ">=3.9"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# name = "Phoenix Spoolman"
# description = "Shows the spool that the Phoenix reader activated and checks it against the exported G-code (Moonraker + Spoolman). Standard library only."
# author = "Ricky1966"
# version = "0.5.0"
# ///
"""Phoenix Spoolman plugin for OrcaSlicer (Python plugin API, nightly builds / releases after 2.4.2).

Two capabilities:

* **Active spool** (script): asks Moonraker which spool is active (the one the Phoenix NFC reader
  set when a tag was read), then asks Spoolman for its details and shows them. Read-only.
* **Spool check** (G-code post-processing): when a G-code file is exported, compares the active
  spool with the filament profile that was sliced (material and nozzle temperature), appends a
  comment block with the result to the end of the G-code, and reports a mismatch.

The check happens when the file is exported, not when the print starts: Klipper cannot ask
Moonraker which spool is active. If the spool is changed after slicing, nothing notices.

OrcaSlicer asks for permission the first time the plugin contacts the network.
"""

import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.request

# Imported now, while the plugin loads: Python imports this codec lazily when it resolves a host name,
# and OrcaSlicer's audit hook blocks reading the interpreter's own files while a G-code file is being
# exported, which made the first network request of the post-processing step fail.
import encodings.idna  # noqa: F401

import orca

DEFAULT_CONFIG = {
    "moonraker_url": "http://192.168.1.36:7125",
    "spoolman_url": "http://192.168.1.36:7912",
    "timeout_seconds": 5,
    # Spool check only. "block" stops the export with an error dialog (OrcaSlicer aborts the export when
    # a post-processing plugin fails, so there is no softer "warning"); "ignore" exports anyway and only
    # writes the comment block. The old value "warn" is treated as "block".
    "on_mismatch": "block",
    "nozzle_temp_tolerance": 25,
    # Active spool panel only: how often the panel asks Moonraker which spool is active.
    "poll_seconds": 3,
}
CONFIG_TAIL_BYTES = 512 * 1024  # Orca writes the settings block at the end of the G-code


class PhoenixError(RuntimeError):
    """A problem to show to the user as it is."""


def get_json(url, timeout):
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        raise PhoenixError("%s answered HTTP %d" % (url, exc.code)) from None
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise PhoenixError("Cannot reach %s (%s)" % (url, getattr(exc, "reason", exc))) from None
    except Exception as exc:  # e.g. an operation refused by OrcaSlicer's audit hook
        raise PhoenixError("Request to %s failed (%s: %s)" % (url, type(exc).__name__, exc)) from None


def active_spool_id(moonraker_url, timeout):
    """Return the spool id Moonraker reports as active, or None."""
    reply = get_json(moonraker_url.rstrip("/") + "/server/spoolman/spool_id", timeout)
    try:
        spool_id = reply["result"]["spool_id"]
    except (KeyError, TypeError):
        raise PhoenixError("Unexpected Moonraker reply: %s" % json.dumps(reply)[:200]) from None
    return spool_id if isinstance(spool_id, int) and not isinstance(spool_id, bool) else None


def describe_spool(spool):
    """One readable block of text for a Spoolman spool dictionary."""
    filament = spool.get("filament") or {}
    vendor = (filament.get("vendor") or {}).get("name")
    name = " ".join(part for part in (vendor, filament.get("name")) if part) or "(unnamed)"
    lines = ["Spool #%s: %s" % (spool.get("id"), name)]
    details = []
    if filament.get("material"):
        details.append("material %s" % filament["material"])
    if filament.get("color_hex"):
        details.append("colour #%s" % str(filament["color_hex"]).lstrip("#").upper())
    if filament.get("diameter"):
        details.append("%s mm" % filament["diameter"])
    if details:
        lines.append(", ".join(details))
    temps = []
    if filament.get("settings_extruder_temp"):
        temps.append("nozzle %s C" % filament["settings_extruder_temp"])
    if filament.get("settings_bed_temp"):
        temps.append("bed %s C" % filament["settings_bed_temp"])
    if temps:
        lines.append("Temperatures: " + ", ".join(temps))
    if isinstance(spool.get("remaining_weight"), (int, float)):
        lines.append("Remaining: %.0f g" % spool["remaining_weight"])
    tags = [tag.get("uid") for tag in spool.get("tags") or [] if tag.get("uid")]
    if tags:
        lines.append("NFC tag: " + ", ".join(tags))
    return "\n".join(lines)


def fetch_active_spool(config):
    """The active Spoolman spool as a dict, or None when Moonraker has none; PhoenixError on failure."""
    timeout = float(config.get("timeout_seconds", DEFAULT_CONFIG["timeout_seconds"]))
    spool_id = active_spool_id(config.get("moonraker_url", DEFAULT_CONFIG["moonraker_url"]), timeout)
    if spool_id is None:
        return None
    base = config.get("spoolman_url", DEFAULT_CONFIG["spoolman_url"]).rstrip("/")
    return get_json("%s/api/v1/spool/%d" % (base, spool_id), timeout)


def active_spool_report(config):
    """Text describing the active spool; raises PhoenixError on any problem."""
    spool = fetch_active_spool(config)
    return "No spool is active in Moonraker." if spool is None else describe_spool(spool)


def read_gcode_settings(path):
    """First-extruder filament_type and nozzle_temperature from the settings block Orca writes
    at the end of the G-code. Missing values are simply absent from the result."""
    with open(path, "rb") as handle:
        handle.seek(0, os.SEEK_END)
        handle.seek(max(0, handle.tell() - CONFIG_TAIL_BYTES))
        tail = handle.read().decode("utf-8", errors="replace")
    settings = {}
    for key in ("filament_type", "nozzle_temperature"):
        found = re.findall(r"^; %s = ([^\r\n]*)$" % key, tail, flags=re.MULTILINE)
        if found:
            first = re.split(r"[;,]", found[-1])[0].strip().strip('"')
            if first:
                settings[key] = first
    return settings


def compare_spool(spool, settings, tolerance):
    """Human-readable problems between the active spool and the sliced filament settings."""
    problems = []
    filament = spool.get("filament") or {}
    spool_material = str(filament.get("material") or "").strip()
    sliced_material = settings.get("filament_type", "")
    if spool_material and sliced_material and spool_material.upper() != sliced_material.upper():
        problems.append("the spool is %s but the profile sliced is %s" % (spool_material, sliced_material))
    spool_temp = filament.get("settings_extruder_temp")
    try:
        sliced_temp = float(settings.get("nozzle_temperature", ""))
    except ValueError:
        sliced_temp = None
    if isinstance(spool_temp, (int, float)) and spool_temp and sliced_temp is not None:
        if abs(sliced_temp - spool_temp) > tolerance:
            problems.append("nozzle %g C sliced, the spool's usual temperature is %g C" % (sliced_temp, spool_temp))
    return problems


def stamp_lines(spool, settings, problems, failure=None):
    """Comment lines appended to the G-code (Klipper ignores them)."""
    lines = ["", "; --- Phoenix Spoolman ---"]
    if failure:
        lines.append("; spool check skipped: %s" % failure)
    elif spool is None:
        lines.append("; no spool was active in Moonraker at export time")
    else:
        filament = spool.get("filament") or {}
        lines.append("; active_spool_id = %s" % spool.get("id"))
        lines.append("; active_spool = %s" % " ".join(
            part for part in ((filament.get("vendor") or {}).get("name"), filament.get("name")) if part))
        lines.append("; active_spool_material = %s" % (filament.get("material") or ""))
        lines.append("; sliced_filament_type = %s" % settings.get("filament_type", ""))
        for tag in spool.get("tags") or []:
            if tag.get("uid"):
                lines.append("; active_spool_tag = %s" % tag["uid"])
        for problem in problems:
            lines.append("; MISMATCH: %s" % problem)
        if not problems:
            lines.append("; spool check: OK")
    lines.append("; --- end Phoenix Spoolman ---")
    return lines


def check_gcode(path, config):
    """Run the spool check on an exported G-code file. Returns (status, message) where status is
    one of "ok", "skipped", "mismatch"; the comment block is appended to the file in every case."""
    failure, spool, settings, problems = None, None, {}, []
    try:
        spool = fetch_active_spool(config)
    except PhoenixError as exc:
        failure = str(exc)
    if failure is None and spool is not None:
        settings = read_gcode_settings(path)
        problems = compare_spool(spool, settings, float(config.get("nozzle_temp_tolerance", 25)))
    with open(path, "a", encoding="utf-8") as gcode:
        gcode.write("\n".join(stamp_lines(spool, settings, problems, failure)) + "\n")
    if failure is not None:
        return "skipped", "Spool check skipped: " + failure
    if spool is None:
        return "ok", "No spool is active in Moonraker."
    if problems:
        return "mismatch", "Spool #%s does not match the sliced profile: %s" % (spool.get("id"), "; ".join(problems))
    return "ok", "Spool #%s matches the sliced profile." % spool.get("id")


class ActiveSpool(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Active spool"

    def get_default_config(self):
        return dict(DEFAULT_CONFIG)

    def _config(self):
        config = dict(DEFAULT_CONFIG)
        try:
            config.update(json.loads(self.get_config() or "{}"))
        except ValueError:
            pass  # a broken saved configuration falls back to the defaults
        return config

    def execute(self):
        try:
            text = active_spool_report(self._config())
        except PhoenixError as exc:
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, str(exc))
        orca.host.ui.message(text, title="Phoenix Spoolman", buttons="ok", icon="info")
        return orca.ExecutionResult.success(text)


class SpoolCheck(orca.slicing.SlicingPipelineCapabilityBase):
    def get_name(self):
        return "Spool check"

    def get_default_config(self):
        return dict(DEFAULT_CONFIG)

    def _config(self):
        config = dict(DEFAULT_CONFIG)
        try:
            config.update(json.loads(self.get_config() or "{}"))
        except ValueError:
            pass
        return config

    def execute(self, ctx):
        if ctx.step != orca.slicing.Step.psGCodePostProcess:
            return orca.ExecutionResult.success()
        config = self._config()
        try:
            status, message = check_gcode(ctx.gcode_path, config)
        except OSError as exc:
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, "Spool check failed: %s" % exc)
        if status == "skipped":
            return orca.ExecutionResult.skipped(message)
        if status == "mismatch" and config.get("on_mismatch") != "ignore":
            return orca.ExecutionResult.failure(
                orca.PluginResult.RecoverableError,
                message + ". To export anyway, set on_mismatch to \"ignore\" in the Spool check configuration.",
            )
        return orca.ExecutionResult.success(message)


# ───────────────────────── Active spool panel ─────────────────────────

HEX_COLOR = re.compile(r"[0-9A-Fa-f]{6}")


def spool_summary(spool):
    """The fields the panel shows, as plain JSON-able values."""
    filament = spool.get("filament") or {}
    color = str(filament.get("color_hex") or "").lstrip("#")
    return {
        "id": spool.get("id"),
        "vendor": (filament.get("vendor") or {}).get("name") or "",
        "name": filament.get("name") or "",
        "material": filament.get("material") or "",
        "color": color.upper() if HEX_COLOR.fullmatch(color) else "",
        "diameter": filament.get("diameter"),
        "nozzle": filament.get("settings_extruder_temp"),
        "bed": filament.get("settings_bed_temp"),
        "remaining": spool.get("remaining_weight") if isinstance(spool.get("remaining_weight"), (int, float)) else None,
        "tags": [t.get("uid") for t in spool.get("tags") or [] if t.get("uid")],
    }


def panel_payload(config):
    """What the panel displays right now. Never raises: a failure becomes {"ok": False, "error": ...}."""
    try:
        spool = fetch_active_spool(config)
    except PhoenixError as exc:
        return {"type": "state", "ok": False, "error": str(exc)}
    except Exception as exc:  # e.g. an operation refused by OrcaSlicer's audit hook
        return {"type": "state", "ok": False, "error": "%s: %s" % (type(exc).__name__, exc)}
    return {"type": "state", "ok": True, "spool": None if spool is None else spool_summary(spool)}


def _first_text(value):
    """The first entry of what OrcaSlicer returns for a per-extruder setting, as plain text."""
    if isinstance(value, (list, tuple)):
        value = value[0] if value else ""
    text = str(value if value is not None else "").strip().strip('"')
    return re.split(r"[;,]", text)[0].strip().strip('"')


def read_selected_filament():
    """The filament profile currently selected in OrcaSlicer, or None when it cannot be read.

    Must run on the UI thread (it reads the live host). Returns {"name", "type", "nozzle"}; "type" and
    "nozzle" are empty/None when the preset does not say.
    """
    try:
        bundle = orca.host.preset_bundle()
        names = bundle.current_filament_preset_names()
        name = _first_text(names)
        kind = nozzle = None
        try:
            kind = _first_text(bundle.full_config_value("filament_type"))
        except Exception:
            kind = ""
        try:
            nozzle = float(_first_text(bundle.full_config_value("nozzle_temperature")))
        except (TypeError, ValueError):
            nozzle = None
        except Exception:
            nozzle = None
        return {"name": name, "type": kind or "", "nozzle": nozzle}
    except Exception:
        return None


def compare_summary(summary, selected, tolerance):
    """Problems between the active spool (spool_summary) and the selected profile; None if unknown."""
    if not summary or not selected:
        return None
    problems = []
    spool_material = str(summary.get("material") or "").strip()
    chosen = str(selected.get("type") or "").strip()
    if spool_material and chosen and spool_material.upper() != chosen.upper():
        problems.append("the spool is %s but the selected profile is %s" % (spool_material, chosen))
    spool_temp, chosen_temp = summary.get("nozzle"), selected.get("nozzle")
    if isinstance(spool_temp, (int, float)) and spool_temp and isinstance(chosen_temp, (int, float)):
        if abs(chosen_temp - spool_temp) > tolerance:
            problems.append("nozzle %g C in the profile, the spool's usual temperature is %g C" % (chosen_temp, spool_temp))
    return problems


def with_selection(payload, selected, tolerance):
    """The panel message: the network state plus what is selected in OrcaSlicer and whether it matches."""
    merged = dict(payload)
    merged["selected"] = selected
    spool = payload.get("spool") if payload.get("ok") else None
    merged["problems"] = compare_summary(spool, selected, tolerance)
    return merged


PANEL_HTML = """<!doctype html>
<html><head><meta charset="utf-8"><title>Phoenix active spool</title>
<style>
body { margin: 0; padding: 12px; font-family: var(--orca-font, sans-serif); color: var(--orca-fg); background: var(--orca-bg); }
h3 { margin: 0 0 10px; font-size: 14px; }
.card { display: flex; gap: 10px; align-items: center; padding: 10px; border: 1px solid var(--orca-border); border-radius: 8px; }
.swatch { width: 38px; height: 38px; border-radius: 6px; border: 1px solid var(--orca-border); flex: none; }
.name { font-weight: 600; word-break: break-word; }
.muted { color: var(--orca-muted); font-size: 12px; }
dl { display: grid; grid-template-columns: auto 1fr; gap: 4px 10px; margin: 12px 0; font-size: 12px; }
dt { color: var(--orca-muted); } dd { margin: 0; word-break: break-all; }
.err { color: #d9534f; font-size: 12px; margin: 8px 0; }
.ok { color: #3c9d5d; font-size: 12px; margin: 8px 0; }
.verdict { margin: 10px 0; padding: 8px; border: 1px solid var(--orca-border); border-radius: 8px; font-size: 12px; }
button { margin-top: 6px; }
</style></head><body>
<h3>Active spool (Phoenix)</h3>
<div id="empty" class="muted">Waiting for the first reading...</div>
<div id="card" class="card" hidden>
  <div id="swatch" class="swatch"></div>
  <div><div id="name" class="name"></div><div id="sub" class="muted"></div></div>
</div>
<dl id="details" hidden></dl>
<div id="verdict" class="verdict" hidden></div>
<div id="err" class="err" hidden></div>
<div class="muted" id="when"></div>
<button id="refresh">Refresh now</button>
<script>
const $ = (id) => document.getElementById(id);
function row(k, v) {
  const dt = document.createElement('dt'); dt.textContent = k;
  const dd = document.createElement('dd'); dd.textContent = v;
  $('details').append(dt, dd);
}
function render(m) {
  $('when').textContent = 'Updated ' + new Date().toLocaleTimeString();
  const bad = !m.ok;
  $('err').hidden = !bad; $('err').textContent = bad ? m.error : '';
  if (bad) return;                       // keep showing the last good spool next to the error
  const s = m.spool;
  $('card').hidden = !s; $('details').hidden = !s;
  $('empty').hidden = !!s; $('empty').textContent = 'No spool is active in Moonraker.';
  if (!s) return;
  $('swatch').style.background = s.color ? '#' + s.color : 'transparent';
  $('name').textContent = '#' + s.id + ' ' + [s.vendor, s.name].filter(Boolean).join(' ');
  $('sub').textContent = [s.material, s.diameter ? s.diameter + ' mm' : ''].filter(Boolean).join(' · ');
  $('details').replaceChildren();
  if (s.color) row('Colour', '#' + s.color);
  if (s.nozzle) row('Nozzle', s.nozzle + ' C');
  if (s.bed) row('Bed', s.bed + ' C');
  if (s.remaining !== null) row('Remaining', Math.round(s.remaining) + ' g');
  if (s.tags.length) row('NFC tag', s.tags.join(', '));
  verdict(m);
}
function verdict(m) {
  const box = $('verdict');
  if (!m.spool) { box.hidden = true; return; }
  box.hidden = false;
  box.replaceChildren();
  const sel = m.selected;
  const line = document.createElement('div');
  line.textContent = 'Selected profile: ' + (sel ? (sel.name || '(unnamed)') : 'cannot be read');
  box.append(line);
  if (!sel || m.problems === null || m.problems === undefined) return;
  const res = document.createElement('div');
  res.className = m.problems.length ? 'err' : 'ok';
  res.textContent = m.problems.length ? 'Mismatch: ' + m.problems.join('; ') : 'Matches the active spool.';
  box.append(res);
}
orca.onMessage((m) => { if (m && m.type === 'state') render(m); });
$('refresh').onclick = () => orca.postMessage({ type: 'refresh' });
orca.postMessage({ type: 'ready' });     // ask for the first data: a post right after creation can be dropped
setInterval(() => orca.postMessage({ type: 'selection' }), 2000);  // the plugin reads the selected profile on the UI thread
</script></body></html>"""


class PanelState:
    """The panel handle and its poller. The authoritative state lives here, not in the page."""

    def __init__(self):
        self.panel = None
        self.stop = threading.Event()
        self.wake = threading.Event()
        self.thread = None
        self.last = None            # latest network state (without the selection)
        self.selected = None        # latest selected profile, read on the UI thread
        self.config = dict(DEFAULT_CONFIG)

    def is_open(self):
        return self.panel is not None and self.panel.is_open()

    def start(self, config):
        self.config = dict(config)
        self.stop.clear()
        self.wake.set()                      # read straight away
        if self.thread is None or not self.thread.is_alive():
            self.thread = threading.Thread(target=self._loop, name="phoenix-spoolman-panel", daemon=True)
            self.thread.start()

    def close(self):
        self.stop.set()
        self.wake.set()
        self.panel = None
        self.last = None
        self.selected = None

    def tolerance(self):
        try:
            return float(self.config.get("nozzle_temp_tolerance", DEFAULT_CONFIG["nozzle_temp_tolerance"]))
        except (TypeError, ValueError):
            return float(DEFAULT_CONFIG["nozzle_temp_tolerance"])

    def message(self):
        return with_selection(self.last, self.selected, self.tolerance())

    def _loop(self):
        while not self.stop.is_set():
            self.wake.clear()
            payload = panel_payload(self.config)
            panel = self.panel
            if panel is None or self.stop.is_set():
                return
            try:
                if panel.is_open():
                    self.last = payload
                    panel.post(self.message())
            except Exception:
                return                       # the panel went away while we were reading
            try:
                interval = max(1.0, float(self.config.get("poll_seconds", DEFAULT_CONFIG["poll_seconds"])))
            except (TypeError, ValueError):
                interval = float(DEFAULT_CONFIG["poll_seconds"])
            self.wake.wait(interval)


PANEL = PanelState()


class ActiveSpoolPanel(orca.script.ScriptPluginCapabilityBase):
    """Run it once: a panel docked beside the 3D view follows the spool the Phoenix reader activated."""

    def get_name(self):
        return "Active spool panel"

    def get_default_config(self):
        return dict(DEFAULT_CONFIG)

    def _config(self):
        config = dict(DEFAULT_CONFIG)
        try:
            config.update(json.loads(self.get_config() or "{}"))
        except ValueError:
            pass
        return config

    def on_unload(self):
        PANEL.close()

    def _on_message(self, data):
        # UI thread: keep it quick; the polling happens on the worker thread.
        kind = data.get("type") if isinstance(data, dict) else None
        if kind in ("ready", "refresh", "selection"):
            PANEL.selected = read_selected_filament()  # live host data: read here, on the UI thread
            if PANEL.last is not None and PANEL.is_open():
                PANEL.panel.post(PANEL.message())  # a reopened page, or a changed profile, shows at once
        if kind in ("ready", "refresh"):
            PANEL.start(self._config())

    def _on_close(self):
        PANEL.close()

    def execute(self):
        if PANEL.is_open():
            PANEL.panel.show()
            return orca.ExecutionResult.success("The active spool panel is already open.")
        try:
            PANEL.panel = orca.host.ui.create_dock_panel(
                html=PANEL_HTML,
                title="Phoenix active spool",
                width=320,
                height=420,
                on_message=self._on_message,
                on_close=self._on_close,
                dock="right",
            )
        except Exception as exc:
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, "Cannot open the panel: %s" % exc)
        return orca.ExecutionResult.success("Active spool panel opened.")


class InspectPresetApi(orca.script.ScriptPluginCapabilityBase):
    """Diagnostic: lists what `orca.host.preset_bundle()` exposes, to find out whether the selected
    filament preset can be read (needed to flag a mismatch with the active spool in the panel)."""

    def get_name(self):
        return "Inspect preset API"

    def execute(self):
        try:
            bundle = orca.host.preset_bundle()
        except Exception as exc:
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, "preset_bundle() failed: %s" % exc)
        members = []
        for name in sorted(n for n in dir(bundle) if not n.startswith("_")):
            try:
                member = getattr(bundle, name)
                kind = "method" if callable(member) else type(member).__name__
            except Exception as exc:
                kind = "error: %s" % exc
            members.append("  %s  (%s)" % (name, kind))
        selected = []
        for label, call in (
            ("current_filament_preset_names()", lambda: bundle.current_filament_preset_names()),
            ("current_filament_presets()", lambda: bundle.current_filament_presets()),
            ("full_config_value('filament_type')", lambda: bundle.full_config_value("filament_type")),
            ("full_config_value('nozzle_temperature')", lambda: bundle.full_config_value("nozzle_temperature")),
        ):
            try:
                value = call()
                selected.append("  %s -> %s %r" % (label, type(value).__name__, value))
            except Exception as exc:
                selected.append("  %s -> error: %s" % (label, exc))
        selected.append("  read_selected_filament() -> %r" % (read_selected_filament(),))
        lines = ["preset_bundle() -> %s" % type(bundle).__name__, "Selected filament, as the panel reads it:"]
        lines += selected + ["", "Members:"] + members
        text = "\n".join(lines)
        print(text, file=sys.stderr)         # also lands in OrcaSlicer's python log
        orca.host.ui.message(text[:3500], title="Phoenix Spoolman", buttons="ok", icon="info")
        return orca.ExecutionResult.success(text)


@orca.plugin
class PhoenixSpoolmanPlugin(orca.base):
    def register_capabilities(self):
        orca.register_capability(ActiveSpool)
        orca.register_capability(SpoolCheck)
        orca.register_capability(ActiveSpoolPanel)
        orca.register_capability(InspectPresetApi)
