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
#include "lvgl.h"

namespace {

constexpr char TAG[] = "TarotBotWatch";
constexpr char NVS_APP[] = "tarotbot";
constexpr char NVS_WIFI[] = "wifi";
constexpr char WIFI_SSID_KEY[] = "ssid";
constexpr char WIFI_PASS_KEY[] = "pass";
constexpr gpio_num_t BOOT_BUTTON = GPIO_NUM_0;
constexpr uint32_t IDLE_SAVER_MS = 60000;
constexpr uint32_t LONG_PRESS_MS = 1500;
constexpr int ACTIVE_BRIGHTNESS = 35;
constexpr int SAVER_BRIGHTNESS = 4;
constexpr uint8_t DECK_BYTES = (CARD_COUNT + 7) / 8;
constexpr uint8_t RTC_ADDR = 0x51;

const lv_color_t MATRIX = lv_color_hex(0x00ff41);
const lv_color_t MATRIX_SOFT = lv_color_hex(0x35a953);
const lv_color_t MATRIX_DIM = lv_color_hex(0x0a3d18);
const lv_color_t BLACK = lv_color_hex(0x000000);
const lv_color_t PANEL = lv_color_hex(0x031107);

enum class ScreenState { Welcome, CardBack, Revealed, Reading, Empty };
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
  lv_obj_t *card_image = nullptr;
  lv_obj_t *body = nullptr;
  lv_obj_t *footer = nullptr;
  lv_obj_t *touch_layer = nullptr;
  std::array<lv_obj_t *, 9> rain{};
};

Ui ui;
ScreenState screen_state = ScreenState::Welcome;
TimeState time_state = TimeState::Unsynced;
std::array<uint8_t, DECK_BYTES> available_cards{};
int current_card = -1;
size_t reading_offset = 0;
size_t reading_next_offset = 0;
bool reading_has_more = false;
bool saver_active = false;
bool wake_only = false;
uint64_t last_activity_ms = 0;
uint64_t button_down_ms = 0;
bool button_was_down = false;
bool button_long_handled = false;
bool wifi_connected = false;
bool setup_portal_running = false;
bool sntp_started = false;
EventGroupHandle_t wifi_events = nullptr;
i2c_master_dev_handle_t rtc_dev = nullptr;
lv_image_dsc_t current_img{};

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
  strftime(buffer, sizeof(buffer), "%H:%M", &local);
  return buffer;
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

