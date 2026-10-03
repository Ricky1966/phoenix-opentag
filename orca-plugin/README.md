# Phoenix Spoolman: OrcaSlicer plugin

> [!CAUTION]
> **Experimental hobby project: use it entirely at your own risk.** It comes with no warranty, and the author is **not responsible** for any damage to printers, PCs, hardware, software or data. Read the Disclaimer section of this README before you build, flash or run anything.
>
> *Progetto sperimentale: uso a proprio rischio e pericolo. L'autore non risponde di alcun danno a stampanti, computer, hardware, software o dati.*

## ⚠️ Disclaimer: use at your own risk

This OrcaSlicer plugin is an experimental hobby project, provided **"as is"**, without warranty of any kind. It means flashing firmware, wiring electronics, writing to NFC tags, sending commands to a printer's firmware (Klipper/Moonraker) and installing software on your computers. Mistakes, bugs or incompatibilities can cause failed prints, damage to your printer(s), your computer(s), the ESP32/PN5180 module, the tags or any other hardware or software, loss of data, or other harm.

By building, installing, running or modifying anything here you accept that you do so **entirely at your own risk**. The author and the contributors are **not responsible or liable** for any damage or loss of any kind: printers, PCs, hardware, software, data or anything else. Test carefully, never leave a printer unattended, back up your configuration before changing it, and read the code before you run it. The warranty disclaimer of the AGPL-3.0 licence (sections 15 and 16) applies as well.

*In italiano: progetto sperimentale da usare **a proprio rischio e pericolo**. L'autore non risponde di alcun danno a stampanti, computer, hardware, software o dati.*

A plugin for OrcaSlicer's Python plugin system, which exists in the **nightly builds** and in
releases **after 2.4.2** (OrcaSlicer 2.4.2 itself does not have it). Standard library only.

## What it does

**Active spool** (script) shows the spool that the Phoenix NFC reader made active: it asks Moonraker
(`/server/spoolman/spool_id`) and then Spoolman (`/api/v1/spool/<id>`), and displays the vendor,
filament, material, colour, diameter, temperatures, remaining weight and the NFC tag. It only
reads; nothing is written to the printer, to Spoolman or to Orca's presets.

**Active spool panel** (script, **new in 0.4.0, not yet run inside OrcaSlicer**) opens a panel docked beside the
3D view that follows the spool the Phoenix reader activated: swatch, vendor and name, material, diameter, nozzle and
bed temperature, remaining weight and NFC tag. Place a spool on the reader and the panel updates within a few
seconds (`poll_seconds`, default 3, minimum 1). A background thread asks Moonraker and Spoolman; the page never talks
to them. If Moonraker or Spoolman cannot be reached the panel keeps the last spool and shows the error. Run the script
once from the Plugins window; running it again just shows the same panel.

Since 0.5.0 the panel also shows the **filament profile currently selected in OrcaSlicer** and whether it matches the
active spool: the spool's material against the profile's `filament_type`, and the profile's nozzle temperature against
the spool's within `nozzle_temp_tolerance` (the same rule as *Spool check*). A mismatch is shown in red as soon as you
place a spool or change the profile (the page asks the plugin every 2 s, and the plugin reads the selection on the UI
thread). If the selection cannot be read, the panel just says so. The panel only reads: OrcaSlicer's plugin API gives
no way to change the selected filament preset, so it cannot select the spool's profile for you.

**Inspect preset API** (script, diagnostic) shows what the panel reads as the selected filament, followed by the members
of `orca.host.preset_bundle()`, in a dialog and in OrcaSlicer's Python log. Use it to see why the panel cannot read the
selection, if it ever cannot.

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

A mismatch is handled according to `on_mismatch`:

- `block` (default): the export stops with an OrcaSlicer error dialog, for example *"Post-processing
  plugin Spool check failed. Error: Spool #4 does not match the sliced profile: the spool is PLA but the
  profile sliced is ASA; nozzle 260 C sliced, the spool's usual temperature is 210 C"*. No G-code file
  is saved. (OrcaSlicer aborts an export whenever a post-processing plugin fails; a softer warning is not
  possible from a plugin.) The old value `warn` is treated as `block`.
- `ignore`: the file is exported and the comment block records `; MISMATCH: …` lines.

If Moonraker or
Spoolman cannot be reached, for example when slicing away from home, the check is skipped and the
G-code is exported unchanged apart from a comment: it never blocks an export.

**Limits.** The check happens at export time, not when the print starts: Klipper cannot ask Moonraker
which spool is active, so a spool changed after slicing is not noticed.

**Note (0.2.1).** OrcaSlicer's audit hook refuses reads of the Python interpreter's own files while a G-code file is being exported. The plugin therefore loads `encodings.idna` when it starts (Python imports it lazily to resolve a host name), and any unexpected error during the check skips it instead of stopping the export. In 0.2.0 that blocked read made the export fail silently: no error, no file.

Not done yet: writing a filament preset from the active spool.

## Install

1. Copy the file into its own folder under the OrcaSlicer data directory, with OrcaSlicer closed:
   `~/.config/OrcaSlicer/orca_plugins/phoenix-spoolman/phoenix_spoolman.py`. Start OrcaSlicer,
   open **Plugins**, press **Refresh** and tick **Activate**. (**Install local plugin** did not work
   in the nightly build tested on Linux; the folder copy does.)
2. Expand the plugin, find the **Active spool** script and press **Run**.
3. OrcaSlicer asks whether the plugin may contact the network. The plugin talks only to the
   Moonraker and Spoolman addresses in its configuration.
4. **Active spool panel** and **Inspect preset API** are scripts: expand the plugin and press **Run**. **Spool check** is a slicing-pipeline plugin: in the **Process** settings, tab **Others**, section **Slicing Pipeline Plugin**, press **Add plugin** and pick "Spool check". Slice, export a G-code file and open its last lines.

## Configuration

Plugins window → select the plugin → **Config** tab → choose **Active spool** or **Spool check** (each has its own configuration; `on_mismatch` and `nozzle_temp_tolerance` only matter for **Spool check**). Defaults:

```json
{"moonraker_url": "http://192.168.1.36:7125", "spoolman_url": "http://192.168.1.36:7912", "timeout_seconds": 5,
 "on_mismatch": "block", "nozzle_temp_tolerance": 25, "poll_seconds": 3}
```

Away from home with Tailscale, use the Phoenix's Tailscale address in both URLs.

## Tests

`python3 -m unittest tests/test_orca_plugin.py` runs the plugin against a stand-in `orca` module and
local fake Moonraker/Spoolman servers. They cannot replace a run inside OrcaSlicer, whose plugin API
is new and may change.
