# Graph Report - BlackHand  (2026-09-09)

## Corpus Check
- 146 files · ~138,187 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1293 nodes · 3889 edges · 50 communities (38 shown, 10 thin omitted)
- Extraction: 65% EXTRACTED · 35% INFERRED · 0% AMBIGUOUS · INFERRED: 1373 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `1d2aad26`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- cJSON_Delete
- cJSON.c
- mp3_service.c
- Modem.c
- voice_memo_service.c
- asoundlib.h
- BlackHand — finished-product feature logic
- modem_core.c
- bluetooth_service.c
- avrcp_service.c
- audio_alsa.c
- rc_thread_main
- blackhand-audio/src/ipc_dispatch.c
- screen_mp3_input
- main
- blackhand-ui (notcurses client)
- blackhand-ui/src/main.c
- bt_route.c
- settings_service.c
- multitap_reset
- comm_service.c
- screen_messages_input
- add_track
- wav_file_header
- sx1276.c
- README.md
- hyperpixel4-init.c
- init.c
- send_msg
- screen_calls.c
- phone_normalize
- sync_from_storage
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

## God Nodes (most connected - your core abstractions)
1. `main()` - 87 edges
2. `cJSON_Delete()` - 84 edges
3. `cJSON_GetObjectItem()` - 54 edges
4. `cJSON_AddStringToObject()` - 51 edges
5. `cJSON_CreateObject()` - 50 edges
6. `ghost_softkeys()` - 40 edges
7. `theme_text_muted()` - 36 edges
8. `cJSON_AddNumberToObject()` - 35 edges
9. `ghost_text()` - 35 edges
10. `theme_text_primary()` - 35 edges

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

## Communities (50 total, 10 thin omitted)

### Community 0 - "cJSON_Delete"
Cohesion: 0.06
Nodes (127): GpsLocation, ModemHealth, ModemStatus, send_result_json(), cJSON_AddItemToArray(), cJSON_AddItemToObject(), cJSON_AddNumberToObject(), cJSON_AddStringToObject() (+119 more)

### Community 1 - "cJSON.c"
Cohesion: 0.06
Nodes (94): cJSON_bool, cJSON_Hooks, internal_hooks, parse_buffer, add_item_to_array(), add_item_to_object(), buffer_skip_whitespace(), cJSON (+86 more)

### Community 2 - "mp3_service.c"
Cohesion: 0.14
Nodes (25): clear_visualizer(), ensure_category_capacity(), get_current_queue_size(), get_current_track_path(), mp3_service_current_collection_name(), mp3_service_current_track_name(), mp3_service_direct_track_count(), mp3_service_get_collection() (+17 more)

### Community 3 - "Modem.c"
Cohesion: 0.05
Nodes (78): ModemGpsFix, gps_get_enabled(), gps_get_fix(), gps_parse_info(), gps_reset_state(), gps_set_enabled(), modem_bringup_thread(), call_session_answered() (+70 more)

### Community 4 - "voice_memo_service.c"
Cohesion: 0.09
Nodes (49): screen_voice_memo_input(), audio_policy_call_active(), ensure_capacity(), insert_at_front(), make_filename_from_title(), make_timestamp_filename(), monotonic_ms(), path_exists() (+41 more)

### Community 5 - "asoundlib.h"
Cohesion: 0.20
Nodes (23): apply_volume(), audio_get_volume(), audio_set_volume(), mixer_try_attach(), snd_mixer_attach(), snd_mixer_close(), snd_mixer_elem_next(), snd_mixer_first_elem() (+15 more)

### Community 6 - "BlackHand — finished-product feature logic"
Cohesion: 0.06
Nodes (33): 10. Voice memos, 11. Notes, 12. Alarms and clock, 13. Storage and file management, 14. Activity history, 15. Notifications and Do Not Disturb, 16. Themes, display, and accessibility, 17. Security and lock screen (+25 more)

### Community 7 - "modem_core.c"
Cohesion: 0.09
Nodes (26): at_parse_cgpsinfo(), at_parse_csq(), nmea_to_deg(), cJSON, modem_call_answer(), modem_call_dial(), modem_call_get_active(), modem_call_hangup() (+18 more)

