#pragma once

#include <Arduino.h>
#include "epd_driver.h"
#include "weather_icon.h"

// Display configuration
static const bool DISPLAY_PORTRAIT_MODE = true;
static const int PORTRAIT_LOGICAL_WIDTH = EPD_HEIGHT;
static const int PORTRAIT_LOGICAL_HEIGHT = EPD_WIDTH;
static const int DISPLAY_FULL_CLEAR_INTERVAL = 30;
static const int DISPLAY_PARTIAL_CLEAR_CYCLES = 1;
static const int DISPLAY_PARTIAL_CLEAR_DURATION = 50;

// Shared buffers
extern uint8_t *framebuffer;
extern uint8_t *rotation_scratch;

// Shared state
extern unsigned long last_push_timestamp;

// Calendar variables
extern byte calendar_status_cache[7];

// Weather variables
extern WeatherConditionCode weather_condition_code;
extern signed char weather_temp;
extern signed char weather_temp_min;
extern signed char weather_temp_max;

// Sensor variables
extern float sensor_temp;
extern byte sensor_humidity;
extern short sensor_co2;
extern short sensor_iaq;

// Info strings
extern String infoString;
extern String alertString;

// Display update counter
extern byte display_update_counter;

void renderDisplay();
