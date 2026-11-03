cmake_minimum_required(VERSION 3.14)
project(blackhand_ui_test_entry C)

# Build the production UI. There is no second implementation to drift.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Phase-2/blackhand-external/package/blackhand-ui/src"
                 "${CMAKE_CURRENT_BINARY_DIR}/blackhand-ui")