### Community 8 - "bluetooth_service.c"
Cohesion: 0.12
Nodes (37): screen_id, screen_bluetooth_input(), bluetooth_service_connect(), bluetooth_service_connect_async(), bluetooth_service_connect_is_running(), bluetooth_service_disconnect(), bluetooth_service_get_connected_mac(), bluetooth_service_get_power() (+29 more)

### Community 9 - "avrcp_service.c"
Cohesion: 0.20
Nodes (26): dbus_bool_t, dbus_int64_t, DBusConnection, DBusHandlerResult, DBusMessage, DBusMessageIter, append_bool_variant(), append_dict_bool() (+18 more)

### Community 10 - "audio_alsa.c"
Cohesion: 0.15
Nodes (22): audio_has_capture(), audio_is_paused(), audio_is_playing(), audio_is_recording(), audio_play_elapsed_ms(), audio_play_pause(), audio_play_resume(), audio_play_seek() (+14 more)

### Community 11 - "rc_thread_main"
Cohesion: 0.17
Nodes (22): audio_init(), audio_play_start(), audio_record_start(), dlog(), pb_open_pcm(), rc_open_pcm(), rc_probe_capture(), rc_thread_main() (+14 more)

### Community 12 - "blackhand-audio/src/ipc_dispatch.c"
Cohesion: 0.34
Nodes (19): cJSON, dispatch(), handle_ping(), handle_play_pause(), handle_play_resume(), handle_play_seek(), handle_play_start(), handle_play_stop() (+11 more)

### Community 13 - "screen_mp3_input"
Cohesion: 0.29
Nodes (15): mp3_playback_state, screen_id, draw_categories(), draw_collections(), draw_now_playing(), draw_tracks(), list_row(), mp3_vol() (+7 more)

### Community 14 - "main"
Cohesion: 0.05
Nodes (119): battery_status_t, bh_theme_t, cellular_status_t, keypad_layout, band_fill(), battery_bracket(), bh_action_cells(), bh_divider() (+111 more)

### Community 15 - "blackhand-ui (notcurses client)"
Cohesion: 0.06
Nodes (40): Core Engineering Philosophy, Black Hand OS Layered Architecture, Safe Simplification Process, Shared Knowledge Directory (.claude/knowledge), Black Hand project note (features + learning method), test note (sample seeded note), cmdline.txt kernel command line, blackhand-ui CMake build (production) (+32 more)

### Community 16 - "blackhand-ui/src/main.c"
Cohesion: 0.09
Nodes (15): create_phone_plane(), create_screen_plane(), normalize_control_key(), dispatch(), find_headphone_device(), has_bit(), headphone_input_init(), headphone_input_shutdown() (+7 more)

### Community 17 - "bt_route.c"
Cohesion: 0.26
Nodes (12): device_visit_fn, bt_route_find_a2dp_sink(), bt_route_set_default(), FILE, commit_route(), find_visit(), for_each_connected(), is_mac_char() (+4 more)

### Community 18 - "settings_service.c"
Cohesion: 0.05
Nodes (62): Note, screen_messages_reset_draft(), begin_edit_existing(), begin_new_note_edit(), screen_id, enter_save_prompt(), save_edit(), screen_notes_flush() (+54 more)

### Community 19 - "multitap_reset"
Cohesion: 0.25
Nodes (15): multitap_state, screen_id, ensure_multitap(), handle_name_input(), start_name_prompt(), start_rename_prompt(), multitap_apply_key(), multitap_backspace() (+7 more)

### Community 20 - "comm_service.c"
Cohesion: 0.17
Nodes (21): CommCall, CommMessage, cJSON, call_icon(), call_type(), comm_service_call_add(), comm_service_call_at(), comm_service_init() (+13 more)

### Community 21 - "screen_messages_input"
Cohesion: 0.24
Nodes (10): screen_id, ensure_mt(), enter_compose(), screen_messages_flush(), screen_messages_input(), screen_messages_start_compose(), comm_service_message_count(), comm_service_message_delete() (+2 more)

### Community 22 - "add_track"
Cohesion: 0.24
Nodes (11): Mp3Category, Mp3Collection, Mp3Track, add_track(), duration_from_mp3(), ensure_collection_capacity(), ensure_track_capacity(), scan_category_dir() (+3 more)

