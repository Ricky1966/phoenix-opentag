#!/usr/bin/env python3
"""
Fix labels in the new-components area (y > 160).

Situation: J16 and J17 were moved by the user in KiCad from their
original rot=0 positions to rot=270 at y=193.04. J18 was also moved
then replaced by U1 (PCF8574AP), but the patch script could not find
J18's labels at the new position so they were left behind.

This script:
  1. Removes J16/J17 labels at wrong positions or with wrong net names.
  2. Removes 8 orphaned labels left over from the deleted J18 connector.
  3. Adds correct labels at the actual J16/J17 pin positions.

Everything else (R5/D1, R6/D2, U1 PCF8574, C5) is already correct.
"""

import re
import uuid
import sys

SCH = 'hardware/ESP32_AZDelivery_Baseboard.kicad_sch'


def _justify(rot):
    return 'left bottom' if rot in (0, 90) else 'right bottom'


def label(text, x, y, rot):
    j = _justify(rot)
    return (
        f'\t(label "{text}"\n'
        f'\t\t(at {x:.2f} {y:.2f} {rot})\n'
        f'\t\t(effects\n'
        f'\t\t\t(font\n'
        f'\t\t\t\t(size 1.27 1.27)\n'
        f'\t\t\t)\n'
        f'\t\t\t(justify {j})\n'
        f'\t\t)\n'
        f'\t\t(uuid "{uuid.uuid4()}")\n'
        f'\t)\n'
    )


def extract_blocks(content, tag):
    """Return list of (start_char, end_char) for every top-level (tag block."""
    blocks = []
    search = f'\t({tag}'
    i = 0
    while True:
        idx = content.find(search, i)
        if idx == -1:
            break
        after = content[idx + len(search):idx + len(search) + 1]
        if after not in (' ', '\n', '\r', '"'):
            i = idx + 1
            continue
        depth = 0
        j = idx
        while j < len(content):
            if content[j] == '(':
                depth += 1
            elif content[j] == ')':
                depth -= 1
                if depth == 0:
                    blocks.append((idx, j + 1))
                    i = j + 1
                    break
            j += 1
        else:
            break
    return blocks


def remove_label(content, rx, ry, rrot, rtext):
    """Remove a single label matching (x,y,rot,text). Returns updated content."""
    for start, end in extract_blocks(content, 'label'):
        blk = content[start:end]
        at_m = re.search(r'\(at\s+([\d.]+)\s+([\d.]+)(?:\s+([\d.]+))?\)', blk)
        txt_m = re.search(r'\(label\s+"([^"]*)"', blk)
        if not at_m or not txt_m:
            continue
        bx = float(at_m.group(1))
        by = float(at_m.group(2))
        brot = float(at_m.group(3)) if at_m.group(3) else 0
        btxt = txt_m.group(1)
        if abs(bx - rx) < 0.01 and abs(by - ry) < 0.01 and abs(brot - rrot) < 0.1 and btxt == rtext:
            tail = '\n' if content[end:end + 1] == '\n' else ''
            content = content[:start] + content[end + len(tail):]
            return content, True
    return content, False


# ---------------------------------------------------------------------------
# Labels to remove: (x, y, rot, text)
# ---------------------------------------------------------------------------
REMOVE = [
    # J16 — wrong position or wrong net name
    (74.93,  187.96,  90, 'OPT4048_INT'),
    (77.47,  187.96,  90, 'GPIO22_I2C_SCL'),
    (80.01,  187.96,  90, 'GPIO21_I2C_SDA'),
    (85.09,  187.96,  90, '3V3'),
    # J17 — wrong position or wrong net name
    (97.79,  187.96,  90, 'GPIO14'),
    (102.87, 187.96,  90, 'PCF8574_P0'),
    (105.41, 187.96,  90, 'GND'),
    # Orphaned J18 labels (J18 connector was removed but labels were missed)
    (116.84, 187.96,  90, 'PCF8574_P3'),
    (119.38, 187.96,  90, 'PCF8574_P2'),
    (121.92, 187.96,  90, 'PCF8574_P1'),
    (124.46, 187.96,  90, 'PCF8574_P0'),
    (127.00, 187.96,  90, 'GPIO22_I2C_SCL'),
    (129.54, 187.96,  90, 'GPIO21_I2C_SDA'),
    (132.08, 187.96,  90, 'GND'),
    (134.62, 187.96,  90, '3V3'),
]

# ---------------------------------------------------------------------------
# Labels to add (correct positions for J16/J17 at rot=270, y=187.96)
# ---------------------------------------------------------------------------
# J16 (OPT4048) at (82.55, 193.04, rot=270):
#   Pin1=3V3  Pin2=GND(exists)  Pin3=SDA  Pin4=SCL  Pin5=INT
# J17 (NEMA17) at (102.87, 193.04, rot=270):
#   Pin1=GPIO13(exists)  Pin2=GPIO14  Pin3=PCF8574_P0  Pin4=GND
ADD = [
    ('3V3',             80.01, 187.96, 90),
    ('GPIO21_I2C_SDA',  85.09, 187.96, 90),
    ('GPIO22_I2C_SCL',  87.63, 187.96, 90),
    ('OPT4048_INT',     90.17, 187.96, 90),
    ('GPIO14',         102.87, 187.96, 90),
    ('PCF8574_P0',     105.41, 187.96, 90),
    ('GND',            107.95, 187.96, 90),
]


def main():
    with open(SCH, 'r') as f:
        content = f.read()

    removed = 0
    for rx, ry, rrot, rtext in REMOVE:
        content, ok = remove_label(content, rx, ry, rrot, rtext)
        if ok:
            removed += 1
        else:
            print(f'  WARNING: not found: ({rx},{ry},{rrot}) "{rtext}"')

    print(f'Removed {removed}/{len(REMOVE)} labels')

    new_labels = ''.join(label(t, x, y, r) for t, x, y, r in ADD)
    marker = '\t(sheet_instances'
    idx = content.find(marker)
    if idx == -1:
        print('ERROR: (sheet_instances marker not found')
        sys.exit(1)
    content = content[:idx] + new_labels + content[idx:]
    print(f'Added {len(ADD)} labels')

    with open(SCH, 'w') as f:
        f.write(content)

    print('Done.')


if __name__ == '__main__':
    main()
