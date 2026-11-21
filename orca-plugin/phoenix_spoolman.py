# /// script
# requires-python = ">=3.9"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# name = "Phoenix Spoolman"
# description = "Shows the spool that the Phoenix reader activated and checks it against the exported G-code (Moonraker + Spoolman). Standard library only."
# author = "Ricky1966"
# version = "0.6.6"
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

import queue
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
    "moonraker_url": "http://phoenix.local:7125",
    "spoolman_url": "http://phoenix.local:7912",
    "timeout_seconds": 5,
    # Spool check only. "block" stops the export with an error dialog (OrcaSlicer aborts the export when
    # a post-processing plugin fails, so there is no softer "warning"); "ignore" exports anyway and only
    # writes the comment block. The old value "warn" is treated as "block".
    "on_mismatch": "block",
    "nozzle_temp_tolerance": 25,
    # Active spool panel only: how often the panel asks Moonraker which spool is active.
    "poll_seconds": 3,
    "inventory_poll_seconds": 30,
    "auto_open_panel": True,
}
CONFIG_TAIL_BYTES = 512 * 1024  # Orca writes the settings block at the end of the G-code


class PhoenixError(RuntimeError):
    """A problem to show to the user as it is."""


class PhoenixPermissionError(PhoenixError):
    """A network operation was denied by OrcaSlicer's permissions."""


def _is_orca_audit_denial(exc):
    message = str(exc)
    return (
        isinstance(exc, PermissionError)
        and message.startswith('Plugin attempted audited operation "')
        and " without permission" in message
    )


def get_json(url, timeout):
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        raise PhoenixError("%s answered HTTP %d" % (url, exc.code)) from None
    except PermissionError as exc:
        if _is_orca_audit_denial(exc):
            raise PhoenixPermissionError("Permission denied for %s (%s)" % (url, exc)) from None
        raise PhoenixError("Cannot reach %s (%s)" % (url, exc)) from None
    except urllib.error.URLError as exc:
        if _is_orca_audit_denial(exc.reason):
            raise PhoenixPermissionError(
                "Permission denied for %s (%s)" % (url, exc.reason)
            ) from None
        raise PhoenixError("Cannot reach %s (%s)" % (url, exc.reason)) from None
    except (OSError, ValueError) as exc:
        raise PhoenixError("Cannot reach %s (%s)" % (url, exc)) from None
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


def spool_display_name(vendor, name):
    """Compose vendor and product without repeating the vendor."""
    vendor = str(vendor or "").strip()
    name = str(name or "").strip()
    if vendor and name.casefold().startswith(vendor.casefold() + " "):
        return name
    return " ".join(part for part in (vendor, name) if part)


def describe_spool(spool):
    """One readable block of text for a Spoolman spool dictionary."""
    filament = spool.get("filament") or {}
    vendor = (filament.get("vendor") or {}).get("name")
    name = spool_display_name(vendor, filament.get("name")) or "(unnamed)"
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



def fetch_spool_inventory(config):
    """Read the spool inventory directly from Spoolman."""
    timeout = float(config.get(
        "timeout_seconds", DEFAULT_CONFIG["timeout_seconds"]
    ))
    base = config.get(
        "spoolman_url", DEFAULT_CONFIG["spoolman_url"]
    ).rstrip("/")
    spools = get_json(base + "/api/v1/spool", timeout)

    if not isinstance(spools, list):
        raise PhoenixError(
            "Unexpected Spoolman inventory response: expected a list"
        )
    return spools



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
        lines.append("; active_spool = %s" % spool_display_name(
            (filament.get("vendor") or {}).get("name"), filament.get("name")))
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
        "filament_id": filament.get("id"),
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



def inventory_payload(config):
    """Build an inventory message independently of the active spool."""
    try:
        spools = fetch_spool_inventory(config)
        items = []
        for spool in spools:
            if not isinstance(spool, dict):
                raise PhoenixError("Unexpected Spoolman inventory item")
            item = spool_summary(spool)
            item["archived"] = bool(spool.get("archived"))
            items.append(item)
        return {"type": "inventory", "ok": True, "spools": items}
    except PhoenixPermissionError as exc:
        return {"type": "inventory", "ok": False, "error": str(exc), "permission_denied": True}
    except PhoenixError as exc:
        return {"type": "inventory", "ok": False, "error": str(exc)}
    except Exception as exc:
        return {
            "type": "inventory",
            "ok": False,
            "error": "%s: %s" % (type(exc).__name__, exc),
        }



def panel_payload(config):
    """What the panel displays right now. Never raises: a failure becomes {"ok": False, "error": ...}."""
    try:
        spool = fetch_active_spool(config)
    except PhoenixPermissionError as exc:
        return {"type": "state", "ok": False, "error": str(exc), "permission_denied": True}
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
    """Read the first filament preset currently assigned in OrcaSlicer.
    Must run on the UI thread.
    """
    try:
        bundle = orca.host.preset_bundle()
        presets = bundle.current_filament_presets()
        if not presets or presets[0] is None:
            return None

        preset = presets[0]
        keys = set(preset.config_keys())

        def read_config(key):
            if key not in keys:
                return None
            return preset.config_value(key)

        name = str(preset.name or "")
        kind = _first_text(read_config("filament_type"))

        try:
            nozzle = float(_first_text(read_config("nozzle_temperature")))
        except (TypeError, ValueError):
            nozzle = None

        return {
            "name": name,
            "type": kind or "",
            "nozzle": nozzle,
        }
    except Exception:
        return None


