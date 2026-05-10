"""Fetch weather data via Apple WeatherKit and return display-ready values.

Process overview:
1. Request current weather data from WeatherKit.
2. Request daily forecast data from WeatherKit.
3. Safely extract current temperature, daily max/min temperatures, and the
        forecast condition code from the JSON payloads.
4. Round numeric temperature values for display.
5. Map WeatherKit condition codes to this project's icon keys.
6. Return a normalized dictionary used by the display layer.

Important API note:
- This implementation is built specifically for Apple WeatherKit endpoints,
    auth flow (JWT), and response schema.
- If you switch to a different weather API provider, you must adjust the
    request URLs, authentication logic, JSON field extraction, and condition
    code-to-icon mapping logic.
"""

import jwt
import json
import time
import os
import urllib.error
import urllib.request
from http_request_ssl import fetch_text_with_ssl

URL_WEATHER_CURRENT = "https://weatherkit.apple.com/api/v1/weather/ja/35.5999/139.6202?dataSets=currentWeather&timezone=Asia%2FTokyo"
URL_WEATHER_FORECAST = "https://weatherkit.apple.com/api/v1/weather/ja/35.5999/139.6202?dataSets=forecastDaily&timezone=Asia%2FTokyo"

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
WEATHERKIT_CONFIG_PATH = os.getenv(
    'WEATHERKIT_CONFIG_FILE',
    os.path.join(BASE_DIR, 'weatherkit_config.local.json')
)

def load_weatherkit_config():
    config = {}
    if os.path.isfile(WEATHERKIT_CONFIG_PATH):
        try:
            with open(WEATHERKIT_CONFIG_PATH, 'r', encoding='utf-8') as f:
                config = json.load(f)
        except (json.JSONDecodeError, OSError) as error:
            raise RuntimeError(
                f'Invalid WeatherKit config file: {WEATHERKIT_CONFIG_PATH} ({error})'
            ) from error

    team = config.get('team_id')
    service = config.get('service_id')
    key = config.get('key_id')
    private_key_path = config.get('private_key_path')

    return team, service, key, private_key_path

team_id, service_id, key_id, pem_file_path = load_weatherkit_config()

# condition codes from https://developer.apple.com/documentation/weatherkitjs/weatherconditioncode
dict_conditionCodes = {
  'BlowingDust': 'air',
  'Clear': 'clear_day',
  'Cloudy': 'cloud',
  'Foggy': 'foggy',
  'Haze': 'foggy',
  'MostlyClear': 'clear_day',
  'MostlyCloudy': 'cloud',
  'PartlyCloudy': 'partly_cloudy_day',
  'Smoky': 'foggy',
  'Breezy': 'air',
  'Windy': 'air',
  'Drizzle': 'rainy_light',
  'HeavyRain': 'rainy_heavy',
  'IsolatedThunderstorms': 'thunderstorm',
  'Rain': 'rainy',
  'SunShowers': 'rainy_light',
  'ScatteredThunderstorms': 'thunderstorm',
  'StrongStorms': 'rainy_heavy',
  'Thunderstorms': 'thunderstorm',
  'Frigid': 'weather_snowy',
  'Hail': 'weather_hail',
  'Hot': 'heat',
  'Flurries': 'weather_snowy',
  'Sleet': 'rainy_snow',
  'Snow': 'weather_snowy',
  'SunFlurries': 'sunny_snowing',
  'WintryMix': 'weather_hail',
  'Blizzard': 'severe_cold',
  'BlowingSnow': "snowing_heavy",
  "FreezingDrizzle": "rainy_snow",
  "FreezingRain": "rainy",
  "HeavySnow": "snowing_heavy",
  "Hurricane": "thunderstorm",
  "TropicalStorm": "thunderstorm"
}

def fetch_weather(url):
    # Generate token times per request so long-running processes don't reuse expired JWTs.
    now = int(time.time())
    payload = {
        'iss': team_id,
        'iat': now,
        'exp': now + 1800,
        'sub': service_id,
    }
    headers = {
        'typ': 'JWT',
        'kid': key_id,
        'id': f'{team_id}.{service_id}',
    }

    with open(pem_file_path, 'r') as f:
        private_key = f.read()

    signed_token = jwt.encode(payload, private_key, algorithm='ES256', headers=headers)
    request = urllib.request.Request(
        url,
        headers={'Authorization': f'Bearer {signed_token}',
                'Content-Type': f'application/json;charset=utf-8'}
    )

    try:
        weather_data = fetch_text_with_ssl(request, timeout=10, resource_name="WeatherKit")
        json_data = json.loads(weather_data)
        return json_data
    except urllib.error.HTTPError as error:
        error_body = ''
        try:
            error_body = error.read().decode('utf-8', errors='replace')
        except Exception:
            pass
        print(f'WeatherKit HTTP error: {error.code} {error.reason}')
        if error_body:
            print('WeatherKit error body:')
            print(error_body)
    except (urllib.error.URLError, TimeoutError) as error:
        print(f"Failed to fetch WeatherKit data: {error}")
    return None

def get_round(value):
    try:
        return round(float(value))
    except (ValueError, TypeError):
        return '-'

def main():
    json_weather_current = fetch_weather(URL_WEATHER_CURRENT)
    json_weather_forecast = fetch_weather(URL_WEATHER_FORECAST)

    json_temp_current = json_weather_current.get('currentWeather', {}).get('temperature', '-') if json_weather_current else '-'
    json_extracted_forecast = json_weather_forecast.get('forecastDaily', {}).get('days', [])[0] if json_weather_forecast else {}
    json_restOfDay_forecast = json_extracted_forecast.get('restOfDayForecast', {}) if json_extracted_forecast else {}

    json_temp_max = json_extracted_forecast.get('temperatureMax', '-') if json_extracted_forecast else '-'
    json_temp_min = json_extracted_forecast.get('temperatureMin', '-') if json_extracted_forecast else '-'
    json_condition = json_restOfDay_forecast.get('conditionCode', '-') if json_restOfDay_forecast else '-'

    temp_current = get_round(json_temp_current)
    temp_max = get_round(json_temp_max)
    temp_min = get_round(json_temp_min)
    print(f"Current temperature: {temp_current}°C, Max: {temp_max}°C, Min: {temp_min}°C, Condition code: {json_condition}")
    condition_code = dict_conditionCodes.get(json_condition, 'unknown')
    
    result = {
        'current': temp_current,
        'max': temp_max,
        'min': temp_min,
        'condition': condition_code,
    }

    return result

if __name__ == "__main__":
    main()