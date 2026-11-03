# Graph Report - BlackHand  (2026-09-08)

## Corpus Check
- 169 files · ~172,198 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1602 nodes · 4747 edges · 57 communities (45 shown, 10 thin omitted)
- Extraction: 65% EXTRACTED · 35% INFERRED · 0% AMBIGUOUS · INFERRED: 1663 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `1d2aad26`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- cJSON_Delete
- cJSON.c
- main
- Modem.c
- src/services/voice_memo_service.c
- audio_alsa.c
- screen_contacts_input
- modem_core.c
- src/services/bluetooth_service.c
- test-ui/services/voice_memo_service.c
- test-ui/services/mp3_service.c
- main
- theme_text_muted
- draw_now_playing
- theme_text_muted
- blackhand-ui (notcurses client)
- blackhand-ui/src/main.c
- alarm_service.c
- src/services/settings_service.c
- multitap_reset
- src/services/comm_service.c
- test-ui/services/settings_service.c
- test-ui/services/theme_service.c
- src/services/theme_service.c
- sx1276.c
- test-ui/services/bluetooth_service.c
- screen_gps_input
- hyperpixel4-init.c
- wipe_phone_data
- src/services/notes_service.c
- init.c
- recv_msg
- screen_calls_input
- phone_normalize
- wipe_phone_data
- panel.bin firmware init blob
- screen_contacts_input
- fbfill
- mkpanelbin.py
- verify-image.sh
- rcS
- theme_service token engine
- rcK
- use-panel.sh
- post-build.sh
- post-image.sh
- setup.sh
- 12-Month Classical Drawing Roadmap note
- Math self-study roadmap note
- Kernel fragment merges onto bcm2711_defconfig
- PIN stored plaintext, no lockout
- test-ui/bh_skin.c
- screen_messages_draw
- src/bh_skin.c
- draw_playback

## God Nodes (most connected - your core abstractions)
1. `cJSON_Delete()` - 81 edges
2. `main()` - 73 edges
3. `main()` - 53 edges
4. `cJSON_GetObjectItem()` - 51 edges
5. `cJSON_AddStringToObject()` - 46 edges
6. `cJSON_CreateObject()` - 46 edges
7. `theme_text_muted()` - 37 edges
8. `ghost_softkeys()` - 35 edges
9. `theme_text_primary()` - 35 edges
10. `cJSON_AddNumberToObject()` - 33 edges

## Surprising Connections (you probably didn't know these)
- `SQLite storage backend (README, outdated)` --semantically_similar_to--> `blackhand-storage service`  [INFERRED] [semantically similar]
  README.md → Phase-2/CODE-REVIEW.md
- `LVGL touchscreen UI (README, outdated)` --semantically_similar_to--> `blackhand-ui (notcurses client)`  [INFERRED] [semantically similar]
  README.md → Phase-2/CODE-REVIEW.md
- `test-ui CMake build (dev tree)` --semantically_similar_to--> `blackhand-ui CMake build (production)`  [INFERRED] [semantically similar]
  test-ui/CMakeLists.txt → Phase-2/blackhand-external/package/blackhand-ui/src/CMakeLists.txt
- `Offline STT engine (Vosk/Whisper.cpp)` --semantically_similar_to--> `blackhand-stt (placeholder voice assistant)`  [INFERRED] [semantically similar]
  README.md → Phase-2/CODE-REVIEW.md
- `Black Hand OS Layered Architecture` --conceptually_related_to--> `Phase 2: Buildroot Embedded Linux`  [INFERRED]
  CLAUDE.md → README.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **IPC service mesh: UI client + audio/modem/storage servers over libblackhand-ipc** — phase_2_code_review_blackhand_ui, phase_2_code_review_blackhand_audio, phase_2_code_review_blackhand_modem, phase_2_code_review_blackhand_storage, phase_2_code_review_blackhand_ipc [EXTRACTED 0.95]
- **README stack (LVGL/SQLite) diverged from implemented stack (notcurses/JSON files)** — readme_lvgl_ui, readme_sqlite_storage, phase_2_code_review_blackhand_ui, phase_2_code_review_blackhand_storage [INFERRED 0.75]
- **SPI display bring-up: config.txt overlay -> panel-mipi-dbi -> panel.bin (ST7789P/NV3030B)** — phase_2_blackhand_external_board_blackhand_rpi_firmware_config_mipi_dbi_spi_overlay, phase_2_display_setup_brief_panel_mipi_dbi, phase_2_display_setup_brief_panel_bin, phase_2_display_setup_brief_st7789p, phase_2_display_setup_brief_nv3030b [INFERRED 0.85]

## Communities (57 total, 10 thin omitted)

