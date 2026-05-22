#!/usr/bin/env python3
"""
Transforms ESP32_AZDelivery_Baseboard.kicad_sch:
  - Replaces J3/J4/J5/J6 (four PN5180 JST connectors) with a single
    Conn_01x13 female pin-header that matches the standard PN5180 module.
  - Fixes R1 value from "10K" to "100K" (pullup must match firmware BED_PULLUP_OHMS=100000).
"""

import re, os, shutil

SCH = os.path.join(os.path.dirname(__file__), 'ESP32_AZDelivery_Baseboard.kicad_sch')

with open(SCH, encoding='utf-8') as f:
    text = f.read()

# ── backup ────────────────────────────────────────────────────────────────────
shutil.copy(SCH, SCH + '.bak')

# ─────────────────────────────────────────────────────────────────────────────
# 1.  Fix R1 value  10K → 100K  (first occurrence only – the resistor instance)
# ─────────────────────────────────────────────────────────────────────────────
text = text.replace(
    '"Value" "10K"\n\t\t\t(at 48.26 21.59 90)',
    '"Value" "100K"\n\t\t\t(at 48.26 21.59 90)',
    1
)

# ─────────────────────────────────────────────────────────────────────────────
# 2.  Remove symbol blocks for J3 / J4 / J5 / J6
#     Each block is delimited by \t(symbol\n … \t)\n  and we find it by uuid.
# ─────────────────────────────────────────────────────────────────────────────
CONNECTOR_UUIDS = [
    '14bb003d-f788-458f-a9f1-cfea09e8887f',  # J5  Conn_01x03
    '7e1b747b-43a1-47fd-8cef-e40612610f6c',  # J3  Conn_01x03
    '96326b79-3d95-4143-ac58-6bf9244b19b2',  # J4  Conn_01x04
    'ba716377-9d25-48eb-9bea-2696d43c00ec',  # J6  Conn_01x03
]

def _remove_symbol_block(t, uuid_str):
    # Match the surrounding (symbol ...) block that contains this uuid line
    pat = (r'\t\(symbol\n'           # opening line
           r'(?:\t\t[^\n]*\n)*?'     # any header lines (non-greedy)
           r'\t\t\(uuid "'           # uuid line start
           + re.escape(uuid_str)
           + r'"\)\n'                # uuid line end
           r'(?:.*?\n)*?'            # any body lines (non-greedy)
           r'\t\)\n')                # closing line
    return re.sub(pat, '', t, flags=re.DOTALL)

for uid in CONNECTOR_UUIDS:
    text = _remove_symbol_block(text, uid)

# ─────────────────────────────────────────────────────────────────────────────
# 3.  Wire surgery
# ─────────────────────────────────────────────────────────────────────────────
def _wire_pts(x1, y1, x2, y2):
    """Return the pts line as it appears in the file."""
    return f'\t\t\t(xy {x1} {y1}) (xy {x2} {y2})'

def _remove_wire(t, x1, y1, x2, y2):
    """Remove a wire block whose pts match (x1,y1)-(x2,y2) in either order."""
    for pts in [_wire_pts(x1,y1,x2,y2), _wire_pts(x2,y2,x1,y1)]:
        pat = (r'\t\(wire\n'
               r'\t\t\(pts\n'
               + re.escape(pts) + r'\n'
               r'\t\t\)\n'
               r'(?:\t\t[^\n]*\n)*'   # stroke block
               r'\t\)\n')
        t = re.sub(pat, '', t)
    return t

def _truncate_wire(t, x1, y1, x2_old, y2_old, x2_new, y2_new):
    """Change the right endpoint of a wire."""
    old = _wire_pts(x1, y1, x2_old, y2_old)
    new = _wire_pts(x1, y1, x2_new, y2_new)
    return t.replace(old, new, 1)

# 3a.  Truncate long ESP32↔PN5180 wires: keep left half, remove connector stub.
#      The PN5180 net-label (at x=154.94) will remain at the new right endpoint.
text = _truncate_wire(text, '116.84', '52.07', '168.91', '52.07', '154.94', '52.07')  # MISO
text = _truncate_wire(text, '119.38', '63.5',  '168.91', '63.5',  '154.94', '63.5')   # RST
text = _truncate_wire(text, '121.92', '49.53', '168.91', '49.53', '154.94', '49.53')  # MOSI
text = _truncate_wire(text, '119.38', '54.61', '168.91', '54.61', '154.94', '54.61')  # CLK
text = _truncate_wire(text, '119.38', '66.04', '168.91', '66.04', '154.94', '66.04')  # BUSY
text = _truncate_wire(text, '120.65', '57.15', '168.91', '57.15', '154.94', '57.15')  # NSS

