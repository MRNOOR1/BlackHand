# BlackHand OS — UI Audit Report
*Per the UI Audit Checklist. Method: static extraction of every input handler, render path, and persistence call from source. Items requiring the physical panel (contrast, debounce timing, key repeat feel) are marked HW-VERIFY.*

---

## Part 0 — Input model  ⚠ ONE CRITICAL FINDING

```
[SCREEN] global input layer (main.c normalize_control_key)
[CLASS]  spec-gap / unhandled-key
[KEYS]   Q='q'→soft-left, E='e'→soft-right, UP/DOWN/LEFT/RIGHT=NCKEY_*,
         numpad fallback 2/8/4/6, CENTER=Enter or '5', quit='x' (home only)
[EXPECT] W=up, S=down, A=left, D=right per the 6-key QWEASD hardware model
[ACTUAL] 'w','s','a','d' ARE HANDLED NOWHERE. If the keypad emits letter
         keycodes, navigation is 100% dead. It works today only because
         testing used a keyboard's arrow keys / numpad.
[FIX]    add w/s/a/d → UP/DOWN/LEFT/RIGHT in normalize_control_key, gated
         like the numpad remap (skip in text-entry modes — note: 'a','d','s','w'
         are also multitap letters, so the same is_*_mode() gate MUST apply).
```

**Center key: RESOLVED, consistent.** CENTER = `NCKEY_ENTER` everywhere, with `'5'` as the keypad alias (suspended in text-entry modes, where 5 types "JKL"). Every "center" reference below means Enter/5. This is one consistent mapping — passes, but it means the hardware needs a 7th key (or 5 on the numpad) for center. **Needs your sign-off: is center = keypad '5' the intended hardware mapping?**

Q+E hold: hands-white exit uses soft-key hold arming on HOME (`Exit 1/2 → 2/2`); no other screen consumes the combination. PASS (partial-hold release behavior: HW-VERIFY).
Key repeat: main loop drops `NCTYPE_REPEAT` events entirely — **holding W/S does NOT auto-repeat scroll** (defect class: unhandled-key; FIX: allow repeats for UP/DOWN only). Debounce: 45ms same-key suppression + RELEASE-duplicate suppression present. PASS (timing HW-VERIFY).

---

## Part A1 — Screen inventory (from code, not menu tree)

12 top-level screens, 34 sub-states:

| Screen | Sub-states found in code |
|---|---|
| HOME | menu, hands-white, hw PIN popup, exit-arm |
| CALLS | log, dial pad, incoming, dialing, active, port-select, delete-confirm |
| MESSAGES | thread list, thread view, compose, delete-confirm |
| CONTACTS | list, profile, edit (name/country/number), delete-confirm |
| MP3 | categories→collections→tracks, player, empty state |
| VOICE | list, recording, name-prompt, playback, rename-prompt |
| NOTES | list, view, edit, save-prompt, delete-confirm |
| ALARM | list, time-edit, ringing |
| THEME | picker |
| BLUETOOTH | off, idle (paired+nearby), scanning, device-detail, connecting |
| GPS | static view |
| SETTINGS | main, appearance, security, hw-pin, wipe-pin→confirm, pin-change, connectivity→BT, system-info |

Inventory vs. checklist expectations — **missing entirely**: BOOT, SHUTDOWN screens (no ceremony; init has no UI feed); SETTINGS: audio, storage, power leaves (also absent from the menu — B8-compliant absence, no blank screens). **Found in code but absent from design notes**: CALLS port-select (diagnostic, flagged not skipped), GPS screen (currently non-functional — modem methods unimplemented; see defect log).

All screens reachable from HOME; all return paths verified in code (see A2).

## Part A2 — Key map matrix

Legend: ✓ defined · ∅ explicit no-op (default case returns same screen — all screens have `default:` handlers, so **no accidental fall-throughs exist anywhere**) · letters = action.

