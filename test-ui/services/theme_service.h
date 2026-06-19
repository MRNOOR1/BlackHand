#ifndef THEME_SERVICE_H
#define THEME_SERVICE_H
/*
 * theme_service.h
 * ----------------------------------------------------------------------------
 * BlackHand OS theme engine. Implements the 12-theme token model from the
 * UI Design Pack (v1.1). Apps own layout; themes own color, selection style,
 * status-strip format, divider style and footer. Every screen reads ONLY from
 * the active theme through the accessors below -- no screen draws raw colors.
 *
 *   1  amber-ledger     5  rushnyk          9  blueprint
 *   2  phosphor-index   6  thermal-index   10  dossier (light)
 *   3  redline-mono     7  instrument      11  one-bit
 *   4  vale-signal      8  boot-rite       12  polar-night
 * ----------------------------------------------------------------------------
 */
#include <stdint.h>

/* How the selected row of a list is rendered. */
typedef enum {
    SEL_CURSOR,   /* invert bar + "> " cursor          (amber, vale)        */
    SEL_INDEX,    /* "0n LABEL" + trailing block        (phosphor)          */
    SEL_BRACKET,  /* centered "[ LABEL ]"               (redline)           */
    SEL_DIAMOND,  /* "diamond LABEL" filled/hollow      (rushnyk, boot)     */
    SEL_HEAT,     /* heat-color row + marker            (thermal)           */
    SEL_REG,      /* invert + "0x0n" register address   (instrument)        */
    SEL_BOX,      /* 1px outlined box + "A.n" ref       (blueprint)         */
    SEL_INK,      /* solid ink bar                      (dossier)           */
    SEL_BIT,      /* full invert, bold                  (one-bit)           */
    SEL_STAR      /* "* " prefix, white text            (polar)             */
} sel_style_t;

/* Status-strip layout for row 0 (and row 1 on the two-row instrument theme). */
typedef enum {
    ST_GLYPH,     /* signal-ramp . HH:MM . [battery]    */
    ST_TEXT,      /* SIM:OK . HH:MM . PWR:NN            */
    ST_TEXT2,     /* SIG:3/4 . HH:MM . PWR:NN           */
    ST_RITE,      /* teal text telemetry               */
    ST_UTC,       /* two-row UTC + RSSI/VBAT telemetry  */
    ST_BITS,      /* block counts . HH:MM . blocks      */
    ST_THERMAL    /* heat-colored signal + battery      */
} status_style_t;

/* Divider style for the rule under the status strip. */
typedef enum {
    DV_SOLID,     /* 1px line                          */
    DV_LABEL,     /* labeled rule  -- TAG --           (redline) */
    DV_BAND,      /* folk ornament band                (rushnyk, boot) */
    DV_DIM,       /* dimension line | ---- TAG ---- |  (blueprint) */
    DV_DBL,       /* double rule                       (dossier) */
    DV_CHK,       /* checker band                      (one-bit) */
    DV_AUR,       /* aurora band + rule                (polar) */
    DV_DOT        /* dotted rule                       (instrument) */
} div_style_t;

/* Footer style for the bottom strip. */
typedef enum {
    FT_WORDMARK,  /* BLACKHAND . v0.4                  */
    FT_REDLINE,   /* corner ticks + BH-OS              (redline) */
    FT_DIAMOND,   /* * BLACKHAND *                     (rushnyk) */
    FT_UTC,       /* sparkline + NET:LTE               (instrument) */
    FT_RITE,      /* INIT OK *                         (boot) */
    FT_DOSSIER,   /* REF: BLACKHAND/0.4                (dossier) */
    FT_BIT,       /* inverted tag + wordmark           (one-bit) */
    FT_POLAR      /* GPS coordinates                   (polar) */
} footer_style_t;

typedef struct {
    const char    *id;         /* "amber-ledger"        */
    const char    *label;      /* "Amber Ledger" (menu) */
    uint32_t       bg;         /* background            */
    uint32_t       bright;     /* selected / active     */
    uint32_t       mid;        /* unselected items      */
    uint32_t       dim;        /* hints, secondary      */
    uint32_t       deep;       /* dividers, faint marks */
    uint32_t       sel_bg;     /* inverted-bar bg (0 = use bright) */
    uint32_t       sel_fg;     /* text on sel_bg  (0 = use bg)     */
    uint32_t       accent;     /* identity accent (red/aurora; 0 = none) */
    uint32_t       accent2;    /* secondary accent              */
    sel_style_t    sel_style;
    status_style_t status_style;
    div_style_t    div_style;
    footer_style_t footer_style;
    uint8_t        is_light;   /* paper-tone (dossier)  */
    uint8_t        is_heat;    /* thermal histogram menu*/
    uint8_t        has_stamp;  /* red rotated stamp     */
    uint8_t        is_vale;    /* event-styled amber    */
} bh_theme_t;

/* lifecycle ----------------------------------------------------------------- */
void theme_service_init(void);
void theme_service_sync_from_settings(void);

/* the active theme ---------------------------------------------------------- */
const bh_theme_t *theme_active(void);
int   theme_active_index(void);
int   theme_count(void);
const bh_theme_t *theme_at(int index);

/* token accessors (legacy names preserved for existing screens) ------------- */
uint32_t theme_bg(void);
uint32_t theme_text_primary(void);   /* -> bright */
uint32_t theme_text_muted(void);     /* -> mid    */
uint32_t theme_border(void);         /* -> deep   */
uint32_t theme_dim(void);            /* -> dim    */
uint32_t theme_deep(void);           /* -> deep   */
uint32_t theme_accent(void);         /* identity accent (falls back to bright) */
uint32_t theme_selection_bg(void);
uint32_t theme_selection_text(void);
const char *theme_rule_glyph(void);
int theme_service_is_dark(void);     /* 1 unless the active theme is light */

/* global semantic action colors (NOT theme tokens; darken on light themes) -- */
uint32_t theme_affirm_border(void);
uint32_t theme_affirm_text(void);
uint32_t theme_destruct_border(void);
uint32_t theme_destruct_text(void);

#endif
