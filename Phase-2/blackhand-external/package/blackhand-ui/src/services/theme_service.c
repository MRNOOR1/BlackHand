/*
 * theme_service.c — the 12 BlackHand themes (Design Pack v1.1).
 * Tokens match the interactive prototype exactly. Theme = data only.
 */
#include "theme_service.h"
#include "settings_service.h"

static const bh_theme_t k_themes[] = {
    /* 1 — amber-ledger: avionics amber. The issued-equipment default. */
    { .id="AMBER-LEDGER",  .bg=0x0B0B09, .fg_bright=0xFAC775, .fg_mid=0xBA7517,
      .fg_dim=0x854F0B, .fg_deep=0x633806, .accent=0xFAC775, .accent2=0xFAC775,
      .sel_bg=0xFAC775, .sel_fg=0x0B0B09,
      .sel_style=SEL_INVERT, .div_style=DIV_SOLID,
      .status_style=STATUS_GLYPH, .footer_style=FOOTER_WORDMARK },

    /* 2 — phosphor-index: green CRT registers. */
    { .id="PHOSPHOR-INDEX", .bg=0x0A0D08, .fg_bright=0xC0DD97, .fg_mid=0x97C459,
      .fg_dim=0x639922, .fg_deep=0x3B6D11, .accent=0xC0DD97, .accent2=0xC0DD97,
      .sel_bg=0xC0DD97, .sel_fg=0x0A0D08,
      .sel_style=SEL_INDEX, .div_style=DIV_SOLID,
      .status_style=STATUS_TEXT, .footer_style=FOOTER_READOUT },

    /* 3 — redline-mono: tactical HUD; red is a scalpel (≤6 chars/screen). */
    { .id="REDLINE-MONO",  .bg=0x0B0B0B, .fg_bright=0xD3D1C7, .fg_mid=0x888780,
      .fg_dim=0x5F5E5A, .fg_deep=0x444441, .accent=0xE24B4A, .accent2=0xF09595,
      .sel_bg=0xA32D2D, .sel_fg=0xF09595,
      .sel_style=SEL_BRACKET, .div_style=DIV_LABELED,
      .status_style=STATUS_BARS, .footer_style=FOOTER_CORNERS },

    /* 4 — vale-signal: amber tuned for event screens. */
    { .id="VALE-SIGNAL",   .bg=0x0B0B09, .fg_bright=0xFAC775, .fg_mid=0xBA7517,
      .fg_dim=0x854F0B, .fg_deep=0x633806, .accent=0xFAC775, .accent2=0xFAC775,
      .sel_bg=0xFAC775, .sel_fg=0x0B0B09,
      .sel_style=SEL_INVERT, .div_style=DIV_SOLID,
      .status_style=STATUS_GLYPH, .footer_style=FOOTER_WORDMARK },

    /* 5 — rushnyk: folk embroidery; ornament is ceremonial. */
    { .id="RUSHNYK",       .bg=0x0C0A0A, .fg_bright=0xF7C1C1, .fg_mid=0xF09595,
      .fg_dim=0xA32D2D, .fg_deep=0x791F1F, .accent=0xE24B4A, .accent2=0xF7C1C1,
      .sel_bg=0xA32D2D, .sel_fg=0xF7C1C1,
      .sel_style=SEL_DIAMOND, .div_style=DIV_ORNAMENT,
      .status_style=STATUS_GLYPH, .footer_style=FOOTER_STAR },

    /* 6 — thermal-index: heatmap; color = 24h usage. */
    { .id="THERMAL-INDEX", .bg=0x0B0B0C, .fg_bright=0xD3D1C7, .fg_mid=0x888780,
      .fg_dim=0x5F5E5A, .fg_deep=0x2C2C2A, .accent=0xE24B4A, .accent2=0xF09595,
      .sel_bg=0xE24B4A, .sel_fg=0x0B0B0C,
      .sel_style=SEL_HEAT, .div_style=DIV_SOLID,
      .status_style=STATUS_THERMAL, .footer_style=FOOTER_HEAT },

    /* 7 — instrument-bench: lab equipment, teal, registers + telemetry. */
    { .id="INSTRUMENT",    .bg=0x0A0B0B, .fg_bright=0x9FE1CB, .fg_mid=0x5DCAA5,
      .fg_dim=0x1D9E75, .fg_deep=0x0F6E56, .accent=0x5DCAA5, .accent2=0x9FE1CB,
      .sel_bg=0x085041, .sel_fg=0xE1F5EE,
      .sel_style=SEL_REG, .div_style=DIV_DOTTED,
      .status_style=STATUS_INSTRUMENT, .footer_style=FOOTER_SPARK },

    /* 8 — boot-rite: rushnyk identity + instrument log (teal = doing). */
    { .id="BOOT-RITE",     .bg=0x0C0A0A, .fg_bright=0xF7C1C1, .fg_mid=0xF09595,
      .fg_dim=0xA32D2D, .fg_deep=0x791F1F, .accent=0x5DCAA5, .accent2=0x5DCAA5,
      .sel_bg=0xA32D2D, .sel_fg=0xF7C1C1,
      .sel_style=SEL_DIAMOND, .div_style=DIV_ORNAMENT,
      .status_style=STATUS_RITE, .footer_style=FOOTER_RITE },

    /* 9 — blueprint: drawing on blue paper; selection is an outline. */
    { .id="BLUEPRINT",     .bg=0x123A78, .fg_bright=0xEAF3FC, .fg_mid=0x7FB3E8,
      .fg_dim=0x4A7BBF, .fg_deep=0x2C5CA0, .accent=0xEAF3FC, .accent2=0xEAF3FC,
      .sel_bg=0xEAF3FC, .sel_fg=0x123A78,
      .sel_style=SEL_BOX, .div_style=DIV_DIMENSION,
      .status_style=STATUS_GLYPH, .footer_style=FOOTER_TITLEBLOCK },

    /* 10 — dossier: the reference light theme. Ink on paper, red stamp. */
    { .id="DOSSIER",       .bg=0xE9E3D1, .fg_bright=0x26241F, .fg_mid=0x3D3A33,
      .fg_dim=0x6B665B, .fg_deep=0x9A937F, .accent=0xA32D2D, .accent2=0xA32D2D,
      .sel_bg=0x26241F, .sel_fg=0xE9E3D1,
      .sel_style=SEL_INK, .div_style=DIV_DOUBLE,
      .status_style=STATUS_TEXT2, .footer_style=FOOTER_REF,
      .light=1, .stamp=1 },

    /* 11 — one-bit: black and white, nothing else. Power-saver mode. */
    { .id="ONE-BIT",       .bg=0x000000, .fg_bright=0xFFFFFF, .fg_mid=0xFFFFFF,
      .fg_dim=0xFFFFFF, .fg_deep=0xFFFFFF, .accent=0xFFFFFF, .accent2=0xFFFFFF,
      .sel_bg=0xFFFFFF, .sel_fg=0x000000,
      .sel_style=SEL_BIT, .div_style=DIV_CHECKER,
      .status_style=STATUS_BITS, .footer_style=FOOTER_BIT },

    /* 12 — polar-night: aurora over dark blue; ✶ ties to rushnyk star. */
    { .id="POLAR-NIGHT",   .bg=0x0A0A16, .fg_bright=0xD8E6F2, .fg_mid=0x5F7390,
      .fg_dim=0x5F7390, .fg_deep=0x2A3450, .accent=0xC77DDB, .accent2=0xFFFFFF,
      .sel_bg=0x2A3450, .sel_fg=0xFFFFFF,
      .sel_style=SEL_STAR, .div_style=DIV_AURORA,
      .status_style=STATUS_GLYPH, .footer_style=FOOTER_COORDS },
};

