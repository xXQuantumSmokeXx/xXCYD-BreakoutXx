#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <cmath>
#include "config.h"
#include "theme.h"

// ── Global objects ────────────────────────────────────────────────────────
TFT_eSPI       disp = TFT_eSPI();
SPIClass       touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// ── State machine ─────────────────────────────────────────────────────────
enum GameState : uint8_t {
    STATE_IDLE            = 0,
    STATE_PLAYING_READY   = 1,
    STATE_PLAYING_BOUNCE  = 2,
    STATE_LEVEL_COMPLETE  = 3,
    STATE_GAME_OVER       = 4,
};
static GameState g_state = STATE_IDLE;

// ── Ball ──────────────────────────────────────────────────────────────────
struct Ball {
    float  x, y;
    float  dx, dy;
    float  speed;
    bool   active;
};
static Ball g_balls[MAX_BALLS];

// ── Bricks ────────────────────────────────────────────────────────────────
static uint8_t g_bricks[BRICK_COLS][BRICK_ROWS + 2];  // +2 for extra rows at high levels
static int     g_brickRows = BRICK_ROWS;

// ── Paddle ────────────────────────────────────────────────────────────────
static int16_t g_paddleX = 0;
static int16_t g_paddleTargetX = 0;
static int16_t g_oldPaddleX = 0;
static int     g_oldPaddleW = PADDLE_W;

// ── Dirty tracking ────────────────────────────────────────────────────────
static bool g_bricksDirty = true;
static int  g_oldBallX[MAX_BALLS], g_oldBallY[MAX_BALLS];
static unsigned long g_lastScore = 0;
static int  g_lastLives = 0, g_lastLevel = 0, g_lastTheme = 0;

// ── Power-ups ─────────────────────────────────────────────────────────────
struct FallingPU {
    int16_t x, y;
    uint8_t type;   // 0=multiball, 1=wide, 2=slow
    bool    alive;
};
static FallingPU    g_fallingPUs[3];
static unsigned long g_puExpire[3] = {0, 0, 0};  // indexed by type

// ── Game data ─────────────────────────────────────────────────────────────
static int           g_lives = 3;
static unsigned long g_score = 0;
static unsigned long g_highScore = 0;
static int           g_level = 1;
static unsigned long g_levelCompleteTimer = 0;

// ── Brightness ────────────────────────────────────────────────────────────
static int g_brightness = 4;  // 0-4 brightness levels

static void setBrightness(int level);  // forward — uses nvsPut

// ── Touch ─────────────────────────────────────────────────────────────────
static int16_t g_tx = 0, g_ty = 0;
static bool    g_touched = false;
static bool    g_wasTouched = false;

// ── 2USB orientation / calibration ────────────────────────────────────────
#if CYD_USB_VERSION == 2
static uint8_t s_madctl = 0x80;
static int     s_touchRotation = 2;

static void applyOrientation() {
    disp.setRotation(1);
    disp.writecommand(0x36);   // TFT_MADCTL
    disp.writedata(s_madctl);
}
static uint8_t madctlForCombo(int idx) {
    switch (idx & 3) {
        case 0:  return 0x28;   // MV | BGR
        case 1:  return 0xA8;   // MV | MY | BGR
        case 2:  return 0x00;   // no swap, no mirror
        default: return 0x80;   // MY only
    }
}
static int madctlToCombo(uint8_t mc) {
    if (mc == 0x28) return 0;
    if (mc == 0xA8) return 1;
    if (mc == 0x00) return 2;
    return 3;
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  NVS helpers
// ═══════════════════════════════════════════════════════════════════════════

static int32_t nvsGet(const char *key, int32_t def) {
    Preferences p;
    p.begin(NVS_NS, true);
    int32_t v = p.getInt(key, def);
    p.end();
    return v;
}

static void nvsPut(const char *key, int32_t val) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putInt(key, val);
    p.end();
}

static void setBrightness(int level) {
    g_brightness = level % 5;
    static const uint8_t duties[] = { 10, 60, 130, 200, 255 };
    analogWrite(TFT_BL, duties[g_brightness]);
    nvsPut("bright", g_brightness);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Touch input
// ═══════════════════════════════════════════════════════════════════════════

static void rawToScreen(int16_t *sx, int16_t *sy, int16_t rx, int16_t ry) {
    *sx = map(ry, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_W - 1);
    *sy = map(rx, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_H - 1, 0);
    #if CYD_USB_VERSION == 2
    *sx = SCREEN_W - 1 - *sx;
    *sy = SCREEN_H - 1 - *sy;
    #endif
    *sx = constrain(*sx, 0, SCREEN_W - 1);
    *sy = constrain(*sy, 0, SCREEN_H - 1);
}

static void readTouch() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int16_t sx, sy;
        rawToScreen(&sx, &sy, p.x, p.y);
        g_tx = sx; g_ty = sy;
        g_touched = true;
    } else {
        g_touched = false;
    }
}

static bool readTap() {
    bool now = g_touched;
    if (!now) { g_wasTouched = false; return false; }
    if (g_wasTouched) return false;
    g_wasTouched = true;
    return true;
}

