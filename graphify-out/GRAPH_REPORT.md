# Graph Report - BlackHand  (2026-10-11)

## Corpus Check
- 144 files · ~140,352 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1277 nodes · 3865 edges · 47 communities (35 shown, 10 thin omitted)
- Extraction: 65% EXTRACTED · 35% INFERRED · 0% AMBIGUOUS · INFERRED: 1354 edges (avg confidence: 0.85)
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
- audio_alsa.c
- BlackHand — finished-product feature logic
- modem_core.c
- bluetooth_service.c
- storage_service.c
- settings_service.c
- main
- theme_service_sync_from_settings
- screen_notes.c
- ghost_softkeys
- blackhand-ui (notcurses client)
- blackhand-ui/src/main.c
- pin_service.c
- notes_service.c
- screen_messages_input
- comm_service.c
- screen_settings_input
- sx1276.c
- README.md
- hyperpixel4-init.c
- init.c
- recv_msg
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

## God Nodes (most connected - your core abstractions)
1. `cJSON_Delete()` - 89 edges
2. `main()` - 87 edges
3. `cJSON_GetObjectItem()` - 55 edges
4. `cJSON_AddStringToObject()` - 49 edges
5. `cJSON_CreateObject()` - 48 edges
6. `ghost_softkeys()` - 40 edges
7. `ghost_text()` - 35 edges
8. `theme_text_muted()` - 35 edges
9. `theme_text_primary()` - 34 edges
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

## Communities (47 total, 10 thin omitted)

### Community 0 - "cJSON_Delete"
Cohesion: 0.05
Nodes (137): GpsLocation, ModemHealth, ModemStatus, audio_play_stop(), cJSON, dispatch(), handle_ping(), handle_play_pause() (+129 more)

### Community 1 - "cJSON.c"
Cohesion: 0.06
Nodes (97): cJSON_bool, cJSON_Hooks, internal_hooks, parse_buffer, add_item_to_array(), add_item_to_object(), buffer_skip_whitespace(), cJSON (+89 more)

### Community 2 - "mp3_service.c"
Cohesion: 0.07
Nodes (63): dbus_bool_t, dbus_int64_t, DBusConnection, DBusHandlerResult, DBusMessage, DBusMessageIter, Mp3Category, Mp3Collection (+55 more)

### Community 3 - "Modem.c"
Cohesion: 0.06
Nodes (58): ModemGpsFix, gps_get_enabled(), gps_get_fix(), gps_parse_info(), gps_reset_state(), gps_set_enabled(), modem_bringup_thread(), call_session_answered() (+50 more)

### Community 4 - "voice_memo_service.c"
Cohesion: 0.08
Nodes (54): dispatch(), set_volume(), sync_volume(), screen_id, screen_voice_memo_input(), audio_policy_call_active(), mp3_service_pause(), mp3_service_resume() (+46 more)

### Community 5 - "audio_alsa.c"
Cohesion: 0.06
Nodes (83): device_visit_fn, apply_volume(), audio_get_volume(), audio_has_capture(), audio_init(), audio_is_paused(), audio_is_playing(), audio_is_recording() (+75 more)

### Community 6 - "BlackHand — finished-product feature logic"
Cohesion: 0.06
Nodes (33): 10. Voice memos, 11. Notes, 12. Alarms and clock, 13. Storage and file management, 14. Activity history, 15. Notifications and Do Not Disturb, 16. Themes, display, and accessibility, 17. Security and lock screen (+25 more)

### Community 7 - "modem_core.c"
Cohesion: 0.11
Nodes (3): cJSON, modem_call_get_active(), modem_sms_list()

### Community 8 - "bluetooth_service.c"
Cohesion: 0.12
Nodes (39): screen_id, screen_bluetooth_input(), bluetooth_service_connect(), bluetooth_service_connect_async(), bluetooth_service_connect_is_running(), bluetooth_service_disconnect(), bluetooth_service_get_connected_mac(), bluetooth_service_get_power() (+31 more)

### Community 9 - "storage_service.c"
Cohesion: 0.22
Nodes (38): cJSON_AddItemToArray(), cJSON_CreateString(), cJSON_DeleteItemFromArray(), cJSON_GetArrayItem(), cJSON_GetArraySize(), cJSON_ReplaceItemInObject(), cJSON, ensure_dirs() (+30 more)

### Community 10 - "settings_service.c"
Cohesion: 0.18
Nodes (12): find_index_by_key(), settings_service_init(), settings_service_load(), settings_service_reset_defaults(), settings_service_save(), settings_service_set_bool(), settings_service_set_brightness(), settings_service_set_timeout_sec() (+4 more)

### Community 11 - "main"
Cohesion: 0.12
Nodes (14): battery_status_t, cellular_status_t, create_phone_plane(), create_screen_plane(), main(), hardware_cleanup(), hardware_get_battery(), hardware_get_cellular() (+6 more)

### Community 12 - "theme_service_sync_from_settings"
Cohesion: 0.16
Nodes (16): bh_theme_t, screen_id, screen_home_input(), screen_id, screen_theme_input(), settings_service_get_bool(), settings_service_get_light_theme(), settings_service_set_light_theme() (+8 more)

### Community 13 - "screen_notes.c"
Cohesion: 0.36
Nodes (11): begin_edit_existing(), begin_new_note_edit(), screen_id, draw_delete_popup(), draw_list(), enter_save_prompt(), save_edit(), screen_notes_flush() (+3 more)