# 3b.  Remove short label-only stubs (signals with no ESP32 wire behind them).
text = _remove_wire(text, '154.94', '68.58', '168.91', '68.58')  # IRQ
text = _remove_wire(text, '154.94', '76.2',  '168.91', '76.2')   # GPIO
text = _remove_wire(text, '154.94', '78.74', '168.91', '78.74')  # AUX
text = _remove_wire(text, '154.94', '81.28', '168.91', '81.28')  # REQ

# 3c.  Remove J3 power-area wires.
text = _remove_wire(text, '165.1',  '38.1',  '168.91', '38.1')   # 3V3 → J3 pin1
text = _remove_wire(text, '163.83', '43.18', '168.91', '43.18')  # 5V  → J3 pin3
text = _remove_wire(text, '124.46', '40.64', '168.91', '40.64')  # GND wire to J3
text = _remove_wire(text, '163.83', '38.1',  '165.1',  '38.1')   # 3V3 label → junction
text = _remove_wire(text, '165.1',  '38.1',  '165.1',  '30.48')  # junction → 3V3 bus
text = _remove_wire(text, '163.83', '43.18', '163.83', '45.72')  # 5V label → bus

# ─────────────────────────────────────────────────────────────────────────────
# 4.  Remove J3-area net-labels and junction
# ─────────────────────────────────────────────────────────────────────────────
def _remove_label(t, name, x, y, angle=0):
    pat = (r'\t\(label "' + re.escape(name) + r'"\n'
           r'\t\t\(at ' + re.escape(str(x)) + r' ' + re.escape(str(y)) + r' '
           + str(angle) + r'\)\n'
           r'(?:\t\t[^\n]*\n|\t\t\t[^\n]*\n|\t\t\t\t[^\n]*\n)*'
           r'\t\)\n')
    return re.sub(pat, '', t)

def _remove_junction(t, x, y):
    pat = (r'\t\(junction\n'
           r'\t\t\(at ' + re.escape(str(x)) + r' ' + re.escape(str(y)) + r'\)\n'
           r'(?:\t\t[^\n]*\n)*'
           r'\t\)\n')
    return re.sub(pat, '', t)

text = _remove_label(text, '3V3', '163.83', '38.1')
text = _remove_label(text, 'GND', '163.83', '40.64')
text = _remove_label(text, '5V',  '163.83', '43.18')
text = _remove_junction(text, '165.1', '38.1')

