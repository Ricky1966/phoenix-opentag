#!/usr/bin/env python3
"""
Allinea NTC_1 (J8/R1/C1) agli altri tre circuiti NTC.
Sposta tutto di dy=-4.21 mm; C1 anche dx=-1.27 mm.

NTC_2/3/4 hanno pitch uniforme 25.00 mm.
NTC_1 corrente è 29.21 mm sotto NTC_2 → si sposta a 25.00 mm.
"""
import re, shutil, sys

SCH = "hardware/ESP32_AZDelivery_Baseboard.kicad_sch"
DY  = -4.21
DXC = -1.27   # solo per C1

def f(v):
    """Format float: max 4 decimali, no trailing zeros."""
    s = f"{v:.4f}".rstrip('0').rstrip('.')
    return s if s else '0'

shutil.copy(SCH, SCH + ".bak_align")

with open(SCH) as fp:
    txt = fp.read()

orig = txt

# ── Helper: sostituisce una coordinata (at x y rot) dentro txt ──────────────
def sub_at(old_x, old_y, old_r, new_x, new_y, new_r, txt):
    old = f"(at {old_x} {old_y} {old_r})"
    new = f"(at {f(new_x)} {f(new_y)} {new_r})"
    if old not in txt:
        print(f"  WARN: non trovato {old}", file=sys.stderr)
        return txt
    return txt.replace(old, new)

def sub_wire(x1,y1,x2,y2, nx1,ny1,nx2,ny2, txt):
    old = f"(xy {x1} {y1}) (xy {x2} {y2})"
    new = f"(xy {f(nx1)} {f(ny1)}) (xy {f(nx2)} {f(ny2)})"
    if old not in txt:
        print(f"  WARN wire: {old}", file=sys.stderr)
        return txt
    return txt.replace(old, new)

def sub_label_at(name, old_x, old_y, old_a, new_x, new_y, new_a, txt):
    old = f'(label "{name}"\n\t\t(at {old_x} {old_y} {old_a})'
    new = f'(label "{name}"\n\t\t(at {f(new_x)} {f(new_y)} {new_a})'
    if old not in txt:
        print(f"  WARN label '{name}': {old}", file=sys.stderr)
        return txt
    return txt.replace(old, new)

def sub_junc(ox, oy, nx, ny, txt):
    old = f"(junction\n\t\t(at {ox} {oy})"
    new = f"(junction\n\t\t(at {f(nx)} {f(ny)})"
    if old not in txt:
        print(f"  WARN junc: ({ox},{oy})", file=sys.stderr)
        return txt
    return txt.replace(old, new)


# ════════════════════════════════════════════════════════════════════════════
# 1. WIRES
# ════════════════════════════════════════════════════════════════════════════
print("Updating wires …")

# R1 SIG → junction: (44.45,109.22)→(44.45,113.03)
txt = sub_wire(44.45,109.22, 44.45,113.03,
               44.45,109.22+DY, 44.45,113.03+DY, txt)

# GPIO34 → junction: (34.29,113.03)→(44.45,113.03)
txt = sub_wire(34.29,113.03, 44.45,113.03,
               34.29,113.03+DY, 44.45,113.03+DY, txt)

# junction → J8 SIG: (44.45,113.03)→(44.45,118.11)
txt = sub_wire(44.45,113.03, 44.45,118.11,
               44.45,113.03+DY, 44.45,118.11+DY, txt)

# junction → C1 SIG: (44.45,113.03)→(48.26,113.03)
# endpoint C1 si sposta anche di dx=-1.27: 48.26→46.99
txt = sub_wire(44.45,113.03, 48.26,113.03,
               44.45,113.03+DY, 48.26+DXC,113.03+DY, txt)

# ════════════════════════════════════════════════════════════════════════════
# 2. LABELS
# ════════════════════════════════════════════════════════════════════════════
print("Updating labels …")

# 3V3: (44.45, 101.60, 90)
txt = sub_label_at("3V3",  "44.45","101.60",90,  44.45,101.60+DY,90, txt)

# GPIO34: (34.29, 113.03, 90)
txt = sub_label_at("GPIO34","34.29","113.03",90, 34.29,113.03+DY,90, txt)

# GND per J8: (46.99, 118.11, 90)
txt = sub_label_at("GND",  "46.99","118.11",90,  46.99,118.11+DY,90, txt)

# GND per C1: (55.88, 113.03, 90) — sposta anche dx
txt = sub_label_at("GND",  "55.88","113.03",90,  55.88+DXC,113.03+DY,90, txt)

