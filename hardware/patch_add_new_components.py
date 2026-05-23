#!/usr/bin/env python3
"""
patch_add_new_components.py

Aggiunge allo schematic KiCad:
  D1+R5   — LED indicatore alimentazione BUCK_5V (330Ω + LED)
  J16     — Connettore OPT4048 color sensor (5-pin JST XH)
  J17     — Connettore driver NEMA17 stepper (4-pin JST XH)
  J18     — Modulo PCF8574 I2C GPIO expander (8-pin JST XH)
  D2+R6   — LED camera oscura, anodo=3V3, catodo=PCF8574_P1 (330Ω + LED)

Aggiunge anche le definizioni simbolo mancanti in lib_symbols:
  Device:LED, Connector_Generic:Conn_01x05, Connector_Generic:Conn_01x08
"""

import uuid

SCH_FILE = "hardware/ESP32_AZDelivery_Baseboard.kicad_sch"


def gen_uuid():
    return str(uuid.uuid4())


with open(SCH_FILE, "r") as f:
    content = f.read()

# ── sanity check ─────────────────────────────────────────────────────────────
for sym in ["Device:LED", "Conn_01x05", "Conn_01x08"]:
    if sym in content:
        print(f"WARN: {sym} già presente in lib_symbols, skip inserimento.")
        exit(0)


# ═══════════════════════════════════════════════════════════════════════════════
# 1. Nuove definizioni simbolo per lib_symbols
# ═══════════════════════════════════════════════════════════════════════════════

# ── Device:LED ────────────────────────────────────────────────────────────────
LED_SYMBOL = """\
		(symbol "Device:LED"
			(pin_names
				(offset 1.016)
				(hide yes)
			)
			(pin_numbers
				(hide yes)
			)
			(exclude_from_sim no)
			(in_bom yes)
			(on_board yes)
			(in_pos_files yes)
			(duplicate_pin_numbers_are_jumpers no)
			(property "Reference" "D"
				(at 0 3.81 0)
				(show_name no)
				(do_not_autoplace no)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Value" "LED"
				(at 0 -3.81 0)
				(show_name no)
				(do_not_autoplace no)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Footprint" ""
				(at 0 0 0)
				(show_name no)
				(do_not_autoplace no)
				(hide yes)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Datasheet" ""
				(at 0 0 0)
				(show_name no)
				(do_not_autoplace no)
				(hide yes)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Description" "Light emitting diode"
				(at 0 0 0)
				(show_name no)
				(do_not_autoplace no)
				(hide yes)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(symbol "LED_0_1"
				(polyline
					(pts (xy -1.27 -1.27) (xy 1.27 0) (xy -1.27 1.27) (xy -1.27 -1.27))
					(stroke
						(width 0.254)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(polyline
					(pts (xy 1.27 -1.27) (xy 1.27 1.27))
					(stroke
						(width 0.254)
						(type default)
					)
					(fill
						(type none)
					)
				)
			)
			(symbol "LED_1_1"
				(pin passive line
					(at -3.81 0 0)
					(length 2.54)
					(name "A"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "1"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at 3.81 0 180)
					(length 2.54)
					(name "K"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "2"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
			)
			(embedded_fonts no)
		)
"""


