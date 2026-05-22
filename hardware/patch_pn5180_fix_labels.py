#!/usr/bin/env python3
"""
Fix per il refactoring del connettore PN5180.

Problema: il primo script ha aggiunto label+fili a x=154.94, ma in quei
punti esistono ancora i fili troncati delle vecchie connessioni GPIO↔PN5180.
Dove lo stesso (x,y) ospita due label di segnale diverso, KiCad unisce le
reti → cortocircuiti (MOSI=RST, MISO=NSS, CLK=MOSI, NSS=MISO).

Soluzione:
  1. Rimuovi le 13 label+fili aggiunti in precedenza a x=154.94.
  2. Rimuovi le 4 label "flottanti" (IRQ/GPIO/AUX/REQ alle vecchie y).
  3. Aggiungi le 13 label+fili in schema sfalsato a DUE COLONNE:
       - pin dispari  → label a x=163.83  (filo corto  5.08 mm)
       - pin pari     → label a x=158.75  (filo lungo 10.16 mm)
     Le label sono ben distanziate e non collidono mai con i fili troncati
     che terminano a x=154.94.
"""

import re, shutil, uuid as _uuid
import os

SCH = os.path.join(os.path.dirname(__file__), 'ESP32_AZDelivery_Baseboard.kicad_sch')

with open(SCH, encoding='utf-8') as f:
    text = f.read()

shutil.copy(SCH, SCH + '.bak2')

# ─── helper: rimuovi label per nome+posizione ──────────────────────────────
def remove_label(t, name, x, y):
    """Rimuove un (label NAME (at X Y ...) ...) dal testo."""
    pat = (r'\t\(label "' + re.escape(name) + r'"\n'
           r'\t\t\(at ' + re.escape(str(x)) + r' ' + re.escape(str(y)) + r' \d+\)\n'
           r'(?:\t\t[^\n]*\n|\t\t\t[^\n]*\n|\t\t\t\t[^\n]*\n|\t\t\t\t\t[^\n]*\n)*'
           r'\t\)\n')
    return re.sub(pat, '', t)

def remove_wire(t, x1, y1, x2, y2):
    """Rimuove un (wire ...) con quei due endpoint (in qualsiasi ordine)."""
    for pts in [f'(xy {x1} {y1}) (xy {x2} {y2})',
                f'(xy {x2} {y2}) (xy {x1} {y1})']:
        pat = (r'\t\(wire\n\t\t\(pts\n\t\t\t' + re.escape(pts) + r'\n'
               r'\t\t\)\n(?:\t\t[^\n]*\n)*\t\)\n')
        t = re.sub(pat, '', t)
    return t

def uid():
    return str(_uuid.uuid4())

# ── 1. Rimuovi le 13 label+fili aggiunti a x=154.94 dal primo script ────────
#    Pinout del nuovo J3:
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

OLD_LABEL_X = 154.94
PIN_X       = 168.91   # x delle aperture pin del connettore

for _k, net, y in PIN_MAP:
    text = remove_label(text, net, OLD_LABEL_X, y)
    text = remove_wire(text, OLD_LABEL_X, y, PIN_X, y)

# ── 2. Rimuovi le 4 label flottanti rimaste alle vecchie y ───────────────────
FLOATING = [
    ('PN5180_IRQ',  154.94, 68.58),
    ('PN5180_GPIO', 154.94, 76.2),
    ('PN5180_AUX',  154.94, 78.74),
    ('PN5180_REQ',  154.94, 81.28),
]
for name, x, y in FLOATING:
    text = remove_label(text, name, x, y)

# ── 3. Aggiungi le 13 label+fili sfalsati a due colonne ─────────────────────
#    pin dispari  → x=163.83  (filo 5.08 mm, posizione "vicina")
#    pin pari     → x=158.75  (filo 10.16 mm, posizione "lontana")

X_ODD  = 163.83   # 168.91 − 5.08
X_EVEN = 158.75   # 168.91 − 10.16

NEW_CONTENT = ''
for k, net, y in PIN_MAP:
    lx = X_ODD if k % 2 == 1 else X_EVEN
    NEW_CONTENT += f"""\t(label "{net}"
\t\t(at {lx} {y} 0)
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify left bottom)
\t\t)
\t\t(uuid "{uid()}")
\t)
\t(wire
\t\t(pts
\t\t\t(xy {lx} {y}) (xy {PIN_X} {y})
\t\t)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(uuid "{uid()}")
\t)
"""

# Inserisci prima del ')' finale che chiude kicad_sch
assert text.endswith(')\n'), "fine file inattesa"
text = text[:-2] + '\n' + NEW_CONTENT + ')\n'

with open(SCH, 'w', encoding='utf-8') as f:
    f.write(text)

print('Fix applicato. Backup: ' + SCH + '.bak2')
