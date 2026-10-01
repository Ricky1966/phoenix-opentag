# /// script
# requires-python = ">=3.9"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# name = "Phoenix Spoolman"
# description = "Shows the spool that the Phoenix reader activated (Moonraker + Spoolman). Standard library only."
# author = "Ricky1966"
# version = "0.1.0"
# ///
"""Phoenix Spoolman plugin for OrcaSlicer (Python plugin API, nightly builds / releases after 2.4.2).

Step 1: the **Active spool** script. It asks Moonraker which spool is active (the one the
Phoenix NFC reader set when a tag was read), then asks Spoolman for its details and shows them.
It only reads: nothing is written to the printer, to Spoolman or to Orca's presets.

OrcaSlicer asks for permission the first time the plugin contacts the network.
"""

import json
import urllib.error
import urllib.request

import orca

DEFAULT_CONFIG = {
    "moonraker_url": "http://192.168.1.36:7125",
    "spoolman_url": "http://192.168.1.36:7912",
    "timeout_seconds": 5,
}


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


def active_spool_report(config):
    """Text describing the active spool; raises PhoenixError on any problem."""
    timeout = float(config.get("timeout_seconds", DEFAULT_CONFIG["timeout_seconds"]))
    spool_id = active_spool_id(config.get("moonraker_url", DEFAULT_CONFIG["moonraker_url"]), timeout)
    if spool_id is None:
        return "No spool is active in Moonraker."
    base = config.get("spoolman_url", DEFAULT_CONFIG["spoolman_url"]).rstrip("/")
    return describe_spool(get_json("%s/api/v1/spool/%d" % (base, spool_id), timeout))


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


@orca.plugin
class PhoenixSpoolmanPlugin(orca.base):
    def register_capabilities(self):
        orca.register_capability(ActiveSpool)
