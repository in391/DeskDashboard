#include "weather_icon.h"
#include "../icon/weather/air.h"
#include "../icon/weather/clear_day.h"
#include "../icon/weather/cloud.h"
#include "../icon/weather/foggy.h"
#include "../icon/weather/heat.h"
#include "../icon/weather/partly_cloudy_day.h"
#include "../icon/weather/rainy.h"
#include "../icon/weather/rainy_heavy.h"
#include "../icon/weather/rainy_light.h"
#include "../icon/weather/rainy_snow.h"
#include "../icon/weather/severe_cold.h"
#include "../icon/weather/snowing_heavy.h"
#include "../icon/weather/sunny_snowing.h"
#include "../icon/weather/thunderstorm.h"
#include "../icon/weather/weather_hail.h"
#include "../icon/weather/weather_snowy.h"

// ---------------------------------------------------------------------------
// String → code lookup table
// ---------------------------------------------------------------------------
struct WeatherConditionEntry {
  const char *name;
  WeatherConditionCode code;
};

static const WeatherConditionEntry kWeatherConditionMap[] = {
  { "air",               WC_AIR           },
  { "windy",             WC_AIR           },
  { "cloud",             WC_CLOUD         },
  { "cloudy",            WC_CLOUD         },
  { "partlycloudy",      WC_PARTLY_CLOUDY },
  { "partly_cloudy",     WC_PARTLY_CLOUDY },
  { "partly_cloudy_day", WC_PARTLY_CLOUDY },
  { "fog",               WC_FOG           },
  { "foggy",             WC_FOG           },
  { "mist",              WC_FOG           },
  { "rain",              WC_RAIN          },
  { "rainy",             WC_RAIN          },
  { "rainylight",        WC_RAIN_LIGHT    },
  { "rain_light",        WC_RAIN_LIGHT    },
  { "drizzle",           WC_RAIN_LIGHT    },
  { "rainyheavy",        WC_RAIN_HEAVY    },
  { "rain_heavy",        WC_RAIN_HEAVY    },
  { "rainysnow",         WC_RAIN_SNOW     },
  { "rain_snow",         WC_RAIN_SNOW     },
  { "sleet",             WC_RAIN_SNOW     },
  { "thunder",           WC_THUNDERSTORM  },
  { "thunderstorm",      WC_THUNDERSTORM  },
  { "snow",              WC_SNOW          },
  { "snowy",             WC_SNOW          },
  { "snowingheavy",      WC_SNOW_HEAVY    },
  { "snow_heavy",        WC_SNOW_HEAVY    },
  { "sunnysnowing",      WC_SUNNY_SNOWING },
  { "sunny_snowing",     WC_SUNNY_SNOWING },
  { "hail",              WC_HAIL          },
  { "severecold",        WC_SEVERE_COLD   },
  { "severe_cold",       WC_SEVERE_COLD   },
  { "heat",              WC_HEAT          },
  { "hot",               WC_HEAT          },
  { "clear_day",         WC_CLEAR_DAY     },
  { "clear",             WC_CLEAR_DAY     },
  { "sunny",             WC_CLEAR_DAY     },
};

static const int kWeatherConditionMapSize =
    (int)(sizeof(kWeatherConditionMap) / sizeof(kWeatherConditionMap[0]));

WeatherConditionCode weatherConditionToCode(const String &condition) {
  String c = condition;
  c.toLowerCase();
  for (int i = 0; i < kWeatherConditionMapSize; i++) {
    if (c == kWeatherConditionMap[i].name) {
      return kWeatherConditionMap[i].code;
    }
  }
  return WC_UNKNOWN;
}

