#pragma once
#include <Arduino.h>

// Numeric codes for each weather condition
enum WeatherConditionCode {
  WC_CLEAR_DAY     = 0,
  WC_AIR           = 1,
  WC_CLOUD         = 2,
  WC_PARTLY_CLOUDY = 3,
  WC_FOG           = 4,
  WC_RAIN          = 5,
  WC_RAIN_LIGHT    = 6,
  WC_RAIN_HEAVY    = 7,
  WC_RAIN_SNOW     = 8,
  WC_THUNDERSTORM  = 9,
  WC_SNOW          = 10,
  WC_SNOW_HEAVY    = 11,
  WC_SUNNY_SNOWING = 12,
  WC_HAIL          = 13,
  WC_SEVERE_COLD   = 14,
  WC_HEAT          = 15,
  WC_UNKNOWN       = -1,
};

// Convert a weather condition string (any case) to a WeatherConditionCode.
WeatherConditionCode weatherConditionToCode(const String &condition);

// Populate bits/width/height/row_bytes for the given condition code.
void selectWeatherIcon(WeatherConditionCode code,
                       const uint8_t *&bits,
                       int &width,
                       int &height,
                       int &row_bytes);

// Convenience overload: accepts a raw string and converts internally.
void selectWeatherIcon(const String &condition,
                       const uint8_t *&bits,
                       int &width,
                       int &height,
                       int &row_bytes);