static bool hitPowerButton() {
    int dx = g_tx - PWR_BTN_X;
    int dy = g_ty - PWR_BTN_Y;
    return (dx * dx + dy * dy) <= (PWR_BTN_R * PWR_BTN_R);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Color helpers
// ═══════════════════════════════════════════════════════════════════════════

static uint16_t dimColor(uint16_t c, int steps) {
    for (int i = 0; i < steps; i++) {
        int r = (c >> 11) & 0x1F;
        int g = (c >> 5)  & 0x3F;
        int b =  c        & 0x1F;
        r = (r > 2) ? r - 2 : 0;
        g = (g > 3) ? g - 3 : 0;
        b = (b > 2) ? b - 2 : 0;
        c = (r << 11) | (g << 5) | b;
    }
    return c;
}

static uint16_t lerpColor(uint16_t a, uint16_t b, int t) {
    int r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
    int r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
    int r3 = r1 + ((r2 - r1) * t >> 8);
    int g3 = g1 + ((g2 - g1) * t >> 8);
    int b3 = b1 + ((b2 - b1) * t >> 8);
    return (uint16_t)((r3 << 11) | (g3 << 5) | b3);
}

static uint16_t brickColor(uint8_t type) {
    if (type == 5)       return lerpColor(g_themeColor, COL_GOLD, 140);
    else if (type == 4)  return lerpColor(g_themeColor, COL_LIGHT_GRAY, 120);
    return g_themeColor;
}

static bool isWidePaddle() { return millis() < g_puExpire[1]; }
static bool isSlow()       { return millis() < g_puExpire[2]; }

static int paddleW()  { return isWidePaddle() ? PADDLE_WIDE_W : PADDLE_W; }

// ═══════════════════════════════════════════════════════════════════════════
//  Level generation
// ═══════════════════════════════════════════════════════════════════════════

static void generateLevel() {
    memset(g_bricks, 0, sizeof(g_bricks));
    g_bricksDirty = true;
    g_brickRows = BRICK_ROWS;
    if (g_level >= 3) g_brickRows = 6;
    if (g_level >= 5) g_brickRows = 7;

    for (int col = 0; col < BRICK_COLS; col++) {
        for (int row = 0; row < g_brickRows; row++) {
            uint8_t type = 1 + (row % 3);  // cycle 1,2,3
            if (random(100) < 8 + g_level * 3) {
                type = (row == 0) ? 5 : 4;  // gold top row, silver below
            }
            g_bricks[col][row] = type;
        }
    }
    // Guarantee at least one special brick
    int sc = random(BRICK_COLS);
    int sr = random(g_brickRows);
    g_bricks[sc][sr] = 5;
}

static bool allBricksCleared() {
    for (int col = 0; col < BRICK_COLS; col++)
        for (int row = 0; row < g_brickRows; row++)
            if (g_bricks[col][row]) return false;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Ball
// ═══════════════════════════════════════════════════════════════════════════

static void resetBallOnPaddle() {
    for (int i = 0; i < MAX_BALLS; i++) {
        g_balls[i].active = false;
        g_oldBallX[i] = g_oldBallY[i] = -1;
    }
    int pw = paddleW();
    g_balls[0].x = g_paddleX + pw / 2.0f;
    g_balls[0].y = PADDLE_Y - BALL_R - 1;
    g_balls[0].dx = 0;
    g_balls[0].dy = 0;
    g_balls[0].speed = BALL_INITIAL_SPEED;
    g_balls[0].active = true;
}

static void launchBall() {
    float spd = g_balls[0].speed;
    // Slight random angle: -30° to -150° (upward)
    float angle = random(60, 121) * PI / 180.0f;  // 60-120 degrees from right
    g_balls[0].dx = cosf(angle) * spd;
    g_balls[0].dy = -sinf(angle) * spd;
    g_state = STATE_PLAYING_BOUNCE;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Power-ups
// ═══════════════════════════════════════════════════════════════════════════

static void spawnPowerUp(int bx, int by) {
    for (int i = 0; i < 3; i++) {
        if (!g_fallingPUs[i].alive) {
            g_fallingPUs[i].x = bx + BRICK_W / 2 - POWERUP_SIZE / 2;
            g_fallingPUs[i].y = by;
            g_fallingPUs[i].type = random(3);
            g_fallingPUs[i].alive = true;
            return;
        }
    }
}

static void activatePowerUp(uint8_t type) {
    unsigned long now = millis();
    switch (type) {
        case 0: {  // Multi-ball
            int srcIdx = 0;
            for (int i = 0; i < MAX_BALLS; i++) {
                if (g_balls[i].active) { srcIdx = i; break; }
            }
            Ball &src = g_balls[srcIdx];
            int added = 0;
            for (int i = 0; i < MAX_BALLS && added < 2; i++) {
                if (!g_balls[i].active) {
                    g_balls[i] = src;
                    g_balls[i].active = true;
                    float angle = atan2f(src.dy, src.dx);
                    float spread = (added == 0) ? -0.4f : 0.4f;
                    angle += spread;
                    g_balls[i].dx = cosf(angle) * src.speed;
                    g_balls[i].dy = sinf(angle) * src.speed;
                    added++;
                }
            }
            break;
        }
        case 1: g_puExpire[1] = now + WIDE_DURATION;  break;  // Wide paddle
        case 2: g_puExpire[2] = now + SLOW_DURATION;  break;  // Slow
    }
}

static void updatePowerUps() {
    int pw = paddleW();
    for (int i = 0; i < 3; i++) {
        if (!g_fallingPUs[i].alive) continue;
        g_fallingPUs[i].y += POWERUP_FALL_SPEED;
        // Paddle collect?
        if (g_fallingPUs[i].y + POWERUP_SIZE >= PADDLE_Y &&
            g_fallingPUs[i].y <= PADDLE_Y + PADDLE_H &&
            g_fallingPUs[i].x + POWERUP_SIZE >= g_paddleX &&
            g_fallingPUs[i].x <= g_paddleX + pw) {
            activatePowerUp(g_fallingPUs[i].type);
            g_fallingPUs[i].alive = false;
        }
        // Off screen
        if (g_fallingPUs[i].y > BOTTOM_BAR_Y) g_fallingPUs[i].alive = false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Physics
// ═══════════════════════════════════════════════════════════════════════════

static void checkWallCollision(Ball &b) {
    if (b.x - BALL_R <= 0)            { b.x = BALL_R;       b.dx = fabsf(b.dx); }
    if (b.x + BALL_R >= SCREEN_W - 1) { b.x = SCREEN_W-1-BALL_R; b.dx = -fabsf(b.dx); }
    if (b.y - BALL_R <= PLAYFIELD_Y0) { b.y = PLAYFIELD_Y0+BALL_R; b.dy = fabsf(b.dy); }
}

static bool checkPaddleCollision(Ball &b) {
    if (b.dy <= 0) return false;
    int pw = paddleW();
    int px = g_paddleX;
    if (b.x + BALL_R < px || b.x - BALL_R > px + pw) return false;
    if (b.y + BALL_R < PADDLE_Y || b.y - BALL_R > PADDLE_Y + PADDLE_H) return false;

    b.y = PADDLE_Y - BALL_R;
    b.dy = -fabsf(b.dy);

    float paddleCx = px + pw / 2.0f;
    float offset = (b.x - paddleCx) / (pw / 2.0f);
    if (offset < -0.85f) offset = -0.85f;
    if (offset >  0.85f) offset =  0.85f;

    float spd = b.speed;
    b.dx = offset * spd;
    float minDY = spd * SP_MIN_DY;
    float maxAbsDY = sqrtf(fmaxf(spd * spd - b.dx * b.dx, 0));
    if (maxAbsDY < minDY) maxAbsDY = minDY;
    b.dy = -maxAbsDY;
    return true;
}

static bool checkBrickCollision(Ball &b) {
    for (int row = 0; row < g_brickRows; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            if (!g_bricks[col][row]) continue;
            int bx = BRICK_X0 + col * (BRICK_W + BRICK_GAP);
            int by = BRICK_Y0 + row * (BRICK_H + BRICK_GAP);

            if (b.x + BALL_R < bx || b.x - BALL_R > bx + BRICK_W) continue;
            if (b.y + BALL_R < by || b.y - BALL_R > by + BRICK_H) continue;

            // Min-overlap axis
            float dL   = (b.x + BALL_R) - bx;
            float dR   = (bx + BRICK_W) - (b.x - BALL_R);
            float dT   = (b.y + BALL_R) - by;
            float dB   = (by + BRICK_H) - (b.y - BALL_R);
            float minH = (dL < dR) ? dL : dR;
            float minV = (dT < dB) ? dT : dB;

            if (minH < minV) {
                b.dx = -b.dx;
                if (dL < dR) b.x = bx - BALL_R;
                else         b.x = bx + BRICK_W + BALL_R;
            } else {
                b.dy = -b.dy;
                if (dT < dB) b.y = by - BALL_R;
                else         b.y = by + BRICK_H + BALL_R;
            }

            bool special = (g_bricks[col][row] >= SPECIAL_BRICK_MIN);
            g_score += (special ? SPECIAL_SCORE : BRICK_SCORE) * g_level;
            // Erase just this brick (no full-area flicker)
            disp.fillRect(bx, by, BRICK_W, BRICK_H, COL_PLAYFIELD);
            g_bricks[col][row] = 0;
            b.speed = fminf(b.speed + SPEED_INCREMENT, MAX_BALL_SPEED);

            if (!special && random(100) < POWERUP_CHANCE_PCT)
                spawnPowerUp(bx, by);

            // Sync speed to other active balls
            for (int i = 0; i < MAX_BALLS; i++)
                if (g_balls[i].active) g_balls[i].speed = b.speed;

            if (allBricksCleared()) {
                g_state = STATE_LEVEL_COMPLETE;
                g_levelCompleteTimer = millis();
                for (int i = 0; i < 3; i++) g_fallingPUs[i].alive = false;
            }
            return true;
        }
    }
    return false;
}

static void updatePhysics() {
    if (g_state != STATE_PLAYING_BOUNCE) return;
    float sm = isSlow() ? 0.5f : 1.0f;

    for (int i = 0; i < MAX_BALLS; i++) {
        if (!g_balls[i].active) continue;
        Ball &b = g_balls[i];
        float mag = sqrtf(b.dx * b.dx + b.dy * b.dy);
        if (mag < 0.001f) { mag = 1.0f; b.dy = -1.0f; }
        b.x += (b.dx / mag) * b.speed * sm;
        b.y += (b.dy / mag) * b.speed * sm;

        checkWallCollision(b);
        checkPaddleCollision(b);
        checkBrickCollision(b);

        // Lost ball
        if (b.y - BALL_R >= PLAYFIELD_Y1) b.active = false;
    }

    // Check for lost life
    int active = 0;
    for (int i = 0; i < MAX_BALLS; i++) if (g_balls[i].active) active++;
    if (active == 0) {
        g_lives--;
        if (g_lives > 0) {
            resetBallOnPaddle();
            g_state = STATE_PLAYING_READY;
        } else {
            g_state = STATE_GAME_OVER;
            if (g_score > g_highScore) {
                g_highScore = g_score;
                nvsPut(NVS_KEY_HIGH, g_highScore);
            }
        }
    }
}

static void updatePaddle() {
    int pw = paddleW();
    int maxX = SCREEN_W - pw;
    if (g_touched) {
        int target = g_tx - pw / 2;
        target = constrain(target, 0, maxX);
        // Direct follow for snappy response
        int diff = target - g_paddleX;
        if (abs(diff) > PADDLE_SPEED) g_paddleX += (diff > 0) ? PADDLE_SPEED : -PADDLE_SPEED;
        else g_paddleX = target;
    }
    g_paddleX = constrain(g_paddleX, 0, maxX);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Rendering
// ═══════════════════════════════════════════════════════════════════════════

static void drawTopBar() {
    // Only redraw when content changes
    if (g_score == g_lastScore && g_level == g_lastLevel &&
        g_lives == g_lastLives && g_themeIdx == g_lastTheme) return;

    disp.fillRect(0, 0, SCREEN_W, TOP_BAR_H, COL_BG);
    disp.setTextFont(2);

    // Score (left)
    disp.setTextColor(g_themeColor, COL_BG);
    disp.setTextDatum(TL_DATUM);
    char buf[32];
    snprintf(buf, sizeof(buf), "SCORE %lu", g_score);
    disp.drawString(buf, 4, 2);

    // Level (centered)
    disp.setTextDatum(TC_DATUM);
    snprintf(buf, sizeof(buf), "LEVEL %d", g_level);
    disp.drawString(buf, SCREEN_W/2 + 3, 2);

    // Separator line
    disp.drawFastHLine(0, TOP_BAR_H - 1, SCREEN_W, dimColor(g_themeColor, 4));

    g_lastScore = g_score; g_lastLevel = g_level; g_lastLives = g_lives;
    g_lastTheme = g_themeIdx;
}

static void drawTopBarForce() {
    g_lastScore = g_lastLevel = g_lastLives = g_lastTheme = -1;
    drawTopBar();
}

static void drawBricks() {
    for (int row = 0; row < g_brickRows; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            uint8_t type = g_bricks[col][row];
            int x = BRICK_X0 + col * (BRICK_W + BRICK_GAP);
            int y = BRICK_Y0 + row * (BRICK_H + BRICK_GAP);
            if (type) {
                uint16_t c = brickColor(type);
                disp.fillRoundRect(x, y, BRICK_W, BRICK_H, 2, c);
                // Highlight top edge
                disp.drawFastHLine(x + 2, y, BRICK_W - 4, dimColor(c, -2));
            }
        }
    }
}

static void drawPaddle() {
    int pw = paddleW();
    uint16_t pc = isWidePaddle() ? lerpColor(g_themeColor, COL_ORANGE, 120) : g_themeColor;
    disp.fillRoundRect(g_paddleX, PADDLE_Y, pw, PADDLE_H, 3, pc);
    // Glow line
    disp.drawFastHLine(g_paddleX + 2, PADDLE_Y, pw - 4, dimColor(pc, -3));
}

static void drawBalls() {
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!g_balls[i].active) continue;
        int bx = (int)g_balls[i].x;
        int by = (int)g_balls[i].y;
        disp.fillCircle(bx, by, BALL_R, COL_WHITE);
        disp.drawCircle(bx, by, BALL_R, g_themeColor);
    }
}

static void drawFallingPUs() {
    static const uint16_t puColors[] = { COL_GREEN, COL_ORANGE, COL_CYAN };
    static const char    puGlyphs[]  = { 'M',      'W',       'S' };
    for (int i = 0; i < 3; i++) {
        if (!g_fallingPUs[i].alive) continue;
        int x = g_fallingPUs[i].x;
        int y = g_fallingPUs[i].y;
        uint16_t c = puColors[g_fallingPUs[i].type];
        disp.fillRoundRect(x, y, POWERUP_SIZE, POWERUP_SIZE, 2, c);
        disp.setTextFont(1);
        disp.setTextColor(COL_BG, c);
        disp.setTextDatum(MC_DATUM);
        char sym[2] = { puGlyphs[g_fallingPUs[i].type], 0 };
        disp.drawString(sym, x + POWERUP_SIZE/2, y + POWERUP_SIZE/2);
    }
}

static void drawBottomBar() {
    unsigned long now = millis();
    int curPU = -1;
    if (g_puExpire[1] > now) curPU = 1;
    else if (g_puExpire[2] > now) curPU = 2;
    int curSec = (curPU > 0) ? (int)((g_puExpire[curPU] - now + 999) / 1000) : 0;

    // Skip redraw if nothing changed
    static int lastSec = -1, lastPU = -2, lastState = -1;
    static int lastBbarTheme = -1;
    if (curPU == lastPU && curSec == lastSec && (int)g_state == lastState && g_themeIdx == lastBbarTheme) return;
    lastPU = curPU; lastSec = curSec; lastState = g_state; lastBbarTheme = g_themeIdx;

    disp.fillRect(0, BOTTOM_BAR_Y, SCREEN_W, BOTTOM_BAR_H, COL_BG);
    disp.drawFastHLine(0, BOTTOM_BAR_Y, SCREEN_W, dimColor(g_themeColor, 4));

    disp.setTextFont(1);
    disp.setTextDatum(TL_DATUM);
    char buf[24] = "";

    if (curPU == 1) {
        snprintf(buf, sizeof(buf), "WIDE %ds", curSec);
        disp.setTextColor(COL_ORANGE, COL_BG);
    } else if (curPU == 2) {
        snprintf(buf, sizeof(buf), "SLOW %ds", curSec);
        disp.setTextColor(COL_CYAN, COL_BG);
    }
    disp.drawString(buf, 6, BOTTOM_BAR_Y + 5);

    // Centered hint
    disp.setTextDatum(TC_DATUM);
    disp.setTextColor(g_themeColor, COL_BG);
    if (g_state == STATE_PLAYING_READY)
        disp.drawString("TAP TO LAUNCH", SCREEN_W/2, BOTTOM_BAR_Y + 5);
    else if (g_state == STATE_IDLE)
        disp.drawString("TAP TO START", SCREEN_W/2, BOTTOM_BAR_Y + 5);

    // Brightness ring (bottom-left)
    int brX = 18, brY = BOTTOM_BAR_Y + BOTTOM_BAR_H/2;
    disp.drawCircle(brX, brY, 7, g_themeColor);
    disp.drawCircle(brX, brY, 6, g_themeColor);

    // Theme ring (bottom-right)
    int trX = 304, trY = BOTTOM_BAR_Y + BOTTOM_BAR_H/2;
    disp.drawCircle(trX, trY, 7, g_themeColor);
    disp.drawCircle(trX, trY, 6, g_themeColor);
}

static void drawPowerBtn() {
    disp.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R, g_themeColor);
    disp.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R - 1, g_themeColor);
    // Small line in center to indicate power
    disp.drawFastHLine(PWR_BTN_X - 3, PWR_BTN_Y, 7, COL_BG);
}

