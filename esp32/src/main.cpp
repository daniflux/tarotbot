#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <esp_system.h>
#include "FreeMono8pt7b.h"
#include "cards_generated.h"

// Waveshare-compatible ESP32-C6-LCD-1.47 pinout.
constexpr int LCD_MISO = 5;
constexpr int LCD_MOSI = 6;
constexpr int LCD_SCLK = 7;
constexpr int LCD_CS = 14;
constexpr int LCD_DC = 15;
constexpr int LCD_RST = 21;
constexpr int LCD_BL = 22;
constexpr int USER_BUTTON = 9; // BOOT button, active low after startup.
constexpr int RGB_LED = 8;     // Onboard WS2812.

// Waveshare recommends no more than 50% backlight for sustained use.
constexpr uint8_t ACTIVE_BACKLIGHT = 96; // 38% of 8-bit PWM.
constexpr uint8_t SAVER_BACKLIGHT = 24;  // 9% while the stars are showing.
constexpr uint32_t SCREEN_SAVER_AFTER_MS = 60000;

constexpr uint16_t GOLD = 0xFEC0;
constexpr uint16_t GOLD_DIM = 0xA4C0;
constexpr uint16_t INK = 0x18C3;
constexpr uint16_t NIGHT = 0x0842;
constexpr uint16_t PURPLE = 0x4010;
constexpr uint16_t VIOLET = 0x8018;
constexpr uint16_t CREAM = 0xFF9C;
constexpr uint16_t MUTED = 0xBDF7;

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *display = new Arduino_ST7789(
    bus, LCD_RST, 0, true, 172, 320, 34, 0, 34, 0);

Preferences preferences;

constexpr uint8_t DECK_BYTES = (CARD_COUNT + 7) / 8;

enum class ScreenState { WELCOME, CARD_BACK, REVEALED, READING, EMPTY };
ScreenState state = ScreenState::WELCOME;
uint8_t availableCards[DECK_BYTES];
int currentCard = -1;
int readingNextOffset = 0;
int readingPageOffset = 0;
bool readingHasMore = false;
uint32_t lastActivityAt = 0;
bool screenSaverActive = false;
bool wakeOnlyPress = false;

struct Star {
  int16_t x;
  int16_t y;
  uint8_t phase;
  uint8_t speed;
};
constexpr uint8_t STAR_COUNT = 30;
Star stars[STAR_COUNT];
uint32_t lastStarFrameAt = 0;

bool buttonWasDown = false;
bool longPressHandled = false;
uint32_t buttonDownAt = 0;

void setBacklight(uint8_t brightness) {
  ledcWrite(LCD_BL, brightness);
}

void setRgb(uint8_t red, uint8_t green, uint8_t blue) {
  // Keep this decorative LED subtle; full-power white is needlessly hot/bright.
  constexpr uint8_t MAX_CHANNEL = 34;
  uint8_t peak = max(red, max(green, blue));
  if (peak > MAX_CHANNEL) {
    red = static_cast<uint16_t>(red) * MAX_CHANNEL / peak;
    green = static_cast<uint16_t>(green) * MAX_CHANNEL / peak;
    blue = static_cast<uint16_t>(blue) * MAX_CHANNEL / peak;
  }
  rgbLedWrite(RGB_LED, red, green, blue);
}

void setAmbientLed() {
  setRgb(24, 8, 34);
}

void setCardLed() {
  if (currentCard < 0) {
    setAmbientLed();
    return;
  }

  // Find the strongest color family in the emoji, ignoring its cream backdrop.
  uint32_t red[12] = {}, green[12] = {}, blue[12] = {}, weights[12] = {};
  const uint16_t *pixels = CARDS[currentCard].emoji;
  for (int i = 0; i < EMOJI_SIZE * EMOJI_SIZE; ++i) {
    uint16_t pixel = pgm_read_word(pixels + i);
    uint8_t r = ((pixel >> 11) & 0x1f) * 255 / 31;
    uint8_t g = ((pixel >> 5) & 0x3f) * 255 / 63;
    uint8_t b = (pixel & 0x1f) * 255 / 31;
    uint8_t high = max(r, max(g, b));
    uint8_t low = min(r, min(g, b));
    uint8_t chroma = high - low;
    if (chroma < 36 || (r > 225 && g > 215 && b > 200)) continue;

    int hue;
    if (high == r) hue = 43 * (static_cast<int>(g) - b) / chroma;
    else if (high == g) hue = 85 + 43 * (static_cast<int>(b) - r) / chroma;
    else hue = 171 + 43 * (static_cast<int>(r) - g) / chroma;
    if (hue < 0) hue += 256;
    uint8_t bin = (static_cast<uint16_t>(hue) * 12) >> 8;
    uint16_t weight = chroma + high / 3;
    red[bin] += static_cast<uint32_t>(r) * weight;
    green[bin] += static_cast<uint32_t>(g) * weight;
    blue[bin] += static_cast<uint32_t>(b) * weight;
    weights[bin] += weight;
  }

  uint8_t best = 0;
  for (uint8_t i = 1; i < 12; ++i) {
    if (weights[i] > weights[best]) best = i;
  }
  if (!weights[best]) setAmbientLed();
  else setRgb(red[best] / weights[best], green[best] / weights[best],
              blue[best] / weights[best]);
}

