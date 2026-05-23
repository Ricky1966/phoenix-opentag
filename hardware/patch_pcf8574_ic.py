#!/usr/bin/env python3
"""
patch_pcf8574_ic.py

Sostituisce J18 (connettore modulo PCF8574 esterno) con il chip PCF8574AP
DIP-16 direttamente sullo schematic + condensatore bypass C5 100nF.

Modifiche:
  - Rimuove J18 (Conn_01x08) e le sue 8 net label
  - Aggiunge simbolo PCF8574 in lib_symbols
  - Aggiunge istanza PCF8574AP con tutti i pin collegati
  - Aggiunge C5 (100nF bypass) tra VDD e GND del PCF8574
"""

import uuid
import re

SCH_FILE = "hardware/ESP32_AZDelivery_Baseboard.kicad_sch"


def gen_uuid():
    return str(uuid.uuid4())


with open(SCH_FILE, "r") as f:
    content = f.read()

# ─── sanity check ─────────────────────────────────────────────────────────────
if "PCF8574" in content and "(lib_id \"PCF8574:PCF8574\")" in content:
    print("WARN: PCF8574 IC già presente, skip.")
    exit(0)

if '(reference "J18")' not in content:
    print("WARN: J18 non trovato nello schematic.")


# ═══════════════════════════════════════════════════════════════════════════════
# 1. Rimuovi J18 symbol block
# ═══════════════════════════════════════════════════════════════════════════════

lines = content.split('\n')

def find_and_remove_symbol(lines, ref):
    """Rimuove il blocco (symbol ...) che contiene (reference "REF")."""
    ref_pattern = f'(reference "{ref}")'
    ref_line_idx = None
    for i, line in enumerate(lines):
        if ref_pattern in line:
            ref_line_idx = i
            break
    if ref_line_idx is None:
        print(f"WARN: simbolo {ref} non trovato")
        return lines

    # Cammina a ritroso fino a \t(symbol
    sym_start = None
    for i in range(ref_line_idx, -1, -1):
        if lines[i] == '\t(symbol':
            sym_start = i
            break
    if sym_start is None:
        print(f"ERRORE: inizio blocco symbol per {ref} non trovato")
        return lines

    # Cammina in avanti fino alla chiusura \t) (la prima a livello simbolo)
    depth = 0
    sym_end = None
    for i in range(sym_start, len(lines)):
        depth += lines[i].count('(') - lines[i].count(')')
        if depth == 0 and i > sym_start:
            sym_end = i
            break
    if sym_end is None:
        print(f"ERRORE: chiusura blocco symbol per {ref} non trovata")
        return lines

    print(f"Rimuovo {ref}: righe {sym_start}–{sym_end}")
    return lines[:sym_start] + lines[sym_end + 1:]

lines = find_and_remove_symbol(lines, "J18")
content = '\n'.join(lines)


# ═══════════════════════════════════════════════════════════════════════════════
# 2. Rimuovi le 8 net label specifiche di J18 (x=120.65)
# ═══════════════════════════════════════════════════════════════════════════════

# Le label J18 erano a x=120.65, y da 185.80 a 168.02 (step 2.54)
J18_LABEL_YS = [185.80, 183.26, 180.72, 178.18, 175.64, 173.10, 170.56, 168.02]

def remove_label_at(content, x, y, tol=0.01):
    """Rimuove il blocco (label ...) che ha (at x y ...)."""
    lines = content.split('\n')
    result = []
    i = 0
    while i < len(lines):
        # cerca (at X Y ...) con X=x e Y=y
        if f'(at {x:.2f} {y:.2f}' in lines[i] and i > 0 and '(label ' in lines[i-1]:
            # trova l'inizio del blocco label (riga precedente con (label)
            start = i - 1
            # trova la chiusura
            depth = 0
            end = start
            for j in range(start, len(lines)):
                depth += lines[j].count('(') - lines[j].count(')')
                if depth == 0 and j > start:
                    end = j
                    break
            print(f"Rimuovo label a ({x:.2f}, {y:.2f}): righe {start}–{end}")
            result = result[:-1]  # rimuovi l'ultima riga già aggiunta (label ...)
            i = end + 1
            continue
        result.append(lines[i])
        i += 1
    return '\n'.join(result)

