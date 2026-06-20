# xXCYD-BreakoutXx

Classic brick-breaker for the ESP32 CYD (Cheap Yellow Display) — touch-slider paddle, themed bricks, power-ups, and persistent high score.

[![Support on Patreon](https://img.shields.io/badge/Support-Patreon-orange)](https://www.patreon.com/c/xXQuantumSmokeXx)

## Screens

| Level 1 | Level 2 |
|----------|--------|
| ![Breakout](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-BreakoutXx/v1.0.0/ScreenShots/Breakout.png) | ![Breakout2](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-BreakoutXx/v1.0.0/ScreenShots/Breakout2.png) |

## Features

- Touch-slider paddle — slide your finger anywhere on screen to move
- 8×5 brick grid (expands to 7 rows at higher levels)
- 9 theme accent colors — tap the bottom-right ring to cycle
- Brightness control — tap bottom-left ring for 5 brightness levels
- Power-ups falling from broken bricks: Multi-ball, Wide Paddle, Slow Ball
- Ball speed ramps up per brick hit
- Level progression with more rows and special gold/silver bricks
- Persistent high score saved to NVS
- Power button (top-right) — tap for deep sleep, touch screen to wake


## Setup

| Board | Firmware File |
|-------|--------------|
| **ESP32-32E** (1-USB) | `CYD-Breakout-1usb.bin` |
| **2USB** (all variants) | `CYD-Breakout-2usb.bin` |

Merged flash images — bootloader + partition table + firmware combined. Flash at offset `0x00`:

```bash
esptool.py --chip esp32 write_flash 0x0 CYD-Breakout-1usb.bin
esptool.py --chip esp32 write_flash 0x0 CYD-Breakout-2usb.bin
```

Or via M5Launcher: copy the `.bin` onto a micro SD card (FAT32), launch [M5Launcher](https://github.com/bmorcelli/M5Launcher), select the firmware, and flash.

## First Boot (2USB)

Two calibration screens appear on first boot — display orientation (tap to cycle, hold 2s to confirm) and touch calibration (tap to cycle, hold 2s when cursor follows finger). Settings persist in NVS. Re-run anytime via serial: send `M` for display, `T` for touch.

## Build

```bash
# 1-USB
pio run --environment cyd_breakout

# 2USB
pio run --environment cyd_breakout_2usb
```

Merged `.bin` files are auto-generated at the project root.


Device responds to `R` (ready check) and `S` (capture) over serial at 115200 baud.

## Credits

Built by xXQuantum-SmokeXx with development assistance from Claude Code.

Calibration system and theming ported from [xXCYD-PokerXx](https://github.com/xXQuantumSmokeXx/xXCYD-PokerXx).
