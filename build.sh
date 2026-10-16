#!/bin/bash
# build.sh cho Hell v3.0 Rapfi Hybrid
# Yêu cầu: emsdk đã cài

set -e

echo "Building Hell v3.0 Rapfi Hybrid..."

SRC="engine/hell_engine_v3_rapfi.cpp"
# Nếu file cũ tên hell_engine.cpp thì dùng nó, nếu có v3 thì ưu tiên v3
if [ -f "engine/hell_engine_v3_rapfi.cpp" ]; then
  SRC="engine/hell_engine_v3_rapfi.cpp"
fi

emcc $SRC -O3 -msimd128 -flto \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=32MB \
  -s MAXIMUM_MEMORY=256MB \
  -s EXPORTED_FUNCTIONS='["_hell_think","_hell_board_ptr","_hell_pen_ptr","_hell_stat","_hell_new_game","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","getValue","setValue"]' \
  -s ENVIRONMENT=web,worker \
  -o engine/hell.js

echo "Done: engine/hell.js + hell.wasm created"
echo "TT 4M entries, DEF x8.5, VCF/VCT Rapfi style"
