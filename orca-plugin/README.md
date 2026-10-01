# Phoenix Spoolman: OrcaSlicer plugin

A plugin for OrcaSlicer's Python plugin system, which exists in the **nightly builds** and in
releases **after 2.4.2** (OrcaSlicer 2.4.2 itself does not have it). Standard library only.

## What it does (step 1)

**Active spool** shows the spool that the Phoenix NFC reader made active: it asks Moonraker
(`/server/spoolman/spool_id`) and then Spoolman (`/api/v1/spool/<id>`), and displays the vendor,
filament, material, colour, diameter, temperatures, remaining weight and the NFC tag. It only
reads; nothing is written to the printer, to Spoolman or to Orca's presets.

Next steps (not done yet): write a filament preset from the active spool, and add a spool check
to the exported G-code.

## Install

1. In OrcaSlicer open **Plugins** and choose **Install local plugin**, then select
   `phoenix_spoolman.py`. (Or copy the file into its own folder under
   `<OrcaSlicer data dir>/orca_plugins/`, for example
   `~/.config/OrcaSlicer/orca_plugins/phoenix-spoolman/phoenix_spoolman.py`.)
2. Expand the plugin, find the **Active spool** script and press **Run**.
3. OrcaSlicer asks whether the plugin may contact the network. The plugin talks only to the
   Moonraker and Spoolman addresses in its configuration.

## Configuration

Plugins window → select the plugin → **Config** tab → **Active spool**. Defaults:

```json
{"moonraker_url": "http://192.168.1.36:7125", "spoolman_url": "http://192.168.1.36:7912", "timeout_seconds": 5}
```

Away from home with Tailscale, use the Phoenix's Tailscale address in both URLs.

## Tests

`python3 -m unittest tests/test_orca_plugin.py` runs the plugin against a stand-in `orca` module and
local fake Moonraker/Spoolman servers. They cannot replace a run inside OrcaSlicer, whose plugin API
is new and may change.