static void drawGameOver() {
    disp.fillRect(40, 70, 240, 90, COL_BG);
    disp.drawRoundRect(40, 70, 240, 90, 8, g_themeColor);
    disp.drawRoundRect(42, 72, 236, 86, 7, g_themeColor);

    disp.setTextFont(4);
    disp.setTextColor(g_themeColor, COL_BG);
    disp.setTextDatum(TC_DATUM);
    disp.drawString("GAME OVER", 160, 82);

    disp.setTextFont(2);
    char buf[40];
    snprintf(buf, sizeof(buf), "SCORE: %lu", g_score);
    disp.drawString(buf, 160, 118);

    disp.setTextFont(1);
    disp.setTextColor(COL_DIM_GRAY, COL_BG);
    snprintf(buf, sizeof(buf), "HIGH: %lu  |  LEVEL %d", g_highScore, g_level);
    disp.drawString(buf, 160, 142);
}

static void renderFrame() {
    // ── Erase old moving objects ──
    // Old balls
    for (int i = 0; i < MAX_BALLS; i++) {
        int ox = g_oldBallX[i], oy = g_oldBallY[i];
        if (ox >= 0 && oy >= 0)
            disp.fillCircle(ox, oy, BALL_R + 1, COL_PLAYFIELD);
    }
    // Old falling power-ups
    for (int i = 0; i < 3; i++) {
        if (g_fallingPUs[i].alive)
            disp.fillRect(g_fallingPUs[i].x, g_fallingPUs[i].y - 1,
                          POWERUP_SIZE, POWERUP_SIZE + 2, COL_PLAYFIELD);
    }
    // Old paddle
    if (g_oldPaddleX != g_paddleX || g_oldPaddleW != paddleW()) {
        disp.fillRect(g_oldPaddleX - 1, PADDLE_Y - 1,
                      g_oldPaddleW + 2, PADDLE_H + 2, COL_PLAYFIELD);
        g_oldPaddleX = g_paddleX;
        g_oldPaddleW = paddleW();
    }

    // ── Draw bricks (only when dirty) ──
    if (g_bricksDirty) {
        // Clear brick area
        int brickAreaH = g_brickRows * (BRICK_H + BRICK_GAP) - BRICK_GAP;
        disp.fillRect(0, BRICK_Y0, SCREEN_W, brickAreaH + 4, COL_PLAYFIELD);
        drawBricks();
        g_bricksDirty = false;
    }

    // ── Draw moving objects ──
    drawFallingPUs();
    drawBalls();
    drawPaddle();

    // ── Save ball positions for next frame erase ──
    for (int i = 0; i < MAX_BALLS; i++) {
        if (g_balls[i].active) {
            g_oldBallX[i] = (int)g_balls[i].x;
            g_oldBallY[i] = (int)g_balls[i].y;
        } else {
            g_oldBallX[i] = g_oldBallY[i] = -1;
        }
    }

    // ── Static bars ──
    drawBottomBar();  // before top bar so theme change is caught
    drawTopBar();
    drawPowerBtn();
}

