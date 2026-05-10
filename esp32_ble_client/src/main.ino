#include "epd_driver.h"
#include "../icon/alert_battery_32x32.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "ble_client_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <time.h>
#include "display_render.h"

uint8_t *framebuffer;
uint8_t *rotation_scratch;

// Display software/hardware wash
static char last_render_minute = -1;
byte display_update_counter = 0;

// Timestamp for periodic BLE activity
static const unsigned long BLE_PUSH_TIMEOUT_MS = 1000; // 1 second in ms
unsigned long last_push_timestamp = 0;

// Task tracker
static bool tracker_data = false;
static bool tracker_push = false;
static bool tracker_display = false;
static TaskHandle_t ble_task_handle = nullptr;

// Payload tracker
bool tracker_time = false;
bool tracker_schedule = false;
bool tracker_weather = false;
bool tracker_sensor = false;
bool tracker_info = false;

static void bleLoopTask(void *param) {
  (void)param;
  for (;;) {
    BleClientManager::loop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void setRtcFromUnixTimestamp(long unix_timestamp_utc) {
  struct timeval tv;
  tv.tv_sec = unix_timestamp_utc;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

// Calendar variables
byte calendar_status_cache[] = {0, 0, 0, 0, 0, 0, 0};

// Weather variables
WeatherConditionCode weather_condition_code = WC_CLEAR_DAY;
signed char weather_temp;
signed char weather_temp_min;
signed char weather_temp_max;

// Sensor variables
float sensor_temp = 0;
byte sensor_humidity = 0;
short sensor_co2 = 0;
short sensor_iaq = 0;

// String for information
String infoString = "";
String alertString = "";

// Returns: 0=Sunday, 1=Monday, ... 6=Saturday, -1 on parse/error
int weekday_from_ymd(const char* ymd) {
  int y = 0, m = 0, d = 0;
  if (sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3) {
    return -1;
  }

  struct tm t = {0};
  t.tm_year = y - 1900;
  t.tm_mon  = m - 1;
  t.tm_mday = d;
  t.tm_hour = 12;  // noon avoids DST edge cases around midnight

  if (mktime(&t) == (time_t)-1) {
    return -1;
  }

  return t.tm_wday;
}

void setup() {
  Serial.begin(115200);
  delay(3000); // Give USB CDC time to attach before we print boot messages
  Serial.println("Starting ESP32 BLE Client application...");

  // Configure RTC timezone so localtime_r returns JST.
  setenv("TZ", "JST-9", 1);
  tzset();

  // Initialize display and framebuffer for LilyGo T5-4.7 S3 driver
  epd_init();
  framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  rotation_scratch =
      (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);

  BleClientManager::begin();
  // Start BLE loop task for handling BLE connection and notifications in the background
  xTaskCreatePinnedToCore(
      bleLoopTask,
      "ble_loop",
      4096,
      nullptr,
      1,
      &ble_task_handle,
      tskNO_AFFINITY);

  // Initial Display Render
  renderDisplay();
}

void processReceivedData(String jsonData) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Update information from the received JSON data
  // timestamp parsing and formatting
  if (doc.containsKey("timestamp")) {
    // Update ESP32 RTC from Unix timestamp (UTC).
    setRtcFromUnixTimestamp(doc["timestamp"].as<time_t>() + 2); // Add 2 seconds to compensate for E-ink update delay and ensure timely display update
    tracker_time = true;
  }
  // Schedule/Calendar parsing
  if (doc.containsKey("calendar")) {
    // Clear calendar status cache before updating with new data
    for (int i = 0; i < 7; i++) {
      calendar_status_cache[i] = 0;
    }

    // Parse calendar data and update cache
    JsonObject calendarObj = doc["calendar"].as<JsonObject>();
    for (JsonPair kv : calendarObj) {
      const char *date = kv.key().c_str();
      const char *status = kv.value().as<const char *>();
      if (date == nullptr || status == nullptr) {
        continue;
      }
      // Determine the weekday for this date and update the corresponding cache entry
      const int wd = weekday_from_ymd(date);
      if (wd < 0 || wd >= 7) {
        continue;
      }
      // Status: "OOO" = out of office, "Work" = working day
      if (strcmp(status, "OOO") == 0) {
        calendar_status_cache[wd] = 1;
      } else if (strcmp(status, "Work") == 0) {
        calendar_status_cache[wd] = 2;
      }
    }

    tracker_schedule = true;
  }
  // Weather information parsing
  if (doc.containsKey("weather")) {
    weather_condition_code = weatherConditionToCode(doc["weather"]["condition"].as<String>());
    weather_temp = doc["weather"]["current"].as<signed char>();
    weather_temp_max = doc["weather"]["max"].as<signed char>();
    weather_temp_min = doc["weather"]["min"].as<signed char>();
    tracker_weather = true;
  }

  // Sensor information parsing
  if (doc.containsKey("sensor")) {
    sensor_temp = doc["sensor"]["temperature_c"].as<float>();
    sensor_humidity = doc["sensor"]["humidity_percent"].as<byte>();
    sensor_iaq = doc["sensor"]["iaq"].as<short>();
    sensor_co2 = doc["sensor"]["co2_eq_ppm"].as<short>();
    tracker_sensor = true;
  }
  
  // Info/Alert string parsing
  if (doc.containsKey("info")) {
    infoString = doc["info"].as<String>();
    tracker_info = true;
  }

  // Alert should be erased if not present in the received data
  // Meaning alert should be sent last for this reason
  if (doc.containsKey("alert")) {
    alertString = doc["alert"].as<String>();
  } else {
    alertString = "";
  }
}

void loop() {
  // Periodically subscribe to BLE notifications if connected, or start advertising if not connected
  if (!tracker_data) {
    // Process any received BLE payloads (e.g. notification pushes from server)
    String blePayload;
    while (BleClientManager::consumePayload(blePayload)) {
      Serial.println("Received BLE payload:");
      Serial.println(blePayload);
      processReceivedData(blePayload);
      last_push_timestamp = millis();
      tracker_push = true;
    }

    // Timeout if it has been too long not receiving push notifications
    if (tracker_push && ((millis() - last_push_timestamp >= BLE_PUSH_TIMEOUT_MS))) {
      tracker_data = true;
      tracker_push = false;
    }
  }

  // Periodic activity tracker for display refresh
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Periodically re-render display every minute to update time and ensure display freshness
  if ((last_render_minute != -1 && timeinfo.tm_min != last_render_minute) || (last_render_minute == -1 && tracker_data == true)) {
    renderDisplay();
    last_render_minute = timeinfo.tm_min;
    tracker_display = true;

    // If ble still not connected after display update, consider data stale and reset trackers to trigger a new connection attempt and data fetch
    if (!BleClientManager::isConnected()) {
      tracker_data = true;
    }
  }

  // If every task is done, deep sleep until the next periodic check to save power
  if (tracker_data && tracker_display) {
    Serial.println("Entering light sleep until next periodic check...");
    vTaskSuspend(ble_task_handle);  // Wait for BLE task to reach a scheduling point before deinit.
    BleClientManager::deinit(); // idle NimBLE (stop scan + disconnect) before sleep
    // Target: second 54 of the current minute
    localtime_r(&now, &timeinfo);
    timeinfo.tm_sec = 54;
    timeinfo.tm_isdst = -1;
    time_t target_wakeup_time = mktime(&timeinfo);
    tracker_data = false;
    tracker_push = false;
    tracker_display = false;
    int sleep_duration_s = (int)(target_wakeup_time - now);
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_duration_s * 1000000ULL);
    esp_light_sleep_start();
    Serial.printf("Simulating light sleep for %d seconds until next periodic check...\n", sleep_duration_s);
    BleClientManager::resumeScan();
    vTaskResume(ble_task_handle);
  } 
  // If is not subscribed once and not connected, continue scanning to reconnect.
  else if (BleClientManager::isScanning() == false && BleClientManager::isConnected() == false) {
    BleClientManager::resumeScan();
  }
}