### Community 0 - "cJSON_Delete"
Cohesion: 0.06
Nodes (142): GpsLocation, ModemHealth, ModemStatus, send_result_json(), main(), cJSON_AddItemToArray(), cJSON_AddItemToObject(), cJSON_AddNumberToObject() (+134 more)

### Community 1 - "cJSON.c"
Cohesion: 0.06
Nodes (96): cJSON_bool, cJSON_Hooks, internal_hooks, parse_buffer, add_item_to_array(), add_item_to_object(), buffer_skip_whitespace(), cJSON (+88 more)

### Community 2 - "main"
Cohesion: 0.10
Nodes (43): main(), screen_id, screen_mp3_input(), add_track(), Mp3Category, Mp3Collection, Mp3Track, clear_visualizer() (+35 more)

### Community 3 - "Modem.c"
Cohesion: 0.06
Nodes (65): modem_bringup_thread(), call_session_answered(), call_session_end(), call_session_set_number(), call_session_start(), cJSON, handle_answer(), handle_dial() (+57 more)

### Community 4 - "src/services/voice_memo_service.c"
Cohesion: 0.05
Nodes (73): dbus_bool_t, dbus_int64_t, DBusConnection, DBusHandlerResult, DBusMessage, DBusMessageIter, dispatch(), find_headphone_device() (+65 more)

### Community 5 - "audio_alsa.c"
Cohesion: 0.06
Nodes (91): device_visit_fn, apply_volume(), audio_get_volume(), audio_has_capture(), audio_init(), audio_is_paused(), audio_is_playing(), audio_is_recording() (+83 more)

### Community 6 - "screen_contacts_input"
Cohesion: 0.08
Nodes (52): begin_add_contact(), begin_edit_contact(), Contact, screen_id, ensure_mt(), put_clipped(), save_contact(), screen_contacts_input() (+44 more)

### Community 7 - "modem_core.c"
Cohesion: 0.08
Nodes (30): at_parse_cgpsinfo(), at_parse_clip(), at_parse_csq(), nmea_to_deg(), cJSON, modem_call_answer(), modem_call_dial(), modem_call_get_active() (+22 more)

### Community 8 - "src/services/bluetooth_service.c"
Cohesion: 0.10
Nodes (48): build_named_device_list(), BtDevice, screen_id, cached_device_connected(), compute_display_offsets(), draw_idle(), get_device_at_display_index(), screen_bluetooth_draw() (+40 more)

### Community 9 - "test-ui/services/voice_memo_service.c"
Cohesion: 0.09
Nodes (46): screen_id, handle_name_input(), screen_voice_memo_input(), FILE, VMPlayMode, VMState, VoiceMemo, ensure_capacity() (+38 more)

### Community 10 - "test-ui/services/mp3_service.c"
Cohesion: 0.08
Nodes (54): screen_id, draw_categories(), draw_collections(), draw_now_playing(), draw_tracks(), list_row(), put_clipped(), screen_mp3_draw() (+46 more)

### Community 11 - "main"
Cohesion: 0.08
Nodes (32): ncinput, create_phone_plane(), create_screen_plane(), debug_record_event(), key_matches_binding(), main(), normalize_control_key(), hardware_cleanup() (+24 more)

### Community 12 - "theme_text_muted"
Cohesion: 0.14
Nodes (41): ghost_confirm_popup(), ghost_fill_rect(), ghost_label_value(), ghost_softkeys(), ghost_text(), keypad_layout, draw_button(), draw_button_fixed() (+33 more)

### Community 13 - "draw_now_playing"
Cohesion: 0.38
Nodes (11): draw_categories(), draw_collections(), draw_now_playing(), draw_tracks(), list_row(), mp3_vol(), put_clipped(), screen_mp3_draw() (+3 more)

### Community 14 - "theme_text_muted"
Cohesion: 0.16
Nodes (40): ghost_confirm_popup(), ghost_fill_rect(), ghost_label_value(), ghost_softkeys(), ghost_text(), draw_connecting(), draw_device(), draw_header() (+32 more)

### Community 15 - "blackhand-ui (notcurses client)"
Cohesion: 0.07
Nodes (35): Core Engineering Philosophy, Black Hand OS Layered Architecture, Safe Simplification Process, Shared Knowledge Directory (.claude/knowledge), Black Hand project note (features + learning method), test note (sample seeded note), cmdline.txt kernel command line, blackhand-ui CMake build (production) (+27 more)

### Community 16 - "blackhand-ui/src/main.c"
Cohesion: 0.10
Nodes (14): ghost_hline(), ghost_set(), draw_battery(), draw_signal(), draw_status_bar(), keypad_layout, draw_button(), draw_button_fixed() (+6 more)

### Community 17 - "alarm_service.c"
Cohesion: 0.15
Nodes (27): Alarm, screen_id, screen_alarm_input(), start_time_prompt_edit(), alarm_service_create(), alarm_service_current_ringing(), alarm_service_delete(), alarm_service_dismiss_current() (+19 more)