### Community 14 - "ghost_softkeys"
Cohesion: 0.06
Nodes (111): keypad_layout, mp3_playback_state, band_fill(), battery_bracket(), bh_action_cells(), bh_divider(), bh_divider_in_status(), bh_footer() (+103 more)

### Community 15 - "blackhand-ui (notcurses client)"
Cohesion: 0.06
Nodes (40): Core Engineering Philosophy, Black Hand OS Layered Architecture, Safe Simplification Process, Shared Knowledge Directory (.claude/knowledge), Black Hand project note (features + learning method), test note (sample seeded note), cmdline.txt kernel command line, blackhand-ui CMake build (production) (+32 more)

### Community 16 - "blackhand-ui/src/main.c"
Cohesion: 0.11
Nodes (6): normalize_control_key(), find_headphone_device(), has_bit(), headphone_input_init(), headphone_input_shutdown(), thread_fn()

### Community 17 - "pin_service.c"
Cohesion: 0.35
Nodes (11): derive(), monotonic_ms(), pin_service_init(), pin_service_lockout_remaining_sec(), pin_service_reset(), pin_service_update(), pin_service_verify(), store_pin() (+3 more)

### Community 18 - "notes_service.c"
Cohesion: 0.21
Nodes (16): Note, FILE, file_store_begin(), file_store_commit(), ensure_capacity(), insert_at_front(), make_safe_filename(), notes_service_create() (+8 more)

### Community 19 - "screen_messages_input"
Cohesion: 0.20
Nodes (19): multitap_state, screen_id, ensure_mt(), enter_compose(), screen_messages_flush(), screen_messages_input(), screen_messages_start_compose(), ensure_multitap() (+11 more)

### Community 20 - "comm_service.c"
Cohesion: 0.08
Nodes (42): CommCall, CommMessage, screen_id, time_t, enter_dial(), enter_log(), is_dial_char(), lookup_name_by_phone() (+34 more)

### Community 21 - "screen_settings_input"
Cohesion: 0.83
Nodes (4): screen_id, handle_pin_input(), reset_pin_entry(), screen_settings_input()

### Community 24 - "sx1276.c"
Cohesion: 0.18
Nodes (17): rx_thread(), rxq_count(), rxq_pop(), rxq_push(), burst_read(), burst_write(), msleep(), open_pinctrl_chip() (+9 more)

### Community 27 - "hyperpixel4-init.c"
Cohesion: 0.24
Nodes (19): clock_tick(), delay_us(), detect_gpio_base(), gpio_direction(), gpio_export(), gpio_mmap_init(), gpio_open_value(), gpio_set() (+11 more)

### Community 30 - "init.c"
Cohesion: 0.10
Nodes (38): pid_t, time_t, kchild_signals(), kdo_shutdown(), klog(), kmono_sec(), kmount_vfs(), kopen_tty() (+30 more)

### Community 31 - "recv_msg"
Cohesion: 0.10
Nodes (32): frame_failed(), ipc_client_connect(), ipc_close(), ipc_server_accept(), ipc_server_listen(), recv_all(), recv_msg(), elapsed_ms() (+24 more)

### Community 34 - "phone_normalize"
Cohesion: 0.36
Nodes (7): CountryCode, country_by_iso(), phone_is_e164(), phone_normalize(), strip_separators(), normalize_dial(), test_country_codes()

### Community 35 - "wipe_phone_data"
Cohesion: 0.20
Nodes (16): save_contact(), screen_messages_reset_draft(), screen_notes_reset_editor(), wipe_phone_data(), Contact, contact_service_create(), contact_service_data_corrupt(), contact_service_delete() (+8 more)

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
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 169 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **10 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `main()` connect `main` to `cJSON_Delete`, `mp3_service.c`, `wipe_phone_data`, `voice_memo_service.c`, `bluetooth_service.c`, `screen_contacts_input`, `settings_service.c`, `theme_service_sync_from_settings`, `screen_notes.c`, `ghost_softkeys`, `blackhand-ui/src/main.c`, `pin_service.c`, `notes_service.c`, `screen_messages_input`, `comm_service.c`, `screen_settings_input`?**
  _High betweenness centrality (0.323) - this node is a cross-community bridge._
- **Why does `cJSON_Delete()` connect `cJSON_Delete` to `cJSON.c`, `wipe_phone_data`, `storage_service.c`, `main`, `comm_service.c`?**
  _High betweenness centrality (0.153) - this node is a cross-community bridge._
- **Why does `cJSON_GetObjectItem()` connect `cJSON_Delete` to `cJSON.c`, `wipe_phone_data`, `storage_service.c`, `main`, `comm_service.c`?**
  _High betweenness centrality (0.084) - this node is a cross-community bridge._
- **Are the 64 inferred relationships involving `cJSON_Delete()` (e.g. with `send_error()` and `send_result()`) actually correct?**
  _`cJSON_Delete()` has 64 INFERRED edges - model-reasoned connections that need verification._
- **Are the 83 inferred relationships involving `main()` (e.g. with `cJSON_Delete()` and `cJSON_GetObjectItem()`) actually correct?**
  _`main()` has 83 INFERRED edges - model-reasoned connections that need verification._
- **Are the 50 inferred relationships involving `cJSON_GetObjectItem()` (e.g. with `dispatch()` and `param_int()`) actually correct?**
  _`cJSON_GetObjectItem()` has 50 INFERRED edges - model-reasoned connections that need verification._
- **Are the 44 inferred relationships involving `cJSON_AddStringToObject()` (e.g. with `send_error()` and `send_result()`) actually correct?**
  _`cJSON_AddStringToObject()` has 44 INFERRED edges - model-reasoned connections that need verification._