void fillBackground() {
  display->fillScreen(NIGHT);
  for (int i = 0; i < 28; ++i) {
    int x = (i * 73 + 19) % display->width();
    int y = (i * 41 + 11) % display->height();
    display->drawPixel(x, y, (i % 3 == 0) ? GOLD : GOLD_DIM);
  }
}

void centerText(const char *text, int y, uint8_t size, uint16_t color) {
  display->setFont(nullptr);
  int width = static_cast<int>(strlen(text)) * 6 * size;
  int x = max(2, (display->width() - width) / 2);
  display->setTextSize(size);
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(text);
}

int wrappedText(const char *text, int x, int y, int maxWidth, uint8_t size,
                uint16_t color, int lineGap = 3) {
  display->setFont(nullptr);
  const int charWidth = 6 * size;
  const int lineHeight = 8 * size + lineGap;
  const int maxChars = max(4, maxWidth / charWidth);
  String remaining(text);
  remaining.trim();

  display->setTextSize(size);
  display->setTextColor(color);
  while (remaining.length()) {
    int take = min(maxChars, static_cast<int>(remaining.length()));
    if (take < static_cast<int>(remaining.length())) {
      int split = remaining.lastIndexOf(' ', take);
      if (split > 0) take = split;
    }
    String line = remaining.substring(0, take);
    line.trim();
    int lineWidth = line.length() * charWidth;
    display->setCursor(x + max(0, (maxWidth - lineWidth) / 2), y);
    display->print(line);
    y += lineHeight;
    remaining.remove(0, take);
    remaining.trim();
  }
  return y;
}

int drawFontPage(const char *text, int start, int x, int firstBaseline,
                 int maxWidth, int maxLines, uint16_t color, bool centered) {
  const int length = strlen(text);
  int pos = start;
  int baseline = firstBaseline;
  display->setFont(&FreeMono8pt7b);
  display->setTextSize(1);
  display->setTextColor(color);

  for (int lineNumber = 0; lineNumber < maxLines && pos < length; ++lineNumber) {
    while (pos < length && text[pos] == ' ') ++pos;
    String line;
    int scan = pos;
    int acceptedEnd = pos;

    while (scan < length) {
      int wordEnd = scan;
      while (wordEnd < length && text[wordEnd] != ' ') ++wordEnd;
      String word = String(text).substring(scan, wordEnd);
      String candidate = line.length() ? line + " " + word : word;
      int16_t x1, y1;
      uint16_t width, height;
      display->getTextBounds(candidate, 0, baseline, &x1, &y1, &width, &height);
      if (width > maxWidth && line.length()) break;
      line = candidate;
      scan = wordEnd;
      while (scan < length && text[scan] == ' ') ++scan;
      acceptedEnd = scan;
    }

    if (!line.length()) break;
    int16_t x1, y1;
    uint16_t width, height;
    display->getTextBounds(line, 0, baseline, &x1, &y1, &width, &height);
    int drawX = centered ? x + max(0, (maxWidth - static_cast<int>(width)) / 2) : x;
    display->setCursor(drawX, baseline);
    display->print(line);
    pos = acceptedEnd;
    baseline += 18;
  }
  display->setFont(nullptr);
  return pos;
}

bool isAvailable(uint8_t index) {
  return availableCards[index / 8] & (1U << (index % 8));
}

void setAvailable(uint8_t index, bool available) {
  uint8_t mask = 1U << (index % 8);
  if (available) availableCards[index / 8] |= mask;
  else availableCards[index / 8] &= ~mask;
}

void resetAvailableCards() {
  memset(availableCards, 0, sizeof(availableCards));
  for (uint8_t i = 0; i < CARD_COUNT; ++i) setAvailable(i, true);
}

uint8_t cardsRemaining() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < CARD_COUNT; ++i) {
    if (isAvailable(i)) ++count;
  }
  return count;
}

void drawFooter(const char *instruction) {
  display->drawFastHLine(12, display->height() - 27, display->width() - 24, GOLD_DIM);
  centerText(instruction, display->height() - 20, 1, MUTED);
}

