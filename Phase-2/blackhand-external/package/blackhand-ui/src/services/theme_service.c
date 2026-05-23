#include "theme_service.h"
#include "settings_service.h"
#include "../config.h"

#include <stdbool.h>

/*
 * Professional futuristic theme palette system.
 *
 * Each theme has distinct light AND dark palettes with carefully chosen
 * colors for high contrast, readability, and a premium feel.
 *
 * Design principles:
 *   - Dark modes use deep, rich backgrounds (not just black)
 *   - Light modes use warm or cool off-whites (never pure white)
 *   - Accent colors carry through both modes for visual identity
 *   - Selection highlights use the theme's accent as bg
 *   - Rule glyphs give each theme tactile character
 */

typedef struct {
    uint32_t bg;
    uint32_t text_primary;
    uint32_t text_muted;
    uint32_t border;
    uint32_t selection_bg;
    uint32_t selection_text;
    const char *rule_glyph;
} theme_palette_t;

/* ── Light palettes ─────────────────────────────────────────────────────── */
static const theme_palette_t k_light_palettes[] = {
    {
        .bg             = UI_THEME_LIGHT_0_BG,
        .text_primary   = UI_THEME_LIGHT_0_TEXT_PRIMARY,
        .text_muted     = UI_THEME_LIGHT_0_TEXT_MUTED,
        .border         = UI_THEME_LIGHT_0_BORDER,
        .selection_bg   = UI_THEME_LIGHT_0_SELECTION_BG,
        .selection_text = UI_THEME_LIGHT_0_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_LIGHT_0_RULE,
    },
    {
        .bg             = UI_THEME_LIGHT_1_BG,
        .text_primary   = UI_THEME_LIGHT_1_TEXT_PRIMARY,
        .text_muted     = UI_THEME_LIGHT_1_TEXT_MUTED,
        .border         = UI_THEME_LIGHT_1_BORDER,
        .selection_bg   = UI_THEME_LIGHT_1_SELECTION_BG,
        .selection_text = UI_THEME_LIGHT_1_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_LIGHT_1_RULE,
    },
    {
        .bg             = UI_THEME_LIGHT_2_BG,
        .text_primary   = UI_THEME_LIGHT_2_TEXT_PRIMARY,
        .text_muted     = UI_THEME_LIGHT_2_TEXT_MUTED,
        .border         = UI_THEME_LIGHT_2_BORDER,
        .selection_bg   = UI_THEME_LIGHT_2_SELECTION_BG,
        .selection_text = UI_THEME_LIGHT_2_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_LIGHT_2_RULE,
    },
    {
        .bg             = UI_THEME_LIGHT_3_BG,
        .text_primary   = UI_THEME_LIGHT_3_TEXT_PRIMARY,
        .text_muted     = UI_THEME_LIGHT_3_TEXT_MUTED,
        .border         = UI_THEME_LIGHT_3_BORDER,
        .selection_bg   = UI_THEME_LIGHT_3_SELECTION_BG,
        .selection_text = UI_THEME_LIGHT_3_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_LIGHT_3_RULE,
    },
    {
        .bg             = UI_THEME_LIGHT_4_BG,
        .text_primary   = UI_THEME_LIGHT_4_TEXT_PRIMARY,
        .text_muted     = UI_THEME_LIGHT_4_TEXT_MUTED,
        .border         = UI_THEME_LIGHT_4_BORDER,
        .selection_bg   = UI_THEME_LIGHT_4_SELECTION_BG,
        .selection_text = UI_THEME_LIGHT_4_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_LIGHT_4_RULE,
    },
};

/* ── Dark palettes — each theme has its own dark background tint ────────── */
static const theme_palette_t k_dark_palettes[] = {
    {
        .bg             = UI_THEME_DARK_0_BG,
        .text_primary   = UI_THEME_DARK_0_TEXT_PRIMARY,
        .text_muted     = UI_THEME_DARK_0_TEXT_MUTED,
        .border         = UI_THEME_DARK_0_BORDER,
        .selection_bg   = UI_THEME_DARK_0_SELECTION_BG,
        .selection_text = UI_THEME_DARK_0_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_DARK_0_RULE,
    },
    {
        .bg             = UI_THEME_DARK_1_BG,
        .text_primary   = UI_THEME_DARK_1_TEXT_PRIMARY,
        .text_muted     = UI_THEME_DARK_1_TEXT_MUTED,
        .border         = UI_THEME_DARK_1_BORDER,
        .selection_bg   = UI_THEME_DARK_1_SELECTION_BG,
        .selection_text = UI_THEME_DARK_1_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_DARK_1_RULE,
    },
    {
        .bg             = UI_THEME_DARK_2_BG,
        .text_primary   = UI_THEME_DARK_2_TEXT_PRIMARY,
        .text_muted     = UI_THEME_DARK_2_TEXT_MUTED,
        .border         = UI_THEME_DARK_2_BORDER,
        .selection_bg   = UI_THEME_DARK_2_SELECTION_BG,
        .selection_text = UI_THEME_DARK_2_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_DARK_2_RULE,
    },
    {
        .bg             = UI_THEME_DARK_3_BG,
        .text_primary   = UI_THEME_DARK_3_TEXT_PRIMARY,
        .text_muted     = UI_THEME_DARK_3_TEXT_MUTED,
        .border         = UI_THEME_DARK_3_BORDER,
        .selection_bg   = UI_THEME_DARK_3_SELECTION_BG,
        .selection_text = UI_THEME_DARK_3_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_DARK_3_RULE,
    },
    {
        .bg             = UI_THEME_DARK_4_BG,
        .text_primary   = UI_THEME_DARK_4_TEXT_PRIMARY,
        .text_muted     = UI_THEME_DARK_4_TEXT_MUTED,
        .border         = UI_THEME_DARK_4_BORDER,
        .selection_bg   = UI_THEME_DARK_4_SELECTION_BG,
        .selection_text = UI_THEME_DARK_4_SELECTION_TEXT,
        .rule_glyph     = UI_THEME_DARK_4_RULE,
    },
};

static bool g_is_dark_mode;
static int g_light_theme;

static int clamp_theme_index(int idx) {
    if (idx < 0) return 0;
    if (idx >= UI_THEME_COUNT) return UI_THEME_COUNT - 1;
    return idx;
}

static const theme_palette_t *active_palette(void) {
    int idx = clamp_theme_index(g_light_theme);
    return g_is_dark_mode ? &k_dark_palettes[idx] : &k_light_palettes[idx];
}

void theme_service_init(void) {
    theme_service_sync_from_settings();
}

void theme_service_sync_from_settings(void){
    g_is_dark_mode = settings_service_get_bool(SETTINGS_KEY_NIGHT_MODE);
    g_light_theme = clamp_theme_index(settings_service_get_light_theme());
}

uint32_t theme_bg(void) {
    return active_palette()->bg;
}

uint32_t theme_text_primary(void){
    return active_palette()->text_primary;
}

uint32_t theme_text_muted(void){
    return active_palette()->text_muted;
}

uint32_t theme_border(void){
    return active_palette()->border;
}

uint32_t theme_selection_bg(void) {
    return active_palette()->selection_bg;
}

uint32_t theme_selection_text(void) {
    return active_palette()->selection_text;
}

const char *theme_rule_glyph(void) {
    return active_palette()->rule_glyph;
}

int theme_service_is_dark(void) {
    return g_is_dark_mode ? 1 : 0;
}
