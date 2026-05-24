#!/usr/bin/env python3
"""
Assegna footprint SMD 0805 a tutti i passivi THT della main PCB:
  C1-C5  : Capacitor_THT → Capacitor_SMD:C_0805_2012Metric
  R1-R6  : Resistor_THT  → Resistor_SMD:R_0805_2012Metric
  D1     : LED_THT 5mm   → LED_SMD:LED_0805_2012Metric
  R6 val : 330 → 120 (due LED parallelo in camera oscura, ~5mA each)
"""

import re, sys

SCH = 'hardware/ESP32_AZDelivery_Baseboard.kicad_sch'

SMD_R   = 'Resistor_SMD:R_0805_2012Metric'
SMD_C   = 'Capacitor_SMD:C_0805_2012Metric'
SMD_LED = 'LED_SMD:LED_0805_2012Metric'

# (reference, new_footprint, new_value_or_None)
TARGETS = {
    'C1': (SMD_C,   None),
    'C2': (SMD_C,   None),
    'C3': (SMD_C,   None),
    'C4': (SMD_C,   None),
    'C5': (SMD_C,   None),
    'R1': (SMD_R,   None),
    'R2': (SMD_R,   None),
    'R3': (SMD_R,   None),
    'R4': (SMD_R,   None),
    'R5': (SMD_R,   None),
    'R6': (SMD_R,   '120'),   # due LED in parallelo → 120Ω
    'D1': (SMD_LED, None),
}

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

def set_property(block, prop_name, new_value):
    """Replace the value string of a property inside a block."""
    pattern = rf'(\(property "{re.escape(prop_name)}" )"([^"]*)"'
    replacement = rf'\1"{new_value}"'
    new_block, n = re.subn(pattern, replacement, block)
    return new_block, n > 0

with open(SCH) as f:
    content = f.read()

changed = {}

for start, end in extract_blocks(content, 'symbol'):
    block = content[start:end]
    # only instances (have lib_id), not lib_symbols definitions
    if '(lib_id ' not in block:
        continue
    ref_m = re.search(r'\(property "Reference" "([^"]+)"', block)
    if not ref_m:
        continue
    ref = ref_m.group(1)
    if ref not in TARGETS:
        continue

    new_fp, new_val = TARGETS[ref]
    new_block = block
    fp_ok = val_ok = False

    new_block, fp_ok = set_property(new_block, 'Footprint', new_fp)
    if new_val is not None:
        new_block, val_ok = set_property(new_block, 'Value', new_val)
    else:
        val_ok = True  # nothing to change

    if fp_ok:
        content = content[:start] + new_block + content[end:]
        changed[ref] = (new_fp, new_val)

with open(SCH, 'w') as f:
    f.write(content)

print(f"Aggiornati {len(changed)} componenti:")
for ref in sorted(changed):
    fp, val = changed[ref]
    fp_short = fp.split(':')[1]
    note = f" (valore→{val})" if val else ""
    print(f"  {ref}: {fp_short}{note}")

missing = [r for r in TARGETS if r not in changed]
if missing:
    print(f"WARNING: non trovati: {missing}")