def read_filament_slots():
    """Read all Orca filament slots on the UI thread."""
    try:
        presets = orca.host.preset_bundle().current_filament_presets()
        result = []
        for index, preset in enumerate(presets, 1):
            if preset is None:
                result.append({"slot": index, "name": "", "type": "", "nozzle": None})
                continue
            keys = set(preset.config_keys())
            def value(key):
                return preset.config_value(key) if key in keys else None
            kind = _first_text(value("filament_type")) or ""
            try:
                nozzle = float(_first_text(value("nozzle_temperature")))
            except (TypeError, ValueError):
                nozzle = None
            result.append({
                "slot": index,
                "name": str(preset.name or ""),
                "type": kind,
                "nozzle": nozzle,
            })
        return result
    except Exception:
        return []


def list_orca_filament_presets():
    """Read loaded filament presets and their material properties.
    Must run on Orca's UI thread.
    """
    collection = orca.host.preset_bundle().filaments
    presets = []

    for name in collection.preset_names():
        preset = collection.find_preset(name)
        if preset is None:
            continue

        try:
            keys = set(preset.config_keys())
        except Exception:
            keys = set()

        def read_config(key):
            if key not in keys:
                return None
            try:
                return preset.config_value(key)
            except Exception:
                return None

        def text_config(key):
            value = read_config(key)
            return _first_text(value) if value is not None else ""

        def number_config(key):
            value = text_config(key)
            try:
                return float(value) if value else None
            except (TypeError, ValueError):
                return None

        presets.append({
            "name": str(name),
            "user": bool(preset.is_user()),
            "visible": bool(preset.is_visible),
            "compatible": bool(preset.is_compatible),
            "type": text_config("filament_type"),
            "nozzle": number_config("nozzle_temperature"),
            "vendor": text_config("filament_vendor"),
            "diameter": number_config("filament_diameter"),
            "density": number_config("filament_density"),
        })

    return presets


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


GENERIC_WORDS = {
    "the", "and", "filament", "filaments", "3d", "printing", "series", "spool",
    "pla", "petg", "abs", "asa", "tpu", "pet", "pc", "pa", "pp", "pva", "hips", "plus", "pro",
}


def _tokens(text):
    """Lower-case alphanumeric words of at least three characters, without generic filler words."""
    words = re.findall(r"[a-z0-9]+", str(text or "").lower())
    return {w for w in words if len(w) >= 3 and w not in GENERIC_WORDS}


def compare_product(summary, selected):
    """A note when the selected profile looks like a different brand or product than the active spool.

    This is a hint, not an error: profile names are free text. The part after "@" (the printer) is ignored.
    Returns None when the profile name is unknown or nothing suggests a difference.
    """
    if not summary or not selected or not selected.get("name"):
        return None
    profile_name = str(selected["name"]).split("@")[0]
    profile = _tokens(profile_name)
    vendor, product = _tokens(summary.get("vendor")), _tokens(summary.get("name"))
    # A specific product name (PolyTerra) is what identifies the spool; when the filament name is only
    # generic words ("The Filament PLA") the vendor is all there is to compare.
    if product:
        same = bool(product & profile)
    else:
        same = not vendor or bool(vendor & profile)
    if same:
        return None
    spool = spool_display_name(summary.get("vendor"), summary.get("name"))
    return "the spool is %s but the selected profile is %s" % (spool, profile_name.strip())


def with_selection(payload, selected, tolerance):
    """The panel message: the network state plus what is selected in OrcaSlicer and whether it matches."""
    merged = dict(payload)
    merged["selected"] = selected
    spool = payload.get("spool") if payload.get("ok") else None
    merged["problems"] = compare_summary(spool, selected, tolerance)
    merged["notes"] = [] if merged["problems"] is None else [n for n in [compare_product(spool, selected)] if n]
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
.warn { color: #d9a441; font-size: 12px; margin: 8px 0; }
.verdict { margin: 10px 0; padding: 8px; border: 1px solid var(--orca-border); border-radius: 8px; font-size: 12px; }
button { margin-top: 6px; }

#inventory-controls {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin: 10px 0;
}
#inventory-controls input,
#inventory-controls select {
  flex: 1 1 120px;
  min-width: 0;
  padding: 6px;
  border: 1px solid var(--orca-border);
  border-radius: 5px;
  color: var(--orca-fg);
  background: var(--orca-bg);
}
.inventory-item {
  padding: 9px 0;
  border-bottom: 1px solid var(--orca-border);
  font-size: 12px;
}
.inventory-item .name {
  font-size: 12px;
}
.inventory-item .muted {
  margin-top: 3px;
}
.inventory-empty {
  opacity: 0.6;
}
</style></head><body>
<h3>Active spool (Phoenix)</h3>
<div id="empty" class="muted">Waiting for the first reading...</div>
<div id="card" class="card" hidden>
  <div id="swatch" class="swatch"></div>
  <div><div id="name" class="name"></div><div id="sub" class="muted"></div></div>
</div>
<dl id="details" hidden></dl>
<div id="verdict" class="verdict" hidden></div>
<div id="association" class="verdict" hidden>
  <div id="association-caption" class="muted"></div>
  <div id="association-saved"></div>
  <select id="association-preset" style="width:100%;margin-top:8px;padding:6px"
          aria-label="Orca filament preset"></select>
  <button id="associate-preset">Associate preset</button>
  <div id="association-feedback" class="muted"></div>
