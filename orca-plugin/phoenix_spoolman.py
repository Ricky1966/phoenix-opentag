# /// script
# requires-python = ">=3.9"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# name = "Phoenix Spoolman"
# description = "Shows the spool that the Phoenix reader activated and checks it against the exported G-code (Moonraker + Spoolman). Standard library only."
# author = "Ricky1966"
# version = "0.2.0"
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
import urllib.error
import urllib.request

import orca

DEFAULT_CONFIG = {
    "moonraker_url": "http://192.168.1.36:7125",
    "spoolman_url": "http://192.168.1.36:7912",
    "timeout_seconds": 5,
    # Spool check only: "warn" reports a mismatch as a recoverable error, "block" as a fatal one,
    # "ignore" only writes the comment block.
    "on_mismatch": "warn",
    "nozzle_temp_tolerance": 25,
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
            kind = orca.PluginResult.FatalError if config.get("on_mismatch") == "block" else orca.PluginResult.RecoverableError
            return orca.ExecutionResult.failure(kind, message)
        return orca.ExecutionResult.success(message)


@orca.plugin
class PhoenixSpoolmanPlugin(orca.base):
    def register_capabilities(self):
        orca.register_capability(ActiveSpool)
        orca.register_capability(SpoolCheck)
