# TarotBot for ESP32-C6-LCD-1.47

A pocket-sized version of [daniflux/tarotbot](https://github.com/daniflux/tarotbot) for the
Waveshare-compatible ESP32-C6 board with a 172x320 ST7789 display.

The firmware contains the original 78-card Emoji Deck, including its names, meanings,
and interpretations. Color emoji are generated as embedded 48x48 bitmaps from
[Twemoji](https://github.com/jdecked/twemoji) artwork, licensed under CC-BY 4.0.

## Controls

- Tap **BOOT**: draw, reveal the emoji/keywords, open the full reading, or continue.
- Hold **BOOT** for 1.5 seconds: reshuffle the full deck.
- After 60 seconds idle, a dim twinkling-star screen saver starts. The first tap
  wakes the previous screen without also advancing it.
- The remaining deck is saved in flash and survives power loss.

Long readings automatically continue onto a second page.

The LCD backlight is limited to about 38% for sustained use, following the board
maker's recommendation to remain below 50%. The onboard RGB LED is off during the
screen saver; while awake it glows purple between readings and samples a dominant
color from the displayed emoji card.

## Picking this project up on another computer

Read [HANDOFF.md](HANDOFF.md) first. It records the exact board, battery and power
parts, the planned soldering and meter-test sequence, safety constraints, and the
remaining enclosure and firmware ideas.

## Build and upload

```powershell
py -m platformio run
py -m platformio run --target upload
py -m platformio device monitor
```

The project is configured for `COM3`. Change `upload_port` and `monitor_port` in
`platformio.ini` if Windows assigns another port.