</div>
<div id="preset-choice" class="verdict" hidden>
  <div class="muted">Choose an associated preset and Orca slot:</div>
  <select id="preset-choice-name" style="width:100%;margin-top:8px"></select>
  <select id="preset-choice-slot" style="width:100%;margin-top:8px"></select>
  <button id="preset-choice-apply">Apply preset</button>
</div>
<div id="err" class="err" hidden></div>
<div class="muted" id="when"></div>
<button id="refresh">Refresh now</button>

<hr style="margin:16px 0; border:0; border-top:1px solid var(--orca-border);">
<h3>Spoolman inventory</h3>
<div id="inventory-status" class="muted">Waiting for inventory...</div>
<div id="inventory-controls">
  <input id="inventory-search" type="search"
         placeholder="Search spools..." aria-label="Search inventory">
  <select id="inventory-material" aria-label="Filter by material">
    <option value="">All materials</option>
  </select>
</div>
<div id="inventory-list"></div>
<div id="inventory-error" class="err" hidden></div>

<script>
const $ = (id) => document.getElementById(id);
function row(k, v) {
  const dt = document.createElement('dt'); dt.textContent = k;
  const dd = document.createElement('dd'); dd.textContent = v;
  $('details').append(dt, dd);
}
let lastSuccessfulReading = null;

function render(m) {
  const bad = !m.ok;
  $('err').hidden = !bad;
  $('err').textContent = bad ? m.error : '';

  if (bad) {
    $('association').hidden = true;
    $('when').textContent = lastSuccessfulReading
      ? 'Offline - last successful reading: ' + lastSuccessfulReading
      : 'Offline - no successful reading';
    $('verdict').hidden = true;
    return;
  }

  lastSuccessfulReading = new Date().toLocaleTimeString();
  $('when').textContent = 'Updated ' + lastSuccessfulReading;
  const s = m.spool;
  $('card').hidden = !s; $('details').hidden = !s;
  $('empty').hidden = !!s; $('empty').textContent = 'No spool is active in Moonraker.';
  if (!s) {
    $('association').hidden = true;
    $('verdict').hidden = true;
    return;
  }
  $('swatch').style.background = s.color ? '#' + s.color : 'transparent';
  const vendor = (s.vendor || '').trim();
  const name = (s.name || '').trim();
  const alreadyPrefixed = vendor && name.toLowerCase().startsWith(vendor.toLowerCase() + ' ');
  $('name').textContent = '#' + s.id + ' ' + (alreadyPrefixed ? name : [vendor, name].filter(Boolean).join(' '));
  $('sub').textContent = [s.material, s.diameter ? s.diameter + ' mm' : ''].filter(Boolean).join(' · ');
  $('details').replaceChildren();
  if (s.color) row('Colour', '#' + s.color);
  if (s.nozzle) row('Nozzle', s.nozzle + ' C');
  if (s.bed) row('Bed', s.bed + ' C');
  if (s.remaining !== null) row('Remaining', Math.round(s.remaining) + ' g');
  if (s.tags.length) row('NFC tag', s.tags.join(', '));
  verdict(m);
  renderAssociation(m);
  updateChoiceSlots(m.slots);
}
function verdict(m) {
  const box = $('verdict');
  if (!m.spool) { box.hidden = true; return; }
  box.hidden = false;
  box.replaceChildren();

  const slots = Array.isArray(m.slots) && m.slots.length
    ? m.slots
    : [{slot: 1, selected: m.selected, problems: m.problems, notes: m.notes}];

  for (const entry of slots) {
    const selected = entry.selected;
    const line = document.createElement('div');
    line.textContent = 'Slot ' + entry.slot + ': ' +
      (selected ? (selected.name || '(unnamed)') : 'cannot be read');
    box.append(line);

    if (!selected || !Array.isArray(entry.problems)) continue;

    const result = document.createElement('div');
    const notes = entry.notes || [];

    if (entry.problems.length) {
      result.className = 'err';
      result.textContent = 'Mismatch: ' + entry.problems.join('; ');
    } else if (notes.length) {
      result.className = 'warn';
      result.textContent = 'Check profile: ' + notes.join('; ');
    } else {
      result.className = 'ok';
      result.textContent = 'Matches the active spool.';
    }
    box.append(result);
  }
}
let activeFilamentId = null;

function renderAssociation(m) {
  const box = $('association');
  const id = Number(m.spool && m.spool.filament_id);
  const previousId = activeFilamentId;
  activeFilamentId = Number.isSafeInteger(id) && id > 0 ? id : null;
  if (previousId !== activeFilamentId) {
    $('preset-choice').hidden = true;
    $('association-feedback').textContent = '';
  }
  box.hidden = activeFilamentId === null;
  if (box.hidden) return;

  const mappings = m.filament_preset_map || {};
  const saved = mappings[String(activeFilamentId)] || [];
  const names = Array.isArray(saved) ? saved : [];

  $('association-caption').textContent =
    'Spoolman filament #' + activeFilamentId + ' — associated: ' +
    (names.length ? names.join(', ') : 'none');

  const savedBox = $('association-saved');
  savedBox.replaceChildren();

  for (const name of names) {
    const line = document.createElement('div');
    line.style.marginTop = '6px';

    const label = document.createElement('span');
    label.textContent = name + ' ';

    const remove = document.createElement('button');
    remove.textContent = 'Rimuovi';
    remove.setAttribute('aria-label', 'Rimuovi associazione ' + name);
    remove.onclick = () => {
      orca.postMessage({
        type: 'remove_association',
        filament_id: activeFilamentId,
        preset_name: name
      });
    };

    line.append(label, remove);
    savedBox.append(line);
  }

  const select = $('association-preset');
  const previous = select.value;
  select.replaceChildren();

  const placeholder = document.createElement('option');
  placeholder.value = '';
  placeholder.textContent = 'Select an Orca preset...';
  select.append(placeholder);

  for (const name of (m.preset_options || [])) {
    const option = document.createElement('option');
    option.value = name;
    option.textContent = name;
    select.append(option);
  }

  if (Array.from(select.options).some(o => o.value === previous)) {
    select.value = previous;
  }

  $('associate-preset').disabled = !select.value;
}