static void redrawAll() {
    disp.fillScreen(COL_PLAYFIELD);
    drawBricks();
    drawTopBarForce();
    drawBottomBar();
    drawPowerBtn();
    drawPaddle();
    drawBalls();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Game lifecycle
// ═══════════════════════════════════════════════════════════════════════════

static void startGame() {
    g_score  = 0;
    g_lives  = 3;
    g_level  = 1;
    g_lastScore = g_lastLevel = g_lastLives = g_lastTheme = -1;
    memset(g_fallingPUs, 0, sizeof(g_fallingPUs));
    memset(g_puExpire, 0, sizeof(g_puExpire));
    generateLevel();
    resetBallOnPaddle();
    g_state = STATE_PLAYING_READY;
    redrawAll();
}

static void nextLevel() {
    g_level++;
    memset(g_fallingPUs, 0, sizeof(g_fallingPUs));
    memset(g_puExpire, 0, sizeof(g_puExpire));
    generateLevel();
    resetBallOnPaddle();
    g_state = STATE_PLAYING_READY;
    redrawAll();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Deep sleep
// ═══════════════════════════════════════════════════════════════════════════

static void goToSleep() {
    disp.fillScreen(COL_BG);
    disp.setTextFont(4);
    disp.setTextColor(g_themeColor, COL_BG);
    disp.setTextDatum(MC_DATUM);
    disp.drawString("SLEEP", SCREEN_W / 2, SCREEN_H / 2);
    delay(500);
    digitalWrite(TFT_BL, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_IRQ, 0);
    esp_deep_sleep_start();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Serial screenshot (RGB332 protocol — same as CYD-Poker)
// ═══════════════════════════════════════════════════════════════════════════

static void sendScreenshot() {
    // Use 8-bit sprite for fast framebuffer capture (same pattern as CYD-Poker)
    TFT_eSprite spr(&disp);
    spr.setColorDepth(8);
    uint8_t *fb = (uint8_t *)spr.createSprite(SCREEN_W, SCREEN_H);
    if (!fb) { Serial.print("OOM:no sprite\n"); return; }

    // Redraw everything into the sprite
    spr.fillSprite(COL_PLAYFIELD);
    spr.setTextColor(g_themeColor, COL_PLAYFIELD);
    spr.setTextFont(2);

    // -- Copy draw calls to spr instead of disp --
    // Bricks
    for (int row = 0; row < g_brickRows; row++)
        for (int col = 0; col < BRICK_COLS; col++) {
            if (!g_bricks[col][row]) continue;
            int x = BRICK_X0 + col * (BRICK_W + BRICK_GAP);
            int y = BRICK_Y0 + row * (BRICK_H + BRICK_GAP);
            spr.fillRoundRect(x, y, BRICK_W, BRICK_H, 2, brickColor(g_bricks[col][row]));
        }
    // Balls
    for (int i = 0; i < MAX_BALLS; i++)
        if (g_balls[i].active) {
            int bx = (int)g_balls[i].x, by = (int)g_balls[i].y;
            spr.fillCircle(bx, by, BALL_R, COL_WHITE);
            spr.drawCircle(bx, by, BALL_R, g_themeColor);
        }
    // Paddle
    int pw = paddleW();
    spr.fillRoundRect(g_paddleX, PADDLE_Y, pw, PADDLE_H, 3, g_themeColor);
    // Top bar
    spr.fillRect(0, 0, SCREEN_W, TOP_BAR_H, COL_BG);
    spr.setTextColor(g_themeColor, COL_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "SCORE %lu", g_score);
    spr.drawString(buf, 4, 2);
    snprintf(buf, sizeof(buf), "LEVEL %d", g_level);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(buf, SCREEN_W/2 + 3, 2);
    spr.drawFastHLine(0, TOP_BAR_H - 1, SCREEN_W, dimColor(g_themeColor, 4));
    // Bottom bar
    spr.fillRect(0, BOTTOM_BAR_Y, SCREEN_W, BOTTOM_BAR_H, COL_BG);
    spr.drawFastHLine(0, BOTTOM_BAR_Y, SCREEN_W, dimColor(g_themeColor, 4));
    spr.setTextFont(1);
    spr.setTextColor(g_themeColor, COL_BG);
    spr.setTextDatum(TC_DATUM);
    if (g_state == STATE_PLAYING_READY)
        spr.drawString("TAP TO LAUNCH", SCREEN_W/2, BOTTOM_BAR_Y + 5);
    else if (g_state == STATE_IDLE)
        spr.drawString("TAP TO START", SCREEN_W/2, BOTTOM_BAR_Y + 5);
    // Brightness ring
    int brX = 18, brY = BOTTOM_BAR_Y + BOTTOM_BAR_H/2;
    spr.drawCircle(brX, brY, 7, g_themeColor);
    spr.drawCircle(brX, brY, 6, g_themeColor);
    // Theme ring
    int trX = 304, trY = BOTTOM_BAR_Y + BOTTOM_BAR_H/2;
    spr.drawCircle(trX, trY, 7, g_themeColor);
    spr.drawCircle(trX, trY, 6, g_themeColor);
    // Power button
    spr.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R, g_themeColor);
    spr.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R - 1, g_themeColor);

    Serial.print("RGB332:");
    Serial.write(fb, SCREEN_W * SCREEN_H);
    Serial.flush();
    spr.deleteSprite();
}

// ═══════════════════════════════════════════════════════════════════════════
//  First-boot calibration (2USB only) — proper CYD-Poker pattern
// ═══════════════════════════════════════════════════════════════════════════

#if CYD_USB_VERSION == 2

static void touchSetRotation(int r) {
    s_touchRotation = r & 3;
    ts.setRotation(s_touchRotation);
    nvsPut("touch_rot", s_touchRotation);
}

static void displayCalibrate() {
    s_madctl = madctlForCombo(0);

    auto drawCal = [&]() {
        disp.fillScreen(COL_BG);
        applyOrientation();
        disp.fillScreen(COL_BG);

        // Asymmetric corner markers (same pattern as CYD-Poker)
        disp.fillTriangle(2, 2, 60, 2, 2, 60, COL_AMBER);
        disp.fillRect(SCREEN_W - 50, 2, 48, 8, g_themeColor);
        disp.fillRect(SCREEN_W - 8, 2, 6, 48, g_themeColor);
        disp.fillCircle(24, SCREEN_H - 24, 20, COL_AMBER);
        disp.fillCircle(24, SCREEN_H - 24, 16, COL_BG);
        disp.fillCircle(24, SCREEN_H - 24, 20, COL_AMBER);
        disp.drawLine(SCREEN_W - 40, SCREEN_H - 24, SCREEN_W - 8, SCREEN_H - 24, g_themeColor);
        disp.drawLine(SCREEN_W - 24, SCREEN_H - 40, SCREEN_W - 24, SCREEN_H - 8, g_themeColor);
        disp.drawCircle(SCREEN_W - 24, SCREEN_H - 24, 14, g_themeColor);

        // Center "T" orientation letter
        disp.fillRect(SCREEN_W/2 - 16, SCREEN_H/2 - 24, 32, 6, COL_WHITE);
        disp.fillRect(SCREEN_W/2 - 4, SCREEN_H/2 - 24, 8, 48, COL_WHITE);

        int idx = madctlToCombo(s_madctl);
        disp.setTextFont(4);
        disp.setTextColor(g_themeColor, COL_BG);
        char buf[16]; snprintf(buf, sizeof(buf), "MODE %d", idx);
        int tw = disp.textWidth(buf);
        disp.setCursor((SCREEN_W - tw) / 2, 68);
        disp.print(buf);

        disp.setTextFont(2);
        disp.setTextColor(COL_WHITE, COL_BG);
        const char *msg1 = "Tap to change";
        disp.setCursor((SCREEN_W - disp.textWidth(msg1)) / 2, SCREEN_H - 72);
        disp.print(msg1);

        disp.setTextFont(1);
        disp.setTextColor(COL_DIM_GRAY, COL_BG);
        const char *msg2 = "Hold 2s to confirm";
        disp.setCursor((SCREEN_W - disp.textWidth(msg2)) / 2, SCREEN_H - 52);
        disp.print(msg2);
    };

    drawCal();
    int curCombo = 0;
    unsigned long holdStart = 0;
    bool wasTouched = false;

    for (;;) {
        readTouch();
        bool nowTouched = g_touched;

        if (nowTouched && !wasTouched) holdStart = millis();
        if (nowTouched) {
            if (millis() - holdStart >= 2000) break;
        }
        if (!nowTouched && wasTouched) {
            if (millis() - holdStart < 1000) {
                curCombo = (curCombo + 1) & 3;
                s_madctl = madctlForCombo(curCombo);
                drawCal();
            }
        }
        wasTouched = nowTouched;
        delay(30);
    }

    nvsPut(NVS_KEY_MADCTL, s_madctl);
    disp.fillScreen(COL_BG);
    delay(300);
}

static void touchCalibrate() {
    s_touchRotation = nvsGet("touch_rot", 2);
    ts.setRotation(s_touchRotation);

    auto drawCal = [&]() {
        disp.fillScreen(COL_BG);
        // Corner crosshairs
        for (int i = 0; i < 4; i++) {
            int cx = (i & 1) ? SCREEN_W - 30 : 30;
            int cy = (i & 2) ? SCREEN_H - 30 : 30;
            disp.drawLine(cx - 15, cy, cx + 15, cy, COL_AMBER);
            disp.drawLine(cx, cy - 15, cx, cy + 15, COL_AMBER);
        }
        if (g_touched) disp.fillCircle(g_tx, g_ty, 6, COL_AMBER);

        disp.setTextFont(2);
        disp.setTextColor(COL_WHITE, COL_BG);
        const char *t = "TOUCH CALIBRATE";
        disp.setCursor((SCREEN_W - disp.textWidth(t)) / 2, 8);
        disp.print(t);

        disp.setTextFont(1);
        disp.setTextColor(COL_DIM_GRAY, COL_BG);
        const char *m = "Tap to cycle, hold 2s when cursor follows finger";
        disp.setCursor((SCREEN_W - disp.textWidth(m)) / 2, SCREEN_H - 20);
        disp.print(m);
    };

    drawCal();
    unsigned long holdStart = 0;
    bool wasTouched = false;
    int curRot = s_touchRotation;

    for (;;) {
        readTouch();
        bool nowTouched = g_touched;

        if (nowTouched && !wasTouched) holdStart = millis();
        if (nowTouched) {
            drawCal();
            if (millis() - holdStart >= 2000) break;
        }
        if (!nowTouched && wasTouched) {
            if (millis() - holdStart < 1000) {
                curRot = (curRot + 1) & 3;
                touchSetRotation(curRot);
                drawCal();
            }
        }
        wasTouched = nowTouched;
        delay(30);
    }

    nvsPut(NVS_KEY_CAL, CURRENT_CAL_VER);
    disp.fillScreen(COL_BG);
    delay(300);
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Boot splash
// ═══════════════════════════════════════════════════════════════════════════

static void showSplash() {
    disp.fillScreen(COL_BG);

    // Breakout logo: brick wall centered
    int bx = 70, by = 9;
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 6; col++) {
            int x = bx + col * (28 + 3);
            int y = by + row * (11 + 3);
            disp.fillRoundRect(x, y, 28, 11, 2, g_themeColor);
        }
    }
    // Ball and paddle below bricks
    int ballX = bx + 3 * 31 + 11, ballY = by + 5 * 14 + 15;
    disp.fillCircle(ballX, ballY, 4, COL_WHITE);
    disp.drawCircle(ballX, ballY, 4, g_themeColor);
    disp.fillRoundRect(ballX - 24, ballY + 11, 48, 8, 3, g_themeColor);

    // Branding
    int tw;
    disp.setTextFont(4);
    disp.setTextColor(g_themeColor, COL_BG);
    tw = disp.textWidth("xXMayDayXx");
    disp.setCursor((SCREEN_W - tw) / 2, 128);
    disp.print("xXMayDayXx");

    disp.setTextFont(2);
    disp.setTextColor(COL_WHITE, COL_BG);
    tw = disp.textWidth("xXCYD-BreakoutXx");
    disp.setCursor((SCREEN_W - tw) / 2, 160);
    disp.print("xXCYD-BreakoutXx");

    disp.setTextColor(g_themeColor, COL_BG);
    tw = disp.textWidth("xXQuantum-SmokeXx");
    disp.setCursor((SCREEN_W - tw) / 2, 184);
    disp.print("xXQuantum-SmokeXx");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    randomSeed(esp_random());

    // Display + brightness
    pinMode(TFT_BL, OUTPUT);
    g_brightness = nvsGet("bright", 4);  // default max
    setBrightness(g_brightness);
    disp.init();
    disp.setRotation(1);
    disp.fillScreen(COL_BG);

    // Touch SPI
    touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    ts.setRotation(
        #if CYD_USB_VERSION == 2
        2
        #else
        0
        #endif
    );
    #if CYD_USB_VERSION == 2
    ts.setRotation(nvsGet("touch_rot", 2));
    #endif

    // Theme
    themeInit();

    // High score
    g_highScore = (unsigned long)nvsGet(NVS_KEY_HIGH, 0);

    // 2USB calibrations
    #if CYD_USB_VERSION == 2
    int calVer = nvsGet(NVS_KEY_CAL, 0);
    s_madctl = (uint8_t)nvsGet(NVS_KEY_MADCTL, 0x80);
    applyOrientation();
    if (calVer < CURRENT_CAL_VER) {
        displayCalibrate();
        applyOrientation();
        touchCalibrate();
    }
    #endif

    // Boot splash
    showSplash();
    delay(4000);

    // Initial state
    generateLevel();
    resetBallOnPaddle();
    g_bricksDirty = true;
    g_lastTheme = -1;
    redrawAll();
}

