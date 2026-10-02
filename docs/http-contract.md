# Reader → Phoenix daemon contract (0.5)

`POST http://<Phoenix>:7913/tag`, or a USB line `PHOENIX-TAG {json}`.
Only present, validated NFC observations are sent. JSON `schema_version: 1`;
`event_id` identifies a single observation (random session + counter). NFC-V UID
as uppercase hexadecimal, 16 digits, MSB first. The daemon also accepts UIDs with
colons and the legacy 0.4 field names.

Fields: `reader_id`, `firmware`, `instance_uuid`, `material_name`, `brand_name`,
`country_of_origin`, `material_class`, `material_type`, `filament_diameter`,
`density`, `nominal_netto_full_weight`, `actual_netto_full_weight`,
`empty_container_weight`, `min_print_temperature`, `max_print_temperature`,
`min_bed_temperature`, `max_bed_temperature`, `color_rgba` (#RRGGBBAA).
Missing fields are omitted. The daemon imports name, brand, RGB, diameter,
density, nominal weight and temperatures; it does not yet interpret the material
enums, the UUID or the actual/container weights. The tag is linked by UID: a tag
rewritten for another spool needs the link corrected by hand in Spoolman.

An HTTP 2xx confirms definitive handling; API errors return 502.
USB confirms with `ack <event_id>\n`; there is no ACK on errors.
Only consecutive identical events are deduplicated, for five seconds.
The firmware keeps only the latest observation and retries with backoff until the
tag is removed or replaced, with no persistent queue. After a reboot it sends
again: the daemon first resolves the UID in Spoolman and does not reset the
remaining weight.

Printer `printing`/`paused`: no spool change and no deferred activation.
The state check and the activation are two separate, non-atomic calls: avoid
scanning while a print is starting.

## Blank tag (firmware 0.6 and later)

A blank ICODE SLIX2 tag (UID `E00401…`, user blocks 0–78 all zero) is reported
**once per presence**, with `reader_id` and `"blank": true`:

- USB: line `PHOENIX-BLANK {json}`;
- WiFi: `POST http://<Phoenix>:7913/blank` (in `auto` mode it falls back to the
  USB line).

The daemon creates and activates nothing. If the printer is idle it forwards the
tag to Spoolman as `POST /api/v1/tag/scan` with `format: "openprinttag-blank"`;
otherwise it ignores it. See `docs/tag-writing.md`. There is no ACK and no retry:
remove the tag and place it again to repeat.

## Limits

Trusted local network, HTTP without authentication or TLS; synchronous POST;
USB 0.4 has no ACK/retry; spool creation and tag linking are separate calls (an
error between them can leave a spool without a tag, to be reconciled by hand).
Orca Slicer and the printer calibration are not modified.