def conn_01xN_symbol(N):
    """Genera definizione simbolo Connector_Generic:Conn_01xNN."""
    pin_ys = [2.54 - (k - 1) * 2.54 for k in range(1, N + 1)]
    body_top = 3.81
    body_bottom = round(pin_ys[-1] - 1.27, 2)
    ref_y = round(body_top + 1.27, 2)
    val_y = round(body_bottom - 1.27, 2)
    tag = f"Conn_01x{N:02d}"

    lines = []
    lines.append(f'\t\t(symbol "Connector_Generic:{tag}"')
    lines.append('\t\t\t(pin_names')
    lines.append('\t\t\t\t(offset 1.016)')
    lines.append('\t\t\t\t(hide yes)')
    lines.append('\t\t\t)')
    lines.append('\t\t\t(exclude_from_sim no)')
    lines.append('\t\t\t(in_bom yes)')
    lines.append('\t\t\t(on_board yes)')
    lines.append('\t\t\t(in_pos_files yes)')
    lines.append('\t\t\t(duplicate_pin_numbers_are_jumpers no)')
    lines.append(f'\t\t\t(property "Reference" "J"')
    lines.append(f'\t\t\t\t(at 0 {ref_y} 0)')
    lines.append('\t\t\t\t(show_name no)')
    lines.append('\t\t\t\t(do_not_autoplace no)')
    lines.append('\t\t\t\t(effects')
    lines.append('\t\t\t\t\t(font')
    lines.append('\t\t\t\t\t\t(size 1.27 1.27)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t)')
    lines.append('\t\t\t)')
    lines.append(f'\t\t\t(property "Value" "{tag}"')
    lines.append(f'\t\t\t\t(at 0 {val_y} 0)')
    lines.append('\t\t\t\t(show_name no)')
    lines.append('\t\t\t\t(do_not_autoplace no)')
    lines.append('\t\t\t\t(effects')
    lines.append('\t\t\t\t\t(font')
    lines.append('\t\t\t\t\t\t(size 1.27 1.27)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t)')
    lines.append('\t\t\t)')
    lines.append('\t\t\t(property "Footprint" ""')
    lines.append('\t\t\t\t(at 0 0 0)')
    lines.append('\t\t\t\t(show_name no)')
    lines.append('\t\t\t\t(do_not_autoplace no)')
    lines.append('\t\t\t\t(hide yes)')
    lines.append('\t\t\t\t(effects')
    lines.append('\t\t\t\t\t(font')
    lines.append('\t\t\t\t\t\t(size 1.27 1.27)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t)')
    lines.append('\t\t\t)')
    lines.append('\t\t\t(property "Datasheet" ""')
    lines.append('\t\t\t\t(at 0 0 0)')
    lines.append('\t\t\t\t(show_name no)')
    lines.append('\t\t\t\t(do_not_autoplace no)')
    lines.append('\t\t\t\t(hide yes)')
    lines.append('\t\t\t\t(effects')
    lines.append('\t\t\t\t\t(font')
    lines.append('\t\t\t\t\t\t(size 1.27 1.27)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t)')
    lines.append('\t\t\t)')
    lines.append(f'\t\t\t(property "Description" "Generic connector, single row, 01x{N:02d}"')
    lines.append('\t\t\t\t(at 0 0 0)')
    lines.append('\t\t\t\t(show_name no)')
    lines.append('\t\t\t\t(do_not_autoplace no)')
    lines.append('\t\t\t\t(hide yes)')
    lines.append('\t\t\t\t(effects')
    lines.append('\t\t\t\t\t(font')
    lines.append('\t\t\t\t\t\t(size 1.27 1.27)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t)')
    lines.append('\t\t\t)')
    lines.append(f'\t\t\t(symbol "{tag}_1_1"')
    # outer body rectangle
    lines.append('\t\t\t\t(rectangle')
    lines.append(f'\t\t\t\t\t(start -1.27 {body_top})')
    lines.append(f'\t\t\t\t\t(end 1.27 {body_bottom})')
    lines.append('\t\t\t\t\t(stroke')
    lines.append('\t\t\t\t\t\t(width 0.254)')
    lines.append('\t\t\t\t\t\t(type default)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t\t(fill')
    lines.append('\t\t\t\t\t\t(type background)')
    lines.append('\t\t\t\t\t)')
    lines.append('\t\t\t\t)')
    # slot rectangles for each pin
    for py in pin_ys:
        top = round(py + 0.127, 3)
        bot = round(py - 0.127, 3)
        lines.append('\t\t\t\t(rectangle')
        lines.append(f'\t\t\t\t\t(start -1.27 {top})')
        lines.append(f'\t\t\t\t\t(end 0 {bot})')
        lines.append('\t\t\t\t\t(stroke')
        lines.append('\t\t\t\t\t\t(width 0.1524)')
        lines.append('\t\t\t\t\t\t(type default)')
        lines.append('\t\t\t\t\t)')
        lines.append('\t\t\t\t\t(fill')
        lines.append('\t\t\t\t\t\t(type none)')
        lines.append('\t\t\t\t\t)')
        lines.append('\t\t\t\t)')
    # pins
    for k, py in enumerate(pin_ys, start=1):
        lines.append('\t\t\t\t(pin passive line')
        lines.append(f'\t\t\t\t\t(at -5.08 {py} 0)')
        lines.append('\t\t\t\t\t(length 3.81)')
        lines.append(f'\t\t\t\t\t(name "Pin_{k}"')
        lines.append('\t\t\t\t\t\t(effects')
        lines.append('\t\t\t\t\t\t\t(font')
        lines.append('\t\t\t\t\t\t\t\t(size 1.27 1.27)')
        lines.append('\t\t\t\t\t\t\t)')
        lines.append('\t\t\t\t\t\t)')
        lines.append('\t\t\t\t\t)')
        lines.append(f'\t\t\t\t\t(number "{k}"')
        lines.append('\t\t\t\t\t\t(effects')
        lines.append('\t\t\t\t\t\t\t(font')
        lines.append('\t\t\t\t\t\t\t\t(size 1.27 1.27)')
        lines.append('\t\t\t\t\t\t\t)')
        lines.append('\t\t\t\t\t\t)')
        lines.append('\t\t\t\t\t)')
        lines.append('\t\t\t\t)')
    lines.append('\t\t\t)')
    lines.append('\t\t\t(embedded_fonts no)')
    lines.append('\t\t)')
    return "\n".join(lines) + "\n"