### Community 18 - "src/services/settings_service.c"
Cohesion: 0.08
Nodes (31): screen_id, screen_home_input(), screen_id, handle_pin_input(), reset_pin_entry(), screen_settings_input(), screen_id, screen_theme_input() (+23 more)

### Community 19 - "multitap_reset"
Cohesion: 0.21
Nodes (16): ensure_mt(), enter_compose(), screen_messages_start_compose(), begin_new_note_edit(), screen_id, screen_notes_input(), multitap_state, multitap_apply_key() (+8 more)

### Community 20 - "src/services/comm_service.c"
Cohesion: 0.16
Nodes (25): screen_id, screen_messages_input(), cJSON, CommMessage, call_icon(), call_type(), comm_service_call_add(), comm_service_call_add_full() (+17 more)

### Community 21 - "test-ui/services/settings_service.c"
Cohesion: 0.12
Nodes (16): screen_id, screen_theme_input(), find_index_by_key(), settings_service_get_light_theme(), settings_service_init(), settings_service_load(), settings_service_save(), settings_service_set_bool() (+8 more)

### Community 22 - "test-ui/services/theme_service.c"
Cohesion: 0.17
Nodes (25): bh_action_cells(), bh_list_item(), draw_delete_popup(), screen_contacts_draw(), screen_theme_draw(), build_dark_theme(), bh_theme_t, clamp_index() (+17 more)

### Community 23 - "src/services/theme_service.c"
Cohesion: 0.19
Nodes (21): bh_action_cells(), screen_theme_draw(), settings_service_get_light_theme(), build_dark_theme(), bh_theme_t, clamp_index(), dark(), theme_accent() (+13 more)

### Community 24 - "sx1276.c"
Cohesion: 0.17
Nodes (18): rx_thread(), rxq_count(), rxq_pop(), rxq_push(), burst_read(), burst_write(), msleep(), open_pinctrl_chip() (+10 more)

### Community 25 - "test-ui/services/bluetooth_service.c"
Cohesion: 0.19
Nodes (19): screen_id, screen_bluetooth_input(), bluetooth_service_connect(), bluetooth_service_device_at(), bluetooth_service_device_count(), bluetooth_service_disconnect(), bluetooth_service_get_power(), bluetooth_service_init() (+11 more)

### Community 27 - "hyperpixel4-init.c"
Cohesion: 0.24
Nodes (19): clock_tick(), delay_us(), detect_gpio_base(), gpio_direction(), gpio_export(), gpio_mmap_init(), gpio_open_value(), gpio_set() (+11 more)

### Community 28 - "wipe_phone_data"
Cohesion: 0.17
Nodes (16): screen_id, screen_home_input(), screen_id, handle_pin_input(), reset_pin_entry(), screen_settings_input(), wipe_phone_data(), comm_service_reset() (+8 more)

### Community 29 - "src/services/notes_service.c"
Cohesion: 0.25
Nodes (17): begin_edit_existing(), enter_save_prompt(), save_edit(), Note, ensure_capacity(), insert_at_front(), make_safe_filename(), notes_service_create() (+9 more)

### Community 30 - "init.c"
Cohesion: 0.12
Nodes (34): pid_t, time_t, kchild_signals(), kdo_shutdown(), klog(), kmono_sec(), kmount_vfs(), kopen_tty() (+26 more)

### Community 31 - "recv_msg"
Cohesion: 0.13
Nodes (28): ipc_client_connect(), ipc_close(), ipc_server_listen(), recv_all(), recv_msg(), elapsed_ms(), main(), nap_ms() (+20 more)

### Community 32 - "screen_calls_input"
Cohesion: 0.18
Nodes (16): screen_id, time_t, enter_dial(), enter_log(), is_dial_char(), lookup_name_by_phone(), mask_number(), monotonic_now() (+8 more)

### Community 34 - "phone_normalize"
Cohesion: 0.36
Nodes (7): CountryCode, country_by_iso(), phone_is_e164(), phone_normalize(), strip_separators(), normalize_dial(), test_country_codes()

### Community 35 - "wipe_phone_data"
Cohesion: 0.18
Nodes (12): save_contact(), wipe_phone_data(), comm_service_reset(), Contact, contact_service_create(), contact_service_delete(), contact_service_get_by_id(), contact_service_init() (+4 more)

### Community 38 - "panel.bin firmware init blob"
Cohesion: 0.23
Nodes (12): NV3030B init sequence (panel-nv3030b.txt), ST7789P init sequence (panel.txt), config.txt (Pi firmware boot config), CST816D capacitive touch on I2C1, RFM95W LoRa radio on SPI0 CE1 (spidev0.1), mipi-dbi-spi overlay (240x304, write-only), CONFIG_EXTRA_FIRMWARE built-in blob hook, NV3030B controller (fallback panel silicon) (+4 more)

