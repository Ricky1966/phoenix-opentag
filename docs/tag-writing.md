# Writing OpenPrintTag tags

Firmware `0.6.0-dev`. Validated on hardware (ESP32 + PN5180) on 1 October 2026, both
from the command line and from the Spoolman web UI.
Supported target: **NXP ICODE SLIX2, UID `E00401…`, 80 addressable blocks, but only
blocks 0–78 (316 bytes) are user memory: block 79 is the NXP counter and is never
written.**
The pins, the PN5180 instance and the hardware initialization are those of the
tested reader.

## Contract and limits

The firmware receives the **complete NFC-V image**: Capability Container, TLV, NDEF
and OpenPrintTag payload. It does not rebuild the tag from the few fields the reader
exposes: extra fields, regions and other records present in the accepted image are
kept byte for byte. The Spoolman fork produces this image with its encoder and the
official field catalog (see `docs/phoenix-writer.md` in the fork).

This is a **complete initialization/reinitialization**, not an update of the remaining
weight or of a single region. `replace` authorizes replacing previous data too, even
data unknown to the reader. Do not use this command for automatic updates of the
auxiliary region: the specification requires preserving offsets, metadata, unknown
fields and uninvolved regions.

Accepted layout: 4-byte CC `E1 40 <capacity/8> 01`, first NDEF TLV at byte 4, short or
extended length, `FE` terminator, optional zero padding. The image must be accepted by
the project's FFF parser and fit the 316 user bytes (CC `E1 40 27 01` at most); physical
bytes not supplied are zeroed. The parser checks the structure and the fields it knows;
it **does not replace the full validation against the official schema** for all extra
fields. Unsupported images are refused before any write. The official file `tag_03.bin`
is a malformed case and is refused on purpose.

No passwords, permanent locks, AFI or DSFID are set. The blocks to change must be
writable; if the protection status cannot be read, the operation stops. Tags protected
by a password are not supported. A tag is considered blank only if all 316 user bytes
are zero: an already formatted tag requires `replace` even if it holds no spool.

