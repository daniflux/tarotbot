# TarotBot for ESP32-S3 AMOLED Watch

Standalone TarotBot firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06 watch board.

This is separate from the older `esp32/` ESP32-C6-LCD-1.47 build. The watch has a different 410x502 AMOLED screen, touch controller, RTC, power chip, and flash layout, so it gets its own firmware folder.

## What It Does

- Boots straight into TarotBot.
- Uses the Arcana Terminal look from the web app's `emoji-matrix` theme, with black AMOLED background, green terminal text, and code-rain accents.
- Draws through the full 78-card deck without repeats.
- Shows firmware-drawn green/black tarot sigils instead of the old emoji card art.
- Saves the remaining deck in flash so power loss does not reshuffle unless you hold to reshuffle.
- Taps advance through draw, reveal, reading, and next card. If interpretations are disabled, tapping after a reveal skips straight to the next card.
- Holding the screen or BOOT button reshuffles the full deck.
- Swipe left from TarotBot to open the Matrix tools hub; tap the gear/settings tile for settings; swipe right or use the BOOT button to back out.
- Settings lets you turn interpretations on/off and set the clock manually with touch steppers.
- After 60 seconds idle, the AMOLED display dims into a screen-wide code-rain clock. The first tap wakes only; it does not accidentally advance the reading.
- Uses Wi-Fi NTP to sync time and stores the result in the onboard PCF85063 RTC.
- If Wi-Fi is unavailable, it falls back to RTC time. If no valid time is known, it displays `TIME UNSYNCED`.

## Watch Controls

- Tap: advance TarotBot, open the highlighted tools/settings area, or adjust a settings row.
- Hold: reshuffle the TarotBot deck. Release after holding is ignored briefly so it does not double-count as a tap.
- Swipe left: TarotBot to tools hub.
- Swipe right: settings to tools hub, or tools hub back to TarotBot.
- BOOT button short press: same as tap in TarotBot; backs out from settings/tools.
- BOOT button hold: reshuffle.

## Wi-Fi Setup

If the watch has no saved Wi-Fi, or the saved Wi-Fi does not connect, it starts a setup hotspot:

- Network: `TarotBot-Setup`
- Password: `arcana78`
- Setup page: `http://192.168.4.1`

Connect your phone or computer to that temporary network, open the setup page, enter your normal Wi-Fi, and save. The watch restarts and tries to sync time.

## Build

Install ESP-IDF 5.5.x or 6.0.x first. Then from this folder:

```powershell
idf.py set-target esp32s3
idf.py build
```

The first build can be slow because ESP-IDF downloads the Waveshare board support package and LVGL.

## Flash Over USB

Windows currently detected the watch as `COM7` on this PC.

```powershell
idf.py -p COM7 flash monitor
```

If the port changes, check Device Manager under **Ports (COM & LPT)** and replace `COM7`.

If flashing hangs or says it is waiting for sync:

1. Close any serial monitor using the port.
2. Fully power the watch off.
3. Hold **BOOT**.
4. Power it on while still holding **BOOT**.
5. Flash again.
6. After flashing, power-cycle the watch. Waveshare notes the board may stay in download mode until restarted.

Waveshare says this board is intended to be flashed over USB/COM. The microSD slot is for storage/media examples, not the normal way to install firmware.

## Factory Recovery

The original Waveshare demo firmware can be restored from their official firmware packages:

- https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
- https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06

Use the same USB/COM flashing path. Their release packages include `flash.bat COMx` helpers and a combined image at flash offset `0x0`.

## Codespaces Note

The older ESP32-C6 firmware came from PR #6 and is not merged into `main` at the time this was added. If you are working in Codespaces, make sure it has this branch or the final merged branch checked out before looking for `esp32-watch/`.
