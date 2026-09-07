#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/time.h>

#include "esp_err.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "cards_generated.h"
#include "card_art_generated.h"
#include "lvgl.h"

namespace {

constexpr char TAG[] = "TarotBotWatch";
constexpr char NVS_APP[] = "tarotbot";
constexpr char NVS_WIFI[] = "wifi";
constexpr char WIFI_SSID_KEY[] = "ssid";
constexpr char WIFI_PASS_KEY[] = "pass";
constexpr char INTERPRETATIONS_KEY[] = "interpretations_enabled";
constexpr gpio_num_t BOOT_BUTTON = GPIO_NUM_0;
constexpr uint32_t IDLE_SAVER_MS = 60000;
constexpr uint32_t LONG_PRESS_MS = 1500;
constexpr uint32_t TOUCH_BLOCK_MS = 650;
constexpr int ACTIVE_BRIGHTNESS = 35;
constexpr int SAVER_BRIGHTNESS = 12;
constexpr uint8_t DECK_BYTES = (CARD_COUNT + 7) / 8;
constexpr uint8_t RTC_ADDR = 0x51;
constexpr int WATCH_W = 410;
constexpr int WATCH_H = 502;
constexpr int DIM_SEGMENTS = 7;

const lv_color_t MATRIX = lv_color_hex(0x00ff41);
const lv_color_t MATRIX_SOFT = lv_color_hex(0x35a953);
const lv_color_t MATRIX_DIM = lv_color_hex(0x0a3d18);
const lv_color_t MATRIX_CLOCK = lv_color_hex(0x00b43a);
const lv_color_t BLACK = lv_color_hex(0x000000);
const lv_color_t PANEL = lv_color_hex(0x031107);

enum class ScreenState { Welcome, CardBack, Revealed, Reading, Empty };
enum class AppView { Tarot, Tools, Settings };
enum class SettingField { Hour, Minute, AmPm, Month, Day, Year };
enum class TimeState { Unsynced, Rtc, Ntp };

struct RtcDateTime {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  bool valid = false;
};

struct Ui {
  lv_obj_t *root = nullptr;
  lv_obj_t *time = nullptr;
  lv_obj_t *status = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *subtitle = nullptr;
  lv_obj_t *panel = nullptr;
  lv_obj_t *card_name = nullptr;
  lv_obj_t *sigil_box = nullptr;
  lv_obj_t *sigil_top = nullptr;
  lv_obj_t *sigil_main = nullptr;
  lv_obj_t *sigil_bottom = nullptr;
  lv_obj_t *card_art = nullptr;
  lv_obj_t *body = nullptr;
  lv_obj_t *footer = nullptr;
  lv_obj_t *touch_layer = nullptr;
  std::array<lv_obj_t *, 28> rain{};
  std::array<std::array<lv_obj_t *, DIM_SEGMENTS>, 4> dim_digits{};
};

Ui ui;
ScreenState screen_state = ScreenState::Welcome;
AppView app_view = AppView::Tarot;
TimeState time_state = TimeState::Unsynced;
SettingField setting_field = SettingField::Hour;
std::array<uint8_t, DECK_BYTES> available_cards{};
int current_card = -1;
RtcDateTime manual_time {};
size_t reading_offset = 0;
size_t reading_next_offset = 0;
bool reading_has_more = false;
bool interpretations_enabled = true;
bool saver_active = false;
bool wake_only = false;
uint64_t last_activity_ms = 0;
uint64_t touch_block_until_ms = 0;
uint64_t button_down_ms = 0;
bool button_was_down = false;
bool button_long_handled = false;
bool touch_long_handled = false;
bool touch_press_valid = false;
lv_point_t touch_press_point {};
uint64_t touch_press_ms = 0;
bool wifi_connected = false;
bool setup_portal_running = false;
bool sntp_started = false;
EventGroupHandle_t wifi_events = nullptr;
i2c_master_dev_handle_t rtc_dev = nullptr;
uint16_t *card_art_pixels = nullptr;
lv_image_dsc_t card_art_img {};

constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
constexpr EventBits_t WIFI_FAILED_BIT = BIT1;

uint8_t bcd_to_dec(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0f);
}

uint8_t dec_to_bcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

uint64_t now_ms() {
  return esp_timer_get_time() / 1000ULL;
}

time_t utc_time_from_tm(struct tm *tm);
std::string dim_time_text(bool hour);

bool nvs_get_string(const char *ns, const char *key, std::string &out) {
  nvs_handle_t handle = 0;
  if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t len = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &len);
  if (err == ESP_OK && len > 1) {
    out.resize(len);
    err = nvs_get_str(handle, key, out.data(), &len);
    if (err == ESP_OK) {
      out.resize(strlen(out.c_str()));
    }
  }
  nvs_close(handle);
  return err == ESP_OK && !out.empty();
}

void nvs_set_string(const char *ns, const char *key, const std::string &value) {
  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open(ns, NVS_READWRITE, &handle));
  ESP_ERROR_CHECK(nvs_set_str(handle, key, value.c_str()));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);
}

bool nvs_get_bool(const char *ns, const char *key, bool fallback) {
  nvs_handle_t handle = 0;
  uint8_t value = fallback ? 1 : 0;
  if (nvs_open(ns, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_u8(handle, key, &value);
    nvs_close(handle);
  }
  return value != 0;
}

void nvs_set_bool(const char *ns, const char *key, bool value) {
  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open(ns, NVS_READWRITE, &handle));
  ESP_ERROR_CHECK(nvs_set_u8(handle, key, value ? 1 : 0));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);
}

bool is_available(uint8_t index) {
  return available_cards[index / 8] & (1U << (index % 8));
}

void set_available(uint8_t index, bool available) {
  uint8_t mask = 1U << (index % 8);
  if (available) {
    available_cards[index / 8] |= mask;
  } else {
    available_cards[index / 8] &= ~mask;
  }
}

void reset_deck() {
  available_cards.fill(0);
  for (uint8_t i = 0; i < CARD_COUNT; ++i) set_available(i, true);
}

uint8_t cards_remaining() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < CARD_COUNT; ++i) {
    if (is_available(i)) ++count;
  }
  return count;
}

void save_deck() {
  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open(NVS_APP, NVS_READWRITE, &handle));
  ESP_ERROR_CHECK(nvs_set_blob(handle, "deck78", available_cards.data(), available_cards.size()));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);
}