void showWelcome() {
  fillBackground();
  centerText("TAROTBOT", 24, 3, GOLD);
  centerText("ONE CARD AT A TIME", 57, 1, MUTED);
  display->fillRoundRect(25, 88, display->width() - 50, 126, 12, PURPLE);
  display->drawRoundRect(25, 88, display->width() - 50, 126, 12, GOLD);
  centerText("*", 108, 5, GOLD);
  centerText("THE ORACLE", 166, 2, CREAM);
  centerText("IS READY", 190, 1, MUTED);
  centerText("78-CARD EMOJI DECK", 235, 1, GOLD);
  drawFooter("TAP BOOT TO DRAW");
  state = ScreenState::WELCOME;
  setAmbientLed();
}

void showCardBack() {
  fillBackground();
  centerText("YOUR CARD AWAITS", 21, 1, GOLD);
  display->fillRoundRect(35, 49, display->width() - 70, 186, 10, PURPLE);
  display->drawRoundRect(35, 49, display->width() - 70, 186, 10, GOLD);
  display->drawRoundRect(42, 56, display->width() - 84, 172, 8, GOLD_DIM);
  centerText("*", 86, 6, GOLD);
  centerText("?", 151, 5, CREAM);
  char count[24];
  snprintf(count, sizeof(count), "%u CARDS REMAIN", cardsRemaining());
  centerText(count, 253, 1, MUTED);
  drawFooter("TAP BOOT TO REVEAL");
  state = ScreenState::CARD_BACK;
  setAmbientLed();
}

void showRevealed() {
  const Card &card = CARDS[currentCard];
  fillBackground();
  display->fillRoundRect(9, 12, display->width() - 18, 267, 12, CREAM);
  display->drawRoundRect(9, 12, display->width() - 18, 267, 12, GOLD);
  centerText("YOUR CARD", 23, 1, VIOLET);
  wrappedText(card.name, 17, 42, display->width() - 34, 2, INK, 3);
  display->draw16bitRGBBitmap((display->width() - EMOJI_SIZE) / 2, 86,
                              card.emoji, EMOJI_SIZE, EMOJI_SIZE);
  display->drawFastHLine(27, 143, display->width() - 54, GOLD_DIM);
  drawFontPage(card.meaning, 0, 20, 165, display->width() - 40, 5, VIOLET, true);
  char count[24];
  snprintf(count, sizeof(count), "%u / %u REMAIN", cardsRemaining(), CARD_COUNT);
  centerText(count, 260, 1, VIOLET);
  drawFooter("TAP: READING   HOLD: SHUFFLE");
  state = ScreenState::REVEALED;
  setCardLed();
}

void showReading(int startOffset) {
  const Card &card = CARDS[currentCard];
  fillBackground();
  display->fillRoundRect(9, 12, display->width() - 18, 267, 12, CREAM);
  display->drawRoundRect(9, 12, display->width() - 18, 267, 12, GOLD);
  centerText(card.name, 25, 1, VIOLET);
  display->drawFastHLine(22, 43, display->width() - 44, GOLD_DIM);
  readingPageOffset = startOffset;
  readingNextOffset = drawFontPage(card.interpretation, startOffset, 19, 65,
                                   display->width() - 38, 11, INK, false);
  readingHasMore = readingNextOffset < static_cast<int>(strlen(card.interpretation));
  drawFooter(readingHasMore ? "TAP BOOT: MORE" : "TAP BOOT: NEXT CARD");
  state = ScreenState::READING;
  setCardLed();
}

void showEmpty() {
  fillBackground();
  centerText("THE DECK", 72, 3, GOLD);
  centerText("IS COMPLETE", 108, 2, CREAM);
  wrappedText("You have walked through all 78 cards in the deck.", 20, 156,
              display->width() - 40, 1, MUTED, 5);
  drawFooter("HOLD BOOT TO RESHUFFLE");
  state = ScreenState::EMPTY;
  setAmbientLed();
}

void redrawCurrentScreen() {
  switch (state) {
    case ScreenState::WELCOME: showWelcome(); break;
    case ScreenState::CARD_BACK: showCardBack(); break;
    case ScreenState::REVEALED: showRevealed(); break;
    case ScreenState::READING: showReading(readingPageOffset); break;
    case ScreenState::EMPTY: showEmpty(); break;
  }
}

void startScreenSaver() {
  screenSaverActive = true;
  setRgb(0, 0, 0);
  setBacklight(SAVER_BACKLIGHT);
  display->fillScreen(NIGHT);
  for (uint8_t i = 0; i < STAR_COUNT; ++i) {
    stars[i] = {static_cast<int16_t>(random(4, display->width() - 4)),
                static_cast<int16_t>(random(4, display->height() - 4)),
                static_cast<uint8_t>(random(256)),
                static_cast<uint8_t>(random(2, 7))};
  }
  lastStarFrameAt = 0;
  Serial.println("Screen saver started");
}

