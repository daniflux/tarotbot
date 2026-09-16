# TarotBot ESP32 home-project handoff

This file is the context handoff for continuing the physical TarotBot on the home
PC. Read it before changing the firmware or assembling the battery circuit.

## Current state

- Target: Waveshare-compatible **ESP32-C6-LCD-1.47**, 172x320 ST7789 display.
- The firmware has been built, flashed, and tested on the physical device over USB.
- It contains the complete 78-card Emoji Deck with names, meanings, descriptions,
  reversed readings, and embedded 48x48 color emoji artwork.
- BOOT advances through draw, reveal, and reading screens. Holding BOOT for 1.5
  seconds reshuffles the deck.
- The remaining deck survives power loss.
- After 60 seconds, a dim twinkling-star screen saver starts. The first tap wakes
  the prior screen without advancing it.
- The RGB LED is off during the screen saver. While awake, it is purple between
  cards and uses a loose dominant-color match for the current emoji card.
- LCD brightness is capped at roughly 38% because the board maker recommends
  remaining below 50% for sustained use.

The current screen saver is visual only: it does not put the ESP32 into deep sleep.
Real sleep and wake support would be a valuable later battery-life improvement.

## Hardware already ordered

From Adafruit:

- Lithium Ion Polymer Battery, 3.7V 350mAh, protected, JST-PH — PID 2750
- Micro-Lipo Charger with USB-C, 100mA default charge rate — PID 4410
- MiniBoost 5V @ 1A, TPS61023 — PID 4654
- 2m 30AWG silicone stranded wire, red — PID 2001
- 2m 30AWG silicone stranded wire, black — PID 2003
- Multi-colored heat-shrink pack — PID 1649

From Amazon:

- YIHUA 926 III 60W temperature-controlled soldering station kit
- Basic digital multimeter with DC voltage, resistance, and continuity
- No-clean liquid flux pen
- 22–30AWG wire stripper/cutter
- Mini SS12F15-style SPDT two-position slide switches

The three-pin SPDT switch is used as an on/off switch with only the center pin and
one outside pin. The remaining outside pin is unused.

## Intended power circuit

The ESP32 board has no battery charger and must **not** be connected directly to a
LiPo. A fully charged one-cell LiPo reaches 4.2V, above the ESP32's 3.3V rail.

```text
Adafruit 350mAh LiPo
        │ JST-PH plug
        ▼
Adafruit Micro-Lipo USB-C charger
        VBAT ── SPDT switch ── MiniBoost IN
        GND  ───────────────── MiniBoost GND
                                  │
                                  ├── 5V OUT ── ESP32 5V
                                  └── GND ───── ESP32 GND
```

On the ESP32's left header row when viewed from the rear with USB-C at the top, the
top hole is **5V** and the hole immediately below is **GND**. Verify the silkscreen
and board orientation again before soldering.

The MiniBoost normally produces about 5.2V; that is intentional and suitable for
the board's 5V input. Do not connect it to the 3V3 hole.

The charger has no true load sharing. Turn the TarotBot off while charging. Charge
by connecting USB-C to the **charger**, not by connecting both the powered battery
circuit and the ESP32 USB port simultaneously.

## Assembly plan

Do not solder immediately when the parts arrive. First photograph both sides of
the charger, boost board, switch, and ESP32 so their actual pad markings can be
confirmed.

1. Watch SparkFun's beginner video:
   <https://www.youtube.com/watch?v=f95i88OSWB4>
2. Work on a nonflammable, ventilated surface with eye protection. Keep the LiPo
   away from the hot iron.
3. Put the multimeter's black lead in COM and red lead in V/ohm/mA. For battery and
   converter tests, select the 20V DC range. Never use a current socket or current
   range for these voltage checks.
4. Verify the battery connector reads approximately +3.0V to +4.2V with red probe
   on the red-wire terminal and black probe on the black-wire terminal.
5. Set the iron around 370 C for the included lead-free solder. Tin and clean the
   tip, then practice stripping and tinning scrap wire ends.
6. With the battery and all USB cables disconnected, solder charger VBAT through
   the switch to MiniBoost IN. Solder charger GND to MiniBoost GND.
7. Use the center and one outside switch terminal. Cover exposed inline joints
   with heat-shrink. Never solder directly to the battery pouch or battery leads.
8. Inspect for loose strands and bridges. Continuity between power and ground must
   not produce a sustained beep.
9. Before attaching the ESP32, plug the battery into the charger, turn the switch
   on, and measure MiniBoost OUT-to-GND. Expect positive output near 5V/5.2V.
10. Switch off and disconnect the battery. Solder MiniBoost OUT to ESP32 5V and
    MiniBoost GND to ESP32 GND. Inspect again, reconnect the battery, and power on.
11. To charge, switch the TarotBot off and connect USB-C to the Micro-Lipo charger.
    Leave its charge-current jumper **open** at the safe 100mA default.

Stop immediately if the battery swells, smells unusual, is damaged, or becomes
hot. The ESP32/display electronics may become mildly warm in normal use; the LiPo
itself should remain cool. Do not puncture, bend, crush, or tightly clamp the cell.

## Building and flashing at home

Install Git, a current Python 3, Node.js, and PlatformIO. Then:

```powershell
git clone https://github.com/daniflux/tarotbot.git
cd tarotbot\esp32
py -m pip install platformio
py -m platformio run
py -m platformio run --target upload
```

`cards_generated.h` is committed, so Node is not needed for an ordinary firmware
build. To regenerate emoji assets after changing the web deck or generator:

```powershell
npm ci
npm run generate-deck
```

Windows may assign a port other than COM3. Update `upload_port` and `monitor_port`
in `platformio.ini`, or run `py -m platformio device list` to find it.

## Enclosure direction

The board PCB is approximately 36.37 x 20.32mm and about 7.6mm thick. The Adafruit
battery is approximately 36 x 19.6 x 5.2mm, making a stacked watch/pendant layout
plausible. Before modeling or printing, measure the assembled stack with calipers.

Desired enclosure qualities:

- Pendant or chunky-watch form, with a comfortable external BOOT-button actuator
- No pressure on the LiPo pouch and some separation from warm electronics
- Access to the charger USB-C port and the physical power switch
- Strain relief for every soldered wire
- No exposed conductors or board edges
- Enough serviceability to inspect or replace the battery

Do not seal the first prototype permanently. A two-piece screw-fastened or snap-fit
case is preferable while the hardware and thermal behavior are still being tested.

## Good next software tasks

1. Measure real battery runtime and temperature in normal use.
2. Add genuine low-power sleep while preserving the starry aesthetic and reliable
   BOOT wake behavior.
3. Add battery-voltage monitoring only after confirming an appropriate ADC divider
   circuit; never connect the LiPo directly to an ESP32 ADC pin.
4. Tune LED/card color mappings after testing them on the physical diffuser.
5. Revisit text sizing once the final enclosure viewing distance is known.
