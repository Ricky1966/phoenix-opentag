#!/usr/bin/env python3
"""
Aggiunge 3 circuiti NTC (J12/J13/J14, R2/R3/R4, C2/C3/C4) per GPIO35/36/39
nella zona sopra il circuito NTC_1 (J8/R1/C1).
Layout: y ~ 5-18, accanto a NTC_1 che è a y ~ 22-38.
"""
import uuid as _uuid

SCH_PATH = "/home/ricky/AndroidStudioProjects/essiccatore/essiccatore/hardware/ESP32_AZDelivery_Baseboard.kicad_sch"
PROJ = "ESP32_AZDelivery_Baseboard"
SHEET_UUID = "6dfc456e-f0f7-4393-96f4-0b7c9f575168"

def u():
    return str(_uuid.uuid4())

def fmt(v):
    """Format coordinate: remove trailing zeros but keep at most 4 decimal places."""
    s = f"{v:.4f}".rstrip('0').rstrip('.')
    return s if '.' in s or s == '0' else s

def wire_block(x1, y1, x2, y2):
    return (
        f"\t(wire\n"
        f"\t\t(pts\n"
        f"\t\t\t(xy {fmt(x1)} {fmt(y1)}) (xy {fmt(x2)} {fmt(y2)})\n"
        f"\t\t)\n"
        f"\t\t(stroke\n"
        f"\t\t\t(width 0)\n"
        f"\t\t\t(type default)\n"
        f"\t\t)\n"
        f"\t\t(uuid \"{u()}\")\n"
        f"\t)"
    )

def junction_block(x, y):
    return (
        f"\t(junction\n"
        f"\t\t(at {fmt(x)} {fmt(y)})\n"
        f"\t\t(diameter 0)\n"
        f"\t\t(color 0 0 0 0)\n"
        f"\t\t(uuid \"{u()}\")\n"
        f"\t)"
    )

def label_block(text, x, y, angle=0, justify="left bottom"):
    return (
        f"\t(label \"{text}\"\n"
        f"\t\t(at {fmt(x)} {fmt(y)} {angle})\n"
        f"\t\t(effects\n"
        f"\t\t\t(font\n"
        f"\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t)\n"
        f"\t\t\t(justify {justify})\n"
        f"\t\t)\n"
        f"\t\t(uuid \"{u()}\")\n"
        f"\t)"
    )

def resistor_block(ref, cx, cy, rot=90):
    uid = u()
    # rot=90 → horizontal: pin2 at cx-3.81 (signal), pin1 at cx+3.81 (3V3)
    # Field positions rotated with component
    pr_x, pr_y = cx, cy - 6.35
    pv_x, pv_y = cx, cy - 3.81
    return (
        f"\t(symbol\n"
        f"\t\t(lib_id \"Device:R\")\n"
        f"\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t(unit 1)\n"
        f"\t\t(body_style 1)\n"
        f"\t\t(exclude_from_sim no)\n"
        f"\t\t(in_bom yes)\n"
        f"\t\t(on_board yes)\n"
        f"\t\t(in_pos_files yes)\n"
        f"\t\t(dnp no)\n"
        f"\t\t(fields_autoplaced yes)\n"
        f"\t\t(uuid \"{uid}\")\n"
        f"\t\t(property \"Reference\" \"{ref}\"\n"
        f"\t\t\t(at {fmt(pr_x)} {fmt(pr_y)} {rot})\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Value\" \"100K\"\n"
        f"\t\t\t(at {fmt(pv_x)} {fmt(pv_y)} {rot})\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Footprint\" \"Resistor_THT:R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Datasheet\" \"\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} 0)\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Description\" \"Resistor\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} 0)\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(pin \"2\"\n"
        f"\t\t\t(uuid \"{u()}\")\n"
        f"\t\t)\n"
        f"\t\t(pin \"1\"\n"
        f"\t\t\t(uuid \"{u()}\")\n"
        f"\t\t)\n"
        f"\t\t(instances\n"
        f"\t\t\t(project \"{PROJ}\"\n"
        f"\t\t\t\t(path \"/{SHEET_UUID}\"\n"
        f"\t\t\t\t\t(reference \"{ref}\")\n"
        f"\t\t\t\t\t(unit 1)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t)"
    )

