"""Configurazione backend ODIN Pi5"""
from pydantic_settings import BaseSettings
from typing import Optional

class Settings(BaseSettings):
    """Impostazioni globali del backend Pi5"""

    # ESP32 connection
    esp32_host: str = "192.168.4.1"
    esp32_port: int = 80
    esp32_base_url: Optional[str] = None  # Se settato, lo usa invece di host:port

    # Server
    host: str = "0.0.0.0"
    port: int = 8000
    debug: bool = False

    # Timeout
    request_timeout: int = 5  # secondi

    class Config:
        env_file = ".env"

    @property
    def esp32_url(self) -> str:
        if self.esp32_base_url:
            return self.esp32_base_url.rstrip('/')
        return f"http://{self.esp32_host}:{self.esp32_port}"

settings = Settings()