void animateScreenSaver() {
  uint32_t now = millis();
  if (now - lastStarFrameAt < 70) return;
  lastStarFrameAt = now;

  for (uint8_t i = 0; i < STAR_COUNT; ++i) {
    Star &star = stars[i];
    display->drawPixel(star.x, star.y, NIGHT);
    display->drawPixel(star.x - 1, star.y, NIGHT);
    display->drawPixel(star.x + 1, star.y, NIGHT);
    display->drawPixel(star.x, star.y - 1, NIGHT);
    display->drawPixel(star.x, star.y + 1, NIGHT);

    star.phase += star.speed;
    uint8_t glow = star.phase < 128 ? star.phase : 255 - star.phase;
    uint8_t warm = 80 + glow;
    uint8_t cool = 70 + glow * 3 / 4;
    uint16_t color = display->color565(warm, warm, cool);
    display->drawPixel(star.x, star.y, color);
    if (glow > 105) {
      display->drawPixel(star.x - 1, star.y, color);
      display->drawPixel(star.x + 1, star.y, color);
      display->drawPixel(star.x, star.y - 1, color);
      display->drawPixel(star.x, star.y + 1, color);
    }
  }
}

void wakeScreen() {
  screenSaverActive = false;
  setBacklight(ACTIVE_BACKLIGHT);
  redrawCurrentScreen();
  lastActivityAt = millis();
  Serial.println("Screen saver ended");
}

void saveDeck() {
  preferences.putBytes("deck78", availableCards, sizeof(availableCards));
}

void reshuffle() {
  resetAvailableCards();
  currentCard = -1;
  saveDeck();
  Serial.println("Deck reshuffled");
  showWelcome();
}

void chooseCard() {
  uint8_t remaining = cardsRemaining();
  if (!remaining) {
    showEmpty();
    return;
  }

  uint8_t target = random(remaining);
  for (uint8_t i = 0; i < CARD_COUNT; ++i) {
    if (isAvailable(i)) {
      if (target == 0) {
        currentCard = i;
        break;
      }
      --target;
    }
  }
  Serial.printf("Card selected: %s\n", CARDS[currentCard].name);
  showCardBack();
}

void revealCard() {
  if (currentCard < 0) return;
  setAvailable(currentCard, false);
  saveDeck();
  Serial.printf("Card revealed: %s (%u remain)\n", CARDS[currentCard].name,
                cardsRemaining());
  showRevealed();
}

void handleTap() {
  lastActivityAt = millis();
  switch (state) {
    case ScreenState::WELCOME:
      chooseCard();
      break;
    case ScreenState::CARD_BACK:
      revealCard();
      break;
    case ScreenState::REVEALED:
      showReading(0);
      break;
    case ScreenState::READING:
      if (readingHasMore) showReading(readingNextOffset);
      else chooseCard();
      break;
    case ScreenState::EMPTY:
      break;
  }
}

void pollButton() {
  bool down = digitalRead(USER_BUTTON) == LOW;
  uint32_t now = millis();

  if (down && !buttonWasDown) {
    buttonDownAt = now;
    longPressHandled = false;
    wakeOnlyPress = screenSaverActive;
    if (screenSaverActive) wakeScreen();
  }

  if (down && !wakeOnlyPress && !longPressHandled && now - buttonDownAt >= 1500) {
    longPressHandled = true;
    reshuffle();
  }

  if (!down && buttonWasDown && !wakeOnlyPress && !longPressHandled && now - buttonDownAt >= 35) {
    handleTap();
  }
  if (!down) wakeOnlyPress = false;
  buttonWasDown = down;
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("\nTarotBot ESP32-C6 starting...");

  pinMode(USER_BUTTON, INPUT_PULLUP);
  ledcAttach(LCD_BL, 5000, 8);
  setBacklight(ACTIVE_BACKLIGHT);
  setRgb(0, 0, 0);

  if (!display->begin(40000000)) {
    Serial.println("Display initialization failed");
  }
  display->setRotation(0);

  preferences.begin("tarotbot", false);
  if (preferences.getBytesLength("deck78") == sizeof(availableCards)) {
    preferences.getBytes("deck78", availableCards, sizeof(availableCards));
  } else {
    resetAvailableCards();
    saveDeck();
  }
  randomSeed(esp_random());

  Serial.printf("Display: %dx%d, cards remaining: %u\n", display->width(),
                display->height(), cardsRemaining());
  if (cardsRemaining()) showWelcome();
  else showEmpty();
  lastActivityAt = millis();
}

void loop() {
  pollButton();
  if (screenSaverActive) animateScreenSaver();
  else if (!buttonWasDown && millis() - lastActivityAt >= SCREEN_SAVER_AFTER_MS) {
    startScreenSaver();
  }
  delay(10);
}