CONN_01x05_SYMBOL = conn_01xN_symbol(5)
CONN_01x08_SYMBOL = conn_01xN_symbol(8)

NEW_LIB_SYMBOLS = LED_SYMBOL + CONN_01x05_SYMBOL + CONN_01x08_SYMBOL


# ═══════════════════════════════════════════════════════════════════════════════
# 2. Helper per costruire S-expression degli elementi schematici
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


def sym_property(name, value, x, y, rotation=0, hide=False, show_name=False):
    hide_str = "\n\t\t\t(hide yes)" if hide else ""
    return f"""\t\t\t(property "{name}" "{value}"
\t\t\t\t(at {x:.2f} {y:.2f} {rotation}){hide_str}
\t\t\t\t(show_name no)
\t\t\t\t(do_not_autoplace no)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
"""


def pin_uuid(num):
    return f"""\t\t\t(pin "{num}"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
"""


def instances_block(ref, sheet_uuid="6dfc456e-f0f7-4393-96f4-0b7c9f575168",
                    project="ESP32_AZDelivery_Baseboard"):
    return f"""\t\t\t(instances
\t\t\t\t(project "{project}"
\t\t\t\t\t(path "/{sheet_uuid}"
\t\t\t\t\t\t(reference "{ref}")
\t\t\t\t\t\t(unit 1)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
"""


def resistor(ref, value, footprint, cx, cy, rotation,
             ref_dx=-6.35, ref_dy=0, val_dx=-3.81, val_dy=0):
    """Device:R symbol instance."""
    ref_rot = 90 if rotation in (0, 180) else 0
    rx = cx + ref_dx
    ry = cy + ref_dy
    vx = cx + val_dx
    vy = cy + val_dy
    sym_uuid = gen_uuid()
    return f"""\t(symbol
\t\t(lib_id "Device:R")
\t\t(at {cx:.2f} {cy:.2f} {rotation})
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(fields_autoplaced yes)
\t\t(uuid "{sym_uuid}")
{sym_property("Reference", ref, rx, ry, ref_rot)}
{sym_property("Value", value, vx, vy, ref_rot)}
{sym_property("Footprint", footprint, cx, cy, 0, hide=True)}
{sym_property("Datasheet", "", cx, cy, 0, hide=True)}
{sym_property("Description", "Resistor", cx, cy, 0, hide=True)}
\t\t\t(pin "2"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
\t\t\t(pin "1"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
{instances_block(ref)}\t)
"""


