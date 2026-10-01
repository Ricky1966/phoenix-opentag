# Phoenix Spoolman: OrcaSlicer plugin

A plugin for OrcaSlicer's Python plugin system, which exists in the **nightly builds** and in
releases **after 2.4.2** (OrcaSlicer 2.4.2 itself does not have it). Standard library only.

## What it does

**Active spool** (script) shows the spool that the Phoenix NFC reader made active: it asks Moonraker
(`/server/spoolman/spool_id`) and then Spoolman (`/api/v1/spool/<id>`), and displays the vendor,
filament, material, colour, diameter, temperatures, remaining weight and the NFC tag. It only
reads; nothing is written to the printer, to Spoolman or to Orca's presets.

**Spool check** (G-code post-processing, experimental in OrcaSlicer) runs when a G-code file is
exported. It compares the active spool with the filament profile that was sliced, reading
`filament_type` and `nozzle_temperature` from the settings block that Orca writes at the end of the
G-code:

- the material of the spool (Spoolman) must equal `filament_type`, ignoring case;
- the sliced nozzle temperature must be within `nozzle_temp_tolerance` (default 25 C) of the spool's
  temperature in Spoolman, when the spool has one.

It always **appends** a comment block to the end of the G-code (the rest of the file is never
rewritten; Klipper ignores comments), for example:

```
; --- Phoenix Spoolman ---
; active_spool_id = 4
; active_spool = Polymaker PolyTerra PLA
; active_spool_material = PLA
; sliced_filament_type = PLA
; active_spool_tag = E004010866073472
; spool check: OK
; --- end Phoenix Spoolman ---
```

A mismatch adds `; MISMATCH: …` lines and is reported according to `on_mismatch`: `warn` (default,
a recoverable error), `block` (a fatal error) or `ignore` (only the comments). If Moonraker or
Spoolman cannot be reached, for example when slicing away from home, the check is skipped and the
G-code is exported unchanged apart from a comment: it never blocks an export.

**Limits.** The check happens at export time, not when the print starts: Klipper cannot ask Moonraker
which spool is active, so a spool changed after slicing is not noticed. How OrcaSlicer reacts to a
recoverable or fatal error from a post-processing plugin (a warning dialog, or an aborted export or
upload) depends on the OrcaSlicer build and has to be observed.

Not done yet: writing a filament preset from the active spool.

## Install

1. Copy the file into its own folder under the OrcaSlicer data directory, with OrcaSlicer closed:
   `~/.config/OrcaSlicer/orca_plugins/phoenix-spoolman/phoenix_spoolman.py`. Start OrcaSlicer,
   open **Plugins**, press **Refresh** and tick **Activate**. (**Install local plugin** did not work
   in the nightly build tested on Linux; the folder copy does.)
2. Expand the plugin, find the **Active spool** script and press **Run**.
3. OrcaSlicer asks whether the plugin may contact the network. The plugin talks only to the
   Moonraker and Spoolman addresses in its configuration.
4. **Spool check** appears among the plugin's features: to use it, select it as the post-processing
   plugin in the print/printer settings (look for the post-processing option that lists plugins), then
   export a G-code file and open its last lines.

## Configuration

Plugins window → select the plugin → **Config** tab → choose **Active spool** or **Spool check** (each has its own configuration; `on_mismatch` and `nozzle_temp_tolerance` only matter for **Spool check**). Defaults:

```json
{"moonraker_url": "http://192.168.1.36:7125", "spoolman_url": "http://192.168.1.36:7912", "timeout_seconds": 5,
 "on_mismatch": "warn", "nozzle_temp_tolerance": 25}
```

Away from home with Tailscale, use the Phoenix's Tailscale address in both URLs.

## Tests

`python3 -m unittest tests/test_orca_plugin.py` runs the plugin against a stand-in `orca` module and
local fake Moonraker/Spoolman servers. They cannot replace a run inside OrcaSlicer, whose plugin API
is new and may change.
