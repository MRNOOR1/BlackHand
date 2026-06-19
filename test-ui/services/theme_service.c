#include "theme_service.h"
#include "settings_service.h"
#include "../config.h"

/*
 * The 12 BlackHand OS themes, in design-pack order. Hex values are the sRGB
 * tokens from the UI Design Pack / interactive prototype. The renderer reads
 * only from the active entry of this table -- there is no per-theme branching
 * in the screens themselves; everything is expressed through the style enums.
 */
static const bh_theme_t k_themes[] = {
    /* 1 -- amber-ledger : military avionics amber phosphor on near-black */
    { "amber-ledger", "Amber Ledger",
      0x0B0B09, 0xFAC775, 0xBA7517, 0x854F0B, 0x633806,
      0xFAC775, 0x0B0B09, 0xFAC775, 0xBA7517,
      SEL_CURSOR, ST_GLYPH, DV_SOLID, FT_WORDMARK, 0,0,0,0 },

    /* 2 -- phosphor-index : green CRT terminal, indexed registers */
    { "phosphor-index", "Phosphor Index",
      0x0A0D08, 0xC0DD97, 0x97C459, 0x639922, 0x3B6D11,
      0x00, 0x00, 0xC0DD97, 0x97C459,
      SEL_INDEX, ST_TEXT, DV_SOLID, FT_WORDMARK, 0,0,0,0 },

    /* 3 -- redline-mono : tactical HUD, warm gray with a red scalpel accent */
    { "redline-mono", "Redline Mono",
      0x0B0B0B, 0xD3D1C7, 0x888780, 0x5F5E5A, 0x444441,
      0x00, 0x00, 0xE24B4A, 0xF09595,
      SEL_BRACKET, ST_GLYPH, DV_LABEL, FT_REDLINE, 0,0,0,0 },

    /* 4 -- vale-signal : amber tuned for event screens */
    { "vale-signal", "Vale Signal",
      0x0B0B09, 0xFAC775, 0xBA7517, 0x854F0B, 0x633806,
      0xFAC775, 0x0B0B09, 0xFAC775, 0xBA7517,
      SEL_CURSOR, ST_GLYPH, DV_SOLID, FT_WORDMARK, 0,0,0,1 },

    /* 5 -- rushnyk : Slavic folk embroidery, red cross-stitch bands */
    { "rushnyk", "Rushnyk",
      0x0C0A0A, 0xF7C1C1, 0xF09595, 0xA32D2D, 0x791F1F,
      0x00, 0x00, 0xE24B4A, 0xF09595,
      SEL_DIAMOND, ST_GLYPH, DV_BAND, FT_DIAMOND, 0,0,0,0 },

    /* 6 -- thermal-index : heatmap, color encodes 24h usage frequency */
    { "thermal-index", "Thermal Index",
      0x0B0B0C, 0xD3D1C7, 0x888780, 0x5F5E5A, 0x444441,
      0x00, 0x00, 0xE24B4A, 0xEF9F27,
      SEL_HEAT, ST_THERMAL, DV_SOLID, FT_WORDMARK, 0,1,0,0 },

    /* 7 -- instrument-bench : scientism, telemetry with units, registers */
    { "instrument-bench", "Instrument",
      0x0A0B0B, 0x9FE1CB, 0x5DCAA5, 0x1D9E75, 0x0F6E56,
      0x085041, 0xE1F5EE, 0x5DCAA5, 0x1D9E75,
      SEL_REG, ST_UTC, DV_DOT, FT_UTC, 0,0,0,0 },

    /* 8 -- boot-rite : rushnyk identity + instrument log, dual accent */
    { "boot-rite", "Boot Rite",
      0x0C0A0A, 0xF7C1C1, 0xF09595, 0xA32D2D, 0x791F1F,
      0x00, 0x00, 0x5DCAA5, 0xA32D2D,
      SEL_DIAMOND, ST_RITE, DV_BAND, FT_RITE, 0,0,0,0 },

    /* 9 -- blueprint : technical drawing, the only colored background */
    { "blueprint", "Blueprint",
      0x123A78, 0xEAF3FC, 0x7FB3E8, 0x4A7BBF, 0x2C5CA0,
      0x00, 0x00, 0x7FB3E8, 0x4A7BBF,
      SEL_BOX, ST_GLYPH, DV_DIM, FT_WORDMARK, 0,0,0,0 },

    /* 10 -- dossier : the reference LIGHT theme, paper-tone field file */
    { "dossier", "Dossier",
      0xE9E3D1, 0x26241F, 0x3D3A33, 0x6B665B, 0x9A937F,
      0x26241F, 0xE9E3D1, 0xA32D2D, 0x6B665B,
      SEL_INK, ST_TEXT2, DV_DBL, FT_DOSSIER, 1,0,1,0 },

    /* 11 -- one-bit : pure black/white brutalism, power-saver mode */
    { "one-bit", "One-Bit",
      0x000000, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF,
      0xFFFFFF, 0x000000, 0xFFFFFF, 0xFFFFFF,
      SEL_BIT, ST_BITS, DV_CHK, FT_BIT, 0,0,0,0 },

    /* 12 -- polar-night : deep indigo, aurora band, GPS footer */
    { "polar-night", "Polar Night",
      0x0A0A16, 0xD8E6F2, 0x5F7390, 0x5F7390, 0x2A3450,
      0xFFFFFF, 0x0A0A16, 0xC77DDB, 0x5F7390,
      SEL_STAR, ST_GLYPH, DV_AUR, FT_POLAR, 0,0,0,0 },
};