def led(ref, value, footprint, cx, cy, rotation,
        ref_dx=3.81, ref_dy=0, val_dx=6.35, val_dy=0):
    """Device:LED symbol instance."""
    ref_rot = 0
    sym_uuid = gen_uuid()
    return f"""\t(symbol
\t\t(lib_id "Device:LED")
\t\t(at {cx:.2f} {cy:.2f} {rotation})
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(fields_autoplaced yes)
\t\t(uuid "{sym_uuid}")
{sym_property("Reference", ref, cx+ref_dx, cy+ref_dy, ref_rot)}
{sym_property("Value", value, cx+val_dx, cy+val_dy, ref_rot)}
{sym_property("Footprint", footprint, cx, cy, 0, hide=True)}
{sym_property("Datasheet", "", cx, cy, 0, hide=True)}
{sym_property("Description", "LED", cx, cy, 0, hide=True)}
\t\t\t(pin "2"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
\t\t\t(pin "1"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
{instances_block(ref)}\t)
"""


def connector(ref, value, footprint, description, lib_id, n_pins,
              cx, cy, rotation=0, ref_dx=5.08, ref_dy=-2.54):
    """Generic Conn_01xNN symbol instance."""
    sym_uuid = gen_uuid()
    pins_block = ""
    for k in range(1, n_pins + 1):
        pins_block += f"""\t\t\t(pin "{k}"
\t\t\t\t(uuid "{gen_uuid()}")
\t\t\t)
"""
    return f"""\t(symbol
\t\t(lib_id "{lib_id}")
\t\t(at {cx:.2f} {cy:.2f} {rotation})
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(uuid "{sym_uuid}")
{sym_property("Reference", ref, cx+ref_dx, cy+ref_dy, 0)}
{sym_property("Value", value, cx+ref_dx+2.54, cy+ref_dy, 0)}
{sym_property("Footprint", footprint, cx, cy, 0, hide=True)}
{sym_property("Datasheet", "", cx, cy, 0, hide=True)}
{sym_property("Description", description, cx, cy, 0, hide=True)}
{pins_block}{instances_block(ref)}\t)
"""


# ═══════════════════════════════════════════════════════════════════════════════
# 3. Coordinate componenti
#
# Rotazione KiCad = CW in screen (y-down)
# Device:R rot=180 → Pin1=(cx, cy-3.81) top, Pin2=(cx, cy+3.81) bottom
# Device:LED rot=270 → Anode=(cx, cy-3.81) top, Cathode=(cx, cy+3.81) bottom
# Conn_01xN rot=0 → Pin_k=(cx-5.08, cy+(2-k)*2.54)
# ═══════════════════════════════════════════════════════════════════════════════

R_FOOTPRINT = "Resistor_THT:R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal"
LED_FOOTPRINT = "LED_THT:LED_D5.0mm"
JST4_FP = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
JST5_FP = "Connector_JST:JST_XH_B5B-XH-A_1x05_P2.50mm_Vertical"
JST8_FP = "Connector_JST:JST_XH_B8B-XH-A_1x08_P2.50mm_Vertical"

# ── BUCK LED (D1+R5) ─────────────────────────────────────────────────────────
R5_CX, R5_CY = 50.80, 170.00   # rot=180
D1_CX, D1_CY = 50.80, 179.40   # rot=270
# R5: Pin1=(50.80,166.19) Pin2=(50.80,173.81)
# D1: Anode=(50.80,175.59) Cathode=(50.80,183.21)