$('association-preset').addEventListener('change', () => {
  $('associate-preset').disabled = !$('association-preset').value;
});

$('associate-preset').onclick = () => {
  const preset = $('association-preset').value;
  if (activeFilamentId === null || !preset) return;

  $('association-feedback').textContent = 'Saving association...';
  orca.postMessage({
    type: 'associate',
    filament_id: activeFilamentId,
    preset_name: preset
  });
};

function updateChoiceSlots(slots) {
  const select = $('preset-choice-slot');
  const previous = select.value;
  select.replaceChildren();
  const numbers = Array.isArray(slots) && slots.length
    ? slots.map(s => s.slot) : [1];
  for (const number of numbers) {
    const option = document.createElement('option');
    option.value = String(number);
    option.textContent = 'Orca slot ' + number;
    select.append(option);
  }
  if (numbers.some(n => String(n) === previous)) select.value = previous;
}

function renderPresetSelection(m) {
  const box = $('preset-choice');
  const feedback = $('association-feedback');

  if (m.status === 'choose') {
    const select = $('preset-choice-name');
    select.replaceChildren();
    for (const name of m.names || []) {
      const option = document.createElement('option');
      option.value = name;
      option.textContent = name;
      select.append(option);
    }
    box.hidden = !select.options.length;
    feedback.textContent = 'Multiple associated presets: choose one.';
  } else {
    box.hidden = true;
    if (m.status === 'selected') {
      feedback.className = 'ok';
      feedback.textContent = 'Selected: ' + m.name +
        ' (slot ' + (m.slot || 1) + ')';
    } else if (m.status === 'unmapped') {
      feedback.className = 'warn';
      feedback.textContent = 'No association yet. Select a preset above.';
    } else if (m.status === 'error') {
      feedback.className = 'err';
      feedback.textContent = m.error || 'Preset selection failed.';
    }
  }
}

$('preset-choice-apply').onclick = () => {
  if (activeFilamentId === null) return;
  orca.postMessage({
    type: 'select_associated',
    filament_id: activeFilamentId,
    preset_name: $('preset-choice-name').value,
    slot: Number($('preset-choice-slot').value)
  });
};

let inventorySpools = [];

function renderInventoryList() {
  const query = $('inventory-search').value.trim().toLowerCase();
  const material = $('inventory-material').value;
  const list = $('inventory-list');
  list.replaceChildren();

  const visible = inventorySpools.filter(s => {
    const searchable = [s.id, s.vendor, s.name, s.material]
      .filter(v => v !== null && v !== undefined)
      .join(' ').toLowerCase();
    return searchable.includes(query) &&
      (!material || s.material === material);
  });

  for (const s of visible) {
    const item = document.createElement('div');
    item.className = 'inventory-item';

    const name = document.createElement('div');
    name.className = 'name';

    const vendor = String(s.vendor || '').trim();
    const product = String(s.name || '').trim();
    const fullName = vendor &&
      product.toLowerCase().startsWith(vendor.toLowerCase() + ' ')
        ? product : [vendor, product].filter(Boolean).join(' ');

    name.textContent = '#' + s.id + ' ' + fullName;

    const details = document.createElement('div');
    details.className = 'muted';

    const weight = typeof s.remaining === 'number' &&
      Number.isFinite(s.remaining)
        ? Math.round(s.remaining) + ' g'
        : 'Weight unknown';

    const exhausted = typeof s.remaining === 'number' &&
      s.remaining <= 0;

    details.textContent = [
      s.material || 'Unknown material',
      weight,
      s.archived ? 'Archived' : exhausted ? 'Empty' : ''
    ].filter(Boolean).join(' · ');

    if (exhausted || s.archived) item.classList.add('inventory-empty');

    item.append(name, details);
    list.append(item);
  }

  if (!visible.length) {
    const empty = document.createElement('div');
    empty.className = 'muted';
    empty.textContent = 'No matching spools.';
    list.append(empty);
  }
}

let lastSuccessfulInventoryReading = null;

