/*
 * theme_service.h — BlackHand OS theme engine v2 (Design Pack, 12 themes).
 *
 * Core rule (blueprints §0): apps own LAYOUT; themes own color, selection
 * style, status format, divider style and footer. Every screen renders the
 * same fixed skeleton; the renderer reads ONLY style enums + tokens from
 * here. Zero layout branching on theme id anywhere.
 *
 * Legacy getters (theme_bg, theme_text_primary, ...) remain mapped so all
 * screens compile:  text_primary→fg_bright · text_muted→fg_mid ·
 * border→fg_dim · selection_*→sel_*.
 */
#ifndef THEME_SERVICE_H
#define THEME_SERVICE_H

#include <stdint.h>

/* ── selection styles ───────────────────────────────────────────────────── */
typedef enum {
    SEL_INVERT,    /* inverted bar + "> " cursor       amber / vale       */
    SEL_INDEX,     /* "0n LABEL" + trailing ■          phosphor           */
    SEL_BRACKET,   /* centered [ LABEL ] in accent     redline            */
    SEL_DIAMOND,   /* ◆ filled / ◇ hollow              rushnyk, boot-rite */
    SEL_HEAT,      /* heat block █ + colored label     thermal            */
    SEL_REG,       /* invert + 0x0n register meta      instrument         */
    SEL_BOX,       /* 1px outlined row, no fill        blueprint          */
    SEL_INK,       /* solid ink bar, ··· meta dim      dossier            */
    SEL_BIT,       /* full invert, bold                one-bit            */
    SEL_STAR,      /* ✶ prefix, white text             polar-night        */
} sel_style_t;

/* ── divider styles (row 1; labeled styles show the screen tag) ─────────── */
typedef enum {
    DIV_SOLID,
    DIV_DOTTED,    /* instrument                                          */
    DIV_ORNAMENT,  /* ◆╳◆╳ band — rushnyk, boot-rite                      */
    DIV_LABELED,   /* ── TAG ──  in accent — redline                      */
    DIV_DIMENSION, /* |—— TAG ——|  drawing dimension line — blueprint     */
    DIV_DOUBLE,    /* ═══ double rule — dossier                           */
    DIV_CHECKER,   /* ▀▄▀▄ — one-bit                                      */
    DIV_AURORA,    /* ▁▂▄▆▄▂▁ aurora band + rule — polar-night            */
} div_style_t;

/* ── status strip formats (row 0; instrument uses rows 0-1) ─────────────── */
typedef enum {
    STATUS_GLYPH,      /* ▂▄▆ · HH:MM · [▓▓░]                             */
    STATUS_TEXT,       /* SIM:OK · HH:MM · PWR:82       phosphor          */
    STATUS_BARS,       /* ▮▮▯▯ · HH:MM · 78%            redline           */
    STATUS_TEXT2,      /* SIG:3/4 · HH:MM · PWR:78      dossier           */
    STATUS_RITE,       /* SIM:OK · HH:MM · PWR:82 teal  boot-rite         */
    STATUS_BITS,       /* ███░ · HH:MM · ██░░           one-bit           */
    STATUS_THERMAL,    /* cold sig · clock · 4 heat-block battery         */
    STATUS_INSTRUMENT, /* UTC HH:MM:SS ┼ / telemetry row                  */
} status_style_t;

/* ── footer styles (rows 18-19; action cells replace on decision screens) ─ */
typedef enum {
    FOOTER_WORDMARK,   /* BLACKHAND · v0.4               amber / vale     */
    FOOTER_READOUT,    /* bordered  SEL 01/04 · RDY      phosphor         */
    FOOTER_CORNERS,    /* ◤  BH-OS  ◥                    redline          */
    FOOTER_STAR,       /* ✶ BLACKHAND ✶ centered         rushnyk          */
    FOOTER_HEAT,       /* HEAT=USE · σ 24H               thermal          */
    FOOTER_SPARK,      /* ▁▂▄▂▁ sparkline · NET:LTE      instrument       */
    FOOTER_RITE,       /* INIT OK · ✶                    boot-rite        */
    FOOTER_TITLEBLOCK, /* BLACKHAND · SHT n/n            blueprint        */
    FOOTER_REF,        /* REF: BLACKHAND/0.4             dossier          */
    FOOTER_BIT,        /* inverted TAG · BLACKHAND       one-bit          */
    FOOTER_COORDS,     /* LAT · LON                      polar-night      */
} footer_style_t;

typedef struct {
    const char    *id;
    uint32_t       bg;
    uint32_t       fg_bright;  /* b — selected/active text                 */
    uint32_t       fg_mid;     /* m — unselected                           */
    uint32_t       fg_dim;     /* d — dividers, hints                      */
    uint32_t       fg_deep;    /* x — bands, faint marks, rules            */
    uint32_t       accent;     /* a1 — scalpel accent (redline/polar)      */
    uint32_t       accent2;    /* a2 — bright accent (redline brackets)    */
    uint32_t       sel_bg;
    uint32_t       sel_fg;
    sel_style_t    sel_style;
    div_style_t    div_style;
    status_style_t status_style;
    footer_style_t footer_style;
    uint8_t        light;      /* dossier: paper bg — semantics darken     */
    uint8_t        stamp;      /* dossier: red stamp on MENU               */
} bh_theme_t;

/* ── global semantic action colors (design pack §2) — NOT theme tokens ──── */
#define BH_AFFIRM_BORDER         0x97C459
#define BH_AFFIRM_TEXT           0xC0DD97
#define BH_DESTRUCT_BORDER       0xA32D2D
#define BH_DESTRUCT_TEXT         0xF09595
/* on light (dossier-derived) backgrounds: */
#define BH_AFFIRM_BORDER_LIGHT   0x2F8A3D
#define BH_AFFIRM_TEXT_LIGHT     0x1F6B2E
#define BH_DESTRUCT_BORDER_LIGHT 0xA32D2D
#define BH_DESTRUCT_TEXT_LIGHT   0x8E2424

void theme_service_init(void);
void theme_service_sync_from_settings(void);

const bh_theme_t *theme_active(void);
int               theme_count(void);
const char       *theme_id_at(int index);

uint32_t       theme_fg_deep(void);
uint32_t       theme_accent(void);
uint32_t       theme_accent2(void);
sel_style_t    theme_sel_style(void);
div_style_t    theme_div_style(void);
status_style_t theme_status_style(void);
footer_style_t theme_footer_style(void);
int            theme_is_light(void);

/* legacy getters (mapped) */
uint32_t theme_bg(void);
uint32_t theme_text_primary(void);
uint32_t theme_text_muted(void);
uint32_t theme_border(void);
uint32_t theme_selection_bg(void);
uint32_t theme_selection_text(void);
const char *theme_rule_glyph(void);
int theme_service_is_dark(void);

#endif
