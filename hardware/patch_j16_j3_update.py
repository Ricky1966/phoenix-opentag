#!/usr/bin/env python3
"""
Aggiorna J16 (Conn_01x05→Conn_01x08 FFC) e J3 (Conn_01x13→Conn_01x10 FFC).

J16 (OPT4048 + LED camera + runout):
  - Sposta cx da 87.63 a 80.01 (8 pin non overlappano J17)
  - Pin 1=3V3, 2=GND, 3=SDA, 4=SCL, 5=OPT4048_INT,
         6=LED_DARK_A, 7=PCF8574_P1, 8=PCF8574_P2
  - Rimuove D2 (LED va nella scatolina FreeCAD)
  - Mantiene R6 (resistenza di limitazione rimane su PCB)
  - Aggiunge label LED_DARK_A su R6 Pin2

J3 (PN5180 FFC 10-pin):
  - Cambia cy da 73.66 a 71.12 (pin 1 rimane a y=68.58)
  - Rimuove 4 no_connect dei pin inutilizzati
  - Aggiunge pin 10 (secondo GND, y=91.44)
"""

import re, uuid, sys

SCH = 'hardware/ESP32_AZDelivery_Baseboard.kicad_sch'


# ---------------------------------------------------------------------------
# Helper: label S-expression
# ---------------------------------------------------------------------------
def _just(rot):
    return 'left bottom' if rot in (0, 90) else 'right bottom'

def label(text, x, y, rot):
    return (
        f'\t(label "{text}"\n'
        f'\t\t(at {x:.2f} {y:.2f} {rot})\n'
        f'\t\t(effects\n'
        f'\t\t\t(font (size 1.27 1.27))\n'
        f'\t\t\t(justify {_just(rot)})\n'
        f'\t\t)\n'
        f'\t\t(uuid "{uuid.uuid4()}")\n'
        f'\t)\n'
    )

def wire(x1, y1, x2, y2):
    return (
        f'\t(wire\n'
        f'\t\t(pts (xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f}))\n'
        f'\t\t(stroke (width 0) (type default))\n'
        f'\t\t(uuid "{uuid.uuid4()}")\n'
        f'\t)\n'
    )


# ---------------------------------------------------------------------------
# Helper: find and remove a top-level block
# ---------------------------------------------------------------------------
def find_block(content, tag, test_fn):
    """Return (start, end) of first block '\t(tag ...' matching test_fn."""
    search = f'\t({tag}'
    i = 0
    while True:
        idx = content.find(search, i)
        if idx == -1:
            return None, None
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
                    break
            j += 1
        blk = content[idx:j + 1]
        if test_fn(blk):
            return idx, j + 1
        i = j + 1
    return None, None


def remove_block(content, start, end):
    tail = '\n' if content[end:end + 1] == '\n' else ''
    return content[:start] + content[end + len(tail):]


# ---------------------------------------------------------------------------
# 1. Conn_01x10 lib_symbol (based on Conn_01x08 + 2 pins)
# ---------------------------------------------------------------------------

def _pin_lib(n):
    """S-expr for pin N in Connector_Generic style."""
    y = 2.54 - (n - 1) * 2.54
    ry = y + 0.127
    ry2 = y - 0.127
    return (
        f'\t\t\t\t(rectangle\n'
        f'\t\t\t\t\t(start -1.27 {ry:.3f})\n'
        f'\t\t\t\t\t(end 0 {ry2:.3f})\n'
        f'\t\t\t\t\t(stroke (width 0.1524) (type default))\n'
        f'\t\t\t\t\t(fill (type none))\n'
        f'\t\t\t\t)\n'
        f'\t\t\t\t(pin passive line\n'
        f'\t\t\t\t\t(at -5.08 {y:.2f} 0)\n'
        f'\t\t\t\t\t(length 3.81)\n'
        f'\t\t\t\t\t(name "Pin_{n}"\n'
        f'\t\t\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        f'\t\t\t\t\t)\n'
        f'\t\t\t\t\t(number "{n}"\n'
        f'\t\t\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        f'\t\t\t\t\t)\n'
        f'\t\t\t\t)\n'
    )

