#!/usr/bin/env bash
# Build engine C++ -> hell.js (WebAssembly nhúng sẵn trong 1 file). Cần cài Emscripten (emcc).
set -e
em++ engine/hell_engine.cpp -O3 -std=c++17 -o hell.js \
  -sMODULARIZE=1 -sEXPORT_NAME=HellModule -sENVIRONMENT=worker -sSINGLE_FILE=1 \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
  -sEXPORTED_FUNCTIONS=_hell_board_ptr,_hell_pen_ptr,_hell_think,_hell_stat,_hell_new_game \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16
echo "OK -> hell.js"
