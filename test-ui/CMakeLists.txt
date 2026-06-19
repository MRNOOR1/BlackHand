cmake_minimum_required(VERSION 3.14)
project(blackhand_ui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(blackhand-ui
  main.c
  draw_utils.c
  frame_renderer.c
  bh_skin.c
  keypad_renderer.c
  platform/hardware.c
  screens/screen_home.c
  screens/screen_settings.c
  screens/screen_calls.c
  screens/screen_messages.c
  screens/screen_contacts.c
  screens/screen_mp3.c
  screens/screen_voice_memo.c
  screens/screen_notes.c
  screens/screen_theme.c
  screens/screen_bluetooth.c
  services/settings_service.c
  services/theme_service.c
  services/notes_service.c
  services/mp3_service.c
  services/voice_memo_service.c
  services/contacts_service.c
  services/comm_service.c
  services/multitap_service.c
  services/pin_service.c
  services/bluetooth_service.c
)

target_include_directories(blackhand-ui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/screens
  ${CMAKE_CURRENT_SOURCE_DIR}/services
)

target_compile_definitions(blackhand-ui PRIVATE
  _XOPEN_SOURCE=700
  _DEFAULT_SOURCE
)

find_path(NOTCURSES_INCLUDE_DIR
  NAMES notcurses/notcurses.h
  HINTS
    "${CMAKE_SYSROOT}/usr/include"
    "${CMAKE_FIND_ROOT_PATH}/usr/include"
)

find_library(NOTCURSES_LIBRARY
  NAMES notcurses
  HINTS
    "${CMAKE_SYSROOT}/usr/lib"
    "${CMAKE_FIND_ROOT_PATH}/usr/lib"
)

find_library(NOTCURSES_CORE_LIBRARY
  NAMES notcurses-core
  HINTS
    "${CMAKE_SYSROOT}/usr/lib"
    "${CMAKE_FIND_ROOT_PATH}/usr/lib"
)

if(NOT NOTCURSES_INCLUDE_DIR)
  message(FATAL_ERROR "notcurses headers not found")
endif()
if(NOT NOTCURSES_LIBRARY AND NOT NOTCURSES_CORE_LIBRARY)
  message(FATAL_ERROR "neither libnotcurses nor libnotcurses-core found")
endif()

target_include_directories(blackhand-ui PRIVATE "${NOTCURSES_INCLUDE_DIR}")

find_library(MPG123_LIBRARY
  NAMES mpg123
  HINTS
    "${CMAKE_SYSROOT}/usr/lib"
    "${CMAKE_FIND_ROOT_PATH}/usr/lib"
)
find_library(OUT123_LIBRARY
  NAMES out123
  HINTS
    "${CMAKE_SYSROOT}/usr/lib"
    "${CMAKE_FIND_ROOT_PATH}/usr/lib"
)

if(NOT MPG123_LIBRARY)
  message(FATAL_ERROR "libmpg123 not found")
endif()
if(NOT OUT123_LIBRARY)
  message(FATAL_ERROR "libout123 not found")
endif()

find_package(Threads REQUIRED)

set(BLACKHAND_UI_LINK_LIBS
  "${MPG123_LIBRARY}"
  "${OUT123_LIBRARY}"
  Threads::Threads
  m
)

if(NOTCURSES_LIBRARY)
  list(APPEND BLACKHAND_UI_LINK_LIBS "${NOTCURSES_LIBRARY}")
endif()
if(NOTCURSES_CORE_LIBRARY)
  list(APPEND BLACKHAND_UI_LINK_LIBS "${NOTCURSES_CORE_LIBRARY}")
endif()

target_link_libraries(blackhand-ui PRIVATE ${BLACKHAND_UI_LINK_LIBS})

install(TARGETS blackhand-ui RUNTIME DESTINATION bin)