### Community 23 - "wav_file_header"
Cohesion: 0.33
Nodes (6): FILE, le16(), le32(), wav_file_append(), wav_file_header(), main()

### Community 24 - "sx1276.c"
Cohesion: 0.17
Nodes (18): rx_thread(), rxq_count(), rxq_pop(), rxq_push(), burst_read(), burst_write(), msleep(), open_pinctrl_chip() (+10 more)

### Community 27 - "hyperpixel4-init.c"
Cohesion: 0.24
Nodes (19): clock_tick(), delay_us(), detect_gpio_base(), gpio_direction(), gpio_export(), gpio_mmap_init(), gpio_open_value(), gpio_set() (+11 more)

### Community 30 - "init.c"
Cohesion: 0.10
Nodes (38): pid_t, time_t, kchild_signals(), kdo_shutdown(), klog(), kmono_sec(), kmount_vfs(), kopen_tty() (+30 more)

### Community 31 - "send_msg"
Cohesion: 0.13
Nodes (31): frame_failed(), ipc_client_connect(), ipc_close(), ipc_server_accept(), ipc_server_listen(), recv_all(), recv_msg(), elapsed_ms() (+23 more)

### Community 32 - "screen_calls.c"
Cohesion: 0.11
Nodes (27): screen_id, time_t, enter_dial(), enter_log(), is_dial_char(), lookup_name_by_phone(), mask_number(), monotonic_now() (+19 more)

### Community 34 - "phone_normalize"
Cohesion: 0.39
Nodes (6): CountryCode, country_by_iso(), phone_is_e164(), phone_normalize(), strip_separators(), test_country_codes()

### Community 35 - "sync_from_storage"
Cohesion: 0.26
Nodes (11): save_contact(), Contact, contact_service_create(), contact_service_delete(), contact_service_get_by_id(), contact_service_init(), contact_service_list_all(), contact_service_shutdown() (+3 more)

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

## Knowledge Gaps
- **59 isolated node(s):** `use-panel.sh script`, `post-build.sh script`, `post-image.sh script`, `setup.sh script`, `Product goal and working assumptions` (+54 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 159 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **10 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `main()` connect `main` to `cJSON_Delete`, `screen_calls.c`, `mp3_service.c`, `sync_from_storage`, `voice_memo_service.c`, `screen_contacts_input`, `bluetooth_service.c`, `avrcp_service.c`, `screen_mp3_input`, `blackhand-ui/src/main.c`, `settings_service.c`, `comm_service.c`, `screen_messages_input`?**
  _High betweenness centrality (0.345) - this node is a cross-community bridge._
- **Why does `cJSON_Delete()` connect `cJSON_Delete` to `cJSON.c`, `Modem.c`, `sync_from_storage`, `voice_memo_service.c`, `audio_alsa.c`, `blackhand-audio/src/ipc_dispatch.c`, `main`, `comm_service.c`?**
  _High betweenness centrality (0.156) - this node is a cross-community bridge._
- **Why does `cJSON_GetObjectItem()` connect `cJSON_Delete` to `cJSON.c`, `Modem.c`, `sync_from_storage`, `voice_memo_service.c`, `blackhand-audio/src/ipc_dispatch.c`, `main`, `comm_service.c`?**
  _High betweenness centrality (0.088) - this node is a cross-community bridge._
- **Are the 83 inferred relationships involving `main()` (e.g. with `cJSON_Delete()` and `cJSON_GetObjectItem()`) actually correct?**
  _`main()` has 83 INFERRED edges - model-reasoned connections that need verification._
- **Are the 59 inferred relationships involving `cJSON_Delete()` (e.g. with `send_error()` and `send_result()`) actually correct?**
  _`cJSON_Delete()` has 59 INFERRED edges - model-reasoned connections that need verification._
- **Are the 49 inferred relationships involving `cJSON_GetObjectItem()` (e.g. with `dispatch()` and `param_int()`) actually correct?**
  _`cJSON_GetObjectItem()` has 49 INFERRED edges - model-reasoned connections that need verification._
- **Are the 46 inferred relationships involving `cJSON_AddStringToObject()` (e.g. with `send_error()` and `send_result()`) actually correct?**
  _`cJSON_AddStringToObject()` has 46 INFERRED edges - model-reasoned connections that need verification._