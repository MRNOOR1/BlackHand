# BlackHand OS — UI Review & Theme Spec v1.0 Implementation Status
*Reviewed as a third-party user walk-through (static analysis of every screen's draw + input paths). 2026-06-11.*

---

## Part 1 — Findings from the walkthrough

### Fixed during this review
1. **Dark-mode toggle was a lie.** Settings → Appearance had `[ON] DARK MODE`, but with the 8 spec themes (all dark instruments) it changed nothing. Removed; Appearance now shows `THEME ▸ AMBER-LEDGER` and opens the picker. The settings header shows the active theme id.
2. **Bluetooth `[On]` softkey was dead** in the OFF state — only Enter powered on. Wired.
3. **Connected BT device invisible** — now named in the header: `BLUETOOTH [ON] ▸ WH-1000XM4`.
4. **Call log stale** — cache never re-read after the modem wrote history. Sync on screen entry + 10s periodic.
5. **Theme picker** listed 5 obsolete palettes — now lists the 8 spec themes, applies instantly, persists.
6. **Caller-ID mismatch** between `+61…` and `0…` formats — last-9-digit matching.

### Open findings (ranked)
1. **Selection styles are only live on Home / Theme / Appearance rows.** Calls, Messages, Contacts, MP3, Notes, Alarm, Bluetooth lists still use bespoke invert-only rendering — on REDLINE-MONO or RUSHNYK these lists won't show bracket/diamond selection. Each list needs its row loop swapped to `ghost_list_row()` (mechanical, one screen at a time).
2. **CALL_INCOMING action cells** — spec wants bordered `ACPT`/`REJ` cells in global semantic green/red. Constants exist (`BH_AFFIRM_*`, `BH_DESTRUCT_*` in theme_service.h); screen_calls still uses softkeys.
3. **No BOOT/SHUTDOWN ceremony.** UI starts straight into Home. The boot-rite layout needs an init→UI event feed (service manager already starts everything in order; it could write `/run/bh-boot-state` lines the UI replays).
4. **Truncation rules not enforced** — long contact names overflow rather than truncating to `A. VALE`; numbers are never masked. Spec §1 rule 6.
5. **Thermal heat buckets unbound** — needs a 24h rolling launch counter (suggest: count screen entries in the UI, persist via settings). Falls back to invert selection.
6. **Instrument telemetry partial** — shows `SIG n/4 · PWR n% · NET:UP` (live, honest), but not `RSSI −67dBm` / `VBAT 3.81V` (needs CSQ raw + AT+CBC plumbed through modem_status → hardware service). Sparkline not implemented.
7. **Spec footers unimplemented** — wordmark/readout-box/sparkline footers conflict with the functional softkey row; softkeys won. Decide: move spec footer content to row above softkeys, or accept softkeys as the footer.
8. **GPS screen still dead** (modem `gps_*` methods unimplemented — pre-existing, on the P1 list).
9. **Dead config tokens** — `UI_THEME_LIGHT/DARK_*` constants in config.h are now unused; trim when convenient.
10. **Transitions** are hard cuts (no animation). Appropriate for an instrument; noting for completeness.

### What passed review
Back-paths from every screen reach Home (no traps); delete-confirm popups consistent; numpad→arrow remapping correctly suspended in all 7 text-entry modes; multitap consistent across compose/contacts/notes; offline banners on Calls/Messages accurate after the health-cache fix; port picker honest about results; no screen draws outside the content region.

---

## Part 2 — Theme spec implementation status

**Engine** (`theme_service.[ch]`): token struct per spec §4 — bg, fg_bright/mid/dim/deep, accent, sel_bg/fg, `sel_style`, `div_style`, `status_style`. Themes are pure data; zero theme-id branching anywhere in render code. Legacy getters mapped so all 12 screens compile untouched. Semantic action colors are global constants, not tokens.

**The 8 themes** — exact spec hex values:

| # | id | sel | div | status |
|---|---|---|---|---|
| 1 | AMBER-LEDGER | INVERT `> ` | solid | `▂▄▆ · HH:MM · [▓▓░]` |
| 2 | PHOSPHOR-INDEX | INDEX `01 …■` | solid | `SIM:OK · HH:MM · PWR:82` |
| 3 | REDLINE-MONO | BRACKET `[ X ]` | `── MENU ──` | `▮▮▯▯ · HH:MM · 78%` |
| 4 | VALE-SIGNAL | INVERT | solid | glyph |
| 5 | RUSHNYK | DIAMOND ◆/◇ | ◆╳ band | glyph |
| 6 | THERMAL-INDEX | HEAT (→invert*) | solid | glyph |
| 7 | INSTRUMENT | INVERT | dotted | 2-row `UTC HH:MM:SS ┼` + telemetry |
| 8 | BOOT-RITE | DIAMOND | ◆╳ band | glyph |

\* pending launch-counter binding.

**Renderers:** status strip = 4 formats in `draw_status_bar` switched on token; divider = 4 styles in `draw_frame`; selection = 6 styles in `ghost_list_row` (draw_utils). Ornament stays ceremonial: the ◆╳ band renders only in the frame divider; in-screen rules are plain.

**Acceptance checklist position:** 6 of 10 items done or partial; remaining: per-screen list conversion, heat binding, RSSI/VBAT binding + sparkline, boot ceremony, truncation rules.

**Rebuild:** `make blackhand-ui-dirclean && make`.
