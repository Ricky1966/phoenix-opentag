#!/usr/bin/env python3
import serial, serial.tools.list_ports, sys, threading, requests, time

API_BASE = "http://192.168.4.1"
BAUD = 115200

def find_port():
    for p in ["/dev/ttyUSB0", "/dev/ttyACM0"]:
        try:
            s = serial.Serial(p, BAUD, timeout=1)
            print(f"Connesso su {p}")
            return s
        except:
            pass
    print("Nessuna porta trovata")
    sys.exit(1)

def serial_reader(ser):
    while True:
        try:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                print(f"[SERIAL] {line}")
        except:
            break

def api_poller():
    endpoints = ["/api/bme280", "/api/bed", "/api/heater"]
    while True:
        for ep in endpoints:
            try:
                r = requests.get(API_BASE + ep, timeout=2)
                print(f"[API] {ep}: {r.text.strip()}")
            except Exception as e:
                print(f"[API] {ep}: errore — {e}")
        time.sleep(5)

ser = find_port()
threading.Thread(target=serial_reader, args=(ser,), daemon=True).start()
threading.Thread(target=api_poller, daemon=True).start()

print("Monitor attivo. Ctrl+C per uscire.\n")
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    ser.close()