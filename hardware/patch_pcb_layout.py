#!/usr/bin/env python3
"""
Riposiziona i footprint nella PCB mantenendo J1, J2 fissi
e preservando J7/J8/R1/C1 che hanno già tracce valide.

Layout a blocchi:
  LEFT  (x≈75-93):  NTC circuits  J12/J13/J14 + R2-R4 + C2-C4  +  J15/JP1
  RIGHT (x≈140-155): Heaters J4/J5/J6/J9 + Fans J10/J11 + PN5180 J3
  ABOVE (già ok):   J7 (BME280)  J8/R1/C1 (NTC1)
"""
import re, shutil, sys

PCB = "hardware/ESP32_AZDelivery_Baseboard.kicad_pcb"
shutil.copy(PCB, PCB + ".bak_layout")

with open(PCB) as f:
    txt = f.read()

# ── Nuove posizioni ──────────────────────────────────────────────────────────
# Formato: (x, y, rot)   rot=None → preserva la rotazione attuale
TARGETS = {
    # PN5180 socket: i traces esistenti finiscono a x≈155, y≈100-130
    "J3":  (155.0, 100.0, 0),

    # Heater/Fan JST 2-pin — colonna destra a x=140, spaziatura 10 mm
    "J4":  (140.0, 104.0, 0),   # HEATER1 GPIO25
    "J5":  (140.0, 114.0, 0),   # HEATER2 GPIO26
    "J6":  (140.0, 124.0, 0),   # HEATER3 GPIO33
    "J9":  (140.0, 134.0, 0),   # HEATER4 GPIO4
    "J10": (140.0, 144.0, 0),   # FAN1    GPIO27
    "J11": (140.0, 154.0, 0),   # FAN2    GPIO32

    # NTC2/3/4 JST sensors — colonna sinistra, 3 righe
    "J12": (75.0,  107.0, 0),   # NTC2 GPIO35
    "J13": (75.0,  118.0, 0),   # NTC3 GPIO36
    "J14": (75.0,  129.0, 0),   # NTC4 GPIO39

    # NTC2/3/4 pullup resistors — colonna centrale-sinistra
    "R2":  (85.0,  107.0, 0),
    "R3":  (85.0,  118.0, 0),
    "R4":  (85.0,  129.0, 0),

    # NTC2/3/4 bypass caps — vicino ai resistori
    "C2":  (93.0,  107.0, 90),
    "C3":  (93.0,  118.0, 90),
    "C4":  (93.0,  129.0, 90),

    # Alimentazione BUCK + jumper — sotto le NTC, lato sinistro
    "J15": (80.0,  143.0, 0),
    "JP1": (90.0,  143.0, 0),
}

# ── Funzione di sostituzione ─────────────────────────────────────────────────
def reposition_footprint(content, ref, new_x, new_y, new_rot):
    """
    Trova il blocco footprint con (reference "ref") e aggiorna
    il (at x y [rot]) a livello di footprint (non nelle property figlie).
    Restituisce il contenuto aggiornato + True se trovato.
    """
    # Trova l'inizio del blocco footprint
    # Cerca "(footprint" che contenga (reference "ref")
    # Pattern: da \(footprint fino al blocco instances o fine blocco
    fp_start = re.compile(r'\(footprint\s+"[^"]*"')

    pos = 0
    while True:
        m = fp_start.search(content, pos)
        if not m:
            return content, False

        # Trova la fine di questo blocco (paren matching)
        start = m.start()
        depth = 0
        i = start
        while i < len(content):
            if content[i] == '(':
                depth += 1
            elif content[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = content[start:i+1]

        # Controlla se questo blocco contiene la reference giusta
        if f'(property "Reference" "{ref}"' not in block:
            pos = m.end()
            continue

        # Trovato il blocco giusto.
        # Sostituisce il (at x y [rot]) che è figlio DIRETTO del footprint
        # (cioè i primi caratteri dopo la lib_id line, prima delle property)
        # Strategia: trova il primo (at ...) nel blocco che NON sia dentro
        # una sub-espressione profonda (layer, uuid, property...).
        # Più semplice: il (at ...) del footprint è sempre nelle prime righe
        # del blocco, prima di qualsiasi (property ...

        # Trova la posizione del primo "(at " nel blocco
        # che sia sulla stessa "profondità 1" del blocco footprint
        at_pat = re.compile(r'\(at\s+([\d\.\-]+)\s+([\d\.\-]+)(?:\s+([\d\.\-]+))?\)')

        # Scansiona il blocco a profondità 1
        block_inner_start = m.end() - start  # dopo "(footprint "..."
        d = 1
        j = block_inner_start
        at_match = None
        while j < len(block):
            c = block[j]
            if c == '(':
                d += 1
                if d == 2:
                    # Inizio di un figlio diretto
                    m2 = at_pat.match(block, j)
                    if m2:
                        at_match = m2
                        break
            elif c == ')':
                d -= 1
            j += 1

        if not at_match:
            print(f"  WARN: (at) non trovato nel blocco {ref}", file=sys.stderr)
            return content, False

        # Costruisce il nuovo (at ...)
        old_rot = at_match.group(3)
        if new_rot is not None:
            new_rot_str = f" {new_rot}" if new_rot != 0 else ""
        else:
            new_rot_str = f" {old_rot}" if old_rot else ""

        new_at = f"(at {new_x} {new_y}{new_rot_str})"

        # Posizioni assolute nel file originale
        abs_at_start = start + at_match.start()
        abs_at_end   = start + at_match.end()

        content = content[:abs_at_start] + new_at + content[abs_at_end:]
        return content, True

# ── Applica tutte le sostituzioni ────────────────────────────────────────────
for ref, (x, y, rot) in TARGETS.items():
    txt, ok = reposition_footprint(txt, ref, x, y, rot)
    if ok:
        print(f"  ✓ {ref:6s} → ({x}, {y}) rot={rot}")
    else:
        print(f"  ✗ {ref:6s} NON TROVATO", file=sys.stderr)

# ── Salva ────────────────────────────────────────────────────────────────────
with open(PCB, 'w') as f:
    f.write(txt)

print("\n✓ Layout PCB aggiornato.")
print("  Apri in KiCad e verifica DRC/ratsnest; poi effettua il routing.")