for y in J18_LABEL_YS:
    content = remove_label_at(content, 120.65, y)


# ═══════════════════════════════════════════════════════════════════════════════
# 3. Simbolo PCF8574 per lib_symbols
# ═══════════════════════════════════════════════════════════════════════════════
#
# DIP-16 pinout (KiCad convention):
#   Sinistra (pin 1-8, top→bottom): A0,A1,A2,P0,P1,P2,P3,VSS
#   Destra   (pin 9-16, bot→top):   P4,P5,P6,P7,INT,SCL,SDA,VDD
#
# Pin lib coords: sinistra x=-10, destra x=+10, y da +8.89 a -8.89 (step 2.54)

LEFT_PINS = [
    ("A0",  "1",  8.89, "input"),
    ("A1",  "2",  6.35, "input"),
    ("A2",  "3",  3.81, "input"),
    ("P0",  "4",  1.27, "bidirectional"),
    ("P1",  "5", -1.27, "bidirectional"),
    ("P2",  "6", -3.81, "bidirectional"),
    ("P3",  "7", -6.35, "bidirectional"),
    ("VSS", "8", -8.89, "power_in"),
]
RIGHT_PINS = [
    ("VDD", "16",  8.89, "power_in"),
    ("SDA", "15",  6.35, "bidirectional"),
    ("SCL", "14",  3.81, "input"),
    ("INT", "13",  1.27, "open_collector"),
    ("P7",  "12", -1.27, "bidirectional"),
    ("P6",  "11", -3.81, "bidirectional"),
    ("P5",  "10", -6.35, "bidirectional"),
    ("P4",  "9",  -8.89, "bidirectional"),
]

def ic_pin(name, number, x, y, angle, pin_type, length=5.08):
    return f"""\t\t\t\t(pin {pin_type} line
\t\t\t\t\t(at {x} {y} {angle})
\t\t\t\t\t(length {length})
\t\t\t\t\t(name "{name}"
\t\t\t\t\t\t(effects
\t\t\t\t\t\t\t(font
\t\t\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t\t\t)
\t\t\t\t\t\t)
\t\t\t\t\t)
\t\t\t\t\t(number "{number}"
\t\t\t\t\t\t(effects
\t\t\t\t\t\t\t(font
\t\t\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t\t\t)
\t\t\t\t\t\t)
\t\t\t\t\t)
\t\t\t\t)
"""

pins_block = ""
for name, num, y, ptype in LEFT_PINS:
    pins_block += ic_pin(name, num, -10.16, y, 0, ptype)
for name, num, y, ptype in RIGHT_PINS:
    pins_block += ic_pin(name, num, 10.16, y, 180, ptype)

PCF8574_SYMBOL = f"""\t\t(symbol "PCF8574:PCF8574"
\t\t\t(pin_names
\t\t\t\t(offset 1.016)
\t\t\t)
\t\t\t(exclude_from_sim no)
\t\t\t(in_bom yes)
\t\t\t(on_board yes)
\t\t\t(in_pos_files yes)
\t\t\t(duplicate_pin_numbers_are_jumpers no)
\t\t\t(property "Reference" "U"
\t\t\t\t(at 0 12.7 0)
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Value" "PCF8574AP"
\t\t\t\t(at 0 -12.7 0)
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Footprint" ""
\t\t\t\t(at 0 0 0)
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(hide yes)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Datasheet" ""
\t\t\t\t(at 0 0 0)
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(hide yes)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Description" "I2C GPIO expander 8-bit, DIP-16"
\t\t\t\t(at 0 0 0)
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(hide yes)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(symbol "PCF8574_1_1"
\t\t\t\t(rectangle
\t\t\t\t\t(start -5.08 11.43)
\t\t\t\t\t(end 5.08 -11.43)
\t\t\t\t\t(stroke
\t\t\t\t\t\t(width 0.254)
\t\t\t\t\t\t(type default)
\t\t\t\t\t)
\t\t\t\t\t(fill
\t\t\t\t\t\t(type background)
\t\t\t\t\t)
\t\t\t\t)
{pins_block}\t\t\t)
\t\t\t(embedded_fonts no)
\t\t)
"""


