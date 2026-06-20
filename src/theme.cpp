#include "theme.h"
#include <Preferences.h>

const ThemeEntry g_themes[THEME_COUNT] = {
    { "CYAN",   0x07FFu },
    { "GREEN",  0x07E0u },
    { "RED",    0xF800u },
    { "ORANGE", 0xFD00u },
    { "YELLOW", 0xFFE0u },
    { "GRAY",   0xCE79u },
    { "PURPLE", 0xF81Fu },
    { "PINK",   0xFC18u },
    { "WHITE",  0xFFFFu },
};

uint16_t g_themeColor = 0x07FFu;  // default CYAN
int      g_themeIdx   = 0;

static Preferences prefs;

void themeInit() {
    prefs.begin("cyd-poker", true);
    g_themeIdx = prefs.getInt("theme", 0);
    if (g_themeIdx < 0 || g_themeIdx >= THEME_COUNT) g_themeIdx = 0;
    g_themeColor = g_themes[g_themeIdx].color;
    prefs.end();
}

void themeNext() {
    g_themeIdx = (g_themeIdx + 1) % THEME_COUNT;
    g_themeColor = g_themes[g_themeIdx].color;
    prefs.begin("cyd-poker", false);
    prefs.putInt("theme", g_themeIdx);
    prefs.end();
}

void themeSet(int idx) {
    if (idx < 0 || idx >= THEME_COUNT) return;
    g_themeIdx = idx;
    g_themeColor = g_themes[idx].color;
    prefs.begin("cyd-poker", false);
    prefs.putInt("theme", idx);
    prefs.end();
}
