# Third-party components

Phoenix OpenTag is distributed under the GNU Affero General Public License v3.0 or later (see `LICENSE`).
It includes the following components that are not covered by that licence. Each keeps its own
licence text, in the directory named below.

## PN5180 Library (modified)

- Location: `lib/PN5180-Library/` (licence: `lib/PN5180-Library/LICENSE`).
- Author: Andreas Trappmann. Upstream: <https://github.com/ATrappmann/PN5180-Library>, version 1.8.1
  (commit `cf61c3a`, 2021-08-19).
- Licence: GNU Lesser General Public License, version 2.1 or (at your option) any later version.
  This is what allows its use as part of a program distributed under the AGPL-3.0.
- Included in this repository on 2026-05-10 and **modified** between 2026-05-10 and 2026-09-30.
  The files below differ from upstream, and carry a notice at the top; the other files are
  unchanged.
  - `PN5180.cpp`: SPI clock lowered from 7 MHz to 1 MHz; a wait for the BUSY pin with a timeout;
    the command helpers now return the success status of the transfer instead of always `true`.
  - `PN5180ISO15693.cpp`: checks on the length of the responses; the write-single-block command
    rewritten (addressed by UID, with the block size checked); new `getSystemInfo()` and
    `getBlockSecurityStatus()`.
  - `PN5180ISO15693.h`: declarations for the above, an `#include <Arduino.h>`, a `responseLength`
    member, and the `strerror` parameter renamed.

You can get the corresponding upstream files from the address above and compare them with the ones
here to see every change.

## OpenPrintTag test data

- Location: `tests/fixtures/` (licence: `tests/fixtures/LICENSE.openprinttag`, MIT).
- Copyright 2025 Prusa Research a.s. Sample tag data taken from
  <https://github.com/OpenPrintTag/openprinttag-specification>, used only by the tests.
  See `tests/fixtures/README.md` for the exact source files.

## Origin of this project

Phoenix OpenTag began as a reduced version of Odin-ESP32 (<https://github.com/Ricky1966/Odin-ESP32>,
AGPL-3.0-or-later, same author): the hardware initialisation comes from Odin's `develop` branch.