### Community 40 - "screen_contacts_input"
Cohesion: 0.26
Nodes (12): country_table(), begin_add_contact(), begin_edit_contact(), Contact, CountryCode, screen_id, current_country(), cycle_country() (+4 more)

### Community 41 - "fbfill"
Cohesion: 0.48
Nodes (4): fbfill script, build(), fill(), px()

### Community 42 - "mkpanelbin.py"
Cohesion: 0.43
Nodes (6): build_stream(), main(), parse_byte(), Parse a decimal or 0x-hex token into an int in 0..255., Re-parse the blob exactly as the kernel does, to prove it is consumable., verify()

### Community 43 - "verify-image.sh"
Cohesion: 0.70
Nodes (4): fail(), pass(), verify-image.sh script, warn()

### Community 44 - "rcS"
Cohesion: 0.60
Nodes (5): rcS script, cap_logs(), save_clock(), say(), write_asound_conf()

### Community 45 - "theme_service token engine"
Cohesion: 0.50
Nodes (4): ghost_list_row selection-style renderer, Dark/light mode toggle removed (dark-only), theme_service token engine, Theme Spec v1.0 (8 dark-instrument themes)

### Community 57 - "test-ui/bh_skin.c"
Cohesion: 0.12
Nodes (21): band_fill(), battery_bracket(), bh_divider(), bh_divider_in_status(), bh_footer(), bh_screen_tag(), bh_status_strip(), clock_hm() (+13 more)

### Community 59 - "screen_messages_draw"
Cohesion: 0.24
Nodes (14): bh_list_item(), draw_row(), draw_delete_popup(), screen_calls_draw(), draw_delete_popup(), screen_contacts_draw(), draw_delete_popup(), screen_messages_draw() (+6 more)

### Community 62 - "src/bh_skin.c"
Cohesion: 0.15
Nodes (19): band_fill(), battery_bracket(), bh_divider(), bh_divider_in_status(), bh_footer(), bh_screen_tag(), bh_status_strip(), clock_hm() (+11 more)

### Community 63 - "draw_playback"
Cohesion: 0.32
Nodes (12): draw_list(), draw_playback(), draw_recording(), ensure_multitap(), format_time_ms(), memo_display_name(), put_clipped(), screen_voice_memo_draw() (+4 more)

## Knowledge Gaps
- **23 isolated node(s):** `use-panel.sh script`, `post-build.sh script`, `post-image.sh script`, `setup.sh script`, `Shared Knowledge Directory (.claude/knowledge)` (+18 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 153 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **10 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `main()` connect `main` to `cJSON_Delete`, `screen_calls_input`, `wipe_phone_data`, `src/services/voice_memo_service.c`, `src/services/bluetooth_service.c`, `screen_contacts_input`, `draw_now_playing`, `theme_text_muted`, `blackhand-ui/src/main.c`, `src/services/settings_service.c`, `multitap_reset`, `src/services/comm_service.c`, `src/services/theme_service.c`, `screen_gps_input`, `screen_messages_draw`, `src/services/notes_service.c`, `src/bh_skin.c`?**
  _High betweenness centrality (0.200) - this node is a cross-community bridge._
- **Why does `cJSON_Delete()` connect `cJSON_Delete` to `cJSON.c`, `main`, `Modem.c`, `audio_alsa.c`, `src/services/comm_service.c`, `recv_msg`?**
  _High betweenness centrality (0.103) - this node is a cross-community bridge._
- **Why does `cJSON_GetObjectItem()` connect `cJSON_Delete` to `cJSON.c`, `main`, `Modem.c`, `audio_alsa.c`, `src/services/comm_service.c`?**
  _High betweenness centrality (0.045) - this node is a cross-community bridge._
- **Are the 56 inferred relationships involving `cJSON_Delete()` (e.g. with `send_error()` and `send_result()`) actually correct?**
  _`cJSON_Delete()` has 56 INFERRED edges - model-reasoned connections that need verification._
- **Are the 69 inferred relationships involving `main()` (e.g. with `cJSON_Delete()` and `cJSON_GetObjectItem()`) actually correct?**
  _`main()` has 69 INFERRED edges - model-reasoned connections that need verification._
- **Are the 48 inferred relationships involving `main()` (e.g. with `ghost_text()` and `draw_frame()`) actually correct?**
  _`main()` has 48 INFERRED edges - model-reasoned connections that need verification._
- **Are the 46 inferred relationships involving `cJSON_GetObjectItem()` (e.g. with `dispatch()` and `param_int()`) actually correct?**
  _`cJSON_GetObjectItem()` has 46 INFERRED edges - model-reasoned connections that need verification._