function renderInventory(m) {
  $('inventory-error').hidden = !!m.ok;
  $('inventory-error').textContent = m.ok ? '' : (m.error || 'Inventory error');

  if (!m.ok) {
    $('inventory-status').textContent = lastSuccessfulInventoryReading
      ? 'Offline - last inventory update: ' +
        lastSuccessfulInventoryReading +
        ' (' + inventorySpools.length + ' cached spools)'
      : 'Offline - no inventory data available';
    return;
  }

  inventorySpools = Array.isArray(m.spools) ? m.spools : [];
  lastSuccessfulInventoryReading = new Date().toLocaleTimeString();
  $('inventory-status').textContent =
    inventorySpools.length + ' spools in Spoolman · Updated ' +
    lastSuccessfulInventoryReading;

  const select = $('inventory-material');
  const previous = select.value;
  const materials = [...new Set(
    inventorySpools.map(s => s.material).filter(Boolean)
  )].sort();

  select.replaceChildren(new Option('All materials', ''));
  for (const material of materials) {
    select.add(new Option(material, material));
  }
  select.value = materials.includes(previous) ? previous : '';

  renderInventoryList();
}

$('inventory-search').addEventListener('input', renderInventoryList);
$('inventory-material').addEventListener('change', renderInventoryList);

orca.onMessage((m) => {
  if (!m) return;
  if (m.type === 'state') render(m);
  if (m.type === 'inventory') renderInventory(m);
  if (m.type === 'preset_selection') renderPresetSelection(m);
  if (m.type === 'association_result') {
    $('association-feedback').className = m.ok ? 'ok' : 'err';
    $('association-feedback').textContent =
      m.ok
        ? (m.action === 'removed'
            ? 'Associazione rimossa.'
            : 'Association saved.')
        : (m.error || 'Save failed.');
  }
});
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
        self.inventory_thread = None
        self.inventory_wake = threading.Event()
        self.network_lock = threading.Lock()
        self.permission_denied = threading.Event()
        self.last = None            # latest network state (without the selection)
        self.selected = None
        self.slots = []
        self.available_presets = []
        self.last_auto_spool_key = None        # latest selected profile, read on the UI thread
        self.command_worker = None
        self.command_queue = None
        self.command_busy = False
        self.config = dict(DEFAULT_CONFIG)

    def attach_command_worker(self, worker, commands):
        """Register the worker for the current panel session."""
        if not self.is_open():
            return False

        if self.stop.is_set():
            self.stop = threading.Event()
            self.wake = threading.Event()
            self.inventory_wake = threading.Event()
            self.network_lock = threading.Lock()
            self.permission_denied = threading.Event()
            self.thread = None
            self.inventory_thread = None

        self.command_worker = worker
        self.command_queue = commands
        return True

    def detach_command_worker(self, worker):
        """Detach only the matching worker."""
        if self.command_worker is not worker:
            return False
        self.command_worker = None
        self.command_queue = None
        self.command_busy = False
        return True

    def enqueue_command(self, command):
        """Queue a command without blocking the UI."""
        commands = self.command_queue
        if self.command_worker is None or commands is None:
            return False
        commands.put_nowait(command)
        return True

    def is_open(self):
        return self.panel is not None and self.panel.is_open()

    def start(self, config, retry_permissions=False):
        self.config = dict(config)
        if self.stop.is_set():
            # New panel session: old workers retain their stopped event.
            self.stop = threading.Event()
            self.wake = threading.Event()
            self.inventory_wake = threading.Event()
            self.network_lock = threading.Lock()
            self.permission_denied = threading.Event()
            self.thread = None
            self.inventory_thread = None
        if retry_permissions:
            with self.network_lock:
                self.permission_denied.clear()
        self.wake.set()
        self.inventory_wake.set()                      # read straight away
        if self.thread is None or not self.thread.is_alive():
            self.thread = threading.Thread(target=self._loop, args=(self.stop, self.wake, self.network_lock, self.permission_denied), name="phoenix-spoolman-panel", daemon=True)
            self.thread.start()
        if self.inventory_thread is None or not self.inventory_thread.is_alive():
            self.inventory_thread = threading.Thread(
                target=self._inventory_loop,
                args=(self.stop, self.inventory_wake, self.network_lock, self.permission_denied),
                name="phoenix-spoolman-inventory",
                daemon=True,
            )
            self.inventory_thread.start()

    def close(self):
        self.stop.set()
        self.wake.set()
        self.inventory_wake.set()

        commands = self.command_queue
        if commands is not None:
            commands.put_nowait({"type": "shutdown"})

        self.command_worker = None
        self.command_queue = None
        self.command_busy = False

        self.panel = None
        self.last = None
        self.selected = None
        self.slots = []
        self.available_presets = []
        self.last_auto_spool_key = None

    def tolerance(self):
        try:
            return float(self.config.get("nozzle_temp_tolerance", DEFAULT_CONFIG["nozzle_temp_tolerance"]))
        except (TypeError, ValueError):
            return float(DEFAULT_CONFIG["nozzle_temp_tolerance"])

    def message(self):
        message = with_selection(self.last, self.selected, self.tolerance())
        if isinstance(message, dict):
            message["slots"] = []
            message["preset_options"] = [
                preset["name"] for preset in self.available_presets
                if preset.get("name") and preset.get("compatible")
            ]
            associations = self.config.get("filament_preset_map", {})
            message["filament_preset_map"] = (
                associations if isinstance(associations, dict) else {}
            )
            for slot in self.slots:
                result = with_selection(self.last, slot, self.tolerance())
                message["slots"].append({
                    "slot": slot["slot"],
                    "selected": slot,
                    "problems": result.get("problems"),
                    "notes": result.get("notes"),
                })
        return message

    def _loop(self, stop, wake, network_lock, permission_denied):
        while not stop.is_set():
            wake.clear()
            with network_lock:
                if stop.is_set() or permission_denied.is_set():
                    payload = None
                else:
                    payload = panel_payload(self.config)
                    if payload.get("permission_denied"):
                        permission_denied.set()
            if payload is None:
                if stop.is_set():
                    return
                wake.wait()
                continue
            panel = self.panel
            if panel is None or stop.is_set() or panel is not self.panel:
                return
            try:
                if panel.is_open():
                    self.last = payload
                    if payload.get("ok"):
                        spool = payload.get("spool")
                        key = None
                        if (isinstance(spool, dict)
                                and type(spool.get("filament_id")) is int
                                and spool["filament_id"] > 0):
                            key = (spool.get("id"), spool["filament_id"])
                        if key != self.last_auto_spool_key:
                            self.last_auto_spool_key = key
                            if key is not None:
                                self.enqueue_command({
                                    "type": "auto_select",
                                    "spool_id": key[0],
                                    "filament_id": key[1],
                                })
                    panel.post(self.message())
            except Exception:
                return                       # the panel went away while we were reading
            try:
                interval = max(1.0, float(self.config.get("poll_seconds", DEFAULT_CONFIG["poll_seconds"])))
            except (TypeError, ValueError):
                interval = float(DEFAULT_CONFIG["poll_seconds"])
            wake.wait(interval)


    def _inventory_loop(self, stop, wake, network_lock, permission_denied):
        """Poll Spoolman with shared permission protection."""
        while not stop.is_set():
            wake.clear()
            with network_lock:
                if stop.is_set() or permission_denied.is_set():
                    payload = None
                else:
                    payload = inventory_payload(self.config)
                    if payload.get("permission_denied"):
                        permission_denied.set()
            if payload is None:
                if stop.is_set():
                    return
                wake.wait()
                continue
            panel = self.panel
            if panel is None or stop.is_set() or panel is not self.panel:
                return
            try:
                if panel.is_open():
                    panel.post(payload)
            except Exception:
                return
            try:
                interval = max(5.0, float(self.config.get(
                    "inventory_poll_seconds",
                    DEFAULT_CONFIG["inventory_poll_seconds"]
                )))
            except (TypeError, ValueError):
                interval = float(DEFAULT_CONFIG["inventory_poll_seconds"])
            wake.wait(interval)


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

    def filament_associations(self):
        raw = self._config().get("filament_preset_map", {})
        if not isinstance(raw, dict):
            return {}
        result = {}
        for key, names in raw.items():
            if (isinstance(key, str) and key.isdecimal()
                    and isinstance(names, list)):
                valid = [
                    name for name in names
                    if isinstance(name, str) and name.strip()
                ]
                if valid:
                    result[key] = list(dict.fromkeys(valid))
        return result

    def save_filament_association(self, filament_id, preset_name):
        if (type(filament_id) is not int or filament_id <= 0
                or not isinstance(preset_name, str)
                or not preset_name.strip()):
            raise ValueError("Invalid filament ID or preset name")

        config = self._config()
        associations = self.filament_associations()
        key = str(filament_id)
        names = associations.setdefault(key, [])

        if preset_name not in names:
            names.append(preset_name)
            config["filament_preset_map"] = associations
            if not self.save_config(json.dumps(config)):
                raise PhoenixError("Cannot save filament associations")

        return list(names)

    def remove_filament_association(self, filament_id, preset_name):
        if (type(filament_id) is not int or filament_id <= 0
                or not isinstance(preset_name, str)
                or not preset_name.strip()):
            raise ValueError("Invalid filament ID or preset name")

        config = self._config()
        associations = self.filament_associations()
        key = str(filament_id)
        names = associations.get(key, [])

        if preset_name not in names:
            return list(names)

        names = [name for name in names if name != preset_name]
        if names:
            associations[key] = names
        else:
            associations.pop(key, None)

        config["filament_preset_map"] = associations
        if not self.save_config(json.dumps(config)):
            raise PhoenixError("Cannot remove filament association")

        return names

    def on_load(self):
        if self._config().get("auto_open_panel") is False:
            return
        if PANEL.is_open():
            return

        self._create_panel()
        self._start_worker()

    def _start_worker(self):
        self._autostart_thread = threading.Thread(
            target=self._run_autostart,
            name="phoenix-spoolman-commands",
            daemon=True,
        )
        self._autostart_thread.start()

    def _run_autostart(self):
        try:
            self._run_commands()
        except Exception as exc:
            print("Phoenix Spoolman worker: %s" % exc, file=sys.stderr)
            PANEL.close()

    def on_unload(self):
        PANEL.close()

    def on_cancelled(self):
        """Stop panel workers before Orca cancels this async capability."""
        PANEL.close()

    def _on_message(self, data):
        # UI thread: read live preset data here, never on the worker.
        kind = data.get("type") if isinstance(data, dict) else None

        if kind == "select_associated":
            PANEL.enqueue_command({
                "type": "select_associated",
                "filament_id": data.get("filament_id"),
                "preset_name": data.get("preset_name"),
                "slot": data.get("slot"),
            })
            return
        if kind == "remove_association":
            response = orca.host.ui.message(
                "Rimuovere associazione con %s?" % data.get("preset_name"),
                title="Phoenix Spoolman",
                buttons="yes_no",
                icon="question",
            )
            if response != "yes":
                return
            PANEL.enqueue_command({
                "type": "remove_association",
                "filament_id": data.get("filament_id"),
                "preset_name": data.get("preset_name"),
            })
            return
        if kind == "associate":
            PANEL.enqueue_command({
                "type": "associate",
                "filament_id": data.get("filament_id"),
                "preset_name": data.get("preset_name"),
            })
            return
        if kind in ("ready", "refresh", "selection"):
            PANEL.selected = read_selected_filament()
            PANEL.slots = read_filament_slots()
            if kind in ("ready", "refresh"):
                try:
                    PANEL.available_presets = list_orca_filament_presets()
                except Exception:
                    PANEL.available_presets = []
            if PANEL.last is not None and PANEL.is_open():
                PANEL.panel.post(PANEL.message())

        if kind in ("ready", "refresh"):
            PANEL.enqueue_command({"type": kind})

    def _on_close(self):
        PANEL.close()

    def _create_panel(self):
        PANEL.panel = orca.host.ui.create_dock_panel(
            html=PANEL_HTML,
            title="Phoenix active spool",
            width=320,
            height=420,
            on_message=self._on_message,
            on_close=self._on_close,
            dock="right",
        )

    def execute(self):
        if PANEL.is_open():
            PANEL.panel.show()
            return orca.ExecutionResult.success(
                "The active spool panel is already open."
            )

        try:
            self._create_panel()
        except Exception as exc:
            return orca.ExecutionResult.failure(
                orca.PluginResult.RecoverableError,
                "Cannot open the panel: %s" % exc
            )

        self._start_worker()
        return orca.ExecutionResult.success("Active spool panel opened.")

    def _run_commands(self):
        worker = threading.current_thread()
        commands = queue.Queue()

        if not PANEL.attach_command_worker(worker, commands):
            return orca.ExecutionResult.failure(
                orca.PluginResult.RecoverableError,
                "Cannot attach the panel command worker."
            )

        # Initial read: do not depend on the timing of the HTML ready message.
        commands.put_nowait({"type": "ready"})

        try:
            while True:
                command = commands.get()
                kind = command.get("type") if isinstance(command, dict) else None

                if kind == "shutdown":
                    break

                if kind in ("ready", "refresh"):
                    PANEL.start(
                        self._config(),
                        retry_permissions=(kind == "refresh"),
                    )
                elif kind == "auto_select":
                    active = (PANEL.last or {}).get("spool") or {}
                    filament_id = command.get("filament_id")
                    if (active.get("id") != command.get("spool_id")
                            or active.get("filament_id") != filament_id):
                        continue

                    names = self.filament_associations().get(str(filament_id), [])
                    if len(names) == 1:
                        try:
                            orca.host.preset.filament.begin_update(1, names[0])
                            result = {
                                "type": "preset_selection",
                                "status": "selected",
                                "name": names[0],
                            }
                        except Exception as exc:
                            result = {
                                "type": "preset_selection",
                                "status": "error",
                                "error": str(exc),
                            }
                    elif len(names) > 1:
                        result = {
                            "type": "preset_selection",
                            "status": "choose",
                            "names": names,
                        }
                    else:
                        result = {
                            "type": "preset_selection",
                            "status": "unmapped",
                        }

                    if PANEL.is_open():
                        PANEL.panel.post(result)
                elif kind == "select_associated":
                    active = (PANEL.last or {}).get("spool") or {}
                    filament_id = command.get("filament_id")
                    preset_name = command.get("preset_name")
                    slot = command.get("slot")

                    try:
                        if (type(filament_id) is not int
                                or filament_id <= 0
                                or active.get("filament_id") != filament_id):
                            raise ValueError("Active filament changed")
                        if (type(slot) is not int
                                or not 1 <= slot <= len(PANEL.slots)):
                            raise ValueError("Invalid Orca filament slot")

                        associated = self.filament_associations().get(
                            str(filament_id), []
                        )
                        if preset_name not in associated:
                            raise ValueError("Preset is not associated")

                        allowed = {
                            item["name"] for item in PANEL.available_presets
                            if item.get("name") and item.get("compatible")
                        }
                        if preset_name not in allowed:
                            raise ValueError("Preset unavailable or incompatible")

                        orca.host.preset.filament.begin_update(slot, preset_name)
                        result = {
                            "type": "preset_selection",
                            "status": "selected",
                            "name": preset_name,
                            "slot": slot,
                        }
                    except Exception as exc:
                        result = {
                            "type": "preset_selection",
                            "status": "error",
                            "error": str(exc),
                        }

                    if PANEL.is_open():
                        PANEL.panel.post(result)

                elif kind == "remove_association":
                    active = (PANEL.last or {}).get("spool") or {}
                    filament_id = command.get("filament_id")
                    preset_name = command.get("preset_name")

                    try:
                        if (type(filament_id) is not int
                                or filament_id <= 0
                                or active.get("filament_id") != filament_id):
                            raise ValueError("Active filament changed")

                        existing = self.filament_associations().get(
                            str(filament_id), []
                        )
                        if preset_name not in existing:
                            raise ValueError("Association not found")

                        self.remove_filament_association(
                            filament_id, preset_name
                        )
                        PANEL.config = self._config()
                        result = {
                            "type": "association_result",
                            "ok": True,
                            "action": "removed",
                        }
                    except (ValueError, PhoenixError) as exc:
                        result = {
                            "type": "association_result",
                            "ok": False,
                            "error": str(exc),
                        }

                    if PANEL.is_open():
                        PANEL.panel.post(result)
                        PANEL.panel.post(PANEL.message())

                elif kind == "associate":
                    filament_id = command.get("filament_id")
                    preset_name = command.get("preset_name")
                    active = (PANEL.last or {}).get("spool") or {}
                    allowed = {
                        item["name"] for item in PANEL.available_presets
                        if item.get("name") and item.get("compatible")
                    }

                    try:
                        if (type(filament_id) is not int
                                or filament_id <= 0
                                or filament_id != active.get("filament_id")):
                            raise ValueError("Invalid active filament ID")
                        if preset_name not in allowed:
                            raise ValueError("Preset unavailable or incompatible")

                        names = self.save_filament_association(
                            filament_id, preset_name
                        )
                        PANEL.config = self._config()
                        result = {"type": "association_result", "ok": True}

                        if len(names) == 1:
                            PANEL.enqueue_command({
                                "type": "auto_select",
                                "spool_id": active.get("id"),
                                "filament_id": filament_id,
                            })
                        elif len(names) > 1 and PANEL.is_open():
                            PANEL.panel.post({
                                "type": "preset_selection",
                                "status": "choose",
                                "names": names,
                            })
                    except (ValueError, PhoenixError) as exc:
                        result = {
                            "type": "association_result",
                            "ok": False,
                            "error": str(exc),
                        }

                    if PANEL.is_open():
                        PANEL.panel.post(result)
                        PANEL.panel.post(PANEL.message())

        finally:
            PANEL.detach_command_worker(worker)

        return orca.ExecutionResult.success(
            "Active spool panel closed."
        )


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
        collection = bundle.filaments
        members.append("")
        members.append("Filament PresetCollection:")
        for name in sorted(n for n in dir(collection) if not n.startswith("_")):
            member = getattr(collection, name)
            kind = "method" if callable(member) else type(member).__name__
            members.append("  %s (%s)" % (name, kind))
        for label, obj in (("Host API", orca.host), ("Host UI API", orca.host.ui)):
            members.append("")
            members.append(label + ":")
            for name in sorted(n for n in dir(obj) if not n.startswith("_")):
                members.append("  " + name)
        selected = []

        try:
            names = bundle.current_filament_preset_names()
            selected.append(
                "  current_filament_preset_names() -> %r" % (names,)
            )
        except Exception as exc:
            selected.append("  Preset names error: %s" % exc)

        try:
            current_presets = bundle.current_filament_presets()
            selected.append(
                "  current_filament_presets() -> %d objects" %
                len(current_presets or [])
            )

            for index, preset in enumerate(current_presets or []):
                selected.append(
                    "  Preset object[%d] -> %s" %
                    (index, type(preset).__name__)
                )

                try:
                    keys = list(preset.config_keys())
                    relevant_keys = [
                        key for key in keys
                        if any(term in str(key).lower() for term in (
                            "filament", "nozzle", "color", "colour",
                            "vendor", "density", "diameter"
                        ))
                    ]

                    selected.append(
                        "    Relevant config keys -> %r" % relevant_keys
                    )

                    candidates = (
                        "filament_type",
                        "nozzle_temperature",
                        "filament_colour",
                        "filament_color",
                        "filament_vendor",
                        "filament_diameter",
                        "filament_density",
                    )

                    for key in candidates:
                        if key in keys:
                            value = preset.config_value(key)
                            selected.append(
                                "    %s -> %r" % (key, value)
                            )
                except Exception as exc:
                    selected.append(
                        "    Config inspection error: %s" % exc
                    )

                attrs = []
                for attr in sorted(
                    n for n in dir(preset) if not n.startswith("_")
                ):
                    try:
                        value = getattr(preset, attr)
                        if callable(value):
                            detail = "<method>"
                        else:
                            detail = repr(value)
                            if len(detail) > 100:
                                detail = detail[:97] + "..."
                    except Exception as exc:
                        detail = "<error: %s>" % exc

                    attrs.append("%s=%s" % (attr, detail))

                selected.extend(
                    "    " + item
                    for item in attrs[:25]
                )

                if len(attrs) > 25:
                    selected.append(
                        "    ... %d more attributes" % (len(attrs) - 25)
                    )

        except Exception as exc:
            selected.append("  Preset object error: %s" % exc)

        for key in ("filament_type", "nozzle_temperature"):
            try:
                selected.append(
                    "  full_config_value(%r) -> %r" %
                    (key, bundle.full_config_value(key))
                )
            except Exception as exc:
                selected.append("  %s error: %s" % (key, exc))

        selected.append(
            "  read_selected_filament() -> %r" %
            (read_selected_filament(),)
        )

        lines = ["preset_bundle() -> %s" % type(bundle).__name__, "Selected filament, as the panel reads it:"]
        host_start = members.index("Host API:")
        members = members[host_start:] + members[:host_start]
        lines += selected + ["", "Members:"] + members
        text = "\n".join(lines)
        print(text, file=sys.stderr)         # also lands in OrcaSlicer's python log
        orca.host.ui.message(text[:3500], title="Phoenix Spoolman", buttons="ok", icon="info")
        return orca.ExecutionResult.success(text)


@orca.plugin
class PhoenixSpoolmanPlugin(orca.base):
    def register_capabilities(self):
        orca.request_permissions(threading=True, network=["http://phoenix.local"])
        orca.register_capability(ActiveSpool)
        orca.register_capability(SpoolCheck)
        orca.register_capability(ActiveSpoolPanel)
