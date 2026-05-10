#pragma once

#include <Arduino.h>

namespace BleClientManager {

void begin();
void deinit();
void loop();
void resumeScan();
bool isConnected();
bool isScanning();
bool consumePayload(String &payload);
bool requestData();
void disconnect();

}  // namespace BleClientManager

// Trackers for main loop activity and display freshness
extern bool tracker_time;
extern bool tracker_schedule;
extern bool tracker_weather;
extern bool tracker_sensor;
extern bool tracker_info;