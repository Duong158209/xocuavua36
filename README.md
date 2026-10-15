# Hell Bot — cờ X O (caro) + engine C++ WebAssembly

- `index.html`      giao diện + bot JS cũ (v1.8, dùng làm dự phòng)
- `engine/hell_engine.cpp`  engine C++ (v2.0)
- `hell-worker.js`  Web Worker nạp engine WASM
- `build.sh`        build C++ -> `hell.js` (cần Emscripten)
- `.github/workflows/pages.yml`  tự build + deploy GitHub Pages

## Đưa lên GitHub
1. Tạo repo, đẩy toàn bộ thư mục này lên nhánh `main`.
2. Repo -> Settings -> Pages -> Source = **GitHub Actions**.
3. Chờ tab Actions chạy xong -> web chạy ở `https://<tên>.github.io/<repo>/`.

Nếu `hell.js` chưa có / lỗi, web tự dùng lại bot JS v1.8.

## Test engine trên máy (không cần emcc)
`g++ -O2 -std=c++17 -DHELL_TEST engine/hell_engine.cpp -o hell_test`
