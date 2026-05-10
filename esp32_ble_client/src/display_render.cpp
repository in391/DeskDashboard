#include "display_render.h"
#include "../font/googlesansflex_9pt_regular.h"
#include "../font/googlesansflex_12pt_regular_reduced.h"
#include "../font/googlesansflex_18pt_regular_reduced.h"
#include "../font/googlesansflex_24pt_regular_reduced.h"
#include "../font/googlesansflex_45pt_light_reduced.h"
#include "../icon/schedule_home_48x48.h"
#include "../icon/schedule_work_48x48.h"
#include "../icon/bluetooth_disabled.h"

// ---------------------------------------------------------------------------
// Low-level pixel helpers
// ---------------------------------------------------------------------------

static inline uint8_t getPixel4bpp(const uint8_t *fb, int x, int y) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
    return 15;
  }
  const uint8_t v = fb[y * (EPD_WIDTH / 2) + (x / 2)];
  return (x & 1) ? ((v >> 4) & 0x0F) : (v & 0x0F);
}

static inline void setPixel4bpp(uint8_t *fb, int x, int y, uint8_t color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
    return;
  }
  uint8_t *v = &fb[y * (EPD_WIDTH / 2) + (x / 2)];
  const uint8_t c = color & 0x0F;
  if (x & 1) {
    *v = (uint8_t)((*v & 0x0F) | (c << 4));
  } else {
    *v = (uint8_t)((*v & 0xF0) | c);
  }
}

static inline bool mapPortraitToPanel(int logical_x, int logical_y, int &panel_x,
                                      int &panel_y) {
  if (logical_x < 0 || logical_x >= PORTRAIT_LOGICAL_WIDTH || logical_y < 0 ||
      logical_y >= PORTRAIT_LOGICAL_HEIGHT) {
    return false;
  }

  panel_x = logical_y;
  panel_y = EPD_HEIGHT - 1 - logical_x;
  return true;
}

// ---------------------------------------------------------------------------
// Text / bitmap drawing helpers
// ---------------------------------------------------------------------------