// ---------------------------------------------------------------------------
// Code → icon data
// ---------------------------------------------------------------------------
void selectWeatherIcon(WeatherConditionCode code,
                       const uint8_t *&bits,
                       int &width,
                       int &height,
                       int &row_bytes) {
  switch (code) {
    case WC_AIR:
      bits = WG_AIR_BITS; width = WG_AIR_WIDTH; height = WG_AIR_HEIGHT; row_bytes = WG_AIR_ROW_BYTES;
      return;
    case WC_CLOUD:
      bits = WG_CLOUD_BITS; width = WG_CLOUD_WIDTH; height = WG_CLOUD_HEIGHT; row_bytes = WG_CLOUD_ROW_BYTES;
      return;
    case WC_PARTLY_CLOUDY:
      bits = WG_PARTLY_CLOUDY_DAY_BITS; width = WG_PARTLY_CLOUDY_DAY_WIDTH; height = WG_PARTLY_CLOUDY_DAY_HEIGHT; row_bytes = WG_PARTLY_CLOUDY_DAY_ROW_BYTES;
      return;
    case WC_FOG:
      bits = WG_FOGGY_BITS; width = WG_FOGGY_WIDTH; height = WG_FOGGY_HEIGHT; row_bytes = WG_FOGGY_ROW_BYTES;
      return;
    case WC_RAIN:
      bits = WG_RAINY_BITS; width = WG_RAINY_WIDTH; height = WG_RAINY_HEIGHT; row_bytes = WG_RAINY_ROW_BYTES;
      return;
    case WC_RAIN_LIGHT:
      bits = WG_RAINY_LIGHT_BITS; width = WG_RAINY_LIGHT_WIDTH; height = WG_RAINY_LIGHT_HEIGHT; row_bytes = WG_RAINY_LIGHT_ROW_BYTES;
      return;
    case WC_RAIN_HEAVY:
      bits = WG_RAINY_HEAVY_BITS; width = WG_RAINY_HEAVY_WIDTH; height = WG_RAINY_HEAVY_HEIGHT; row_bytes = WG_RAINY_HEAVY_ROW_BYTES;
      return;
    case WC_RAIN_SNOW:
      bits = WG_RAINY_SNOW_BITS; width = WG_RAINY_SNOW_WIDTH; height = WG_RAINY_SNOW_HEIGHT; row_bytes = WG_RAINY_SNOW_ROW_BYTES;
      return;
    case WC_THUNDERSTORM:
      bits = WG_THUNDERSTORM_BITS; width = WG_THUNDERSTORM_WIDTH; height = WG_THUNDERSTORM_HEIGHT; row_bytes = WG_THUNDERSTORM_ROW_BYTES;
      return;
    case WC_SNOW:
      bits = WG_WEATHER_SNOWY_BITS; width = WG_WEATHER_SNOWY_WIDTH; height = WG_WEATHER_SNOWY_HEIGHT; row_bytes = WG_WEATHER_SNOWY_ROW_BYTES;
      return;
    case WC_SNOW_HEAVY:
      bits = WG_SNOWING_HEAVY_BITS; width = WG_SNOWING_HEAVY_WIDTH; height = WG_SNOWING_HEAVY_HEIGHT; row_bytes = WG_SNOWING_HEAVY_ROW_BYTES;
      return;
    case WC_SUNNY_SNOWING:
      bits = WG_SUNNY_SNOWING_BITS; width = WG_SUNNY_SNOWING_WIDTH; height = WG_SUNNY_SNOWING_HEIGHT; row_bytes = WG_SUNNY_SNOWING_ROW_BYTES;
      return;
    case WC_HAIL:
      bits = WG_WEATHER_HAIL_BITS; width = WG_WEATHER_HAIL_WIDTH; height = WG_WEATHER_HAIL_HEIGHT; row_bytes = WG_WEATHER_HAIL_ROW_BYTES;
      return;
    case WC_SEVERE_COLD:
      bits = WG_SEVERE_COLD_BITS; width = WG_SEVERE_COLD_WIDTH; height = WG_SEVERE_COLD_HEIGHT; row_bytes = WG_SEVERE_COLD_ROW_BYTES;
      return;
    case WC_HEAT:
      bits = WG_HEAT_BITS; width = WG_HEAT_WIDTH; height = WG_HEAT_HEIGHT; row_bytes = WG_HEAT_ROW_BYTES;
      return;
    default: // WC_CLEAR_DAY and WC_UNKNOWN both fall through to the default
      bits = WG_CLEAR_DAY_BITS; width = WG_CLEAR_DAY_WIDTH; height = WG_CLEAR_DAY_HEIGHT; row_bytes = WG_CLEAR_DAY_ROW_BYTES;
      return;
  }
}

void selectWeatherIcon(const String &condition,
                       const uint8_t *&bits,
                       int &width,
                       int &height,
                       int &row_bytes) {
  selectWeatherIcon(weatherConditionToCode(condition), bits, width, height, row_bytes);
}