# Body: pin 1 top=3.81+(gap), pin N bottom= -(N-1)*2.54-1.27*2
# For N=10: body from 3.81 to -(10-1)*2.54-1.27 = -22.86+1.27 = -21.59... like Conn_01x08
# Conn_01x08 body end = -16.51 = -(8-1)*2.54 - 1.27 - 1.27 + 1.27... let me just extrapolate
# Conn_01x08: N=8, body_end_y = -16.51 = -2*(8)+?
# Formula: body_end_y = 3.81 - N*2.54 - 1.27 = 3.81 - 8*2.54 - 1.27 = 3.81-20.32-1.27=-17.78 NO
# Empirically: Conn_01x08 = -16.51; adding 2 pins = -16.51 - 2*2.54 = -21.59

def conn_01x10_libsym():
    body_end = -16.51 - 2 * 2.54  # = -21.59
    val_y = body_end - 1.27        # label position below body
    pins_str = ''
    for n in range(1, 11):
        pins_str += _pin_lib(n)

    return (
        '\t\t(symbol "Connector_Generic:Conn_01x10"\n'
        '\t\t\t(pin_names\n'
        '\t\t\t\t(offset 1.016)\n'
        '\t\t\t\t(hide yes)\n'
        '\t\t\t)\n'
        '\t\t\t(exclude_from_sim no)\n'
        '\t\t\t(in_bom yes)\n'
        '\t\t\t(on_board yes)\n'
        '\t\t\t(in_pos_files yes)\n'
        '\t\t\t(duplicate_pin_numbers_are_jumpers no)\n'
        '\t\t\t(property "Reference" "J"\n'
        '\t\t\t\t(at 0 5.08 0)\n'
        '\t\t\t\t(show_name no)\n'
        '\t\t\t\t(do_not_autoplace no)\n'
        '\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t\t)\n'
        f'\t\t\t(property "Value" "Conn_01x10"\n'
        f'\t\t\t\t(at 0 {val_y:.2f} 0)\n'
        '\t\t\t\t(show_name no)\n'
        '\t\t\t\t(do_not_autoplace no)\n'
        '\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t\t)\n'
        '\t\t\t(property "Footprint" ""\n'
        '\t\t\t\t(at 0 0 0)\n'
        '\t\t\t\t(show_name no)\n'
        '\t\t\t\t(do_not_autoplace no)\n'
        '\t\t\t\t(hide yes)\n'
        '\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t\t)\n'
        '\t\t\t(property "Datasheet" ""\n'
        '\t\t\t\t(at 0 0 0)\n'
        '\t\t\t\t(show_name no)\n'
        '\t\t\t\t(do_not_autoplace no)\n'
        '\t\t\t\t(hide yes)\n'
        '\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t\t)\n'
        '\t\t\t(property "Description" "Generic connector, single row, 01x10"\n'
        '\t\t\t\t(at 0 0 0)\n'
        '\t\t\t\t(show_name no)\n'
        '\t\t\t\t(do_not_autoplace no)\n'
        '\t\t\t\t(hide yes)\n'
        '\t\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t\t)\n'
        '\t\t\t(symbol "Conn_01x10_1_1"\n'
        f'\t\t\t\t(rectangle\n'
        f'\t\t\t\t\t(start -1.27 3.81)\n'
        f'\t\t\t\t\t(end 1.27 {body_end:.2f})\n'
        '\t\t\t\t\t(stroke (width 0.254) (type default))\n'
        '\t\t\t\t\t(fill (type background))\n'
        '\t\t\t\t)\n'
        + pins_str +
        '\t\t\t)\n'
        '\t\t\t(embedded_fonts no)\n'
        '\t\t)\n'
    )


# ---------------------------------------------------------------------------
# 2. J16 new symbol instance (Conn_01x08 at cx=80.01)
# ---------------------------------------------------------------------------

