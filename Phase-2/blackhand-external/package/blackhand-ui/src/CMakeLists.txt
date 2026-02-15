cmake_minimum_required(VERSION 3.14)
project(blackhand_ui C)

set(CMAKE_C_STANDARD 11)

add_executable(blackhand-ui main.c)

target_compile_options(blackhand-ui PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Find notcurses-core from Buildroot sysroot
find_path(NOTCURSES_INCLUDE_DIR
  NAMES notcurses/notcurses.h
  HINTS
    "${CMAKE_SYSROOT}/usr/include"
    "${CMAKE_FIND_ROOT_PATH}/usr/include"
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
if(NOT NOTCURSES_CORE_LIBRARY)
  message(FATAL_ERROR "libnotcurses-core not found")
endif()

message(STATUS "notcurses include: ${NOTCURSES_INCLUDE_DIR}")
message(STATUS "notcurses-core lib: ${NOTCURSES_CORE_LIBRARY}")

target_include_directories(blackhand-ui PRIVATE "${NOTCURSES_INCLUDE_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(blackhand-ui PRIVATE "${NOTCURSES_CORE_LIBRARY}" Threads::Threads m)

install(TARGETS blackhand-ui RUNTIME DESTINATION bin)