References: [official specification](https://github.com/OpenPrintTag/openprinttag-specification),
[NFC rules and region updates](https://github.com/OpenPrintTag/openprinttag-specification/blob/main/docs_src/nfc_technical_details.md),
[reference encoder](https://github.com/OpenPrintTag/openprinttag-specification/blob/main/utils/nfc_initialize.py).

## Algorithm

1. Read the UID, the geometry and all blocks, keeping a snapshot.
2. Before the commit, validate the image, read the tag again and compare the UID and
   the content with the snapshot. A tag that changed in the meantime is refused.
3. Require explicit authorization to replace if data exists. If the bytes are already
   identical, answer `unchanged` without writing anything.
4. Read the protections; write an empty NDEF TLV in block 1, then only the differing
   blocks and the CC. Every written block is read back immediately.
5. Compare the whole prepared image, publish the NDEF length in block 1 last, and
   compare all 316 bytes again.

All NFC read/write commands are addressed to the expected UID. There are no automatic
write retries and no rollback. The publication order reduces the risk of exposing
incomplete data, but **it does not make the EEPROM atomic**: an interruption can leave
a blank, partial or already written tag without confirmation. Only `verified` /
`unchanged` with `verified: true` confirm the outcome. After a lost response or
`may_have_changed: true`, inspect and compare before deciding on a new, explicit
attempt. The `write-info` command returns the dump needed for that; the client saves it
by itself before every replacement (see "Backup before replacing").

## Serial protocol, 115200 baud

Only one owner of the port: do not open a monitor, the CLI and the daemon at the same
time. The commands end with a newline. Replies start with `PHOENIX-WRITE ` followed by
JSON; logs and `PHOENIX-TAG` lines are not writer replies.

- `write-info`: enters write mode, pauses sending and reads the snapshot. Replies
  `status: ready`, `ticket` (16 hex digits), `expires_ms: 60000`, the canonical
  MSB-first UID without separators, `capacity: 316`, `block_size: 4`, `blank` and
  `memory_hex`. With no tag it replies with an error and authorizes no write.
- `write-commit <ticket> <new|replace> <image_hex>`: valid single-use ticket, image of
  10 to 316 bytes. `new` refuses occupied tags; `replace` is the explicit consent to
  replace all of the content. The ticket is consumed even if the commit fails.
- `write-cancel`: deletes the ticket. It does not interrupt a write already in
  progress: the firmware runs the commit synchronously before handling other commands.
- `write-resume`: lifts the reading pause so the tag on the reader is read again without
  being removed. Refused with `ticket_open` while a write ticket is still pending. The
  bridge sends it only after a verified write that has been linked to a spool.

The ticket expires after 60 seconds, does not survive a reboot and is not an
authentication credential. Access to the serial port must be reserved to the local
bridge. Lines longer than 768 characters are discarded entirely.

Commit reply: `status`, `verified`, `may_have_changed`, `block` (-1 if not applicable),
`writes` (write attempts). Protocol errors may contain only `status` and
`verified: false`.
Statuses: `verified`, `unchanged`, `no_tag_or_io_error`, `unsupported_tag`,
`read_failed`, `tag_changed`, `invalid_image`, `overwrite_required`,
`security_status_unavailable`, `locked`, `write_failed`, `verify_failed`; plus
ticket/command/format errors.

After success, error or cancellation, normal reads stay paused until the field is empty
for three consecutive detections (about 4.5 s), unless `write-resume` is sent. While a
ticket is still valid the session stays open until commit, cancellation or expiry.
This lets the bridge link the UID to the spool before a new read. The writer does not
change the data of the spool that is active in Moonraker. Do not start an OTA during a
write; the serial protocol remains available even when WiFi is absent.

## Manual test on Phoenix

First install the firmware of this branch (USB, or OTA with the existing procedure).
Copy `host/phoenix_opentag_write.py` next to the Python daemon; it only uses the
standard library and works with the CB1's Python 3.9.

Stop **only the OpenTag daemon** to free the serial port:

```bash
sudo systemctl stop phoenix-opentag
```

Place a single test tag and inspect it:

```bash
cd ~/phoenix-opentag/host
python3 phoenix_opentag_write.py \
  --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

The inspection does not write memory. Note the UID. To write an image prepared for that
spool (replace the example path and UID):

```bash
python3 phoenix_opentag_write.py \
  --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
  --image /path/to/spool.bin --expect-uid E004010866073472
```

Add `--replace` only if you want to erase/replace the existing content. The client
checks the UID and confirms with the firmware; it does not retry a failed commit.

### Backup before replacing

The firmware has no rollback: if a write is interrupted, the previous content of the tag
is lost. For this reason, when the tag is **not blank**, the client saves its memory
(exactly 316 bytes, blocks 0–78, from the `memory_hex` of `write-info`) to a file
**before** sending the commit:

- default folder `~/phoenix-tag-backups` (`--backup-dir` to change it, created with
  `0700` permissions);
- name `<UID>-<YYYYMMDD>-<HHMMSS>.bin`, permissions `0600`, written with `fsync` and read
  back for verification;
- an existing backup is never overwritten (a `-2`, `-3`… suffix is added);
- **if the copy fails** (disk full, folder not writable, dump not supplied by the
  firmware) the client cancels the session with `write-cancel` **without sending the
  commit**: the tag stays intact;
- the path appears in the result (`backup`) and in the error message if the write is not
  confirmed. A blank tag does not need one and produces no file.

Older backups of 320 bytes (made before the block-79 fix) are still accepted as `--image`:
the last four bytes (the counter) are dropped.

To restore the previous content, with the tag's UID and after removing and replacing the
tag:

```bash
python3 phoenix_opentag_write.py \
  --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
  --image ~/phoenix-tag-backups/E004010866073472-20260930-201500.bin \
  --expect-uid E004010866073472 --replace
```

A restore follows the same rules as any write: the backup must be an OpenPrintTag image
that is valid for the firmware (CC, NDEF TLV, terminator, the rest zero). If the tag held
something else, or data that was already corrupted, the file is still the faithful copy
of the bytes to start from. After a failed restore, or a tag left with an empty NDEF
(`03 00 FE 00` in block 1), `--replace` is needed because the tag is no longer blank.
Do not use the official fixtures on real spools: they contain sample identities/data.
Do not copy the same `instance_uuid` to several spools: the encoder must handle the
identity as the specification requires. Use only the ESP32's port, never a Klipper MCU.

After the test, even after an error, remove the tag and restart the daemon:

```bash
sudo systemctl start phoenix-opentag
```

## Automated tests

`bash tests/run_host.sh` and `python3 -m unittest discover -s tests -p 'test_*.py'`.
The simulations include interruptions at every write, failed verifications, protections,
a changed tag, expired/reused tickets, paused sending, an unwritable block 79 and a
non-zero block-79 counter. Physical testing is still needed after any change to the
firmware.

## Bridge for the Spoolman «Scrivi tag» button

The daemon exposes the authenticated API `/writer/{arm,prepare,commit,finish,cancel,reset}`.
It is disabled until the `PHOENIX_WRITER_TOKEN` variable is set (at least 32 random
characters). It requires `--serial`: the existing serial thread is the only owner of the
port, even when normal reads arrive over WiFi. Do not run the serial monitor or the manual
writer in parallel.

Deploy `host/phoenix_opentag_daemon.py`, `host/phoenix_opentag_write.py` and
`host/phoenix_writer_bridge.py` together. The firmware must include the 316-byte fix.
The systemd service keeps its arguments; set the token through the service
environment (for example `EnvironmentFile=`), never in the repository.
`PHOENIX_TAG_BACKUP_DIR` changes the backup directory (default `~/phoenix-tag-backups`).

The `Ricky1966/Spoolman` fork requires `SPOOLMAN_WRITER_TOKEN` with the same value and
`SPOOLMAN_WRITER_URL=http://192.168.1.36:7913` (an address reachable from the
container). Configure a single Spoolman process and a single USB reader. The complete
documentation is `docs/phoenix-writer.md` in the fork.

Opening the form reserves the reader **before** the user places the tag: the daemon
pauses the automatic processing of USB and HTTP reads, avoiding spool creation or
changes during the write. The preparation acquires the UID, the firmware ticket and the
original memory. The commit consumes the session, saves the backup before a replacement
and does not retry after errors or lost responses. Spoolman links the UID only after a
positive firmware verification. Closing the dialog releases the reader; after a verified
and linked write it also sends `write-resume`, so the tag is read again without being
removed and the spool becomes active.

After a closed page or a restart, remove the tag and use the explicit session recovery
in the form. A recovery neither repeats a write nor restores the memory. The API rejects
direct browser requests and requires Bearer auth; keep the services on a trusted network
or use TLS between machines.

Validation: unit tests, a local test with real HTTP, a Spoolman SQLite database, a browser
and an emulated serial line, and a physical test on Phoenix on 1 October 2026 (SLIX2
`E004010866073075`): recovery after an interruption with `--replace`, a write from the
Spoolman UI verified by the firmware, the tag linked to the spool, and a correct re-read
without creating a duplicate.

## Blank tag on the reader (offer to write)

When an ICODE SLIX2 tag (UID `E00401…`) whose blocks 0–78 are all zero is placed on the
reader, the firmware reports it **once per presence**:

- USB: line `PHOENIX-BLANK {"schema_version":1,"uid":"E004…","reader_id":"…","blank":true}`;
- WiFi: `POST /blank` with the same JSON (in `auto` mode it falls back to the USB line).

The daemon **creates and activates nothing**: if the printer is idle it forwards a scan
to Spoolman, `POST /api/v1/tag/scan` with `format: "openprinttag-blank"`, and Spoolman
asks the user for confirmation (see `docs/phoenix-writer.md` in the fork). The reader
stays "disarmed", meaning the signal is ignored, if:

- Moonraker is unreachable;
- `print_stats.state` is `printing` or `paused`;
- `idle_timeout.state` is `Printing` (Klipper is running G-code: macros, heating,
  filament load/unload);
- a write session is open.

After a verified and linked write, closing the session sends `write-resume` (not just
`write-cancel`): the firmware reads the tag again without it being removed, and the normal
pipeline recognizes it and makes the spool active.
