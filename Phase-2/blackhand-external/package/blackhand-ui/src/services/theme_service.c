#include "theme_service.h"
#include "settings_service.h"

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
    /* 0: Desert Storm — warm sand tones, brass accents */
    {
        .bg             = 0xf5edd6,  /* warm parchment */
        .text_primary   = 0x2b1d0e,  /* dark espresso */
        .text_muted     = 0x8a7355,  /* desert khaki */
        .border         = 0xa8894a,  /* aged brass */
        .selection_bg   = 0xc9a84c,  /* burnished gold */
        .selection_text  = 0x1a1008,  /* near-black */
        .rule_glyph     = "=",
    },
    /* 1: Neon Grid — cyan/electric blue, cyberpunk feel */
    {
        .bg             = 0xe8f4fa,  /* ice blue */
        .text_primary   = 0x0a1e2e,  /* deep navy */
        .text_muted     = 0x3d6b82,  /* steel cyan */
        .border         = 0x00b4d8,  /* electric cyan */
        .selection_bg   = 0x0096c7,  /* vivid cyan */
        .selection_text  = 0xffffff,  /* white */
        .rule_glyph     = "~",
    },
    /* 2: Ocean Steel — cool blue-grey, industrial precision */
    {
        .bg             = 0xeaeff5,  /* blue mist */
        .text_primary   = 0x151d2b,  /* dark slate */
        .text_muted     = 0x546a87,  /* gunmetal blue */
        .border         = 0x3a6ea5,  /* deep steel blue */
        .selection_bg   = 0x2c5f8a,  /* navy steel */
        .selection_text  = 0xf0f5ff,  /* near-white */
        .rule_glyph     = "-",
    },
    /* 3: Solar Ember — amber/orange, warm energy */
    {
        .bg             = 0xfff0e0,  /* pale amber */
        .text_primary   = 0x2d1a08,  /* dark burnt sienna */
        .text_muted     = 0x8f5c2a,  /* warm copper */
        .border         = 0xd4700a,  /* tangerine */
        .selection_bg   = 0xe87a12,  /* bright ember */
        .selection_text  = 0xfff8f0,  /* cream */
        .rule_glyph     = "#",
    },
    /* 4: Monochrome Ops — pure greyscale, tactical */
    {
        .bg             = 0xf0f0f0,  /* light grey */
        .text_primary   = 0x121212,  /* near-black */
        .text_muted     = 0x606060,  /* medium grey */
        .border         = 0x404040,  /* charcoal */
        .selection_bg   = 0x2a2a2a,  /* dark grey */
        .selection_text  = 0xf0f0f0,  /* light grey */
        .rule_glyph     = ".",
    },
};

/* ── Dark palettes — each theme has its own dark background tint ────────── */
static const theme_palette_t k_dark_palettes[] = {
    /* 0: Desert Storm dark — deep warm charcoal, gold accents */
    {
        .bg             = 0x14100a,  /* dark walnut */
        .text_primary   = 0xe8dcc4,  /* warm cream */
        .text_muted     = 0x9a8563,  /* antique brass */
        .border         = 0x7a6332,  /* dark gold */
        .selection_bg   = 0x5c4a24,  /* deep bronze */
        .selection_text  = 0xf5edd6,  /* parchment */
        .rule_glyph     = "=",
    },
    /* 1: Neon Grid dark — deep midnight, electric cyan glow */
    {
        .bg             = 0x060e18,  /* midnight blue */
        .text_primary   = 0xc8f0ff,  /* ice glow */
        .text_muted     = 0x4da8c4,  /* muted cyan */
        .border         = 0x00a0c4,  /* neon cyan */
        .selection_bg   = 0x0d5a78,  /* deep teal */
        .selection_text  = 0xe0faff,  /* bright ice */
        .rule_glyph     = "~",
    },
    /* 2: Ocean Steel dark — deep navy, cool steel highlights */
    {
        .bg             = 0x0a1020,  /* abyss blue */
        .text_primary   = 0xd0dae8,  /* pale steel */
        .text_muted     = 0x7090b0,  /* slate blue */
        .border         = 0x3870a0,  /* steel accent */
        .selection_bg   = 0x1e3f60,  /* deep ocean */
        .selection_text  = 0xe0ebf5,  /* ice blue */
        .rule_glyph     = "-",
    },
    /* 3: Solar Ember dark — deep umber, orange fire accents */
    {
        .bg             = 0x160e04,  /* dark umber */
        .text_primary   = 0xffd8a8,  /* warm peach */
        .text_muted     = 0xc08040,  /* copper */
        .border         = 0xb85e10,  /* dark amber */
        .selection_bg   = 0x6b3810,  /* deep rust */
        .selection_text  = 0xffecd0,  /* warm cream */
        .rule_glyph     = "#",
    },
    /* 4: Monochrome Ops dark — near-black, surgical grey */
    {
        .bg             = 0x0c0c0c,  /* near-black */
        .text_primary   = 0xd8d8d8,  /* light grey */
        .text_muted     = 0x787878,  /* mid grey */
        .border         = 0x505050,  /* charcoal */
        .selection_bg   = 0x333333,  /* dark grey */
        .selection_text  = 0xe8e8e8,  /* near-white */
        .rule_glyph     = ".",
    },
};

static bool g_is_dark_mode;
static int g_light_theme;

static int clamp_theme_index(int idx) {
    if (idx < 0) return 0;
    if (idx > 4) return 4;
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
    g_is_dark_mode = settings_service_get_bool("night_mode");
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
