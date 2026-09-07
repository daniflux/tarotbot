# TarotBot for ESP32-S3 AMOLED Watch

Standalone TarotBot firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06 watch board.

This is separate from the older `esp32/` ESP32-C6-LCD-1.47 build. The watch has a different 410x502 AMOLED screen, touch controller, RTC, power chip, and flash layout, so it gets its own firmware folder.

## What It Does

- Boots straight into TarotBot.
- Uses the Arcana Terminal look from the web app's `emoji-matrix` theme.
- Draws through the full 78-card Emoji Deck without repeats.
- Saves the remaining deck in flash so power loss does not reshuffle unless you hold to reshuffle.
- Taps advance through draw, reveal, reading, and next card.
- Holding the screen or BOOT button reshuffles the full deck.
- After 60 seconds idle, the AMOLED display dims into a terminal-style low-light screen. The first tap wakes only; it does not accidentally advance the reading.
- Uses Wi-Fi NTP to sync time and stores the result in the onboard PCF85063 RTC.
- If Wi-Fi is unavailable, it falls back to RTC time. If no valid time is known, it displays `TIME UNSYNCED`.

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
