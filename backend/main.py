"""ODIN Backend - FastAPI proxy su Raspberry Pi 5"""
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import logging
import sys
from config import settings
from routers import api

logging.basicConfig(
    level=logging.DEBUG if settings.debug else logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

@asynccontextmanager
async def lifespan(app: FastAPI):
    yield
    await api.shutdown()

app = FastAPI(
    title="ODIN Backend",
    description="Proxy FastAPI per ODIN dryer (Pi5 → ESP32)",
    version="0.1.0",
    lifespan=lifespan
)

# CORS per APK mobile via Tailscale
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # APK su Tailscale può venire da IP diversi
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Include router API
app.include_router(api.router)

@app.get("/health")
async def health():
    """Health check endpoint"""
    return {"status": "ok", "service": "ODIN Backend"}

@app.get("/")
async def root():
    """Root endpoint"""
    return {
        "name": "ODIN Backend",
        "version": "0.1.0",
        "esp32_url": settings.esp32_url
    }

if __name__ == "__main__":
    import uvicorn

    logger.info(f"Starting ODIN Backend")
    logger.info(f"ESP32 URL: {settings.esp32_url}")
    logger.info(f"Listening on {settings.host}:{settings.port}")

    uvicorn.run(
        app,
        host=settings.host,
        port=settings.port,
        log_level="debug" if settings.debug else "info"
    )