std::string time_status_text() {
  if (time_state == TimeState::Ntp) return "NTP SYNCED // " + date_text();
  if (time_state == TimeState::Rtc) return "RTC HOLD // " + date_text();
  if (setup_portal_running) return "SETUP: TAROTBOT-SETUP // 192.168.4.1";
  return "TIME UNSYNCED";
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

void set_card_image(int card_index) {
  if (card_index < 0) {
    lv_obj_add_flag(ui.card_image, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  current_img = {};
  current_img.header.w = EMOJI_SIZE;
  current_img.header.h = EMOJI_SIZE;
  current_img.header.cf = LV_COLOR_FORMAT_RGB565;
  current_img.data_size = EMOJI_SIZE * EMOJI_SIZE * 2;
  current_img.data = reinterpret_cast<const uint8_t *>(CARDS[card_index].emoji);
  lv_image_set_src(ui.card_image, &current_img);
  lv_obj_clear_flag(ui.card_image, LV_OBJ_FLAG_HIDDEN);
}

void update_time_labels() {
  set_text(ui.time, time_text());
  set_text(ui.status, time_status_text());
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
  set_text(ui.title, ">TAROTBOT_");
  set_text(ui.subtitle, "DREAMING IN LOW LIGHT");
  set_text(ui.card_name, "ARCANA TERMINAL");
  lv_obj_add_flag(ui.card_image, LV_OBJ_FLAG_HIDDEN);
  set_text(ui.body, "screen dimmed // touch once to wake");
  set_text(ui.footer, "FIRST TOUCH WAKES ONLY");
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
      reading_offset = 0;
      screen_state = ScreenState::Reading;
      render_screen();
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

void render_screen() {
  update_time_labels();
  lv_obj_clear_flag(ui.panel, LV_OBJ_FLAG_HIDDEN);

  char count[48];
  snprintf(count, sizeof(count), "%u OF %u CARDS REMAIN", cards_remaining(), CARD_COUNT);

  switch (screen_state) {
    case ScreenState::Welcome:
      set_text(ui.title, ">TAROTBOT_");
      set_text(ui.subtitle, "ARCANA // TERMINAL");
      set_text(ui.card_name, "ORACLE READY");
      set_card_image(-1);
      set_text(ui.body, "78-node entropy protocol online.\nNo duplicate returns until the deck is complete.");
      set_text(ui.footer, "TAP TO DRAW // HOLD TO SHUFFLE");
      break;
    case ScreenState::CardBack:
      set_text(ui.title, ">DRAW BUFFER");
      set_text(ui.subtitle, count);
      set_text(ui.card_name, "[ CARD FACE HIDDEN ]");
      set_card_image(-1);
      set_text(ui.body, "A card has been selected from the remaining deck.\nTap to reveal the signal.");
      set_text(ui.footer, "TAP TO REVEAL // HOLD TO SHUFFLE");
      break;
    case ScreenState::Revealed:
      set_text(ui.title, ">CARD REVEALED");
      set_text(ui.subtitle, count);
      set_text(ui.card_name, CARDS[current_card].name);
      set_card_image(current_card);
      set_text(ui.body, CARDS[current_card].meaning);
      set_text(ui.footer, "TAP FOR READING // HOLD TO SHUFFLE");
      break;
    case ScreenState::Reading:
      set_text(ui.title, ">READING");
      set_text(ui.subtitle, CARDS[current_card].name);
      set_text(ui.card_name, "INTERPRETATION");
      set_card_image(-1);
      reading_next_offset = page_text(CARDS[current_card].interpretation, reading_offset, 360, reading_has_more);
      set_text(ui.footer, reading_has_more ? "TAP FOR MORE // HOLD TO SHUFFLE" : "TAP NEXT CARD // HOLD TO SHUFFLE");
      break;
    case ScreenState::Empty:
      set_text(ui.title, ">DECK COMPLETE");
      set_text(ui.subtitle, "ALL 78 CARDS WALKED");
      set_text(ui.card_name, "RESHUFFLE REQUIRED");
      set_card_image(-1);
      set_text(ui.body, "The full deck has passed through the terminal.\nHold to reseed the oracle.");
      set_text(ui.footer, "HOLD TO SHUFFLE");
      break;
  }
}

void touch_event_cb(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED) {
    advance();
  } else if (code == LV_EVENT_LONG_PRESSED) {
    touch_activity();
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
  static const char *glyphs[] = {"01", "78", "XI", "IV", "THE", "ARC", "RUN", "SIG", "NTP"};
  for (size_t i = 0; i < ui.rain.size(); ++i) {
    const char *text = glyphs[(frame + i * 3) % (sizeof(glyphs) / sizeof(glyphs[0]))];
    lv_label_set_text(ui.rain[i], text);
    lv_obj_set_y(ui.rain[i], (lv_obj_get_y(ui.rain[i]) + 9 + i) % 502);
  }
}

void create_ui() {
  ui.root = lv_screen_active();
  lv_obj_remove_style_all(ui.root);
  lv_obj_set_style_bg_color(ui.root, BLACK, 0);
  lv_obj_set_style_bg_opa(ui.root, LV_OPA_COVER, 0);

  for (size_t i = 0; i < ui.rain.size(); ++i) {
    ui.rain[i] = lv_label_create(ui.root);
    lv_label_set_text(ui.rain[i], "01");
    style_label(ui.rain[i], &lv_font_montserrat_14, MATRIX_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(ui.rain[i], 18 + i * 44, (i * 57) % 470);
    lv_obj_set_style_text_opa(ui.rain[i], static_cast<lv_opa_t>(45 + (i % 4) * 22), 0);
  }

  ui.time = lv_label_create(ui.root);
  style_label(ui.time, &lv_font_montserrat_24, MATRIX, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(ui.time, 20, 12);
  lv_obj_set_size(ui.time, 120, 34);

  ui.status = lv_label_create(ui.root);
  style_label(ui.status, &lv_font_montserrat_12, MATRIX_SOFT, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_pos(ui.status, 145, 17);
  lv_obj_set_size(ui.status, 245, 42);

  ui.title = lv_label_create(ui.root);
  style_label(ui.title, &lv_font_montserrat_24, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.title, 20, 64);
  lv_obj_set_size(ui.title, 370, 34);

  ui.subtitle = lv_label_create(ui.root);
  style_label(ui.subtitle, &lv_font_montserrat_14, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.subtitle, 20, 101);
  lv_obj_set_size(ui.subtitle, 370, 38);

  ui.panel = lv_obj_create(ui.root);
  lv_obj_remove_style_all(ui.panel);
  lv_obj_set_style_bg_color(ui.panel, PANEL, 0);
  lv_obj_set_style_bg_opa(ui.panel, LV_OPA_90, 0);
  lv_obj_set_style_border_color(ui.panel, MATRIX_SOFT, 0);
  lv_obj_set_style_border_width(ui.panel, 2, 0);
  lv_obj_set_pos(ui.panel, 28, 145);
  lv_obj_set_size(ui.panel, 354, 270);

  ui.card_name = lv_label_create(ui.panel);
  style_label(ui.card_name, &lv_font_montserrat_20, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.card_name, 18, 18);
  lv_obj_set_size(ui.card_name, 318, 52);

  ui.card_image = lv_image_create(ui.panel);
  lv_obj_set_pos(ui.card_image, 153, 75);
  lv_obj_set_size(ui.card_image, EMOJI_SIZE, EMOJI_SIZE);

  ui.body = lv_label_create(ui.panel);
  style_label(ui.body, &lv_font_montserrat_16, MATRIX, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.body, 24, 137);
  lv_obj_set_size(ui.body, 306, 112);

  ui.footer = lv_label_create(ui.root);
  style_label(ui.footer, &lv_font_montserrat_14, MATRIX_SOFT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_pos(ui.footer, 20, 438);
  lv_obj_set_size(ui.footer, 370, 44);

  ui.touch_layer = lv_obj_create(ui.root);
  lv_obj_remove_style_all(ui.touch_layer);
  lv_obj_set_size(ui.touch_layer, 410, 502);
  lv_obj_set_pos(ui.touch_layer, 0, 0);
  lv_obj_add_flag(ui.touch_layer, LV_OBJ_FLAG_CLICKABLE);
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
        advance();
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