# ── J16 OPT4048 5-pin ────────────────────────────────────────────────────────
J16_CX, J16_CY = 85.09, 178.38  # rot=0
# x_pin=80.01; Pin1=(80.01,180.92) Pin2=(80.01,178.38) ... Pin5=(80.01,170.76)

# ── J17 Stepper 4-pin ────────────────────────────────────────────────────────
J17_CX, J17_CY = 105.41, 177.80  # rot=0
# x_pin=100.33; Pin1=(100.33,180.34) ... Pin4=(100.33,172.72)

# ── J18 PCF8574 8-pin ────────────────────────────────────────────────────────
J18_CX, J18_CY = 125.73, 183.26  # rot=0
# x_pin=120.65; Pin1=(120.65,185.80) ... Pin8=(120.65,168.02)

# ── Dark chamber LED (D2+R6) ─────────────────────────────────────────────────
R6_CX, R6_CY = 148.00, 170.00   # rot=180
D2_CX, D2_CY = 148.00, 179.40   # rot=270
# R6: Pin1=(148.00,166.19) Pin2=(148.00,173.81)
# D2: Anode=(148.00,175.59) Cathode=(148.00,183.21)


# ═══════════════════════════════════════════════════════════════════════════════
# 4. Costruzione degli elementi schematici
# ═══════════════════════════════════════════════════════════════════════════════

new_symbols = ""
new_wires_labels = ""

# ── R5 (330Ω, BUCK LED series resistor) ──────────────────────────────────────
new_symbols += resistor(
    ref="R5", value="330", footprint=R_FOOTPRINT,
    cx=R5_CX, cy=R5_CY, rotation=180,
    ref_dx=-6.35, ref_dy=0, val_dx=-3.81, val_dy=0
)

# ── D1 (LED indicatore BUCK_5V) ───────────────────────────────────────────────
new_symbols += led(
    ref="D1", value="LED", footprint=LED_FOOTPRINT,
    cx=D1_CX, cy=D1_CY, rotation=270,
    ref_dx=3.81, ref_dy=0, val_dx=6.35, val_dy=0
)

# ── J16 OPT4048 (Conn_01x05) ─────────────────────────────────────────────────
new_symbols += connector(
    ref="J16", value="OPT4048",
    footprint=JST5_FP,
    description="OPT4048 color sensor: VCC,GND,SDA,SCL,INT",
    lib_id="Connector_Generic:Conn_01x05",
    n_pins=5,
    cx=J16_CX, cy=J16_CY, rotation=0,
    ref_dx=5.08, ref_dy=-2.54
)

# ── J17 Stepper driver control (Conn_01x04) ───────────────────────────────────
new_symbols += connector(
    ref="J17", value="NEMA17_CTRL",
    footprint=JST4_FP,
    description="NEMA17 stepper driver: STEP,DIR,EN,GND",
    lib_id="Connector_Generic:Conn_01x04",
    n_pins=4,
    cx=J17_CX, cy=J17_CY, rotation=0,
    ref_dx=5.08, ref_dy=-2.54
)

# ── J18 PCF8574 module (Conn_01x08) ──────────────────────────────────────────
new_symbols += connector(
    ref="J18", value="PCF8574_MOD",
    footprint=JST8_FP,
    description="PCF8574 I2C GPIO expander module: VCC,GND,SDA,SCL,P0-P3",
    lib_id="Connector_Generic:Conn_01x08",
    n_pins=8,
    cx=J18_CX, cy=J18_CY, rotation=0,
    ref_dx=5.08, ref_dy=-2.54
)

# ── R6 (330Ω, dark chamber LED series resistor) ───────────────────────────────
new_symbols += resistor(
    ref="R6", value="330", footprint=R_FOOTPRINT,
    cx=R6_CX, cy=R6_CY, rotation=180,
    ref_dx=-6.35, ref_dy=0, val_dx=-3.81, val_dy=0
)