| Screen.state | Q (LSK) | W/up | E (RSK) | A/left | S/down | D/right | CENTER |
|---|---|---|---|---|---|---|---|
| HOME.menu | exit-arm | sel− | open | ∅ | sel+ | ∅ | open |
| CALLS.log | home | sel− | dial pad | del-confirm | sel+ | port-select | call sel |
| CALLS.dial | cancel | ∅ | call | ∅ | ∅ | ∅ | call |
| CALLS.incoming | reject | ∅ | answer | reject | ∅ | answer | answer |
| CALLS.active | hangup | ∅ | ∅ | hangup | ∅ | ∅ | hangup |
| CALLS.ports | back | sel− | try | back | sel+ | ∅ | try |
| MSGS.threads | home | sel− | compose | del-confirm | sel+ | compose | open thread |
| MSGS.thread | back | scroll− | reply | back | scroll+ | ∅ | reply |
| MSGS.compose | cancel | field− | send | ∅ | field+ | ∅ | next field |
| CONTACTS.list | home | sel− | add | del-confirm | sel+ | ∅ | profile |
| CONTACTS.profile | back | action− | edit | ∅ | action+ | ∅ | do action |
| CONTACTS.edit | cancel | field− | save | country− | field+ | country+ | next/save |
| MP3.browse | back/home | sel− | home | ∅ | sel+ | ∅ | open |
| MP3.player | back(plays on) | vol+ | stop | prev | vol− | next | play/pause |
| VOICE.list | home | sel− | record | del-confirm | sel+ | rename | play |
| VOICE.rec | stop+save | ∅ | stop+save | ∅ | ∅ | ∅ | stop+save |
| VOICE.name | skip(default) | ∅ | save | ∅ | ∅ | ∅ | save |
| VOICE.play | back | speed+ | delete | seek− | speed− | seek+ | pause |
| NOTES.list | home | sel− | new | del-confirm | sel+ | edit | view |
| NOTES.view/edit | back→prompt | scroll/MT | save/edit | MT | scroll/MT | MT | — |
| ALARM.list | home | sel− | new | del-confirm | sel+ | edit | toggle |
| ALARM.ringing | snooze | ∅ | dismiss | ∅ | ∅ | ∅ | stop |
| THEME | settings | sel− | apply | settings | sel+ | apply | apply |
| BT.off | settings | ∅ | power on | settings | ∅ | ∅ | power on |
| BT.idle | settings | sel− | power off | settings | sel+ | ∅ | open/scan |
| BT.device | back | opt− | ∅ | back | opt+ | ∅ | connect/forget |
| GPS | back | ∅ | toggle gps | ∅ | ∅ | ∅ | refresh |
| SETTINGS.* | back/main | sel− | varies | varies | sel+ | varies | open/confirm |

**Dead ends: none.** Every state has a Q-path toward HOME (exceptions as designed: hands-white needs Q+E+PIN; alarm ringing needs stop/snooze/dismiss).

**Orphan actions — the bluetooth test, applied everywhere:**
- BT toggle ✓ (fixed this session: RSK powers on/off, was Enter-only) · connect ✓ (center on device) · forget ✓ (device detail, option 2) · paired list ✓
- Theme apply ✓ (center, instant + persisted)
- PIN change confirm path ✓ (settings, current→new→confirm)
- **GPS screen FAIL** — `[SCREEN] gps [CLASS] orphan-action [ACTUAL] E calls gps_enable → modem replies "unknown method" → every key's purpose fails [FIX] implement AT+CGPS methods in modem_dispatch or remove menu entry until then.`

**Soft-key consistency:** Q=back universal ✓. E is contextual and differs between siblings: notes/voice/alarm E=new, messages E=compose, MP3 browse E=jump-to-home, MP3 player E=stop, BT E=power. **UX decision for you:** MP3's E=home and BT's E=power break the "E creates/acts" pattern — intentional?

**Destructive confirmations:** delete note/memo/alarm/contact/call-log-entry, white-wipe — all show confirm popups with full key coverage ✓. **Exception:** `[SCREEN] voice.playback [CLASS] spec-gap [ACTUAL] E=delete goes through confirm ✓ but VOICE.rec Q stops AND SAVES (no discard path exists — you cannot abort a recording without saving) [FIX] decide: add discard, or document save-always.`

## Part A3 — Theme & mode matrix

