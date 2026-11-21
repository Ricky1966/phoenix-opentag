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

**Active spool panel** (script, verified with OrcaSlicer 2.5.1, version 0.6.6)
opens automatically when the plugin loads, unless `auto_open_panel` is disabled.
It can also be opened manually from the Plugins window.

The docked panel displays the active Moonraker/Spoolman spool, its material,
colour, temperatures, remaining weight and NFC information. It also displays
the Spoolman inventory and the filament profiles selected in OrcaSlicer.

The panel compares the active filament with Orca's selected profiles, checking
material and nozzle temperature and highlighting possible mismatches.

**Filament preset associations:** users can associate a Spoolman filament with
one or more existing OrcaSlicer presets. Associations are saved persistently.
When the active spool changes, a single associated preset is selected
automatically in Orca. If several presets are associated, the user can choose
which one to use and select the target filament slot.

Associations can also be removed from the panel, with confirmation.
The Refresh action requests updated spool, inventory and preset information.

The panel uses background worker threads so network polling does not block
the user interface. Moonraker and Spoolman permissions are requested together
for the default host `phoenix.local`; alternative hosts may require separate
authorization. Network failures are reported in the panel.

The plugin selects existing OrcaSlicer presets; it does not create or rewrite
filament preset files.

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

Not implemented: generating a new OrcaSlicer filament preset from Spoolman metadata. Selecting existing associated presets is supported.

## Install

Verified with OrcaSlicer 2.5.1 on Linux.

1. Open OrcaSlicer's Plugins manager and install the local
   `phoenix_spoolman.py` file.
2. Activate the plugin if necessary and grant its requested permissions.
   The default configuration requests network access to `phoenix.local`
   and permission to create threads.
3. The Active spool panel opens automatically and loads the current
   Moonraker/Spoolman state. It can also be opened manually from Plugins.
4. Configure Moonraker and Spoolman URLs if they differ from the defaults.
5. Optionally add **Spool check** under Process settings, Others,
   Slicing Pipeline Plugin, to validate exported G-code.

The **Active spool** and **Inspect preset API** diagnostic scripts remain
available in the Plugins manager.

No modification to OrcaSlicer's C++ code is required.

## Configuration

Plugins window → select the plugin → **Config** tab → configure the
appropriate capability. **Active spool panel** controls automatic opening,
polling intervals, connections and filament associations. **Spool check**
uses `on_mismatch` to decide whether to block an incompatible G-code export.
`nozzle_temp_tolerance` is used by both the panel and Spool check.

Defaults:

```json
{"moonraker_url": "http://phoenix.local:7125", "spoolman_url": "http://phoenix.local:7912", "timeout_seconds": 5,
 "on_mismatch": "block", "nozzle_temp_tolerance": 25, "poll_seconds": 3, "inventory_poll_seconds": 30, "auto_open_panel": true}
```

Away from home with Tailscale, use the Phoenix's Tailscale address in both URLs.

## Tests

`python3 -m unittest tests/test_orca_plugin.py` runs the plugin against a stand-in `orca` module and
local fake Moonraker/Spoolman servers. They cannot replace a run inside OrcaSlicer, whose plugin API
is new and may change.