# ═══════════════════════════════════════════════════════════════════════════════
# 4. Helper builder elementi schematici
# ═══════════════════════════════════════════════════════════════════════════════

def wire(x1, y1, x2, y2):
    return f"""\t(wire
\t\t(pts
\t\t\t(xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f})
\t\t)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(uuid "{gen_uuid()}")
\t)
"""


def label(name, x, y, rotation, justify="left bottom"):
    return f"""\t(label "{name}"
\t\t(at {x:.2f} {y:.2f} {rotation})
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify {justify})
\t\t)
\t\t(uuid "{gen_uuid()}")
\t)
"""


def no_connect(x, y):
    return f"""\t(no_connect
\t\t(at {x:.2f} {y:.2f})
\t\t(uuid "{gen_uuid()}")
\t)
"""


def sym_prop(name, value, x, y, rotation=0, hide=False):
    hide_line = "\n\t\t\t\t(hide yes)" if hide else ""
    return f"""\t\t\t(property "{name}" "{value}"
\t\t\t\t(at {x:.2f} {y:.2f} {rotation}){hide_line}
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
"""


def instances_block(ref):
    return f"""\t\t\t(instances
\t\t\t\t(project "ESP32_AZDelivery_Baseboard"
\t\t\t\t\t(path "/6dfc456e-f0f7-4393-96f4-0b7c9f575168"
\t\t\t\t\t\t(reference "{ref}")
\t\t\t\t\t\t(unit 1)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
"""


# ═══════════════════════════════════════════════════════════════════════════════
# 5. PCF8574AP IC — coordinate
#
# Posizionato a (125.73, 183.26) — dove era J18
# Pin sinistra: x_abs = 125.73 - 10.16 = 115.57
# Pin destra:   x_abs = 125.73 + 10.16 = 135.89
# Pin y_abs = 183.26 + lib_y
# ═══════════════════════════════════════════════════════════════════════════════

IC_CX, IC_CY = 125.73, 183.26
PIN_X_L = IC_CX - 10.16   # 115.57
PIN_X_R = IC_CX + 10.16   # 135.89

def abs_pin_l(lib_y):
    return PIN_X_L, IC_CY + lib_y

def abs_pin_r(lib_y):
    return PIN_X_R, IC_CY + lib_y

# Costruisco il blocco symbol dell'istanza PCF8574
all_pins_block = ""
all_pin_numbers = [str(k) for k in range(1, 17)]
for num in all_pin_numbers:
    all_pins_block += f"""\t\t\t(pin "{num}"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
"""

U1_BLOCK = f"""\t(symbol
\t\t(lib_id "PCF8574:PCF8574")
\t\t(at {IC_CX:.2f} {IC_CY:.2f} 0)
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(uuid "{gen_uuid()}")
{sym_prop("Reference", "U1", IC_CX, IC_CY - 13.97, 0)}
{sym_prop("Value", "PCF8574AP", IC_CX, IC_CY + 13.97, 0)}
{sym_prop("Footprint", "Package_DIP:DIP-16_W7.62mm", IC_CX, IC_CY, 0, hide=True)}
{sym_prop("Datasheet", "https://www.ti.com/lit/ds/symlink/pcf8574.pdf", IC_CX, IC_CY, 0, hide=True)}
{sym_prop("Description", "I2C GPIO expander 8-bit DIP-16, indirizzo 0x20", IC_CX, IC_CY, 0, hide=True)}
{all_pins_block}{instances_block("U1")}\t)
"""

# ═══════════════════════════════════════════════════════════════════════════════
# 6. Condensatore bypass C5 (100nF) — vicino al VDD del PCF8574
#
# Device:C rotation=0: Pin1 wire at (cx, cy+1.27), Pin2 wire at (cx, cy-1.27)
# Posizioniamo a (145, 186) — a destra del PCF8574
# Pin1 (basso, +) at (145, 187.27) → 3V3
# Pin2 (alto, −) at (145, 184.73) → GND
# ═══════════════════════════════════════════════════════════════════════════════

C5_CX, C5_CY = 145.0, 186.0

