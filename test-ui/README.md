# UI development entry point

This directory delegates to the production UI in
`Phase-2/blackhand-external/package/blackhand-ui/src`. Its former duplicate
sources were removed. Changes and fixes belong in the production tree.

CMake requires the same Linux dependencies as the shipping UI; this entry
point does not emulate Linux input, D-Bus or audio devices on macOS.
Portable regression tests live in `Phase-2/tests`.
