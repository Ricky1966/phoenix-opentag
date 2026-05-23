#!/usr/bin/env python3
"""
Aggiunge connettore alimentazione BUCK + jumper selezione USB/BUCK.

  J15  = morsetto a vite 2-pin Phoenix  (+5V e GND dal buck converter 24V→5V)
  JP1  = header 3-pin jumper selezione  (PIN1=BUCK_5V, PIN2=5V/VIN, PIN3=NC)

Posizione: sotto i connettori riscaldatori, a sinistra dell'ESP32
  J15 at (75.0, 150.0) rot=270
  JP1 at (88.0, 150.0) rot=270

Pin positions con rot=270: pin_sch = (cx + y_lib, cy + x_lib)
  Conn_01x02: Pin1 lib(-5.08,0) → (cx,    cy-5.08)
              Pin2 lib(-5.08,-2.54) → (cx-2.54, cy-5.08)
  Conn_01x03: Pin1 lib(-5.08,+2.54) → (cx+2.54, cy-5.08)
              Pin2 lib(-5.08, 0)    → (cx,      cy-5.08)
              Pin3 lib(-5.08,-2.54) → (cx-2.54, cy-5.08)
"""
import uuid, shutil, sys

SCH = "hardware/ESP32_AZDelivery_Baseboard.kicad_sch"
SHEET_UUID = "6dfc456e-f0f7-4393-96f4-0b7c9f575168"
PROJECT   = "ESP32_AZDelivery_Baseboard"

shutil.copy(SCH, SCH + ".bak_buck")

with open(SCH) as fp:
    txt = fp.read()

def uid():
    return str(uuid.uuid4())

# ── Posizioni ────────────────────────────────────────────────────────────────
# J15 (screw terminal 2-pin)
J15_CX, J15_CY = 75.0, 150.0
# rot=270: Pin1=(cx, cy-5.08)  Pin2=(cx-2.54, cy-5.08)
J15_P1X, J15_P1Y = J15_CX,        J15_CY - 5.08   # BUCK_5V
J15_P2X, J15_P2Y = J15_CX - 2.54, J15_CY - 5.08   # GND

# JP1 (3-pin jumper header)
JP1_CX, JP1_CY = 88.0, 150.0
# rot=270: Pin1=(cx+2.54,cy-5.08) Pin2=(cx,cy-5.08) Pin3=(cx-2.54,cy-5.08)
JP1_P1X, JP1_P1Y = JP1_CX + 2.54, JP1_CY - 5.08   # BUCK_5V
JP1_P2X, JP1_P2Y = JP1_CX,        JP1_CY - 5.08   # 5V (ESP32 VIN)
JP1_P3X, JP1_P3Y = JP1_CX - 2.54, JP1_CY - 5.08   # NC

def fmt(v):
    s = f"{v:.4f}".rstrip('0').rstrip('.')
    return s if s else '0'

# ── Simbolo J15 ──────────────────────────────────────────────────────────────
j15_block = f"""\t(symbol
\t\t(lib_id "Connector_Generic:Conn_01x02")
\t\t(at {fmt(J15_CX)} {fmt(J15_CY)} 270)
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(uuid "{uid()}")
\t\t(property "Reference" "J15"
\t\t\t(at {fmt(J15_CX+7.62)} {fmt(J15_CY+0.254)} 0)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Value" "BUCK_5V_IN"
\t\t\t(at {fmt(J15_CX+9.398)} {fmt(J15_CY-0.508)} 0)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Footprint" "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal"
\t\t\t(at {fmt(J15_CX)} {fmt(J15_CY)} 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Datasheet" ""
\t\t\t(at {fmt(J15_CX)} {fmt(J15_CY)} 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Description" "Buck converter 5V power input, screw terminal"
\t\t\t(at {fmt(J15_CX)} {fmt(J15_CY)} 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(pin "1"
\t\t\t(uuid "{uid()}")
\t\t)
\t\t(pin "2"
\t\t\t(uuid "{uid()}")
\t\t)
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "/{SHEET_UUID}"
\t\t\t\t\t(reference "J15")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)
"""

