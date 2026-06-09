# ODIN Backend — FastAPI su Raspberry Pi 5

Backend proxy FastAPI che funge da middleware tra APK mobile (via Tailscale) e firmware ESP32.

```
APK (via Tailscale) → Pi5 Backend (8000) → ESP32 (192.168.4.1)
```

## Setup su Raspberry Pi 5

### 1. Clone il repository

```bash
cd ~
git clone https://github.com/Ricky1966/Odin.git
cd Odin/backend
```

### 2. Crea ambiente virtuale Python

```bash
python3 -m venv venv
source venv/bin/activate
```

### 3. Installa dipendenze

```bash
pip install -r requirements.txt
```

### 4. Configurazione (opzionale)

Crea file `.env` per override:

```env
ESP32_HOST=192.168.4.1
ESP32_PORT=80
HOST=0.0.0.0
PORT=8000
DEBUG=false
REQUEST_TIMEOUT=5
```

Se non presente, usa valori di default in `config.py`.

### 5. Avvia il backend

```bash
python main.py
```

Oppure con uvicorn direttamente:

```bash
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

### 6. Verifica

```bash
curl http://localhost:8000/health
# {"status":"ok","service":"ODIN Backend"}

curl http://localhost:8000/api/slots
# {"slots":[...], "freeSlot":1}
```

## Endpoint disponibili

Tutti gli endpoint fanno proxy a `http://192.168.4.1` (ESP32).

| Metodo | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/slots` | Stato di tutti i 4 slot bobine |
| POST | `/api/slot/drying-time?slot=0&temperature=45&minutes=360` | Aggiorna tempo essiccazione |
| POST | `/api/slots/clear-all` | Resetta tutti gli slot |
| GET | `/api/sensor` | Leggi sensori T°/umidità |
| GET | `/api/heater` | Stato riscaldatore |
| POST | `/api/heater/auto-on` | Accendi AUTO |
| POST | `/api/heater/off` | Spegni |
| GET | `/health` | Health check |

## APK Mobile - Connessione

L'APK si connette a:
```
http://100.97.249.32:8000
```

(IP Tailscale della Pi5 - configurato in `lib/services/prefs.dart`)

## Troubleshooting

### ESP32 non raggiungibile

```
HTTPException: 503 - ESP32 non raggiungibile
```

Verifica:
```bash
ping 192.168.4.1
curl http://192.168.4.1/api/slots
```

### Port 8000 già in uso

```bash
lsof -i :8000
kill -9 <PID>
```

Oppure cambia port in `.env`:
```env
PORT=8001
```

### Connessione Tailscale

Verifica che Pi5 sia online in Tailscale:
```bash
tailscale ip
# 100.97.249.32
```

## Development

### Reload automatico

```bash
uvicorn main:app --reload
```

### Debug logging

```env
DEBUG=true
```

## Future

- [ ] Internazionalizzazione (i18n) - tradurre label/messaggi ESP32
- [ ] Autenticazione/Autorizzazione
- [ ] Logging avanzato
- [ ] Caching risposte
- [ ] Parser GCode per consumo filamento
