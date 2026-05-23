#!/usr/bin/env python3
"""
Seconda passata di fix label per J16/J17.

Il problema: l'utente ha spostato J16/J17 in KiCad e i label si sono
spostati con loro, poi ha riposizionato i connettori ma i label sono
rimasti nelle posizioni sbagliate con net name sbagliati.

Fix:
  1. Rimuove tutti i label a y=187.96 tranne GND di C5 a x=137.16
  2. Ri-aggiunge label corretti per J16 e J17
  3. Rimuove il wire stub (158.75, 165.86)→(158.75, 166.37) che va nel vuoto
"""

import re
import uuid
import sys

SCH = 'hardware/ESP32_AZDelivery_Baseboard.kicad_sch'


def justify(rot):
    return 'left bottom' if rot in (0, 90) else 'right bottom'


def label(text, x, y, rot):
    return (
        f'\t(label "{text}"\n'
        f'\t\t(at {x:.2f} {y:.2f} {rot})\n'
        f'\t\t(effects\n'
        f'\t\t\t(font\n'
        f'\t\t\t\t(size 1.27 1.27)\n'
        f'\t\t\t)\n'
        f'\t\t\t(justify {justify(rot)})\n'
        f'\t\t)\n'
        f'\t\t(uuid "{uuid.uuid4()}")\n'
        f'\t)\n'
    )


def extract_blocks(content, tag):
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
        depth, j = 0, idx
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


def remove_block(content, start, end):
    tail = '\n' if content[end:end + 1] == '\n' else ''
    return content[:start] + content[end + len(tail):]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    with open(SCH, 'r') as f:
        content = f.read()

    # --- 1. Rimuovi tutti i label a y≈187.96 tranne C5 GND a x=137.16 ---
    removed = 0
    changed = True
    while changed:
        changed = False
        for start, end in extract_blocks(content, 'label'):
            blk = content[start:end]
            at_m = re.search(r'\(at\s+([\d.]+)\s+([\d.]+)(?:\s+([\d.]+))?\)', blk)
            if not at_m:
                continue
            x, y = float(at_m.group(1)), float(at_m.group(2))
            if abs(y - 187.96) < 0.01 and abs(x - 137.16) > 0.01:
                content = remove_block(content, start, end)
                removed += 1
                changed = True
                break  # restart after mutation

    print(f'Rimossi {removed} label errati a y=187.96')

    # --- 2. Aggiungi label corretti ---
    # J16 (OPT4048) a (82.55, 193.04, rot=270) → pin a y=187.96
    #   Pin1=3V3, Pin2=GND, Pin3=SDA, Pin4=SCL, Pin5=INT
    # J17 (NEMA17)  a (102.87, 193.04, rot=270) → pin a y=187.96
    #   Pin1=GPIO13, Pin2=GPIO14, Pin3=PCF8574_P0, Pin4=GND
    add = [
        ('3V3',            80.01, 187.96, 90),
        ('GND',            82.55, 187.96, 90),
        ('GPIO21_I2C_SDA', 85.09, 187.96, 90),
        ('GPIO22_I2C_SCL', 87.63, 187.96, 90),
        ('OPT4048_INT',    90.17, 187.96, 90),
        ('GPIO13',        100.33, 187.96, 90),
        ('GPIO14',        102.87, 187.96, 90),
        ('PCF8574_P0',    105.41, 187.96, 90),
        ('GND',           107.95, 187.96, 90),
    ]
    marker = '\t(sheet_instances'
    idx = content.find(marker)
    if idx == -1:
        print('ERRORE: (sheet_instances non trovato')
        sys.exit(1)
    new_labels = ''.join(label(t, x, y, r) for t, x, y, r in add)
    content = content[:idx] + new_labels + content[idx:]
    print(f'Aggiunti {len(add)} label corretti')

    # --- 3. Rimuovi wire stub (158.75, 165.86)→(158.75, 166.37) ---
    wire_removed = 0
    for start, end in extract_blocks(content, 'wire'):
        blk = content[start:end]
        pts = re.findall(r'\(xy\s+([\d.]+)\s+([\d.]+)\)', blk)
        if len(pts) < 2:
            continue
        coords = [(float(x), float(y)) for x, y in pts]
        target = {(158.75, 165.86), (158.75, 166.37)}
        if set(coords) == target:
            content = remove_block(content, start, end)
            wire_removed += 1
            break

    print(f'Rimossi {wire_removed} wire stub orfano')

    with open(SCH, 'w') as f:
        f.write(content)

    print('Fatto.')


if __name__ == '__main__':
    main()