# ── Simbolo JP1 ──────────────────────────────────────────────────────────────
jp1_block = f"""\t(symbol
\t\t(lib_id "Connector_Generic:Conn_01x03")
\t\t(at {fmt(JP1_CX)} {fmt(JP1_CY)} 270)
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(uuid "{uid()}")
\t\t(property "Reference" "JP1"
\t\t\t(at {fmt(JP1_CX+7.62)} {fmt(JP1_CY+0.254)} 0)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Value" "USB_BUCK_SEL"
\t\t\t(at {fmt(JP1_CX+9.398)} {fmt(JP1_CY-0.508)} 0)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
\t\t\t(at {fmt(JP1_CX)} {fmt(JP1_CY)} 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Datasheet" ""
\t\t\t(at {fmt(JP1_CX)} {fmt(JP1_CY)} 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Description" "Power select jumper: 1=BUCK_5V 2=5V(VIN) 3=NC. Cap 1-2=BUCK, cap 2-3=USB"
\t\t\t(at {fmt(JP1_CX)} {fmt(JP1_CY)} 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(pin "1"
\t\t\t(uuid "{uid()}")
\t\t)
\t\t(pin "2"
\t\t\t(uuid "{uid()}")
\t\t)
\t\t(pin "3"
\t\t\t(uuid "{uid()}")
\t\t)
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "/{SHEET_UUID}"
\t\t\t\t\t(reference "JP1")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)
"""

# ── Label helper ─────────────────────────────────────────────────────────────
def make_label(name, x, y, angle, justify="left bottom"):
    return f"""\t(label "{name}"
\t\t(at {fmt(x)} {fmt(y)} {angle})
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify {justify})
\t\t)
\t\t(uuid "{uid()}")
\t)
"""

# ── No-connect helper ────────────────────────────────────────────────────────
def make_noconnect(x, y):
    return f"""\t(no_connect
\t\t(at {fmt(x)} {fmt(y)})
\t\t(uuid "{uid()}")
\t)
"""

# ── Net labels ───────────────────────────────────────────────────────────────
# J15 pin1: BUCK_5V  (rot=90, pointing upward → justify left bottom)
# J15 pin2: GND      (rot=90)
# JP1 pin1: BUCK_5V  (rot=90)
# JP1 pin2: 5V       (rot=90)
# JP1 pin3: no-connect

labels = ""
labels += make_label("BUCK_5V", J15_P1X, J15_P1Y, 90, "left bottom")
labels += make_label("GND",     J15_P2X, J15_P2Y, 90, "left bottom")
labels += make_label("BUCK_5V", JP1_P1X, JP1_P1Y, 90, "left bottom")
labels += make_label("5V",      JP1_P2X, JP1_P2Y, 90, "left bottom")

noconn = make_noconnect(JP1_P3X, JP1_P3Y)

# ── Inserisci prima di (sheet_instances ──────────────────────────────────────
ANCHOR = "\t(sheet_instances"
if ANCHOR not in txt:
    print("ERRORE: ancora sheet_instances non trovata!", file=sys.stderr)
    sys.exit(1)

insert_block = j15_block + jp1_block + labels + noconn
txt = txt.replace(ANCHOR, insert_block + ANCHOR, 1)

with open(SCH, 'w') as fp:
    fp.write(txt)

print("✓ J15 (BUCK 5V screw terminal) e JP1 (power select jumper) aggiunti.")
print(f"  J15 pin1 BUCK_5V : ({fmt(J15_P1X)}, {fmt(J15_P1Y)})")
print(f"  J15 pin2 GND     : ({fmt(J15_P2X)}, {fmt(J15_P2Y)})")
print(f"  JP1 pin1 BUCK_5V : ({fmt(JP1_P1X)}, {fmt(JP1_P1Y)})")
print(f"  JP1 pin2 5V(VIN) : ({fmt(JP1_P2X)}, {fmt(JP1_P2Y)})")
print(f"  JP1 pin3 NC      : ({fmt(JP1_P3X)}, {fmt(JP1_P3Y)})")
