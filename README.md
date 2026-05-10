# HomeDisplay

A home information display system built on a LilyGo T5 4.7″ e-paper screen (ESP32-S3) and a Raspberry Pi. The Raspberry Pi collects data from several sources and streams it to the display over Bluetooth Low Energy (BLE).

## Features

- **Indoor environment** – temperature, humidity, CO₂, and IAQ from a local sensor
- **Weather** – current temperature, daily high/low, and condition icon via Apple WeatherKit
- **Calendar schedule** – work/out-of-office status for the next 7 days via Google Calendar and holiday
- **Transit alert** – real-time train delay status during morning commute hours
- **News headlines** – three AI-summarised headlines fetched from AllSides via Google Gemini

## Architecture

```
┌──────────────────────────────────────┐        BLE GATT        ┌─────────────────────────┐
│         Raspberry Pi                 │ ──────────────────────▶ │  LilyGo T5 4.7″ (ESP32) │
│  raspi_ble_server/ble_server.py      │                         │  esp32_ble_client/       │
│  ┌────────────┐  ┌─────────────────┐ │                         │  e-paper display         │
│  │ File watch │  │ Scheduled tasks │ │                         └─────────────────────────┘
│  │ (sensor)   │  │ weather/cal/news│ │
│  └────────────┘  └─────────────────┘ │
└──────────────────────────────────────┘
         ▲              ▲
  HomeTemp JSON   External APIs
  (inotify)       WeatherKit / Google Calendar etc.
```

The Raspberry Pi runs a BLE GATT server that the ESP32 connects to as a client. Each data type is sent as a separate JSON notification. The display re-renders once per minute.

## Hardware

