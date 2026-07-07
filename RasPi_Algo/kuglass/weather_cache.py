from __future__ import annotations

import json
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass

from .models import WeatherContext
from .utils import now_ms


@dataclass
class WeatherCache:
    latitude: float = 37.5665
    longitude: float = 126.9780
    refresh_s: int = 180
    stale_s: int = 600
    source: str = "mock"
    _last_fetch_s: float = 0.0
    _context: WeatherContext = WeatherContext()

    def get(self) -> WeatherContext:
        current = time.time()
        if self.source == "mock":
            return self._mock_context()
        if current - self._last_fetch_s >= self.refresh_s:
            try:
                self._context = self.fetch_once()
                self._last_fetch_s = current
            except Exception:
                pass
        stale = (now_ms() - self._context.updated_ms) > self.stale_s * 1000
        if stale != self._context.stale:
            self._context = WeatherContext(
                self._context.temperature_c,
                self._context.cloud_cover,
                self._context.precipitation_mm,
                self._context.uv_index,
                stale,
                self._context.source,
                self._context.updated_ms,
            )
        return self._context

    def fetch_once(self) -> WeatherContext:
        params = urllib.parse.urlencode(
            {
                "latitude": self.latitude,
                "longitude": self.longitude,
                "current": "temperature_2m,precipitation,cloud_cover,uv_index",
            }
        )
        url = f"https://api.open-meteo.com/v1/forecast?{params}"
        with urllib.request.urlopen(url, timeout=4.0) as response:
            payload = json.loads(response.read().decode("utf-8"))
        current = payload.get("current", {})
        return WeatherContext(
            temperature_c=float(current.get("temperature_2m", 25.0)),
            cloud_cover=float(current.get("cloud_cover", 40.0)) / 100.0,
            precipitation_mm=float(current.get("precipitation", 0.0)),
            uv_index=float(current.get("uv_index", 3.0)),
            stale=False,
            source="open-meteo",
            updated_ms=now_ms(),
        )

    def _mock_context(self) -> WeatherContext:
        phase = (time.time() / 12.0) % 1.0
        temp = 28.0 + 5.0 * phase
        return WeatherContext(temp, 0.25, 0.0, 6.0, False, "mock", now_ms())


def make_weather_cache(config: dict) -> WeatherCache:
    cfg = config.get("weather", {})
    return WeatherCache(
        float(cfg.get("latitude", 37.5665)),
        float(cfg.get("longitude", 126.9780)),
        int(cfg.get("refresh_s", 180)),
        int(cfg.get("stale_s", 600)),
        str(cfg.get("source", "mock")),
    )