def capacitor_block(ref, cx, cy, rot=0):
    uid = u()
    # rot=0 → vertical: pin1 at cy-3.81 (top/signal), pin2 at cy+3.81 (bottom/GND)
    pr_x, pr_y = cx + 3.81, cy - 1.27
    pv_x, pv_y = cx + 3.81, cy + 1.27
    return (
        f"\t(symbol\n"
        f"\t\t(lib_id \"Device:C\")\n"
        f"\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t(unit 1)\n"
        f"\t\t(body_style 1)\n"
        f"\t\t(exclude_from_sim no)\n"
        f"\t\t(in_bom yes)\n"
        f"\t\t(on_board yes)\n"
        f"\t\t(in_pos_files yes)\n"
        f"\t\t(dnp no)\n"
        f"\t\t(fields_autoplaced yes)\n"
        f"\t\t(uuid \"{uid}\")\n"
        f"\t\t(property \"Reference\" \"{ref}\"\n"
        f"\t\t\t(at {fmt(pr_x)} {fmt(pr_y)} {rot})\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t\t(justify left)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Value\" \"100nF\"\n"
        f"\t\t\t(at {fmt(pv_x)} {fmt(pv_y)} {rot})\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t\t(justify left)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Footprint\" \"Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Datasheet\" \"\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Description\" \"Unpolarized capacitor\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(pin \"2\"\n"
        f"\t\t\t(uuid \"{u()}\")\n"
        f"\t\t)\n"
        f"\t\t(pin \"1\"\n"
        f"\t\t\t(uuid \"{u()}\")\n"
        f"\t\t)\n"
        f"\t\t(instances\n"
        f"\t\t\t(project \"{PROJ}\"\n"
        f"\t\t\t\t(path \"/{SHEET_UUID}\"\n"
        f"\t\t\t\t\t(reference \"{ref}\")\n"
        f"\t\t\t\t\t(unit 1)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t)"
    )

def connector_block(ref, value, cx, cy, rot=180):
    uid = u()
    # For rot=180: pins open to the right at cx+5.08
    # pin1 at (cx+5.08, cy), pin2 at (cx+5.08, cy-2.54)
    # Properties: place below body (between JST and C)
    pr_x, pr_y = cx, cy + 3.0   # below JST
    pv_x, pv_y = cx, cy + 5.0
    return (
        f"\t(symbol\n"
        f"\t\t(lib_id \"Connector_Generic:Conn_01x02\")\n"
        f"\t\t(at {fmt(cx)} {fmt(cy)} {rot})\n"
        f"\t\t(unit 1)\n"
        f"\t\t(body_style 1)\n"
        f"\t\t(exclude_from_sim no)\n"
        f"\t\t(in_bom yes)\n"
        f"\t\t(on_board yes)\n"
        f"\t\t(in_pos_files yes)\n"
        f"\t\t(dnp no)\n"
        f"\t\t(fields_autoplaced yes)\n"
        f"\t\t(uuid \"{uid}\")\n"
        f"\t\t(property \"Reference\" \"{ref}\"\n"
        f"\t\t\t(at {fmt(pr_x)} {fmt(pr_y)} 0)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Value\" \"{value}\"\n"
        f"\t\t\t(at {fmt(pv_x)} {fmt(pv_y)} 0)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Footprint\" \"Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} 0)\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Datasheet\" \"\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} 0)\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(property \"Description\" \"Generic connector, single row, 01x02\"\n"
        f"\t\t\t(at {fmt(cx)} {fmt(cy)} 0)\n"
        f"\t\t\t(hide yes)\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n"
        f"\t\t\t\t(font\n"
        f"\t\t\t\t\t(size 1.27 1.27)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t\t(pin \"1\"\n"
        f"\t\t\t(uuid \"{u()}\")\n"
        f"\t\t)\n"
        f"\t\t(pin \"2\"\n"
        f"\t\t\t(uuid \"{u()}\")\n"
        f"\t\t)\n"
        f"\t\t(instances\n"
        f"\t\t\t(project \"{PROJ}\"\n"
        f"\t\t\t\t(path \"/{SHEET_UUID}\"\n"
        f"\t\t\t\t\t(reference \"{ref}\")\n"
        f"\t\t\t\t\t(unit 1)\n"
        f"\t\t\t\t)\n"
        f"\t\t\t)\n"
        f"\t\t)\n"
        f"\t)"
    )