C5_BLOCK = f"""\t(symbol
\t\t(lib_id "Device:C")
\t\t(at {C5_CX:.2f} {C5_CY:.2f} 0)
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(fields_autoplaced yes)
\t\t(uuid "{gen_uuid()}")
{sym_prop("Reference", "C5", C5_CX + 2.54, C5_CY, 0)}
{sym_prop("Value", "100nF", C5_CX + 2.54, C5_CY + 2.54, 0)}
{sym_prop("Footprint", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm", C5_CX, C5_CY, 0, hide=True)}
{sym_prop("Datasheet", "", C5_CX, C5_CY, 0, hide=True)}
{sym_prop("Description", "Bypass capacitor 100nF", C5_CX, C5_CY, 0, hide=True)}
\t\t\t(pin "1"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
\t\t\t(pin "2"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
{instances_block("C5")}\t)
"""

# ═══════════════════════════════════════════════════════════════════════════════
# 7. Net label per tutti i pin del PCF8574 + C5
# ═══════════════════════════════════════════════════════════════════════════════

new_elements = U1_BLOCK + C5_BLOCK

# ── Left side labels ──────────────────────────────────────────────────────────
# Rotation=180, justify right bottom → testo va a SINISTRA del pin
left_label_map = [
    ("A0",  8.89,  "GND"),
    ("A1",  6.35,  "GND"),
    ("A2",  3.81,  "GND"),
    ("P0",  1.27,  "PCF8574_P0"),
    ("P1", -1.27,  "PCF8574_P1"),
    ("P2", -3.81,  "PCF8574_P2"),
    ("P3", -6.35,  "PCF8574_P3"),
    ("VSS",-8.89,  "GND"),
]
for _, lib_y, net in left_label_map:
    px, py = abs_pin_l(lib_y)
    new_elements += label(net, px, py, 180, "right bottom")

# ── Right side labels ─────────────────────────────────────────────────────────
# Rotation=0, justify left bottom → testo va a DESTRA del pin
right_label_map = [
    ("VDD", 8.89,  "3V3"),
    ("SDA", 6.35,  "GPIO21_I2C_SDA"),
    ("SCL", 3.81,  "GPIO22_I2C_SCL"),
    ("INT", 1.27,  None),          # no_connect
    ("P7", -1.27,  "PCF8574_P7"),
    ("P6", -3.81,  "PCF8574_P6"),
    ("P5", -6.35,  "PCF8574_P5"),
    ("P4", -8.89,  "PCF8574_P4"),
]
for _, lib_y, net in right_label_map:
    px, py = abs_pin_r(lib_y)
    if net is None:
        new_elements += no_connect(px, py)
    else:
        new_elements += label(net, px, py, 0, "left bottom")

# ── C5 bypass cap labels ──────────────────────────────────────────────────────
# Device:C rotation=0: Pin1 wire at (cx, cy+1.27), Pin2 wire at (cx, cy-1.27)
new_elements += label("3V3", C5_CX, C5_CY - 1.27, 90, "left bottom")
new_elements += label("GND", C5_CX, C5_CY + 1.27, 270, "right bottom")


# ═══════════════════════════════════════════════════════════════════════════════
# 8. Inserisci nel file
# ═══════════════════════════════════════════════════════════════════════════════

# 8a. Aggiungi PCF8574 a lib_symbols (prima della chiusura)
LIB_END = "\t)\n\t(junction"
if LIB_END not in content:
    print("ERRORE: marker fine lib_symbols non trovato!")
    exit(1)
content = content.replace(LIB_END, PCF8574_SYMBOL + "\t)\n\t(junction", 1)

# 8b. Aggiungi elementi schematici prima di (sheet_instances
SHEET_MARKER = "\t(sheet_instances"
if SHEET_MARKER not in content:
    print("ERRORE: marker (sheet_instances non trovato!")
    exit(1)
content = content.replace(SHEET_MARKER, new_elements + "\t(sheet_instances", 1)

with open(SCH_FILE, "w") as f:
    f.write(content)

print("OK: PCF8574AP DIP-16 aggiunto allo schematic, J18 rimosso.")
print(f"File: {SCH_FILE}")
print(f"U1 (PCF8574AP) posizionato a ({IC_CX}, {IC_CY})")
print(f"C5 (100nF bypass) posizionato a ({C5_CX}, {C5_CY})")
