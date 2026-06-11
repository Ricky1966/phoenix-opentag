"""API proxy endpoints - forward requests to ESP32"""
from fastapi import APIRouter, HTTPException
from fastapi.responses import JSONResponse
import httpx
from typing import Any, Optional
from config import settings
import logging
import json
from pathlib import Path

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api", tags=["api"])

# Path to translations directory
TRANSLATIONS_DIR = Path(__file__).parent.parent / "translations"

# Client per comunicare con ESP32
esp32_client = None

def get_esp32_client():
    global esp32_client
    if esp32_client is None:
        esp32_client = httpx.AsyncClient(
            base_url=settings.esp32_url,
            timeout=settings.request_timeout
        )
    return esp32_client

async def _proxy_get(endpoint: str, params: Optional[dict] = None):
    """Helper per GET request a ESP32"""
    try:
        client = get_esp32_client()
        response = await client.get(endpoint, params=params)
        response.raise_for_status()
        return response.json()
    except httpx.HTTPStatusError as e:
        logger.error(f"ESP32 error on {endpoint}: {e.status_code}")
        raise HTTPException(status_code=e.status_code, detail=str(e))
    except httpx.RequestError as e:
        logger.error(f"Connection error to ESP32: {e}")
        raise HTTPException(status_code=503, detail="ESP32 non raggiungibile")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise HTTPException(status_code=500, detail="Errore interno")

async def _proxy_post(endpoint: str, data: Optional[dict] = None, params: Optional[dict] = None):
    """Helper per POST request a ESP32"""
    try:
        client = get_esp32_client()
        response = await client.post(endpoint, json=data, params=params)
        response.raise_for_status()
        return response.json()
    except httpx.HTTPStatusError as e:
        logger.error(f"ESP32 error on {endpoint}: {e.status_code}")
        raise HTTPException(status_code=e.status_code, detail=str(e))
    except httpx.RequestError as e:
        logger.error(f"Connection error to ESP32: {e}")
        raise HTTPException(status_code=503, detail="ESP32 non raggiungibile")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise HTTPException(status_code=500, detail="Errore interno")

# ──────────────────────────────────────────────────────────
# TRANSLATIONS ENDPOINTS
# ──────────────────────────────────────────────────────────

@router.get("/translations/{language}")
async def get_translations(language: str):
    """Ritorna file traduzioni per lingua (fallback: EN)"""
    lang_file = TRANSLATIONS_DIR / f"{language}.json"

    if not lang_file.exists():
        logger.warning(f"Language {language} not found, falling back to EN")
        lang_file = TRANSLATIONS_DIR / "en.json"

    try:
        with open(lang_file, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception as e:
        logger.error(f"Error loading translations for {language}: {e}")
        raise HTTPException(status_code=500, detail="Errore caricamento traduzioni")

@router.get("/translations")
async def list_translations():
    """Lista lingue disponibili"""
    try:
        files = list(TRANSLATIONS_DIR.glob("*.json"))
        languages = [f.stem for f in files]
        return {"available_languages": sorted(languages)}
    except Exception as e:
        logger.error(f"Error listing translations: {e}")
        raise HTTPException(status_code=500, detail="Errore listaggio lingue")

# ──────────────────────────────────────────────────────────
# SLOT ENDPOINTS
# ──────────────────────────────────────────────────────────

@router.get("/slots")
async def get_slots():
    """Leggi stato di tutti i 4 slot dalle bobine"""
    return await _proxy_get("/api/slots")

@router.post("/slot/drying-time")
async def update_slot_drying_time(
    slot: int,
    temperature: int,
    minutes: int
):
    """Aggiorna tempo di essiccazione per uno slot"""
    return await _proxy_post(
        "/api/slot/drying-time",
        params={"slot": slot, "temperature": temperature, "minutes": minutes}
    )

@router.post("/slots/clear-all")
async def clear_all_slots():
    """Resetta tutti gli slot"""
    return await _proxy_post("/api/slots/clear-all")

# ──────────────────────────────────────────────────────────
# NFC ENDPOINTS (FUTURE)
# ──────────────────────────────────────────────────────────

@router.get("/nfc/read")
async def read_nfc():
    """Leggi tag NFC (auto-assign al primo slot libero)"""
    return await _proxy_get("/api/nfc/read")

# ──────────────────────────────────────────────────────────
# SENSOR ENDPOINTS
# ──────────────────────────────────────────────────────────

@router.get("/bme280")
async def get_bme280():
    """Leggi sensore T°/umidità"""
    return await _proxy_get("/api/bme280")

@router.get("/heater")
async def get_heater():
    """Leggi stato riscaldatore"""
    return await _proxy_get("/api/heater")

@router.get("/bed")
async def get_bed():
    """Leggi temperatura bed"""
    return await _proxy_get("/api/bed")

@router.get("/sensor")
async def get_sensor():
    """Leggi tutti i sensori"""
    return await _proxy_get("/api/sensor")

# ──────────────────────────────────────────────────────────
# HEATER CONTROL ENDPOINTS
# ──────────────────────────────────────────────────────────

@router.post("/heater/auto-on")
async def heater_auto_on():
    """Accendi riscaldatore in modalità AUTO"""
    return await _proxy_post("/api/heater/auto-on")

@router.post("/heater/auto-off")
async def heater_auto_off():
    """Spegni modalità AUTO"""
    return await _proxy_post("/api/heater/auto-off")

@router.post("/heater/on")
async def heater_on():
    """Accendi riscaldatore manuale"""
    return await _proxy_post("/api/heater/on")

@router.post("/heater/off")
async def heater_off():
    """Spegni riscaldatore"""
    return await _proxy_post("/api/heater/off")

async def shutdown():
    """Cleanup on shutdown"""
    global esp32_client
    if esp32_client:
        await esp32_client.aclose()
