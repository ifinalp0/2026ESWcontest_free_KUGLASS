from __future__ import annotations

from .models import WeatherContext


def weather_to_thermal_factor(weather: WeatherContext) -> float:
    temp_factor = max(0.0, min(1.0, (weather.temperature_c - 24.0) / 14.0))
    cloud_relief = max(0.0, min(1.0, weather.cloud_cover))
    rain_relief = 1.0 if weather.precipitation_mm > 0.2 else 0.0
    uv_factor = max(0.0, min(1.0, weather.uv_index / 9.0))
    return max(0.0, min(1.0, 0.62 * temp_factor + 0.38 * uv_factor - 0.22 * cloud_relief - 0.18 * rain_relief))