# ── D2 (LED camera oscura, catodo verso PCF8574_P1) ──────────────────────────
new_symbols += led(
    ref="D2", value="LED", footprint=LED_FOOTPRINT,
    cx=D2_CX, cy=D2_CY, rotation=270,
    ref_dx=3.81, ref_dy=0, val_dx=6.35, val_dy=0
)

# ── Wire: R5 Pin2 → D1 Anode ─────────────────────────────────────────────────
new_wires_labels += wire(R5_CX, R5_CY + 3.81, D1_CX, D1_CY - 3.81)
# (50.80, 173.81) → (50.80, 175.59)

# ── Wire: R6 Pin2 → D2 Anode ─────────────────────────────────────────────────
new_wires_labels += wire(R6_CX, R6_CY + 3.81, D2_CX, D2_CY - 3.81)
# (148.00, 173.81) → (148.00, 175.59)

# ── Labels for D1+R5 circuit ─────────────────────────────────────────────────
new_wires_labels += label("BUCK_5V", R5_CX, R5_CY - 3.81, 90, "left bottom")
new_wires_labels += label("GND",     D1_CX, D1_CY + 3.81, 270, "right bottom")

# ── Labels for D2+R6 circuit ─────────────────────────────────────────────────
new_wires_labels += label("3V3",         R6_CX, R6_CY - 3.81, 90, "left bottom")
new_wires_labels += label("PCF8574_P1",  D2_CX, D2_CY + 3.81, 270, "right bottom")

# ── Labels for J16 (OPT4048) pins ────────────────────────────────────────────
j16_xp = J16_CX - 5.08  # 80.01
for k, net in enumerate(["3V3", "GND", "GPIO21_I2C_SDA", "GPIO22_I2C_SCL", "OPT4048_INT"], 1):
    py = J16_CY + (2 - k) * 2.54
    new_wires_labels += label(net, j16_xp, py, 180, "right bottom")

# ── Labels for J17 (Stepper) pins ────────────────────────────────────────────
j17_xp = J17_CX - 5.08  # 100.33
for k, net in enumerate(["GPIO13", "GPIO14", "PCF8574_P0", "GND"], 1):
    py = J17_CY + (2 - k) * 2.54
    new_wires_labels += label(net, j17_xp, py, 180, "right bottom")

# ── Labels for J18 (PCF8574) pins ────────────────────────────────────────────
j18_xp = J18_CX - 5.08  # 120.65
for k, net in enumerate(["3V3", "GND", "GPIO21_I2C_SDA", "GPIO22_I2C_SCL",
                          "PCF8574_P0", "PCF8574_P1", "PCF8574_P2", "PCF8574_P3"], 1):
    py = J18_CY + (2 - k) * 2.54
    new_wires_labels += label(net, j18_xp, py, 180, "right bottom")


# ═══════════════════════════════════════════════════════════════════════════════
# 5. Inserimento nel file
# ═══════════════════════════════════════════════════════════════════════════════

# 5a. Inserisce nuovi simboli lib prima della chiusura di lib_symbols
#     Il marker è la chiusura "\t)\n" seguita da "\t(junction" (primo elemento)
LIB_END_MARKER = "\t)\n\t(junction"
if LIB_END_MARKER not in content:
    print("ERRORE: marker fine lib_symbols non trovato!")
    exit(1)
content = content.replace(LIB_END_MARKER,
                          NEW_LIB_SYMBOLS + "\t)\n\t(junction", 1)

# 5b. Inserisce simboli + wire + label prima di (sheet_instances
SHEET_MARKER = "\t(sheet_instances"
if SHEET_MARKER not in content:
    print("ERRORE: marker (sheet_instances non trovato!")
    exit(1)
content = content.replace(SHEET_MARKER,
                          new_symbols + new_wires_labels + "\t(sheet_instances", 1)

with open(SCH_FILE, "w") as f:
    f.write(content)

print("OK: aggiunto D1/R5, J16, J17, J18, D2/R6 allo schematic.")
print(f"File: {SCH_FILE}")