| Component | Details |
|-----------|---------|
| Display | [LilyGo T5 4.7″ e-Paper S3](https://www.lilygo.cc/products/t5-4-7-inch-e-paper-v2-3) (ESP32-S3, 4.7″ EPD) |
| Server | Raspberry Pi (any model with Bluetooth 4.0+) |
| Sensor | Any sensor compatible with the [HomeTemp](https://github.com/in391/HomeTemp) project |

## Repository Structure

```
HomeDisplay/
├── raspi_ble_server/        # Python BLE GATT server (runs on Raspberry Pi)
│   ├── ble_server.py        # Main server: BLE setup, file monitor, scheduled tasks
│   ├── check_weather.py     # Apple WeatherKit integration
│   ├── check_calendar.py    # Google Calendar integration
│   ├── check_subway.py      # Train delay status
│   ├── check_news.py        # AllSides news scraper + Gemini AI summariser
│   ├── http_request_ssl.py  # SSL-aware HTTP helper
│   ├── requirements.txt     # Python dependencies
│   └── install.sh           # systemd service installer
└── esp32_ble_client/        # PlatformIO firmware (runs on ESP32-S3)
    ├── src/
    │   ├── main.ino             # Main loop, JSON parsing, display scheduling
    │   ├── ble_client_manager.* # NimBLE GATT client
    │   ├── display_render.*     # e-paper rendering logic
    │   └── weather_icon.*       # Weather condition icon bitmaps
    ├── boards/                  # Custom PlatformIO board definitions
    ├── font/                    # Embedded fonts for the display
    ├── icon/                    # Embedded icon bitmaps
    ├── shell/                   # For 3d printing shell of E-Ink display board
    └── platformio.ini           # PlatformIO project configuration
```

## Raspberry Pi BLE Server Setup

### Prerequisites

- Python 3.9+
- BlueZ (Bluetooth stack) with `--experimental` flag enabled (required for GATT server support)
- A Python virtual environment

### 1. Install Python dependencies

```bash
cd raspi_ble_server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 2. Configure API credentials

#### Apple WeatherKit

Create `raspi_ble_server/weatherkit_config.local.json` (kept out of git by default):

```json
{
  "team_id": "YOUR_TEAM_ID",
  "service_id": "com.yourapp.client",
  "key_id": "YOUR_KEY_ID",
  "private_key_path": "/absolute/path/to/AuthKey_YOUR_KEY_ID.pem"
}
```

#### Google Calendar

Run the following command once to authorise access. A `token.json` file will be created automatically on first use:

```bash
python3 check_calendar.py
```

Place your `credentials.json` (OAuth 2.0 client secret downloaded from Google Cloud Console) in the `raspi_ble_server/` directory.

#### Google Gemini / Vertex AI (optional, for news headlines)

Set the following environment variables for Vertex AI access:

| Variable | Description |
|----------|-------------|
| `GOOGLE_CLOUD_PROJECT` | Your Google Cloud project ID |
| `GOOGLE_APPLICATION_CREDENTIALS` | Path to your Application Default Credentials JSON file |

### 3. Install as a systemd service

Edit the configuration variables at the top of `install.sh` (`VENV_DIR`, `SERVICE_HOME`) to match your Raspberry Pi setup, then run:

```bash
sudo ./install.sh
```

This will:
- Configure BlueZ to start with the `--experimental` flag
- Create and enable the `homedisplay-ble.service` systemd unit

Check the service status:

```bash
systemctl status homedisplay-ble.service
journalctl -u homedisplay-ble.service -f
```

### 4. Sensor data source

Create `raspi_ble_server/server_config.local.json` (kept out of git by default):

```json
{
  "home_temp_json_path": "/absolute/path/to/sensor_latest.log",
  "service_uuid": "a25659a2-0de7-4f74-a149-94f47b218ba3",
  "characteristic_uuid": "a25659a2-0de7-4f74-a149-94f47b218ba4",
  "request_characteristic_uuid": "a25659a2-0de7-4f74-a149-94f47b218ba5"
}
```

The server watches that file for changes using inotify. The file is expected to be a JSON object with the following keys:

```json
{
  "temperature_c": 23.5,
  "humidity_percent": 52,
  "iaq": 50,
  "co2_eq_ppm": 600
}
```

## ESP32 BLE Client Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)

### Build and Flash

The default build target is `T5-ePaper-S3` (LilyGo T5 4.7″ S3). 

### Key Libraries

| Library | Purpose |
|---------|---------|
| [LilyGo-EPD47](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47) | e-paper display driver |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Lightweight BLE GATT client |
| [ArduinoJson](https://arduinojson.org/) | JSON parsing |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | Peripheral sensor support |

## BLE Protocol

The Raspberry Pi exposes a single GATT service with two characteristics: Notify and Request. Three UUIDs are used for this project. Ensure these UUIDs match on both the client and server.
Note: It is highly recommended to change these for your own implementation.

| Role | UUID |
|------|------|
| Service | `a25659a2-0de7-4f74-a149-94f47b218ba3` |
| Notify Characteristic | `a25659a2-0de7-4f74-a149-94f47b218ba4` |
| Request Characteristic | `a25659a2-0de7-4f74-a149-94f47b218ba5` |

The ESP32 subscribes to notifications on the **Notify Characteristic**. Each notification carries a JSON object for a single data type:

| Key | Description | Example |
|-----|-------------|---------|
| `sensor` | Indoor environment data | `{"temperature_c": 23.5, "humidity_percent": 52, "iaq": 50, "co2_eq_ppm": 600}` |
| `weather` | Weather data | `{"current": 18, "max": 21, "min": 14, "condition": "partly_cloudy_day"}` |
| `calendar` | 7-day schedule (Mon–Sun) | `{"2026-05-04": "Work", "2026-05-05": "OOO", ...}` |
| `alert` | Transit delay message | `"Train Delay Alert!"` or `""` |
| `info` | AI news headlines | `"Headline 1. Headline 2. Headline 3."` |
| `timestamp` | Unix timestamp for RTC sync | `1746345600` |

The ESP32 can write `"get"` to the **Request Characteristic** to request an immediate push of all cached payloads.

## Data Update Schedule

Data is queried based on the intervals below to reduce network traffic. Please change an interval that fits your usage.

| Data | Update Trigger |
|------|---------------|
| Sensor | On every change to the sensor JSON file (inotify) |
| Weather & Calendar | Every hour at **:50** past the hour |
| Transit alert | On every sensor file change during **07:00–08:59** |
| News headlines | At **05:50** and **17:50** daily |
| RTC sync (timestamp) | Daily at **03:00** |

Please note that this trigger is based on the sensor data file which is updated every minute.

## License

This project is for personal use. See individual library licences for third-party components.