# ════════════════════════════════════════════════════════════════════════════
# 3. JUNCTION
# ════════════════════════════════════════════════════════════════════════════
print("Updating junction …")
txt = sub_junc("44.45","113.03", 44.45,113.03+DY, txt)

# ════════════════════════════════════════════════════════════════════════════
# 4. COMPONENT SYMBOLS – aggiorna il blocco nel file in-place
# ════════════════════════════════════════════════════════════════════════════
print("Updating component symbol blocks …")

# ── R1 ──────────────────────────────────────────────────────────────────────
# Tutti gli (at ...) dentro il blocco R1 hanno y=105.41; aggiorniamo solo dentro il blocco.
# Strategia: trova il blocco, fai replace all dentro di esso, riscrivi.

def update_symbol_block(content, ref, replacements):
    """
    Trova il blocco (symbol ... (instances ... (reference "ref") ...) )
    e applica le sostituzioni (old_str, new_str) solo al suo interno.
    """
    # Pattern che trova il blocco corretto (non il più interno ma quello con reference)
    pattern = re.compile(
        r'(\(symbol\n\t\t\(lib_id "[^"]+"\)\n\t\t\(at [^\n]+\n'  # header
        r'.*?'
        r'\(reference "' + re.escape(ref) + r'"\)'
        r'.*?'
        r'\n\t\))',
        re.DOTALL
    )
    m = pattern.search(content)
    if not m:
        print(f"  WARN: blocco simbolo {ref} non trovato!", file=sys.stderr)
        return content
    block = m.group(1)
    new_block = block
    for old, new in replacements:
        if old not in new_block:
            print(f"  WARN in {ref}: '{old}' non trovato", file=sys.stderr)
        new_block = new_block.replace(old, new)
    return content[:m.start()] + new_block + content[m.end():]

# R1: tutti i (at ... 105.41 ...) → 101.20; (at ... 106.426 ...) → 102.216, etc.
r1_subs = [
    ("(at 44.45 105.41 180)", f"(at 44.45 {f(105.41+DY)} 180)"),
    ("(at 37.592 106.426 90)", f"(at 37.592 {f(106.426+DY)} 90)"),
    ("(at 40.64 107.188 90)", f"(at 40.64 {f(107.188+DY)} 90)"),
    ("(at 46.228 105.41 90)", f"(at 46.228 {f(105.41+DY)} 90)"),
    ("(at 44.45 105.41 0)", f"(at 44.45 {f(105.41+DY)} 0)"),  # ×2 ma replace-all
]
txt = update_symbol_block(txt, "R1", r1_subs)

# C1: sposta di (dx=-1.27, dy=-4.21)
c1_subs = [
    ("(at 52.07 113.03 90)",  f"(at {f(52.07+DXC)} {f(113.03+DY)} 90)"),
    ("(at 52.07 105.41 0)",   f"(at {f(52.07+DXC)} {f(105.41+DY)} 0)"),
    ("(at 55.626 105.41 0)",  f"(at {f(55.626+DXC)} {f(105.41+DY)} 0)"),
    ("(at 55.88 112.0648 0)", f"(at {f(55.88+DXC)} {f(112.0648+DY)} 0)"),
    ("(at 52.07 113.03 0)",   f"(at {f(52.07+DXC)} {f(113.03+DY)} 0)"),  # ×2
]
txt = update_symbol_block(txt, "C1", c1_subs)

# J8
j8_subs = [
    ("(at 46.99 123.19 270)", f"(at 46.99 {f(123.19+DY)} 270)"),
    ("(at 54.61 123.444 0)",  f"(at 54.61 {f(123.444+DY)} 0)"),
    ("(at 56.388 122.682 0)", f"(at 56.388 {f(122.682+DY)} 0)"),
    ("(at 46.99 123.19 0)",   f"(at 46.99 {f(123.19+DY)} 0)"),  # ×3
]
txt = update_symbol_block(txt, "J8", j8_subs)

# ════════════════════════════════════════════════════════════════════════════
# 5. WRITE
# ════════════════════════════════════════════════════════════════════════════
if txt == orig:
    print("⚠ Nessuna modifica effettuata!")
    sys.exit(1)

changed = sum(1 for a, b in zip(orig.splitlines(), txt.splitlines()) if a != b)
print(f"Righe modificate: {changed}")

with open(SCH, 'w') as fp:
    fp.write(txt)

print("✓ Allineamento NTC_1 completato.")
print(f"  J8:  ({46.99}, {123.19}) → ({46.99}, {f(123.19+DY)})")
print(f"  R1:  ({44.45}, {105.41}) → ({44.45}, {f(105.41+DY)})")
print(f"  C1:  ({52.07}, {113.03}) → ({f(52.07+DXC)}, {f(113.03+DY)})")