void load_deck() {
  nvs_handle_t handle = 0;
  size_t size = available_cards.size();
  if (nvs_open(NVS_APP, NVS_READWRITE, &handle) == ESP_OK &&
      nvs_get_blob(handle, "deck78", available_cards.data(), &size) == ESP_OK &&
      size == available_cards.size()) {
    nvs_close(handle);
    return;
  }
  if (handle != 0) nvs_close(handle);
  reset_deck();
  save_deck();
}

std::string html_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string url_decode(const char *value, size_t len) {
  std::string out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    if (value[i] == '+') {
      out.push_back(' ');
    } else if (value[i] == '%' && i + 2 < len) {
      int hi = hex_value(value[i + 1]);
      int lo = hex_value(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      }
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

std::string form_value(const std::string &body, const char *key) {
  std::string needle = std::string(key) + "=";
  size_t start = body.find(needle);
  if (start == std::string::npos) return "";
  start += needle.size();
  size_t end = body.find('&', start);
  if (end == std::string::npos) end = body.size();
  return url_decode(body.data() + start, end - start);
}

esp_err_t rtc_read(uint8_t reg, uint8_t *data, size_t len) {
  if (!rtc_dev) return ESP_ERR_INVALID_STATE;
  return i2c_master_transmit_receive(rtc_dev, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

esp_err_t rtc_write(uint8_t reg, const uint8_t *data, size_t len) {
  if (!rtc_dev) return ESP_ERR_INVALID_STATE;
  std::array<uint8_t, 12> buffer{};
  if (len + 1 > buffer.size()) return ESP_ERR_INVALID_ARG;
  buffer[0] = reg;
  memcpy(buffer.data() + 1, data, len);
  return i2c_master_transmit(rtc_dev, buffer.data(), len + 1, pdMS_TO_TICKS(100));
}

bool rtc_get(RtcDateTime &out) {
  uint8_t data[7] = {};
  if (rtc_read(0x04, data, sizeof(data)) != ESP_OK) return false;
  bool voltage_low = data[0] & 0x80;
  out.second = bcd_to_dec(data[0] & 0x7f);
  out.minute = bcd_to_dec(data[1] & 0x7f);
  out.hour = bcd_to_dec(data[2] & 0x3f);
  out.day = bcd_to_dec(data[3] & 0x3f);
  out.month = bcd_to_dec(data[5] & 0x1f);
  out.year = 2000 + bcd_to_dec(data[6]);
  out.valid = !voltage_low && out.year >= 2024 && out.month >= 1 && out.month <= 12 &&
              out.day >= 1 && out.day <= 31 && out.hour < 24 && out.minute < 60 && out.second < 60;
  return out.valid;
}

void rtc_set_from_time(time_t value) {
  if (!rtc_dev || value < 1704067200) return;
  struct tm tm {};
  gmtime_r(&value, &tm);
  uint8_t data[7] = {
      dec_to_bcd(static_cast<uint8_t>(tm.tm_sec)),
      dec_to_bcd(static_cast<uint8_t>(tm.tm_min)),
      dec_to_bcd(static_cast<uint8_t>(tm.tm_hour)),
      dec_to_bcd(static_cast<uint8_t>(tm.tm_mday)),
      dec_to_bcd(static_cast<uint8_t>(tm.tm_wday)),
      dec_to_bcd(static_cast<uint8_t>(tm.tm_mon + 1)),
      dec_to_bcd(static_cast<uint8_t>((tm.tm_year + 1900) - 2000)),
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_write(0x04, data, sizeof(data)));
}

void init_rtc() {
  i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
  i2c_device_config_t cfg {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = RTC_ADDR;
  cfg.scl_speed_hz = 100000;
  esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &rtc_dev);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RTC unavailable: %s", esp_err_to_name(err));
    rtc_dev = nullptr;
    return;
  }

  RtcDateTime rtc {};
  if (rtc_get(rtc)) {
    struct tm tm {};
    tm.tm_year = rtc.year - 1900;
    tm.tm_mon = rtc.month - 1;
    tm.tm_mday = rtc.day;
    tm.tm_hour = rtc.hour;
    tm.tm_min = rtc.minute;
    tm.tm_sec = rtc.second;
    time_t utc = utc_time_from_tm(&tm);
    timeval tv {};
    tv.tv_sec = utc;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    time_state = TimeState::Rtc;
    ESP_LOGI(TAG, "Restored time from RTC");
  }
}

bool system_time_valid() {
  time_t now = 0;
  time(&now);
  return now >= 1704067200;
}

time_t utc_time_from_tm(struct tm *tm) {
  char *old_tz = getenv("TZ");
  std::string saved_tz = old_tz ? old_tz : "";
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t value = mktime(tm);
  if (old_tz) {
    setenv("TZ", saved_tz.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return value;
}

std::string time_text() {
  if (!system_time_valid()) return "--:--";
  time_t now = 0;
  time(&now);
  struct tm local {};
  localtime_r(&now, &local);
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%I:%M %p", &local);
  std::string text = buffer;
  if (!text.empty() && text[0] == '0') text.erase(0, 1);
  return text;
}

std::string date_text() {
  if (!system_time_valid()) return "TIME UNSYNCED";
  time_t now = 0;
  time(&now);
  struct tm local {};
  localtime_r(&now, &local);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%a %b %d", &local);
  return buffer;
}

int days_in_month(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  if (month < 1 || month > 12) return 31;
  return days[month - 1];
}

void normalize_manual_time() {
  manual_time.year = std::clamp(manual_time.year, 2024, 2099);
  manual_time.month = std::clamp(manual_time.month, 1, 12);
  manual_time.day = std::clamp(manual_time.day, 1, days_in_month(manual_time.year, manual_time.month));
  manual_time.hour = (manual_time.hour % 24 + 24) % 24;
  manual_time.minute = (manual_time.minute % 60 + 60) % 60;
  manual_time.second = 0;
  manual_time.valid = true;
}

void load_manual_time_from_system() {
  time_t now = 0;
  time(&now);
  struct tm local {};
  if (system_time_valid()) {
    localtime_r(&now, &local);
    manual_time.year = local.tm_year + 1900;
    manual_time.month = local.tm_mon + 1;
    manual_time.day = local.tm_mday;
    manual_time.hour = local.tm_hour;
    manual_time.minute = local.tm_min;
  } else {
    manual_time.year = 2026;
    manual_time.month = 9;
    manual_time.day = 7;
    manual_time.hour = 6;
    manual_time.minute = 26;
  }
  normalize_manual_time();
}

void adjust_manual_time(SettingField field, int delta) {
  switch (field) {
    case SettingField::Hour:
      manual_time.hour += delta;
      break;
    case SettingField::Minute:
      manual_time.minute += delta;
      break;
    case SettingField::AmPm:
      manual_time.hour = (manual_time.hour + 12) % 24;
      break;
    case SettingField::Month:
      manual_time.month += delta;
      break;
    case SettingField::Day:
      manual_time.day += delta;
      break;
    case SettingField::Year:
      manual_time.year += delta;
      break;
  }
  normalize_manual_time();
}

time_t manual_time_to_epoch() {
  normalize_manual_time();
  struct tm local {};
  local.tm_year = manual_time.year - 1900;
  local.tm_mon = manual_time.month - 1;
  local.tm_mday = manual_time.day;
  local.tm_hour = manual_time.hour;
  local.tm_min = manual_time.minute;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return mktime(&local);
}

void save_manual_time() {
  time_t value = manual_time_to_epoch();
  if (value < 1704067200) return;
  timeval tv {};
  tv.tv_sec = value;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  time_state = TimeState::Rtc;
  rtc_set_from_time(value);
}

std::string dim_time_text(bool hour) {
  if (!system_time_valid()) return "--";
  time_t now = 0;
  time(&now);
  struct tm local {};
  localtime_r(&now, &local);
  char buffer[4];
  int display_hour = local.tm_hour % 12;
  if (display_hour == 0) display_hour = 12;
  snprintf(buffer, sizeof(buffer), "%02d", hour ? display_hour : local.tm_min);
  return buffer;
}

std::string time_status_text() {
  if (time_state == TimeState::Ntp) return date_text() + " // NTP";
  if (time_state == TimeState::Rtc) return date_text() + " // RTC";
  if (setup_portal_running) return "WIFI SETUP ACTIVE";
  return "TIME UNSYNCED";
}

std::string setup_details_text() {
  if (setup_portal_running) return "WIFI: SETUP ACTIVE\nSSID TAROTBOT-SETUP\nPASS arcana78\nURL 192.168.4.1";
  if (wifi_connected) return "WIFI CONNECTED\nNTP SYNC ENABLED";
  return "WIFI SAVED\nSETUP OPENS IF FAILS";
}

void touch_activity() {
  last_activity_ms = now_ms();
}

void style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color, lv_text_align_t align) {
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

void set_text(lv_obj_t *label, const std::string &text) {
  lv_label_set_text(label, text.c_str());
}

void show_obj(lv_obj_t *obj, bool show) {
  if (!obj) return;
  if (show) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

bool card_art_bit(int card_index, int x, int y) {
  if (card_index < 0 || card_index >= CARD_COUNT) return false;
  int bit = y * CARD_ART_W + x;
  return (CARD_ART[card_index][bit / 8] & (1U << (bit % 8))) != 0;
}

void init_card_art_image() {
  if (card_art_pixels) return;
  size_t bytes = CARD_ART_W * CARD_ART_H * sizeof(uint16_t);
  card_art_pixels = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!card_art_pixels) {
    card_art_pixels = static_cast<uint16_t *>(malloc(bytes));
  }
  if (!card_art_pixels) {
    ESP_LOGE(TAG, "No memory for card art buffer");
    return;
  }
  memset(card_art_pixels, 0, bytes);
  card_art_img.header.magic = LV_IMAGE_HEADER_MAGIC;
  card_art_img.header.cf = LV_COLOR_FORMAT_RGB565;
  card_art_img.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
  card_art_img.header.w = CARD_ART_W;
  card_art_img.header.h = CARD_ART_H;
  card_art_img.header.stride = CARD_ART_W * sizeof(uint16_t);
  card_art_img.header.reserved_2 = 0;
  card_art_img.data_size = bytes;
  card_art_img.data = reinterpret_cast<const uint8_t *>(card_art_pixels);
  card_art_img.reserved = nullptr;
  card_art_img.reserved_2 = nullptr;
}

void render_card_art(int card_index) {
  bool show = card_index >= 0 && card_index < CARD_COUNT && card_art_pixels;
  show_obj(ui.card_art, show);
  show_obj(ui.sigil_box, false);
  if (!show) return;

  constexpr uint16_t green = 0x07e8;
  constexpr uint16_t black = 0x0000;
  for (int y = 0; y < CARD_ART_H; ++y) {
    for (int x = 0; x < CARD_ART_W; ++x) {
      bool on = card_art_bit(card_index, x, y);
      card_art_pixels[y * CARD_ART_W + x] = on ? green : black;
    }
  }
  lv_image_set_src(ui.card_art, &card_art_img);
  lv_obj_invalidate(ui.card_art);
}

void show_dim_digits(bool show) {
  for (auto &digit : ui.dim_digits) {
    for (lv_obj_t *segment : digit) {
      show_obj(segment, show);
    }
  }
}

void restore_layer_order() {
  for (lv_obj_t *rain : ui.rain) {
    lv_obj_move_to_index(rain, 0);
  }
  lv_obj_move_foreground(ui.time);
  lv_obj_move_foreground(ui.status);
  lv_obj_move_foreground(ui.title);
  lv_obj_move_foreground(ui.subtitle);
  lv_obj_move_foreground(ui.panel);
  lv_obj_move_foreground(ui.footer);
  lv_obj_move_foreground(ui.touch_layer);
}

void layout_dim_digit(int index, int value, int x, int y, int w, int h, int t) {
  static const bool segs[10][DIM_SEGMENTS] = {
      {true, true, true, true, true, true, false},
      {false, true, true, false, false, false, false},
      {true, true, false, true, true, false, true},
      {true, true, true, true, false, false, true},
      {false, true, true, false, false, true, true},
      {true, false, true, true, false, true, true},
      {true, false, true, true, true, true, true},
      {true, true, true, false, false, false, false},
      {true, true, true, true, true, true, true},
      {true, true, true, true, false, true, true},
  };
  int half = h / 2;
  struct SegmentRect {
    int x;
    int y;
    int w;
    int h;
  };
  SegmentRect rects[DIM_SEGMENTS] = {
      {x + t, y, w - 2 * t, t},
      {x + w - t, y + t, t, half - t},
      {x + w - t, y + half, t, half - t},
      {x + t, y + h - t, w - 2 * t, t},
      {x, y + half, t, half - t},
      {x, y + t, t, half - t},
      {x + t, y + half - t / 2, w - 2 * t, t},
  };

  for (int i = 0; i < DIM_SEGMENTS; ++i) {
    lv_obj_t *segment = ui.dim_digits[index][i];
    lv_obj_set_pos(segment, rects[i].x, rects[i].y);
    lv_obj_set_size(segment, rects[i].w, rects[i].h);
    lv_obj_set_style_bg_color(segment, MATRIX_CLOCK, 0);
    lv_obj_set_style_bg_opa(segment, segs[value][i] ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

void render_dim_time_digits() {
  std::string hour = dim_time_text(true);
  std::string minute = dim_time_text(false);
  int values[4] = {0, 0, 0, 0};
  if (hour.size() >= 2 && minute.size() >= 2 && hour[0] != '-') {
    values[0] = hour[0] - '0';
    values[1] = hour[1] - '0';
    values[2] = minute[0] - '0';
    values[3] = minute[1] - '0';
  }

  int w = 116;
  int h = 176;
  int t = 20;
  layout_dim_digit(0, values[0], 70, 50, w, h, t);
  layout_dim_digit(1, values[1], 224, 50, w, h, t);
  layout_dim_digit(2, values[2], 70, 276, w, h, t);
  layout_dim_digit(3, values[3], 224, 276, w, h, t);
}

void set_label_font(lv_obj_t *label, const lv_font_t *font) {
  lv_obj_set_style_text_font(label, font, 0);
}

void layout_panel_box(int x, int y, int w, int h) {
  lv_obj_set_pos(ui.panel, x, y);
  lv_obj_set_size(ui.panel, w, h);
  show_obj(ui.card_name, true);
  lv_obj_set_pos(ui.card_name, 12, 10);
  lv_obj_set_size(ui.card_name, w - 24, 34);
  lv_obj_set_pos(ui.body, 16, 54);
  lv_obj_set_size(ui.body, w - 32, h - 68);
}

void layout_footer(int y = 450, int h = 38) {
  lv_obj_set_pos(ui.footer, 28, y);
  lv_obj_set_size(ui.footer, 354, h);
}

void layout_sigil_box(int x, int y, int w, int h) {
  lv_obj_set_pos(ui.sigil_box, x, y);
  lv_obj_set_size(ui.sigil_box, w, h);
  lv_obj_set_pos(ui.sigil_top, 8, 8);
  lv_obj_set_size(ui.sigil_top, w - 16, 28);
  lv_obj_set_pos(ui.sigil_main, 8, h / 2 - 34);
  lv_obj_set_size(ui.sigil_main, w - 16, 72);
  lv_obj_set_pos(ui.sigil_bottom, 8, h - 38);
  lv_obj_set_size(ui.sigil_bottom, w - 16, 30);
}

size_t page_text(const char *text, size_t start, size_t max_chars, bool &has_more) {
  size_t len = strlen(text);
  if (start >= len) {
    has_more = false;
    return len;
  }
  size_t end = std::min(len, start + max_chars);
  if (end < len) {
    size_t split = end;
    while (split > start && text[split] != ' ') --split;
    if (split > start + 40) end = split;
  }
  has_more = end < len;
  std::string page(text + start, end - start);
  set_text(ui.body, page);
  return end;
}

std::string sigil_rank_for(int card_index) {
  static const char *major[] = {"0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
                                "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX",
                                "XX", "XXI"};
  static const char *minor[] = {"A", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
                                "PG", "KN", "QN", "KG"};
  if (card_index < 0) return "--";
  if (card_index < 22) return major[card_index];
  return minor[(card_index - 22) % 14];
}

std::string sigil_suit_for(int card_index) {
  if (card_index < 0) return "VOID";
  if (card_index < 22) return "ARCANA";
  static const char *suits[] = {"FIRE", "CUPS", "SWRD", "COIN"};
  return suits[(card_index - 22) / 14];
}

std::string sigil_name_for(int card_index) {
  if (card_index < 0) return "TAROTBOT";
  std::string name = CARDS[card_index].name;
  if (name.size() > 18) name.resize(18);
  return name;
}

void render_sigil(int card_index) {
  bool show = card_index >= 0;
  show_obj(ui.sigil_box, show);
  if (!show) return;
  set_label_font(ui.sigil_main, &lv_font_montserrat_48);
  set_text(ui.sigil_top, "[" + sigil_suit_for(card_index) + "]");
  set_text(ui.sigil_main, sigil_rank_for(card_index));
  set_text(ui.sigil_bottom, sigil_name_for(card_index));
}

void update_time_labels() {
  set_text(ui.time, time_text());
  set_text(ui.status, time_status_text());
  if (saver_active) {
    render_dim_time_digits();
  }
}

void render_screen();

void wake_from_saver() {
  saver_active = false;
  wake_only = true;
  bsp_display_brightness_set(ACTIVE_BRIGHTNESS);
  render_screen();
  touch_activity();
}

void start_saver() {
  saver_active = true;
  bsp_display_brightness_set(SAVER_BRIGHTNESS);
  render_screen();
}

void choose_card() {
  uint8_t remaining = cards_remaining();
  if (remaining == 0) {
    screen_state = ScreenState::Empty;
    render_screen();
    return;
  }
  uint8_t target = esp_random() % remaining;
  for (uint8_t i = 0; i < CARD_COUNT; ++i) {
    if (!is_available(i)) continue;
    if (target == 0) {
      current_card = i;
      break;
    }
    --target;
  }
  screen_state = ScreenState::CardBack;
  render_screen();
}

void reveal_card() {
  if (current_card < 0) return;
  set_available(static_cast<uint8_t>(current_card), false);
  save_deck();
  screen_state = ScreenState::Revealed;
  render_screen();
}

void reshuffle() {
  reset_deck();
  save_deck();
  current_card = -1;
  reading_offset = 0;
  reading_next_offset = 0;
  reading_has_more = false;
  app_view = AppView::Tarot;
  screen_state = ScreenState::Welcome;
  render_screen();
}

void advance() {
  if (saver_active) {
    wake_from_saver();
    return;
  }
  if (wake_only) {
    wake_only = false;
    touch_activity();
    return;
  }
  touch_activity();
  switch (screen_state) {
    case ScreenState::Welcome:
      choose_card();
      break;
    case ScreenState::CardBack:
      reveal_card();
      break;
    case ScreenState::Revealed:
      if (interpretations_enabled) {
        reading_offset = 0;
        screen_state = ScreenState::Reading;
        render_screen();
      } else {
        choose_card();
      }
      break;
    case ScreenState::Reading:
      if (reading_has_more) {
        reading_offset = reading_next_offset;
        render_screen();
      } else {
        choose_card();
      }
      break;
    case ScreenState::Empty:
      break;
  }
}

void layout_panel(int y, int h) {
  layout_panel_box(46, 128, 318, 288);
  lv_obj_set_pos(ui.body, 16, y);
  lv_obj_set_size(ui.body, 290, h);
}

void render_saver() {
  update_time_labels();
  restore_layer_order();
  show_obj(ui.time, false);
  show_obj(ui.status, false);
  show_obj(ui.title, false);
  show_obj(ui.subtitle, false);
  show_obj(ui.panel, false);
  show_obj(ui.footer, false);
  show_obj(ui.sigil_box, false);
  show_obj(ui.card_art, false);
  show_dim_digits(true);
  render_dim_time_digits();
  for (auto &digit : ui.dim_digits) {
    for (lv_obj_t *segment : digit) lv_obj_move_foreground(segment);
  }
  lv_obj_move_foreground(ui.touch_layer);
}

void render_tools() {
  update_time_labels();
  restore_layer_order();
  show_dim_digits(false);
  show_obj(ui.time, true);
  show_obj(ui.status, true);
  show_obj(ui.title, true);
  show_obj(ui.subtitle, true);
  show_obj(ui.panel, true);
  show_obj(ui.footer, true);
  show_obj(ui.body, true);
  show_obj(ui.card_art, false);
  set_label_font(ui.body, &lv_font_unscii_16);
  layout_footer();
  layout_panel_box(54, 132, 302, 284);
  set_text(ui.title, ">TOOLS_");
  set_text(ui.subtitle, "MATRIX WITCHWARE HUB");
  set_text(ui.card_name, "SYSTEM HUB");
  render_sigil(0);
  set_text(ui.sigil_top, "[CONFIG]");
  set_text(ui.sigil_main, LV_SYMBOL_SETTINGS);
  set_label_font(ui.sigil_main, &lv_font_montserrat_48);
  set_text(ui.sigil_bottom, "SETTINGS");
  layout_sigil_box(56, 58, 190, 142);
  set_text(ui.body, "[TAROTBOT]\n[FUTURE SLOTS]");
  lv_obj_set_pos(ui.body, 16, 218);
  lv_obj_set_size(ui.body, 270, 52);
  set_text(ui.footer, "TAP GEAR SETTINGS // SWIPE BACK");
}

std::string settings_text() {
  int display_hour = manual_time.hour % 12;
  if (display_hour == 0) display_hour = 12;
  const char *meridiem = manual_time.hour >= 12 ? "PM" : "AM";
  std::string setup = setup_details_text();
  char buffer[360];
  snprintf(buffer, sizeof(buffer),
           "READINGS: %s\n"
           "TAP TOP TO TOGGLE\n"
           "TIME: %02d:%02d %s\n"
           "HOUR   -        +\n"
           "MIN    -        +\n"
           "MONTH  -        +\n"
           "DAY    -        +\n"
           "YEAR   -        +\n"
           "AM/PM TAP\n"
           "SAVE RTC\n\n%s",
           interpretations_enabled ? "ON " : "OFF", display_hour, manual_time.minute, meridiem,
           setup.c_str());
  return buffer;
}

void render_settings() {
  update_time_labels();
  restore_layer_order();
  show_dim_digits(false);
  show_obj(ui.time, true);
  show_obj(ui.status, true);
  show_obj(ui.title, true);
  show_obj(ui.subtitle, true);
  show_obj(ui.panel, true);
  show_obj(ui.footer, true);
  show_obj(ui.body, true);
  show_obj(ui.sigil_box, false);
  show_obj(ui.card_art, false);
  set_label_font(ui.body, &lv_font_unscii_16);
  layout_footer();
  layout_panel_box(44, 112, 322, 336);
  set_text(ui.title, ">SETTINGS");
  set_text(ui.subtitle, "TIME // WIFI // ORACLE");
  set_text(ui.card_name, "CONFIG BUFFER");
  lv_obj_set_pos(ui.body, 16, 50);
  lv_obj_set_size(ui.body, 290, 274);
  set_text(ui.body, settings_text());
  set_text(ui.footer, "LEFT - // RIGHT + // SWIPE BACK");
}

void render_tarot() {
  update_time_labels();
  restore_layer_order();
  show_dim_digits(false);
  show_obj(ui.time, true);
  show_obj(ui.status, true);
  show_obj(ui.title, true);
  show_obj(ui.subtitle, true);
  show_obj(ui.panel, true);
  show_obj(ui.footer, true);
  show_obj(ui.body, true);
  set_label_font(ui.body, &lv_font_unscii_16);
  layout_footer();

  char count[48];
  snprintf(count, sizeof(count), "%u OF %u CARDS REMAIN", cards_remaining(), CARD_COUNT);

  switch (screen_state) {
    case ScreenState::Welcome:
      layout_panel(216, 54);
      set_text(ui.title, ">TAROTBOT_");
      set_text(ui.subtitle, "ARCANA // TERMINAL");
      set_text(ui.card_name, "ORACLE READY");
      show_obj(ui.card_art, false);
      render_sigil(21);
      set_text(ui.sigil_top, "[DECK]");
      set_text(ui.sigil_main, "78");
      set_label_font(ui.sigil_main, &lv_font_montserrat_48);
      set_text(ui.sigil_bottom, "NODES");
      layout_sigil_box(58, 58, 202, 146);
      set_text(ui.body, "TAP TO OPEN\nTHE NEXT NODE");
      set_text(ui.footer, "TAP DRAW // HOLD SHUFFLE");
      break;
    case ScreenState::CardBack:
      layout_panel(258, 24);
      set_text(ui.title, ">DRAW BUFFER");
      set_text(ui.subtitle, count);
      set_text(ui.card_name, "CARD FACE HIDDEN");
      show_obj(ui.card_art, false);
      show_obj(ui.sigil_box, true);
      set_text(ui.sigil_top, "[FACE DOWN]");
      set_text(ui.sigil_main, "?");
      set_label_font(ui.sigil_main, &lv_font_montserrat_48);
      set_text(ui.sigil_bottom, "TAP TO FLIP");
      layout_sigil_box(48, 52, 222, 196);
      set_text(ui.body, "SIGNAL WAITING");
      set_text(ui.footer, "TAP REVEAL // HOLD SHUFFLE");
      break;
    case ScreenState::Revealed:
      set_text(ui.title, ">CARD REVEALED");
      set_text(ui.subtitle, CARDS[current_card].name);
      layout_panel_box(88, 106, 234, 350);
      show_obj(ui.card_name, false);
      lv_obj_set_pos(ui.card_art, (234 - CARD_ART_W) / 2, 3);
      render_card_art(current_card);
      show_obj(ui.body, false);
      layout_footer(462, 30);
      set_text(ui.footer, interpretations_enabled ? "TAP READING // HOLD" : "TAP NEXT // HOLD");
      break;
    case ScreenState::Reading:
      layout_panel(66, 200);
      set_text(ui.title, ">READING");
      set_text(ui.subtitle, CARDS[current_card].name);
      set_text(ui.card_name, "INTERPRETATION");
      show_obj(ui.card_art, false);
      render_sigil(-1);
      reading_next_offset = page_text(CARDS[current_card].interpretation, reading_offset, 235, reading_has_more);
      set_text(ui.footer, reading_has_more ? "TAP MORE // HOLD SHUFFLE" : "TAP NEXT // HOLD SHUFFLE");
      break;
    case ScreenState::Empty:
      layout_panel(112, 122);
      set_text(ui.title, ">DECK COMPLETE");
      set_text(ui.subtitle, "ALL 78 CARDS WALKED");
      set_text(ui.card_name, "RESHUFFLE REQUIRED");
      show_obj(ui.card_art, false);
      render_sigil(-1);
      set_text(ui.body, "FULL DECK PROCESSED\nHOLD TO RESEED ORACLE");
      set_text(ui.footer, "HOLD TO SHUFFLE");
      break;
  }
}

void render_screen() {
  if (saver_active) {
    render_saver();
  } else if (app_view == AppView::Tools) {
    render_tools();
  } else if (app_view == AppView::Settings) {
    render_settings();
  } else {
    render_tarot();
  }
}

void handle_tools_click(const lv_point_t &point) {
  touch_activity();
  if (point.y < 345) {
    load_manual_time_from_system();
    app_view = AppView::Settings;
  } else {
    app_view = AppView::Tarot;
  }
  render_screen();
}

void handle_settings_click(const lv_point_t &point) {
  touch_activity();
  if (point.y >= 120 && point.y < 190) {
    interpretations_enabled = !interpretations_enabled;
    nvs_set_bool(NVS_APP, INTERPRETATIONS_KEY, interpretations_enabled);
    render_screen();
    return;
  }

  struct Row {
    int y1;
    int y2;
    SettingField field;
  };
  static const Row rows[] = {{212, 244, SettingField::Hour},   {244, 276, SettingField::Minute},
                             {276, 308, SettingField::Month},  {308, 340, SettingField::Day},
                             {340, 372, SettingField::Year},   {372, 404, SettingField::AmPm}};
  for (const Row &row : rows) {
    if (point.y >= row.y1 && point.y < row.y2) {
      setting_field = row.field;
      adjust_manual_time(setting_field, point.x < 205 ? -1 : 1);
      render_screen();
      return;
    }
  }

  if (point.y >= 404 && point.y < 448) {
    save_manual_time();
    render_screen();
    return;
  }

  if (point.y >= 430) {
    app_view = AppView::Tools;
    render_screen();
  }
}

void handle_horizontal_swipe() {
  uint64_t now = now_ms();
  if (now < touch_block_until_ms) return;
  if (saver_active) {
    wake_from_saver();
    return;
  }
  touch_activity();
  if (app_view == AppView::Tarot) {
    app_view = AppView::Tools;
  } else if (app_view == AppView::Settings) {
    app_view = AppView::Tools;
  } else {
    app_view = AppView::Tarot;
  }
  touch_block_until_ms = now + TOUCH_BLOCK_MS;
  render_screen();
}

void touch_event_cb(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev) {
      lv_indev_get_point(indev, &touch_press_point);
      touch_press_valid = true;
      touch_press_ms = now_ms();
    }
  } else if (code == LV_EVENT_RELEASED) {
    if (touch_press_valid) {
      lv_point_t point {};
      lv_indev_t *indev = lv_event_get_indev(event);
      if (indev) lv_indev_get_point(indev, &point);
      int dx = point.x - touch_press_point.x;
      int dy = point.y - touch_press_point.y;
      int abs_dx = dx < 0 ? -dx : dx;
      int abs_dy = dy < 0 ? -dy : dy;
      if (abs_dx >= 58 && abs_dx > abs_dy + 22 && now_ms() - touch_press_ms < 1800) {
        handle_horizontal_swipe();
      }
      touch_press_valid = false;
    }
  } else if (code == LV_EVENT_CLICKED) {
    uint64_t now = now_ms();
    if (now < touch_block_until_ms) {
      return;
    }
    touch_long_handled = false;
    lv_point_t point {};
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev) lv_indev_get_point(indev, &point);
    if (app_view == AppView::Tools) {
      handle_tools_click(point);
    } else if (app_view == AppView::Settings) {
      handle_settings_click(point);
    } else {
      advance();
    }
  } else if (code == LV_EVENT_LONG_PRESSED) {
    touch_activity();
    touch_long_handled = true;
    touch_block_until_ms = now_ms() + TOUCH_BLOCK_MS;
    reshuffle();
  }
}

void time_timer_cb(lv_timer_t *) {
  update_time_labels();
}

void idle_timer_cb(lv_timer_t *) {
  if (!saver_active && now_ms() - last_activity_ms >= IDLE_SAVER_MS) {
    start_saver();
  }
}

void rain_timer_cb(lv_timer_t *) {
  static uint32_t frame = 0;
  ++frame;
  static const char *streams[] = {
      "0\n1\n{\n}", "1\n0\n[\n]", "<\n>\n/\n\\", "7\n8\nX\nI", "R\nU\nN\n0",
      "H\nE\nX\n1", "N\nT\nP\n0", "S\nI\nG\n1", "0\n0\n1\n1", "{\n}\n[\n]"};
  for (size_t i = 0; i < ui.rain.size(); ++i) {
    uint32_t tick = frame / (saver_active ? 1 : 2);
    const char *text = streams[(tick + i * 3) % (sizeof(streams) / sizeof(streams[0]))];
    lv_label_set_text(ui.rain[i], text);
    lv_obj_set_style_text_opa(ui.rain[i], saver_active ? static_cast<lv_opa_t>(110 + (i % 4) * 24)
                                                       : static_cast<lv_opa_t>(48 + (i % 4) * 14),
                              0);
    lv_obj_set_y(ui.rain[i], (lv_obj_get_y(ui.rain[i]) + (saver_active ? 18 : 8) + (i % 5)) % WATCH_H);
  }
  if (saver_active) {
    update_time_labels();
    render_dim_time_digits();
  }
}

void create_ui() {
  ui.root = lv_screen_active();
  lv_obj_remove_style_all(ui.root);
  lv_obj_clear_flag(ui.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui.root, BLACK, 0);
  lv_obj_set_style_bg_opa(ui.root, LV_OPA_COVER, 0);

  for (size_t i = 0; i < ui.rain.size(); ++i) {
    ui.rain[i] = lv_label_create(ui.root);
    lv_label_set_text(ui.rain[i], "01");
    style_label(ui.rain[i], &lv_font_unscii_16, MATRIX_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(ui.rain[i], 4 + (i % 14) * 29, (i * 37) % WATCH_H);
    lv_obj_set_style_text_opa(ui.rain[i], static_cast<lv_opa_t>(48 + (i % 4) * 14), 0);
  }

  ui.time = lv_label_create(ui.root);
  style_label(ui.time, &lv_font_unscii_16, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.time, 104, 10);
  lv_obj_set_size(ui.time, 202, 26);

  ui.status = lv_label_create(ui.root);
  style_label(ui.status, &lv_font_unscii_8, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.status, 58, 36);
  lv_obj_set_size(ui.status, 294, 24);

  ui.title = lv_label_create(ui.root);
  style_label(ui.title, &lv_font_unscii_16, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.title, 34, 60);
  lv_obj_set_size(ui.title, 342, 32);

  ui.subtitle = lv_label_create(ui.root);
  style_label(ui.subtitle, &lv_font_unscii_16, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.subtitle, 34, 84);
  lv_obj_set_size(ui.subtitle, 342, 34);

  ui.panel = lv_obj_create(ui.root);
  lv_obj_remove_style_all(ui.panel);
  lv_obj_clear_flag(ui.panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui.panel, PANEL, 0);
  lv_obj_set_style_bg_opa(ui.panel, LV_OPA_90, 0);
  lv_obj_set_style_border_color(ui.panel, MATRIX_SOFT, 0);
  lv_obj_set_style_border_width(ui.panel, 2, 0);
  lv_obj_set_pos(ui.panel, 44, 124);
  lv_obj_set_size(ui.panel, 322, 292);

  ui.card_name = lv_label_create(ui.panel);
  style_label(ui.card_name, &lv_font_unscii_16, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.card_name, 16, 14);
  lv_obj_set_size(ui.card_name, 326, 46);

  ui.sigil_box = lv_obj_create(ui.panel);
  lv_obj_remove_style_all(ui.sigil_box);
  lv_obj_clear_flag(ui.sigil_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui.sigil_box, BLACK, 0);
  lv_obj_set_style_bg_opa(ui.sigil_box, LV_OPA_80, 0);
  lv_obj_set_style_border_color(ui.sigil_box, MATRIX, 0);
  lv_obj_set_style_border_width(ui.sigil_box, 2, 0);
  lv_obj_set_pos(ui.sigil_box, 90, 72);
  lv_obj_set_size(ui.sigil_box, 178, 132);

  ui.sigil_top = lv_label_create(ui.sigil_box);
  style_label(ui.sigil_top, &lv_font_unscii_16, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.sigil_top, 8, 8);
  lv_obj_set_size(ui.sigil_top, 162, 24);

  ui.sigil_main = lv_label_create(ui.sigil_box);
  style_label(ui.sigil_main, &lv_font_montserrat_48, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.sigil_main, 8, 30);
  lv_obj_set_size(ui.sigil_main, 162, 58);

  ui.sigil_bottom = lv_label_create(ui.sigil_box);
  style_label(ui.sigil_bottom, &lv_font_unscii_16, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.sigil_bottom, 8, 96);
  lv_obj_set_size(ui.sigil_bottom, 162, 28);

  ui.body = lv_label_create(ui.panel);
  style_label(ui.body, &lv_font_unscii_16, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.body, 20, 216);
  lv_obj_set_size(ui.body, 318, 58);

  init_card_art_image();
  ui.card_art = lv_image_create(ui.panel);
  if (card_art_pixels) {
    lv_image_set_src(ui.card_art, &card_art_img);
  }
  lv_obj_set_size(ui.card_art, CARD_ART_W, CARD_ART_H);
  lv_obj_add_flag(ui.card_art, LV_OBJ_FLAG_HIDDEN);

  ui.footer = lv_label_create(ui.root);
  style_label(ui.footer, &lv_font_unscii_16, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.footer, 28, 450);
  lv_obj_set_size(ui.footer, 354, 38);

  for (auto &digit : ui.dim_digits) {
    for (lv_obj_t *&segment : digit) {
      segment = lv_obj_create(ui.root);
      lv_obj_remove_style_all(segment);
      lv_obj_set_style_bg_color(segment, MATRIX, 0);
      lv_obj_set_style_bg_opa(segment, LV_OPA_60, 0);
      lv_obj_set_style_radius(segment, 2, 0);
      lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
    }
  }

  ui.touch_layer = lv_obj_create(ui.root);
  lv_obj_remove_style_all(ui.touch_layer);
  lv_obj_clear_flag(ui.touch_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(ui.touch_layer, WATCH_W, WATCH_H);
  lv_obj_set_pos(ui.touch_layer, 0, 0);
  lv_obj_add_flag(ui.touch_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ui.touch_layer, touch_event_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(ui.touch_layer, touch_event_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(ui.touch_layer, touch_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(ui.touch_layer, touch_event_cb, LV_EVENT_LONG_PRESSED, nullptr);

  lv_timer_create(time_timer_cb, 1000, nullptr);
  lv_timer_create(idle_timer_cb, 250, nullptr);
  lv_timer_create(rain_timer_cb, 170, nullptr);
  render_screen();
}

void button_task(void *) {
  gpio_config_t cfg {};
  cfg.pin_bit_mask = 1ULL << BOOT_BUTTON;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&cfg);

  while (true) {
    bool down = gpio_get_level(BOOT_BUTTON) == 0;
    uint64_t now = now_ms();
    if (down && !button_was_down) {
      button_down_ms = now;
      button_long_handled = false;
      if (saver_active && bsp_display_lock(100)) {
        wake_from_saver();
        bsp_display_unlock();
      }
    }
    if (down && !button_long_handled && now - button_down_ms >= LONG_PRESS_MS) {
      button_long_handled = true;
      if (!saver_active && bsp_display_lock(100)) {
        touch_activity();
        reshuffle();
        bsp_display_unlock();
      }
    }
    if (!down && button_was_down && !button_long_handled && now - button_down_ms > 35) {
      if (bsp_display_lock(100)) {
        if (app_view == AppView::Settings) {
          app_view = AppView::Tools;
          touch_activity();
          render_screen();
        } else if (app_view == AppView::Tools) {
          app_view = AppView::Tarot;
          touch_activity();
          render_screen();
        } else {
          advance();
        }
        bsp_display_unlock();
      }
    }
    button_was_down = down;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void sntp_sync_cb(struct timeval *tv) {
  time_state = TimeState::Ntp;
  rtc_set_from_time(tv->tv_sec);
  ESP_LOGI(TAG, "NTP time synchronized and saved to RTC");
}

void start_sntp() {
  if (sntp_started) return;
  sntp_started = true;
  setenv("TZ", "PST8PDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
  esp_sntp_init();
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *) {
  static int retry = 0;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_connected = false;
    if (retry++ < 8) {
      esp_wifi_connect();
    } else {
      retry = 0;
      xEventGroupSetBits(wifi_events, WIFI_FAILED_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    retry = 0;
    wifi_connected = true;
    xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    start_sntp();
  }
}

esp_err_t setup_get_handler(httpd_req_t *req) {
  std::string saved_ssid;
  nvs_get_string(NVS_WIFI, WIFI_SSID_KEY, saved_ssid);
  std::string page =
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>TarotBot Wi-Fi</title><style>body{background:#000;color:#00ff41;font-family:monospace;"
      "max-width:520px;margin:32px auto;padding:0 18px}input,button{box-sizing:border-box;width:100%;"
      "padding:12px;margin:8px 0;background:#031107;color:#00ff41;border:1px solid #00ff41}"
      "button{background:#00ff41;color:#000;font-weight:700}</style></head><body>"
      "<h1>&gt;TAROTBOT_WIFI</h1><p>Enter the Wi-Fi network this watch should use for time sync.</p>"
      "<form method='post' action='/save'><label>Network name</label><input name='ssid' required value='" +
      html_escape(saved_ssid) +
      "'><label>Password</label><input name='pass' type='password'><button>Save and restart</button></form>"
      "<p>After saving, reconnect your phone/computer to your normal Wi-Fi.</p></body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page.c_str(), page.size());
}

void restart_task(void *) {
  vTaskDelay(pdMS_TO_TICKS(1200));
  esp_restart();
}

esp_err_t setup_post_handler(httpd_req_t *req) {
  std::string body;
  body.resize(req->content_len);
  int received = 0;
  while (received < req->content_len) {
    int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
    if (ret <= 0) return ESP_FAIL;
    received += ret;
  }
  std::string ssid = form_value(body, "ssid");
  std::string pass = form_value(body, "pass");
  if (ssid.empty() || ssid.size() > 32 || pass.size() > 64) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi details");
    return ESP_OK;
  }
  nvs_set_string(NVS_WIFI, WIFI_SSID_KEY, ssid);
  nvs_set_string(NVS_WIFI, WIFI_PASS_KEY, pass);
  const char *reply = "Saved. TarotBot is restarting now.";
  httpd_resp_send(req, reply, HTTPD_RESP_USE_STRLEN);
  xTaskCreate(restart_task, "restart", 2048, nullptr, 1, nullptr);
  return ESP_OK;
}

void start_setup_portal() {
  setup_portal_running = true;
  wifi_config_t ap_cfg {};
  strcpy(reinterpret_cast<char *>(ap_cfg.ap.ssid), "TarotBot-Setup");
  strcpy(reinterpret_cast<char *>(ap_cfg.ap.password), "arcana78");
  ap_cfg.ap.ssid_len = strlen("TarotBot-Setup");
  ap_cfg.ap.channel = 6;
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  httpd_config_t server_cfg = HTTPD_DEFAULT_CONFIG();
  server_cfg.lru_purge_enable = true;
  httpd_handle_t server = nullptr;
  ESP_ERROR_CHECK(httpd_start(&server, &server_cfg));

  httpd_uri_t root {};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = setup_get_handler;
  httpd_register_uri_handler(server, &root);

  httpd_uri_t save {};
  save.uri = "/save";
  save.method = HTTP_POST;
  save.handler = setup_post_handler;
  httpd_register_uri_handler(server, &save);

  ESP_LOGW(TAG, "Wi-Fi setup portal: SSID TarotBot-Setup, password arcana78, http://192.168.4.1");
}

void init_wifi() {
  wifi_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

  std::string ssid;
  std::string pass;
  bool has_wifi = nvs_get_string(NVS_WIFI, WIFI_SSID_KEY, ssid);
  nvs_get_string(NVS_WIFI, WIFI_PASS_KEY, pass);
  if (!has_wifi) {
    start_setup_portal();
    return;
  }

  wifi_config_t sta_cfg {};
  snprintf(reinterpret_cast<char *>(sta_cfg.sta.ssid), sizeof(sta_cfg.sta.ssid), "%s", ssid.c_str());
  snprintf(reinterpret_cast<char *>(sta_cfg.sta.password), sizeof(sta_cfg.sta.password), "%s", pass.c_str());
  sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  sta_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                         pdFALSE, pdFALSE, pdMS_TO_TICKS(18000));
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Wi-Fi connected");
  } else {
    ESP_LOGW(TAG, "Saved Wi-Fi did not connect; starting setup portal");
    start_setup_portal();
  }
}

void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

}  // namespace

extern "C" void app_main(void) {
  setenv("TZ", "PST8PDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
  init_nvs();
  load_deck();
  interpretations_enabled = nvs_get_bool(NVS_APP, INTERPRETATIONS_KEY, true);
  load_manual_time_from_system();

  lv_display_t *display = bsp_display_start();
  if (!display) {
    ESP_LOGE(TAG, "Display failed to start");
    return;
  }
  bsp_display_brightness_set(ACTIVE_BRIGHTNESS);
  init_rtc();

  if (bsp_display_lock(0)) {
    create_ui();
    bsp_display_unlock();
  }

  last_activity_ms = now_ms();
  xTaskCreate(button_task, "button", 3072, nullptr, 4, nullptr);
  init_wifi();
}