def j16_instance(old_block):
    """Build new J16 symbol instance keeping UUID and some properties."""
    uuid_m = re.search(r'\(uuid\s+"([^"]+)"', old_block)
    sym_uuid = uuid_m.group(1) if uuid_m else str(uuid.uuid4())
    pins = ''.join(
        f'\t\t(pin "{n}"\n\t\t\t(uuid "{uuid.uuid4()}")\n\t\t)\n'
        for n in range(1, 9)
    )
    return (
        '\t(symbol\n'
        '\t\t(lib_id "Connector_Generic:Conn_01x08")\n'
        '\t\t(at 80.01 193.04 270)\n'
        '\t\t(unit 1)\n'
        '\t\t(body_style 1)\n'
        '\t\t(exclude_from_sim no)\n'
        '\t\t(in_bom yes)\n'
        '\t\t(on_board yes)\n'
        '\t\t(in_pos_files yes)\n'
        '\t\t(dnp no)\n'
        f'\t\t(uuid "{sym_uuid}")\n'
        '\t\t(property "Reference" "J16"\n'
        '\t\t\t(at 68.58 193.04 90)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        '\t\t(property "Value" "OPT4048+LED+RUNOUT"\n'
        '\t\t\t(at 79.01 196.34 90)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        '\t\t(property "Footprint" ""\n'
        '\t\t\t(at 80.01 193.04 0)\n'
        '\t\t\t(hide yes)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        '\t\t(property "Datasheet" ""\n'
        '\t\t\t(at 80.01 193.04 0)\n'
        '\t\t\t(hide yes)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        '\t\t(property "Description" '
        '"OPT4048 color sensor + dark LED + runout: 3V3,GND,SDA,SCL,INT,LED+,LED-,RUNOUT"\n'
        '\t\t\t(at 80.01 193.04 0)\n'
        '\t\t\t(hide yes)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        + pins +
        '\t\t(instances\n'
        '\t\t\t(project "ESP32_AZDelivery_Baseboard"\n'
        f'\t\t\t\t(path "/6dfc456e-f0f7-4393-96f4-0b7c9f575168"\n'
        '\t\t\t\t\t(reference "J16")\n'
        '\t\t\t\t\t(unit 1)\n'
        '\t\t\t\t)\n'
        '\t\t\t)\n'
        '\t\t)\n'
        '\t)\n'
    )


# ---------------------------------------------------------------------------
# 3. J3 new symbol instance (Conn_01x10 at cy=71.12)
# ---------------------------------------------------------------------------

def j3_instance(old_block):
    uuid_m = re.search(r'\(uuid\s+"([^"]+)"', old_block)
    sym_uuid = uuid_m.group(1) if uuid_m else str(uuid.uuid4())
    pins = ''.join(
        f'\t\t(pin "{n}"\n\t\t\t(uuid "{uuid.uuid4()}")\n\t\t)\n'
        for n in range(1, 11)
    )
    return (
        '\t(symbol\n'
        '\t\t(lib_id "Connector_Generic:Conn_01x10")\n'
        '\t\t(at 209.55 71.12 180)\n'
        '\t\t(unit 1)\n'
        '\t\t(body_style 1)\n'
        '\t\t(exclude_from_sim no)\n'
        '\t\t(in_bom yes)\n'
        '\t\t(on_board yes)\n'
        '\t\t(in_pos_files yes)\n'
        '\t\t(dnp no)\n'
        f'\t\t(uuid "{sym_uuid}")\n'
        '\t\t(property "Reference" "J3"\n'
        '\t\t\t(at 218.694 49.022 0)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)) (justify left))\n'
        '\t\t)\n'
        '\t\t(property "Value" "PN5180_FFC10"\n'
        '\t\t\t(at 230.378 49.022 0)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)) (justify left))\n'
        '\t\t)\n'
        '\t\t(property "Footprint" ""\n'
        '\t\t\t(at 209.55 71.12 0)\n'
        '\t\t\t(hide yes)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        '\t\t(property "Datasheet" ""\n'
        '\t\t\t(at 209.55 71.12 0)\n'
        '\t\t\t(hide yes)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        '\t\t(property "Description" '
        '"PN5180 NFC FFC 10-pin: GND,BUSY,SCK,MISO,MOSI,NSS,RST,3V3,5V,GND"\n'
        '\t\t\t(at 209.55 71.12 0)\n'
        '\t\t\t(hide yes)\n'
        '\t\t\t(show_name no)\n'
        '\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects (font (size 1.27 1.27)))\n'
        '\t\t)\n'
        + pins +
        '\t\t(instances\n'
        '\t\t\t(project "ESP32_AZDelivery_Baseboard"\n'
        f'\t\t\t\t(path "/6dfc456e-f0f7-4393-96f4-0b7c9f575168"\n'
        '\t\t\t\t\t(reference "J3")\n'
        '\t\t\t\t\t(unit 1)\n'
        '\t\t\t\t)\n'
        '\t\t\t)\n'
        '\t\t)\n'
        '\t)\n'
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    with open(SCH, 'r') as f:
        content = f.read()

    # ------------------------------------------------------------------ #
    # A. Add Conn_01x10 to lib_symbols (after Conn_01x08 block)
    # ------------------------------------------------------------------ #
    if '"Connector_Generic:Conn_01x10"' in content:
        print('Conn_01x10 già presente in lib_symbols')
    else:
        marker = '\t\t(symbol "Connector_Generic:Conn_01x08"'
        idx = content.find(marker)
        if idx == -1:
            print('ERRORE: Conn_01x08 non trovato in lib_symbols')
            sys.exit(1)
        # find end of Conn_01x08 block
        depth, j = 0, idx
        while j < len(content):
            if content[j] == '(': depth += 1
            elif content[j] == ')':
                depth -= 1
                if depth == 0: break
            j += 1
        insert_after = j + 1
        if content[insert_after:insert_after+1] == '\n':
            insert_after += 1
        new_sym = conn_01x10_libsym()
        content = content[:insert_after] + new_sym + content[insert_after:]
        print('Aggiunto Conn_01x10 a lib_symbols')

    # ------------------------------------------------------------------ #
    # B. Replace J16 symbol instance
    # ------------------------------------------------------------------ #
    start, end = find_block(content, 'symbol',
                             lambda b: '"Reference" "J16"' in b)
    if start is None:
        print('ERRORE: J16 non trovato')
        sys.exit(1)
    old_j16 = content[start:end]
    new_j16 = j16_instance(old_j16)
    content = content[:start] + new_j16 + content[end + (1 if content[end:end+1]=='\n' else 0):]
    print('Sostituito J16 → Conn_01x08 at cx=80.01')

    # ------------------------------------------------------------------ #
    # C. Replace J3 symbol instance
    # ------------------------------------------------------------------ #
    start, end = find_block(content, 'symbol',
                             lambda b: '"Reference" "J3"' in b and 'Conn_01x' in b)
    if start is None:
        print('ERRORE: J3 non trovato')
        sys.exit(1)
    old_j3 = content[start:end]
    new_j3 = j3_instance(old_j3)
    content = content[:start] + new_j3 + content[end + (1 if content[end:end+1]=='\n' else 0):]
    print('Sostituito J3 → Conn_01x10 at cy=71.12')

    # ------------------------------------------------------------------ #
    # D. Remove 4 NC markers for old J3 unused pins (x=214.63, y=58..67)
    # ------------------------------------------------------------------ #
    nc_removed = 0
    for target_y in [58.42, 60.96, 63.50, 66.04]:
        changed = True
        while changed:
            changed = False
            start, end = find_block(content, 'no_connect',
                lambda b, ty=target_y: (
                    re.search(r'\(at\s+214\.63\s+([\d.]+)\)', b) is not None and
                    abs(float(re.search(r'\(at\s+214\.63\s+([\d.]+)\)', b).group(1)) - ty) < 0.01
                ))
            if start is not None:
                content = remove_block(content, start, end)
                nc_removed += 1
                changed = True
    print(f'Rimossi {nc_removed} no_connect J3 inutilizzati')

    # ------------------------------------------------------------------ #
    # E. Remove D2 symbol instance
    # ------------------------------------------------------------------ #
    start, end = find_block(content, 'symbol',
                             lambda b: '"Reference" "D2"' in b)
    if start is not None:
        content = remove_block(content, start, end)
        print('Rimosso D2 (LED va nella scatolina FreeCAD)')
    else:
        print('WARNING: D2 non trovato')

    # ------------------------------------------------------------------ #
    # F. Remove D2 cathode label PCF8574_P1 at (158.75, 182.88, 270)
    # ------------------------------------------------------------------ #
    removed = False
    start, end = find_block(content, 'label',
        lambda b: 'PCF8574_P1' in b and
                  re.search(r'\(at\s+158\.75\s+182\.88\s+270\)', b) is not None)
    if start is not None:
        content = remove_block(content, start, end)
        removed = True
    print(f'Label PCF8574_P1@D2 {"rimosso" if removed else "non trovato"}')

    # ------------------------------------------------------------------ #
    # G. Remove wire R6 Pin2 → D2 Anode: (158.75,173.48)→(158.75,175.26)
    # ------------------------------------------------------------------ #
    wire_removed = False
    start, end = find_block(content, 'wire',
        lambda b: (
            re.search(r'\(xy\s+158\.75\s+173\.48\)', b) and
            re.search(r'\(xy\s+158\.75\s+175\.26\)', b)
        ) is not None)
    if start is not None:
        content = remove_block(content, start, end)
        wire_removed = True
    print(f'Wire R6→D2 {"rimosso" if wire_removed else "non trovato"}')

    # ------------------------------------------------------------------ #
    # H. Remove ALL labels at y≈187.96 (except C5 GND at x=137.16)
    #    Then re-add correct labels for J16 (8 pin) and J17 (4 pin)
    # ------------------------------------------------------------------ #
    removed_count = 0
    changed = True
    while changed:
        changed = False
        start, end = find_block(content, 'label',
            lambda b: (
                re.search(r'\(at\s+([\d.]+)\s+187\.96', b) is not None and
                abs(float(re.search(r'\(at\s+([\d.]+)\s+187\.96', b).group(1)) - 137.16) > 0.01
            ))
        if start is not None:
            content = remove_block(content, start, end)
            removed_count += 1
            changed = True
    print(f'Rimossi {removed_count} label a y=187.96')

    # J16 pin positions at cx=80.01, rot=270:
    # abs_x = cx - lib_y_k  where lib_y = [2.54, 0, -2.54, -5.08, -7.62, -10.16, -12.7, -15.24]
    # → x = [77.47, 80.01, 82.55, 85.09, 87.63, 90.17, 92.71, 95.25]
    new_labels = [
        # J16
        ('3V3',            77.47, 187.96, 90),
        ('GND',            80.01, 187.96, 90),
        ('GPIO21_I2C_SDA', 82.55, 187.96, 90),
        ('GPIO22_I2C_SCL', 85.09, 187.96, 90),
        ('OPT4048_INT',    87.63, 187.96, 90),
        ('LED_DARK_A',     90.17, 187.96, 90),
        ('PCF8574_P1',     92.71, 187.96, 90),
        ('PCF8574_P2',     95.25, 187.96, 90),
        # J17 (unchanged: GPIO13,GPIO14,PCF8574_P0,GND)
        ('GPIO13',        100.33, 187.96, 90),
        ('GPIO14',        102.87, 187.96, 90),
        ('PCF8574_P0',    105.41, 187.96, 90),
        ('GND',           107.95, 187.96, 90),
    ]
    new_labels_text = ''.join(label(t, x, y, r) for t, x, y, r in new_labels)

    marker = '\t(sheet_instances'
    idx = content.find(marker)
    if idx == -1:
        print('ERRORE: sheet_instances non trovato')
        sys.exit(1)
    content = content[:idx] + new_labels_text + content[idx:]
    print(f'Aggiunti {len(new_labels)} label a y=187.96 (J16×8 + J17×4)')

    # ------------------------------------------------------------------ #
    # I. Add LED_DARK_A label on R6 Pin2 (158.75, 173.48, rot=270)
    # ------------------------------------------------------------------ #
    idx = content.find(marker)
    content = content[:idx] + label('LED_DARK_A', 158.75, 173.48, 270) + content[idx:]
    print('Aggiunto label LED_DARK_A su R6 Pin2')

    # ------------------------------------------------------------------ #
    # J. Add J3 pin10: wire (214.63,91.44)→(228.60,91.44) + GND label
    # ------------------------------------------------------------------ #
    idx = content.find(marker)
    new_items = (
        wire(214.63, 91.44, 228.60, 91.44) +
        label('GND', 228.60, 91.44, 180)
    )
    content = content[:idx] + new_items + content[idx:]
    print('Aggiunto wire+GND per J3 pin 10 a y=91.44')

    with open(SCH, 'w') as f:
        f.write(content)
    print('\nFatto.')


if __name__ == '__main__':
    main()
