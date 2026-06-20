#pragma once
#include <cstdint>

#define THEME_COUNT 9

struct ThemeEntry {
    const char *name;
    uint16_t    color;
};

extern const ThemeEntry g_themes[THEME_COUNT];
extern uint16_t g_themeColor;
extern int      g_themeIdx;

void themeInit();
void themeNext();         // cycle to next theme
void themeSet(int idx);