void loop() {
    static unsigned long lastFrame = 0;
    unsigned long now = millis();

    // Serial commands
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'R' || c == 'r') Serial.print("READY");
        else if (c == 'S' || c == 's') sendScreenshot();
        else if (c >= '0' && c <= '9') redrawAll();
        #if CYD_USB_VERSION == 2
        else if (c == 'M' || c == 'm') {
            int cur = madctlToCombo(s_madctl);
            cur = (cur + 1) & 3;
            s_madctl = madctlForCombo(cur);
            nvsPut(NVS_KEY_MADCTL, s_madctl);
            nvsPut(NVS_KEY_CAL, CURRENT_CAL_VER);
            applyOrientation();
            redrawAll();
        }
        else if (c == 'T' || c == 't') {
            int cur = (nvsGet("touch_rot", 2) + 1) & 3;
            touchSetRotation(cur);
            nvsPut(NVS_KEY_CAL, CURRENT_CAL_VER);
        }
        #endif
    }

    // Frame cap ~60fps
    if (now - lastFrame < 16) { delay(1); return; }
    lastFrame = now;

    // Touch
    readTouch();
    bool tap = readTap();

    // Power button (any state)
    if (tap && hitPowerButton()) { goToSleep(); return; }

    // Bottom bar ring taps: brightness (left) and theme (right)
    if (tap) {
        int brX = 18, brY = BOTTOM_BAR_Y + BOTTOM_BAR_H/2;
        int trX = 304, trY = BOTTOM_BAR_Y + BOTTOM_BAR_H/2;
        int bdx = g_tx - brX, bdy = g_ty - brY;
        int tdx = g_tx - trX, tdy = g_ty - trY;
        if (bdx*bdx + bdy*bdy <= 100) {  // Brightness ring (r=10)
            setBrightness(g_brightness + 1);
            delay(200); return;
        }
        if (tdx*tdx + tdy*tdy <= 100) {  // Theme ring (r=10)
            themeNext();
            g_bricksDirty = true;
            g_lastTheme = -1;
            delay(200); return;
        }
    }

    // Game state dispatch
    switch (g_state) {
        case STATE_IDLE:
            updatePaddle();
            if (tap && g_ty < BOTTOM_BAR_Y) startGame();
            break;

        case STATE_PLAYING_READY:
            updatePaddle();
            if (tap && g_ty < BOTTOM_BAR_Y) launchBall();
            break;

        case STATE_PLAYING_BOUNCE:
            updatePaddle();
            updatePowerUps();
            updatePhysics();
            break;

        case STATE_LEVEL_COMPLETE:
            updatePaddle();
            if (now - g_levelCompleteTimer > 2000) nextLevel();
            break;

        case STATE_GAME_OVER:
            if (tap) { startGame(); delay(200); return; }
            break;
    }

    // Render
    renderFrame();
}