- **Hardcoded colors:** render code is clean — screens pull from theme getters. Two legacy leaks: `frame_renderer.c` low-battery blink uses `COL_GHOST_LOW` (config.h constant), and ~10 `COL_*` constants remain defined in config.h with scattered single uses (keypad renderer, hints). `[CLASS] unthemed [FIX] map COL_GHOST_LOW→global destructive red; sweep remaining COL_* to tokens.]`
- **Mode switch:** **dark/light no longer exists** — implemented per Theme Spec v1.0 (all 8 themes are dark instruments; night-mode toggle removed). **This audit checklist asks for light variants of all 8 themes — direct contradiction with spec v1.0 §5. Needs your sign-off: dark-only (spec v1.0) or derived light variants (this checklist).** Currently: dark-only.
- Theme switch re-renders every screen instantly ✓ (all colors fetched per-frame; no cached colors anywhere).
- Persistence: theme index persists via settings file ✓; mode N/A (single mode).
- **Modals/overlays themed:** confirm popups ✓, PIN popups ✓, save prompts ✓ (all use ghost_* helpers → tokens).
- Empty states themed ✓ (all 8 list screens have themed empty messages).
- **Selection style coverage: FAIL (known, ranked #1 in UI-REVIEW.md).** `ghost_list_row` (6 styles) is live on HOME, THEME, SETTINGS.appearance only. Calls/Messages/Contacts/MP3/Notes/Voice/Alarm/BT lists use bespoke invert rendering — on RUSHNYK they show no diamonds, on REDLINE no brackets. `[FIX] mechanical conversion, one row-loop per screen.`
- Contrast on cheap TFT: HW-VERIFY (amber fg_deep #633806 and rushnyk #791F1F on near-black are the risk rows).

## Part A4 — State matrix

- **Empty data:** all list screens guard `count==0` with messages + working keys; W/S clamped, no phantom selection ✓.
- **Max data:** no enforced limits anywhere (notes/memos/alarms/contacts unbounded; comm caches cap at 128 silently). `[CLASS] undefined-state [FIX] define limits + "list full" messages; silent cap on calls/threads at 128 should at least be stated.`
- **Long strings:** lists truncate by width ✓ (snprintf precision/clip), but **no `A. VALE`-style name truncation, no number masking** (spec §1.6 unimplemented). Multi-line SMS bodies clip at one line in thread view (no wrap) — flagged.
- **Storage unavailable:** UI storage writes go through the storage daemon (atomic tmp+rename ✓); contacts/notes/settings local writes: notes uses rename for rename only, **content writes are direct fopen("w") — non-atomic** `[FIX] tmp+rename in notes_service/settings_service/alarm_service.` Settings parser falls back to defaults on missing file ✓; corrupt-file replacement: re-written on next save ✓.
- **Interrupt stack:** alarm fires → music auto-pauses ✓ → forced to SCREEN_ALARM. **Return path FAIL:** dismiss returns to HOME, not the prior screen. Static buffers mean note text/compose drafts survive (state intact when you navigate back manually), and recordings keep recording (voice_memo_service is independent of the visible screen — not lost, but also not surfaced). `[CLASS] undefined-state [FIX] save prev_screen at alarm fire, restore on dismiss.` Incoming call interrupt: same pattern, same fix. PIN entry/hands-white: alarm fire during hands-white DOES yank to alarm screen — security hole vs. feature? **Sign-off needed.**
- **Reboot mid-write:** storage daemon atomic ✓; local UI files not (above).

## Part B highlights (deltas from the checklist's expectations)

- **B1 Notes:** view/edit/save-prompt flow present; save prompt fires from edit ✓; D=edit, center=view ✓ via list keys. Rename collision: **overwrites silently** `[FIX] append suffix or block]`. Exiting read-only does NOT prompt save (only edit does) — *deviation from notes-spec, but arguably correct; flagged as UX decision rather than bug.*
- **B2 Voice:** naming prompt pre-fills date-stamp default ✓; **first-keypress-replaces-default NOT implemented** (cursor appends). Playback W/S=speed ✓ with on-screen speed indicator ✓; A/D seek ✓ (5s step). Recording stop = any of Q/E/center, always saves (see A2 flag).
- **B3 Music:** folder model ✓ (categories→collections→tracks, loose mp3s as tracks); scan on entry ✓ (10s cooldown); .mp3-only ✓; title from filename (underscores→spaces ✓), **ID3 metadata not read** (filename always) — minor spec deviation. Player shows track/collection/progress/duration/state ✓ live ✓. Queue/end-of-collection behavior ✓; background playback + return-to-player ✓; current-track highlight in list ✓. Volume: **MP3 volume ≠ OS volume** — mp3_service has its own volume vs. audio service master `[CLASS] spec-gap, two volume states — sign-off]`.
- **B6 Security:** **two divergent PIN components** (home hands-white popup vs. settings PIN screens) `[FIX] extract shared pin_entry widget]`. PIN storage: **plaintext in settings file** `[FLAG: hash it or accept for v0.4]`. Wrong-PIN retry timeout: not implemented (infinite retries) — **sign-off**. Hands-white battery: renders every frame (~30fps) — **no minute-tick idle render; battery claim not honored** `[FIX] skip render unless minute/state changed]`.
- **B7 Bluetooth:** all checklist items now pass except: disconnect-mid-audio UI indicator updates on next poll (~seconds, acceptable), audio fallback to default output handled by bluealsa routing — HW-VERIFY.
- **B9 Alarm:** list/edit/toggle/persist ✓; ringing keys ✓ (center=stop, Q=snooze 5min, E=dismiss); snooze re-fires ✓; **no snooze count limit** (infinite) — sign-off; **alarms while powered off: not possible, no RTC wake — must be documented**; 20-alarm limit: not enforced (unbounded).

---

## Spec-gaps requiring Mohammad's sign-off (silent defaults found in code)

1. W/S/A/D letter keys unmapped — confirm hardware emits arrows, or approve the mapping fix (CRITICAL).
2. Center key = Enter/'5' — confirm against actual keypad hardware.
3. Dark-only themes (spec v1.0) vs. light variants (this checklist) — pick one.
4. Recording abort: currently impossible — every stop saves. Add discard?
5. MP3 vs. system volume: two independent volumes today.
6. PIN: plaintext storage, infinite retries, no lockout.
7. Alarm: infinite snoozes, no RTC wake (no alarms while off), no 20-alarm cap.
8. Alarm/call interrupt returns to HOME, not prior screen.
9. Notes rename collision overwrites; viewing a note doesn't prompt save (edit does).
10. E-key semantics differ across sibling apps (MP3 E=home/stop, others E=new).
11. Hands-white during alarm fire exits to alarm screen (bypasses the lock visually).

## Sign-off status

**FAIL** (as expected for this stage) — blocking items: Part 0 W/S/A/D mapping, selection-style coverage on 8 list screens, GPS orphan screen, alarm interrupt return-path. Everything else is enumerated above with one-line fixes. No screen lacks theming; no accidental unhandled keys exist (every handler has a default case); the two cited failure classes from the brief (bluetooth dead-end, unthemed screens) are now impossible.
