#!/usr/bin/env python3
import requests, time, csv, os
from datetime import datetime

API_BASE = "http://192.168.4.1"
LOG_FILE = "essiccatore_log.csv"
INTERVAL = 60  # secondi

endpoints = {
    "bme280": "/api/bme280",
    "bed":    "/api/bed",
    "heater": "/api/heater",
}

def fetch(ep):
    try:
        r = requests.get(API_BASE + ep, timeout=3)
        return r.json()
    except Exception as e:
        return {"error": str(e)}

write_header = not os.path.exists(LOG_FILE)
with open(LOG_FILE, "a", newline="") as f:
    writer = csv.writer(f)
    if write_header:
        writer.writerow(["timestamp", "bme_temp", "bme_rh", "ntc_temp", "heater_output_pct", "heater_setpoint"])

    print(f"Logger avviato → {LOG_FILE}  (ogni {INTERVAL}s, Ctrl+C per fermare)\n")
    while True:
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        bme    = fetch(endpoints["bme280"])
        bed    = fetch(endpoints["bed"])
        heater = fetch(endpoints["heater"])

        bme_t  = bme.get("temperature", "")
        bme_rh = bme.get("humidity", "")
        ntc_t  = bed.get("temperature", "")
        h_out  = heater.get("outputPercent", "")
        h_set  = heater.get("setpoint", "")

        writer.writerow([ts, bme_t, bme_rh, ntc_t, h_out, h_set])
        f.flush()

        print(f"[{ts}]  BME: {bme_t}°C {bme_rh}%RH  |  NTC: {ntc_t}°C  |  Heater: {h_out}% (set {h_set}°C)")
        time.sleep(INTERVAL)