static void drawTextMapped(GFXfont *font, const char *text, int logical_x,
                           int logical_y, uint8_t color) {
  if (font == nullptr) {
    return;
  }

  if (!DISPLAY_PORTRAIT_MODE) {
    int cursor_x = logical_x;
    int cursor_y = logical_y;
    writeln(font, text, &cursor_x, &cursor_y, framebuffer);
    return;
  }

  if (rotation_scratch == nullptr) {
    return;
  }

  memset(rotation_scratch, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  // Render text in normal orientation onto scratch.
  // We copy all non-white pixels to avoid glyph clipping from bounds rounding.
  int32_t render_cursor_x = 0;
  int32_t render_cursor_y = 96;
  write_mode(font, text, &render_cursor_x, &render_cursor_y, rotation_scratch,
             BLACK_ON_WHITE, nullptr);

  for (int sy = 0; sy < EPD_HEIGHT; sy++) {
    for (int sx = 0; sx < EPD_WIDTH; sx++) {
      const uint8_t c = getPixel4bpp(rotation_scratch, sx, sy);
      if (c >= 15) {
        continue;
      }

      const int lx = logical_x + sx;
      const int ly = logical_y + sy;
      int px = 0;
      int py = 0;
      if (mapPortraitToPanel(lx, ly, px, py)) {
        setPixel4bpp(framebuffer, px, py, color);
      }
    }
  }
}

static void drawTextMappedCentered(GFXfont *font, const char *text,
                                   int logical_y, uint8_t color) {
  if (font == nullptr || text == nullptr) {
    return;
  }

  int32_t cursor_x = 0;
  int32_t cursor_y = 96;
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t w = 0;
  int32_t h = 0;
  get_text_bounds(font, text, &cursor_x, &cursor_y, &x1, &y1, &w, &h,
                  nullptr);

  const int logical_x = (PORTRAIT_LOGICAL_WIDTH - w) / 2 - x1;
  drawTextMapped(font, text, logical_x, logical_y, color);
}

static void drawStringMapped(GFXfont *font, String text, int logical_x, int logical_y,
                             uint8_t color) {
  if (font == nullptr || text == nullptr) {
    return;
  }
  int32_t stringWidth = PORTRAIT_LOGICAL_WIDTH - logical_x * 2;
  int32_t cursor_x = 0;
  int32_t cursor_y = 96;
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t w = 0;
  int32_t h = 0;
  float stringRatio = 0;
  int32_t split_index = 0;
  for (int i = 0; i < 5; i++) {
    get_text_bounds(font, text.c_str(), &cursor_x, &cursor_y, &x1, &y1, &w, &h,
                    nullptr);
    if (w == 0) {
      break;
    } else if (w <= stringWidth) {
      drawTextMapped(font, text.c_str(), logical_x, logical_y + 50 * i, color);
      break;
    } else {
      stringRatio = (float)stringWidth / (float)w;
      split_index = round(text.length() * stringRatio);
      for (int j = 0; j >= -5 && split_index + j > 0; j--) {
        if (text.charAt(split_index + j) == ' ') {
          split_index = split_index + j;
          break;
        }
      }
      drawTextMapped(font, text.substring(0, split_index).c_str(), logical_x, logical_y + 50 * i, color);
      text = text.substring(split_index + (text.charAt(split_index) == ' ' ? 1 : 0));
    }
  }
}

static void drawTextMappedRightAligned(GFXfont *font, const char *text,
                                       int logical_y, int right_margin,
                                       uint8_t color) {
  if (font == nullptr || text == nullptr) {
    return;
  }

  int32_t cursor_x = 0;
  int32_t cursor_y = 96;
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t w = 0;
  int32_t h = 0;
  get_text_bounds(font, text, &cursor_x, &cursor_y, &x1, &y1, &w, &h,
                  nullptr);

  const int margin = (right_margin < 0) ? 0 : right_margin;
  const int logical_x = PORTRAIT_LOGICAL_WIDTH - margin - w - x1;
  drawTextMapped(font, text, logical_x, logical_y, color);
}

static void drawBitmap1bppMapped(const uint8_t *bits, int width, int height,
                                 int row_bytes, int logical_x,
                                 int logical_y, uint8_t color) {
  if (bits == nullptr || width <= 0 || height <= 0 || row_bytes <= 0) {
    return;
  }

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const int byte_index = y * row_bytes + (x / 8);
      const uint8_t v = pgm_read_byte(&bits[byte_index]);
      const bool is_black = ((v >> (7 - (x % 8))) & 0x01) != 0;
      if (!is_black) {
        continue;
      }

      int panel_x = 0;
      int panel_y = 0;
      if (mapPortraitToPanel(logical_x + x, logical_y + y, panel_x, panel_y)) {
        setPixel4bpp(framebuffer, panel_x, panel_y, color);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// renderDisplay
// ---------------------------------------------------------------------------

void renderDisplay() {
  //---------------------
  // 1. Form a pure white background in memory
  //---------------------
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  //---------------------
  // 2. Variable declarations and preparations
  //---------------------
  bool isFresh = last_push_timestamp != 0; //&& (millis()- last_push_timestamp < 600000); // Consider data stale after 10 minutes

  // Time Variables
  time_t now = time(nullptr) + 1;// Add 1 second to ensure we roll over to the next minute for timely display update, especially when called at :59
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char timeStringBuff[64] = "--:-- --";
  char dateStringBuff[64] = "-- --";
  strftime(timeStringBuff, sizeof(timeStringBuff), "%I:%M %p", &timeinfo);
  strftime(dateStringBuff, sizeof(dateStringBuff), "%B %d", &timeinfo);

  // Calendar variables
  int current_weekday = timeinfo.tm_wday; // Sunday=0, Monday=1, ..., Saturday=6
  int calendar_colors[] = {0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C}; // Default color for all days
  int calendar_status[] = {0, 0, 0, 0, 0, 0, 0}; // Default no schedule
  int calendar_status_colors[] = {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}; // Default color for schedule icon

  // If data is fresh
  if (isFresh) {
    // Update calendar status
    for (int i = 0; i < 7; i++) {
      calendar_status[i] = calendar_status_cache[i];
    }
  }

  // Highlight current day
  if (current_weekday >= 0 && current_weekday < 7) {
    calendar_colors[current_weekday] = 0x00;
    calendar_status_colors[current_weekday] = 0x00;
  }

  // Weather variables
  const uint8_t *weather_bits = nullptr;
  int weather_width = 0;
  int weather_height = 0;
  int weather_row_bytes = 0;
  selectWeatherIcon(weather_condition_code, weather_bits, weather_width,
                      weather_height, weather_row_bytes);
  char tempCurBuff[16] = "--°";
  char tempMaxBuff[16] = "--°";
  char tempMinBuff[16] = "--°";
  if (isFresh) {
    snprintf(tempCurBuff, sizeof(tempCurBuff), "%d°", weather_temp);
    snprintf(tempMaxBuff, sizeof(tempMaxBuff), "%d°", weather_temp_max);
    snprintf(tempMinBuff, sizeof(tempMinBuff), "%d°", weather_temp_min);
  }

  // Sensor variables
  char tempBuff[16] = "--°";
  char humBuff[16] = "--%";
  char iaqBuff[16] = "--";
  char co2Buff[16] = "-- ppm";

  if (isFresh) {
    if ((int)(sensor_temp * 10) % 10 == 0) {
      snprintf(tempBuff, sizeof(tempBuff), "%d°", (int)sensor_temp);
    } else {
      snprintf(tempBuff, sizeof(tempBuff), "%.1f°", sensor_temp);
    }
    snprintf(humBuff, sizeof(humBuff), "%d%%", sensor_humidity);
    snprintf(iaqBuff, sizeof(iaqBuff), "%d", sensor_iaq);
    snprintf(co2Buff, sizeof(co2Buff), "%d ppm", sensor_co2);
  }

  // Info/Alert variables
  char infoBuff[128] = "Connecting...";
  char alertBuff[128] = "";
  if (isFresh) {
    strncpy(infoBuff, infoString.c_str(), sizeof(infoBuff) - 1);
    strncpy(alertBuff, alertString.c_str(), sizeof(alertBuff) - 1);
    infoBuff[sizeof(infoBuff) - 1] = '\0'; // Ensure null-termination
    alertBuff[sizeof(alertBuff) - 1] = '\0'; // Ensure null-termination
  }

  //---------------------
  // 3. Draw each block
  //---------------------
  int logical_x = 28;
  int logical_y = 47;

  // Date/Time Block
  drawTextMappedCentered((GFXfont *)&GoogleSansFlex_45pt_Light,
                timeStringBuff, logical_y, 0x00);

  logical_y += 50;
  drawTextMappedCentered((GFXfont *)&GoogleSansFlex_18pt_Regular,
                dateStringBuff, logical_y, 0x08);

  // Weekday Block
  logical_y += 40;
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "S", 0*68+54, logical_y+28, calendar_colors[0]);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "M", 1*68+48, logical_y+28, calendar_colors[1]);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "T", 2*68+54, logical_y+28, calendar_colors[2]);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "W", 3*68+48, logical_y+28, calendar_colors[3]);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "T", 4*68+54, logical_y+28, calendar_colors[4]);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "F", 5*68+54, logical_y+28, calendar_colors[5]);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "S", 6*68+54, logical_y+28, calendar_colors[6]);
  // Draw schedule icon
  for (int i = 0; i < 7; i++) {
    if (calendar_status[i] == 0) {
      continue; // No schedule, skip icon
    } else if (calendar_status[i] == 1) {
      // OOO status
      drawBitmap1bppMapped(SCHEDULE_HOME_48X48_BITS,
              SCHEDULE_HOME_48X48_WIDTH,
              SCHEDULE_HOME_48X48_HEIGHT,
              SCHEDULE_HOME_48X48_ROW_BYTES, i*68+40, logical_y+140, calendar_status_colors[i]);
    } else if (calendar_status[i] == 2) {
      // Work status
      drawBitmap1bppMapped(SCHEDULE_WORK_48X48_BITS,
              SCHEDULE_WORK_48X48_WIDTH,
              SCHEDULE_WORK_48X48_HEIGHT,
              SCHEDULE_WORK_48X48_ROW_BYTES, i*68+40, logical_y+140, calendar_status_colors[i]);
    }
  }

  // Weather Block
  logical_y += 190;
  
  drawBitmap1bppMapped(weather_bits,
              weather_width,
              weather_height,
              weather_row_bytes, 60, 330, 0x00);
  drawTextMappedRightAligned((GFXfont *)&GoogleSansFlex_45pt_Light, tempCurBuff, 327, 200, 0x00);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "H", 380,
        logical_y - 40, 0x0B);
  drawTextMappedRightAligned((GFXfont *)&GoogleSansFlex_18pt_Regular, tempMaxBuff, 287, 60, 0x08);
  drawTextMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, "L", 380,
        logical_y + 0, 0x0B);
  drawTextMappedRightAligned((GFXfont *)&GoogleSansFlex_18pt_Regular, tempMinBuff, 327, 60, 0x08);

  // Sensor Block
  logical_y += 80;
  drawTextMapped((GFXfont *)&GoogleSansFlex_12pt_Regular, "Temp", 80, logical_y-20, 0x0D);
  drawTextMapped((GFXfont *)&GoogleSansFlex_24pt_Regular, tempBuff, 80, logical_y+28, 0x00);
  drawTextMapped((GFXfont *)&GoogleSansFlex_12pt_Regular, "IAQ", 80+180, logical_y-20, 0x0D);
  drawTextMapped((GFXfont *)&GoogleSansFlex_24pt_Regular, iaqBuff, 80+180, logical_y+28, 0x00);

  // IAQ Indicator
  for (int i = 0; i < 5; i++) {
    if ((i+1)*100 <= sensor_iaq) {
      epd_fill_rect(logical_y+28+60, 165 - i*25, 40, 20, 0x00, framebuffer);
    } else {
      epd_draw_rect(logical_y+28+60, 165 - i*25, 40, 20, 0xA0, framebuffer);
    }
  }

  logical_y += 80;
  drawTextMapped((GFXfont *)&GoogleSansFlex_12pt_Regular, "Humid", 80, logical_y-20, 0x0D);
  drawTextMapped((GFXfont *)&GoogleSansFlex_24pt_Regular, humBuff, 80, logical_y+28, 0x00);
  drawTextMapped((GFXfont *)&GoogleSansFlex_12pt_Regular, "CO²", 80+180, logical_y-20, 0x0D);
  drawTextMapped((GFXfont *)&GoogleSansFlex_24pt_Regular, co2Buff, 80+180, logical_y+28, 0x00);

  // News Block
  logical_y += 100;
  drawStringMapped((GFXfont *)&GoogleSansFlex_18pt_Regular, infoBuff, 80, logical_y, 0x00);

  // Alert Block
  drawTextMappedCentered((GFXfont *)&GoogleSansFlex_18pt_Regular, alertBuff, -48, 0x00);

  // Connection Status Block
  if (millis() - last_push_timestamp > 90000) { // If last push is more than 1 minutes 30 seconds ago, consider connection lost and show Bluetooth disabled icon
    drawBitmap1bppMapped(BLUETOOTH_DISABLED_32X32_BITS,
              BLUETOOTH_DISABLED_32X32_WIDTH,
              BLUETOOTH_DISABLED_32X32_HEIGHT,
              BLUETOOTH_DISABLED_32X32_ROW_BYTES, 490, 20, 0x09);
  }

  //---------------------
  // 4. Push buffer to physical display
  //---------------------
  epd_poweron();
  // Run a full clean periodically; partial-only updates can show stripe artifacts.
  if (display_update_counter % DISPLAY_FULL_CLEAR_INTERVAL == 0) {
    epd_clear();
    display_update_counter = 0;
  } else {
    epd_clear_area_cycles(epd_full_screen(), DISPLAY_PARTIAL_CLEAR_CYCLES,
                          DISPLAY_PARTIAL_CLEAR_DURATION);
  }
  display_update_counter++;
  epd_draw_grayscale_image(epd_full_screen(), framebuffer);

  //---------------------
  // 5. Power off entirely (POWER_EN low) to prevent damage and save battery during sleep
  //---------------------
  epd_poweroff_all();
}