#define THEME_N ((int)(sizeof(k_themes) / sizeof(k_themes[0])))

static int g_active = 0;

static int clamp_idx(int idx)
{
    if (idx < 0) return 0;
    if (idx >= THEME_N) return THEME_N - 1;
    return idx;
}

void theme_service_init(void)               { theme_service_sync_from_settings(); }
void theme_service_sync_from_settings(void) { g_active = clamp_idx(settings_service_get_light_theme()); }

const bh_theme_t *theme_active(void) { return &k_themes[g_active]; }
int               theme_count(void)  { return THEME_N; }

const char *theme_id_at(int index)
{
    if (index < 0 || index >= THEME_N) return "";
    return k_themes[index].id;
}

uint32_t       theme_fg_deep(void)      { return k_themes[g_active].fg_deep; }
uint32_t       theme_accent(void)       { return k_themes[g_active].accent; }
uint32_t       theme_accent2(void)      { return k_themes[g_active].accent2; }
sel_style_t    theme_sel_style(void)    { return k_themes[g_active].sel_style; }
div_style_t    theme_div_style(void)    { return k_themes[g_active].div_style; }
status_style_t theme_status_style(void) { return k_themes[g_active].status_style; }
footer_style_t theme_footer_style(void) { return k_themes[g_active].footer_style; }
int            theme_is_light(void)     { return k_themes[g_active].light; }

uint32_t theme_bg(void)             { return k_themes[g_active].bg; }
uint32_t theme_text_primary(void)   { return k_themes[g_active].fg_bright; }
uint32_t theme_text_muted(void)     { return k_themes[g_active].fg_mid; }
uint32_t theme_border(void)         { return k_themes[g_active].fg_dim; }
uint32_t theme_selection_bg(void)   { return k_themes[g_active].sel_bg; }
uint32_t theme_selection_text(void) { return k_themes[g_active].sel_fg; }

const char *theme_rule_glyph(void)
{
    /* In-screen rules only — bands/checkers/auroras live in the frame
     * divider; ornament is ceremonial, never wallpaper. */
    switch (k_themes[g_active].div_style) {
        case DIV_DOTTED: return "┄";
        case DIV_DOUBLE: return "═";
        default:         return "─";
    }
}

int theme_service_is_dark(void) { return k_themes[g_active].light ? 0 : 1; }