static const int k_count = (int)(sizeof(k_themes) / sizeof(k_themes[0]));

static int       g_index     = UI_DEFAULT_THEME_INDEX;
static int       g_dark_mode = 0;
static bh_theme_t g_dark_theme;

static int clamp_index(int idx) {
    if (idx < 0) return 0;
    if (idx >= k_count) return k_count - 1;
    return idx;
}

/* Darken a single RGB color by 55%. 0 is a sentinel (meaning "inherit"),
 * so it is passed through unchanged. */
static uint32_t dark(uint32_t c) {
    if (c == 0) return 0;
    uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * 55 / 100);
    uint8_t g = (uint8_t)(((c >>  8) & 0xFF) * 55 / 100);
    uint8_t b = (uint8_t)( (c        & 0xFF) * 55 / 100);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void build_dark_theme(void) {
    const bh_theme_t *s = &k_themes[clamp_index(g_index)];
    g_dark_theme          = *s;            /* copy id, label, style enums, flags */
    g_dark_theme.bg       = dark(s->bg);
    g_dark_theme.bright   = dark(s->bright);
    g_dark_theme.mid      = dark(s->mid);
    g_dark_theme.dim      = dark(s->dim);
    g_dark_theme.deep     = dark(s->deep);
    g_dark_theme.sel_bg   = dark(s->sel_bg);
    g_dark_theme.sel_fg   = dark(s->sel_fg);
    g_dark_theme.accent   = dark(s->accent);
    g_dark_theme.accent2  = dark(s->accent2);
}

void theme_service_init(void) {
    theme_service_sync_from_settings();
}

void theme_service_sync_from_settings(void) {
    g_index    = clamp_index(settings_service_get_light_theme());
    g_dark_mode = settings_service_get_bool(SETTINGS_KEY_NIGHT_MODE) ? 1 : 0;
    if (g_dark_mode) build_dark_theme();
}

const bh_theme_t *theme_active(void) {
    if (g_dark_mode) return &g_dark_theme;
    return &k_themes[clamp_index(g_index)];
}

int theme_active_index(void) { return clamp_index(g_index); }
int theme_count(void)        { return k_count; }

const bh_theme_t *theme_at(int index) {
    return &k_themes[clamp_index(index)];
}

uint32_t theme_bg(void)           { return theme_active()->bg; }
uint32_t theme_text_primary(void) { return theme_active()->bright; }
uint32_t theme_text_muted(void)   { return theme_active()->mid; }
uint32_t theme_border(void)       { return theme_active()->deep; }
uint32_t theme_dim(void)          { return theme_active()->dim; }
uint32_t theme_deep(void)         { return theme_active()->deep; }

uint32_t theme_accent(void) {
    const bh_theme_t *t = theme_active();
    return t->accent ? t->accent : t->bright;
}

uint32_t theme_selection_bg(void) {
    const bh_theme_t *t = theme_active();
    return t->sel_bg ? t->sel_bg : t->bright;
}

uint32_t theme_selection_text(void) {
    const bh_theme_t *t = theme_active();
    return t->sel_fg ? t->sel_fg : t->bg;
}

const char *theme_rule_glyph(void) {
    /* Single-cell glyph used by screens that draw their own content rules. */
    switch (theme_active()->div_style) {
        case DV_DOT: return ".";
        case DV_DBL: return "=";
        default:     return "─"; /* light horizontal */
    }
}

int theme_service_is_dark(void) {
    return theme_active()->is_light ? 0 : 1;
}

/* ---- global semantic action colors (darken on the light theme) ----------- */
uint32_t theme_affirm_border(void)   { return theme_active()->is_light ? 0x2F8A3D : 0x97C459; }
uint32_t theme_affirm_text(void)     { return theme_active()->is_light ? 0x1F6B2E : 0xC0DD97; }
uint32_t theme_destruct_border(void) { return 0xA32D2D; }
uint32_t theme_destruct_text(void)   { return theme_active()->is_light ? 0x8E2424 : 0xF09595; }