# ── Geometry constants ────────────────────────────────────────────────────────
# JST center y; pin1 (GND) at y_jst, pin2 (NTC) at y_jst-2.54 = y_sig
Y_JST = 10.0
Y_SIG = Y_JST - 2.54          # 7.46  — signal line
Y_GPIO = 5.0                   # GPIO label (wire goes up from Y_SIG to here)
# Capacitor: center so pin1 (top) is 2.54mm below Y_SIG
Y_CAP_PIN1 = Y_SIG + 2.54     # 10.0  — same y as JST pin1 but different x
Y_CAP_CY   = Y_CAP_PIN1 + 3.81  # 13.81
Y_CAP_PIN2 = Y_CAP_CY + 3.81    # 17.62  — GND

NTC_CIRCUITS = [
    {"gpio": "GPIO35", "x_jst": 5.0,  "jref": "J12", "rref": "R2", "cref": "C2", "n": 2},
    {"gpio": "GPIO36", "x_jst": 30.0, "jref": "J13", "rref": "R3", "cref": "C3", "n": 3},
    {"gpio": "GPIO39", "x_jst": 55.0, "jref": "J14", "rref": "R4", "cref": "C4", "n": 4},
]

blocks = []

for ntc in NTC_CIRCUITS:
    x = ntc["x_jst"]

    # JST Conn_01x02 rot=180 center at (x, Y_JST)
    #   pin1 (GND)    at (x+5.08, Y_JST)
    #   pin2 (signal) at (x+5.08, Y_SIG)
    jst_pin1_x = x + 5.08
    jst_pin2_x = x + 5.08
    junc_x = x + 10.16          # junction/C column

    # Resistor 100K rot=90 center at (x+17.78, Y_SIG)
    #   pin2 (signal) at x+13.97, pin1 (3V3) at x+21.59
    r_cx = x + 17.78

    # Capacitor 100nF rot=0 center at (junc_x, Y_CAP_CY)
    #   pin1 (top/signal) at Y_CAP_PIN1, pin2 (bottom/GND) at Y_CAP_PIN2
    c_cx = junc_x

    # ── Symbols ──────────────────────────────────────────────────────────────
    blocks.append(connector_block(ntc["jref"], f"NTC{ntc['n']}_SENSOR", x, Y_JST))
    blocks.append(resistor_block(ntc["rref"], r_cx, Y_SIG))
    blocks.append(capacitor_block(ntc["cref"], c_cx, Y_CAP_CY))

    # ── Wires ────────────────────────────────────────────────────────────────
    # JST pin2 → junction (horizontal, left to right at Y_SIG)
    blocks.append(wire_block(jst_pin2_x, Y_SIG, junc_x, Y_SIG))
    # junction → R pin2 (continue right at Y_SIG)
    blocks.append(wire_block(junc_x, Y_SIG, x + 13.97, Y_SIG))
    # junction → GPIO label (vertical, going up)
    blocks.append(wire_block(junc_x, Y_SIG, junc_x, Y_GPIO))
    # junction → C pin1 (vertical, going down)
    blocks.append(wire_block(junc_x, Y_SIG, junc_x, Y_CAP_PIN1))

    # ── Junction (4 wires meet here) ─────────────────────────────────────────
    blocks.append(junction_block(junc_x, Y_SIG))

    # ── Net labels ───────────────────────────────────────────────────────────
    # GND at JST pin1
    blocks.append(label_block("GND", jst_pin1_x, Y_JST))
    # 3V3 at R pin1
    blocks.append(label_block("3V3", x + 21.59, Y_SIG))
    # GND at C pin2
    blocks.append(label_block("GND", c_cx, Y_CAP_PIN2))
    # GPIO at top of signal stub
    blocks.append(label_block(ntc["gpio"], junc_x, Y_GPIO))

new_content = "\n".join(blocks) + "\n"

# ── Patch file ────────────────────────────────────────────────────────────────
with open(SCH_PATH, "r") as f:
    content = f.read()

last = content.rfind(")")
assert last != -1, "Nessuna parentesi chiusa trovata!"

patched = content[:last] + new_content + content[last:]

with open(SCH_PATH, "w") as f:
    f.write(patched)

print("✓ Aggiunti 3 circuiti NTC:")
print("  NTC_2 GPIO35: J12, R2, C2  @  x=5")
print("  NTC_3 GPIO36: J13, R3, C3  @ x=30")
print("  NTC_4 GPIO39: J14, R4, C4  @ x=55")
print(f"  y_signal={Y_SIG}, y_jst={Y_JST}, y_cap_gnd={Y_CAP_PIN2}")
