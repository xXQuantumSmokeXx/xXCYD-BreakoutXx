#pragma once

// ── Hardware version ──────────────────────────────────────────────────────
#ifndef CYD_USB_VERSION
#define CYD_USB_VERSION  1
#endif

// ── Display (ILI9341 on HSPI) ─────────────────────────────────────────────
#define TFT_MOSI   13
#define TFT_MISO   12
#define TFT_SCLK   14
#define TFT_CS     15
#define TFT_DC      2
#define TFT_RST    -1
#define TFT_BL     21

// ── Touch (XPT2046 on VSPI) ───────────────────────────────────────────────
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25
#define TOUCH_X_MIN  300
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX 3800

// ── Screen (landscape, rotation 1) ────────────────────────────────────────
#define SCREEN_W   320
#define SCREEN_H   240

// ── Colors (RGB565) ───────────────────────────────────────────────────────
#define COL_BG          0x0000
#define COL_PLAYFIELD   0x1082   // deep navy — nice contrast for bricks
#define COL_WHITE       0xFFFF
#define COL_BLACK       0x0000
#define COL_RED         0xF800
#define COL_GREEN       0x07E0
#define COL_CYAN        0x07FF
#define COL_ORANGE      0xFD00
#define COL_GOLD        0xFDA0
#define COL_DIM_GRAY    0x4208
#define COL_LIGHT_GRAY  0xC618
#define COL_AMBER       0xFD40u
#define COL_PALE_YELLOW 0xFFEF

// ── Layout ────────────────────────────────────────────────────────────────
#define TOP_BAR_H       22
#define BOTTOM_BAR_Y    218
#define BOTTOM_BAR_H    22

#define PLAYFIELD_Y0    TOP_BAR_H
#define PLAYFIELD_Y1    BOTTOM_BAR_Y

// ── Bricks ────────────────────────────────────────────────────────────────
#define BRICK_COLS      8
#define BRICK_ROWS      5
#define BRICK_W         38
#define BRICK_H         12
#define BRICK_GAP       2
#define BRICK_X0        1       // centered: (320 - (8*38 + 7*2)) / 2 = 1
#define BRICK_Y0        26

#define SPECIAL_BRICK_MIN 4    // types 4+ are special (gold/silver)

// ── Paddle ────────────────────────────────────────────────────────────────
#define PADDLE_W        48
#define PADDLE_H        8
#define PADDLE_Y        208
#define PADDLE_WIDE_W   96
#define PADDLE_SPEED    8       // px per frame — snappy response

// ── Ball ──────────────────────────────────────────────────────────────────
#define BALL_R          2
#define MAX_BALLS       5
#define BALL_INITIAL_SPEED  3.2f
#define MAX_BALL_SPEED      6.0f
#define SPEED_INCREMENT     0.05f
#define SP_MIN_DY           0.3f

// ── Power-ups ─────────────────────────────────────────────────────────────
#define POWERUP_SIZE        8
#define POWERUP_FALL_SPEED  1
#define POWERUP_CHANCE_PCT  12
#define WIDE_DURATION       15000
#define SLOW_DURATION       10000

// ── Scoring ───────────────────────────────────────────────────────────────
#define BRICK_SCORE     10
#define SPECIAL_SCORE   50

// ── Power button ──────────────────────────────────────────────────────────
#define PWR_BTN_X       306
#define PWR_BTN_Y       10
#define PWR_BTN_R       7

// ── NVS keys ──────────────────────────────────────────────────────────────
#define NVS_NS          "cyd-breakout"
#define NVS_KEY_THEME   "theme"
#define NVS_KEY_HIGH    "high"
#define NVS_KEY_CAL     "cal_ver"
#define NVS_KEY_MADCTL  "madctl"
#define CURRENT_CAL_VER 2