# ─────────────────────────────────────────────────────────────────────────────
# 5.  Add Conn_01x13 to lib_symbols  (insert before Conn_01x19)
# ─────────────────────────────────────────────────────────────────────────────
CONN_01X13_LIB = r"""		(symbol "Connector_Generic:Conn_01x13"
			(pin_names
				(offset 1.016)
				(hide yes)
			)
			(exclude_from_sim no)
			(in_bom yes)
			(on_board yes)
			(in_pos_files yes)
			(duplicate_pin_numbers_are_jumpers no)
			(property "Reference" "J"
				(at 0 17.78 0)
				(show_name no)
				(do_not_autoplace no)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Value" "Conn_01x13"
				(at 0 -17.78 0)
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
			(property "Description" "Generic connector, single row, 01x13, script generated (kicad-library-utils/schlib/autogen/connector/)"
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
			(property "ki_keywords" "connector"
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
			(property "ki_fp_filters" "Connector*:*_1x??_*"
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
			(symbol "Conn_01x13_1_1"
				(rectangle
					(start -1.27 16.51)
					(end 1.27 -16.51)
					(stroke
						(width 0.254)
						(type default)
					)
					(fill
						(type background)
					)
				)
				(rectangle
					(start -1.27 15.367)
					(end 0 15.113)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 12.827)
					(end 0 12.573)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 10.287)
					(end 0 10.033)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 7.747)
					(end 0 7.493)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 5.207)
					(end 0 4.953)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 2.667)
					(end 0 2.413)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 0.127)
					(end 0 -0.127)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 -2.413)
					(end 0 -2.667)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 -4.953)
					(end 0 -5.207)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 -7.493)
					(end 0 -7.747)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 -10.033)
					(end 0 -10.287)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 -12.573)
					(end 0 -12.827)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(rectangle
					(start -1.27 -15.113)
					(end 0 -15.367)
					(stroke
						(width 0.1524)
						(type default)
					)
					(fill
						(type none)
					)
				)
				(pin passive line
					(at -5.08 15.24 0)
					(length 3.81)
					(name "Pin_1"
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
					(at -5.08 12.7 0)
					(length 3.81)
					(name "Pin_2"
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
				(pin passive line
					(at -5.08 10.16 0)
					(length 3.81)
					(name "Pin_3"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "3"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 7.62 0)
					(length 3.81)
					(name "Pin_4"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "4"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 5.08 0)
					(length 3.81)
					(name "Pin_5"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "5"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 2.54 0)
					(length 3.81)
					(name "Pin_6"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "6"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 0 0)
					(length 3.81)
					(name "Pin_7"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "7"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 -2.54 0)
					(length 3.81)
					(name "Pin_8"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "8"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 -5.08 0)
					(length 3.81)
					(name "Pin_9"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "9"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 -7.62 0)
					(length 3.81)
					(name "Pin_10"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "10"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 -10.16 0)
					(length 3.81)
					(name "Pin_11"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "11"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 -12.7 0)
					(length 3.81)
					(name "Pin_12"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "12"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
				)
				(pin passive line
					(at -5.08 -15.24 0)
					(length 3.81)
					(name "Pin_13"
						(effects
							(font
								(size 1.27 1.27)
							)
						)
					)
					(number "13"
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

# Insert before the Conn_01x19 symbol entry
text = text.replace(
    '\t\t(symbol "Connector_Generic:Conn_01x19"\n',
    CONN_01X13_LIB + '\t\t(symbol "Connector_Generic:Conn_01x19"\n',
    1
)

# ─────────────────────────────────────────────────────────────────────────────
# 6.  Add new J3 connector instance (Conn_01x13 at 173.99, 59.69)
# ─────────────────────────────────────────────────────────────────────────────
# Pin signal mapping: (pin_number, net_label, y_schematic)
# Connector NOT mirrored; pin k → y = 59.69 - (7-k)*2.54
PIN_MAP = [
    (1,  '5V',          44.45),
    (2,  '3V3',         46.99),
    (3,  'PN5180_RST',  49.53),
    (4,  'PN5180_NSS',  52.07),
    (5,  'PN5180_MOSI', 54.61),
    (6,  'PN5180_MISO', 57.15),
    (7,  'PN5180_CLK',  59.69),
    (8,  'PN5180_BUSY', 62.23),
    (9,  'GND',         64.77),
    (10, 'PN5180_IRQ',  67.31),
    (11, 'PN5180_GPIO', 69.85),
    (12, 'PN5180_REQ',  72.39),
    (13, 'PN5180_AUX',  74.93),
]

import uuid as _uuid

def _uid():
    return str(_uuid.uuid4())

_pin_uids = {k: _uid() for k, _, _ in PIN_MAP}

NEW_J3 = f"""\t(symbol
\t\t(lib_id "Connector_Generic:Conn_01x13")
\t\t(at 173.99 59.69 0)
\t\t(unit 1)
\t\t(body_style 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(in_pos_files yes)
\t\t(dnp no)
\t\t(fields_autoplaced yes)
\t\t(uuid "{_uid()}")
\t\t(property "Reference" "J3"
\t\t\t(at 176.53 58.4199 0)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left)
\t\t\t)
\t\t)
\t\t(property "Value" "PN5180_1x13"
\t\t\t(at 176.53 60.9599 0)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left)
\t\t\t)
\t\t)
\t\t(property "Footprint" "Connector_PinSocket_2.54mm:PinSocket_1x13_P2.54mm_Vertical"
\t\t\t(at 173.99 59.69 0)
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
\t\t\t(at 173.99 59.69 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Description" "PN5180 NFC module 13-pin female header"
\t\t\t(at 173.99 59.69 0)
\t\t\t(hide yes)
\t\t\t(show_name no)
\t\t\t(do_not_autoplace no)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
"""
for k, _net, _y in PIN_MAP:
    NEW_J3 += f'\t\t(pin "{k}"\n\t\t\t(uuid "{_pin_uids[k]}")\n\t\t)\n'

NEW_J3 += f"""\t\t(instances
\t\t\t(project "ESP32_AZDelivery_Baseboard"
\t\t\t\t(path "/6dfc456e-f0f7-4393-96f4-0b7c9f575168"
\t\t\t\t\t(reference "J3")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)
"""

# ─────────────────────────────────────────────────────────────────────────────
# 7.  Add 13 net-labels + wires for the new connector
# ─────────────────────────────────────────────────────────────────────────────
LABEL_X  = 154.94
PIN_X    = 168.91   # = 173.99 − 5.08

NEW_LABELS_WIRES = ''
for _k, net, y in PIN_MAP:
    NEW_LABELS_WIRES += f"""\t(label "{net}"
\t\t(at {LABEL_X} {y} 0)
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify left bottom)
\t\t)
\t\t(uuid "{_uid()}")
\t)
\t(wire
\t\t(pts
\t\t\t(xy {LABEL_X} {y}) (xy {PIN_X} {y})
\t\t)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(uuid "{_uid()}")
\t)
"""

# Insert everything just before the final bare ')' that closes (kicad_sch ...).
# The file ends with:  \t(embedded_fonts no)\n)\n
# We insert our new elements between the last top-level child and that final ')'.
assert text.endswith(')\n'), "unexpected file ending"
text = text[:-2] + '\n' + NEW_J3 + NEW_LABELS_WIRES + ')\n'

with open(SCH, 'w', encoding='utf-8') as f:
    f.write(text)

print('Done. Backup saved as', SCH + '.bak')
