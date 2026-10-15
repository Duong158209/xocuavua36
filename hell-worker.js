// Web Worker cho engine C++ (WebAssembly). hell.js do build.sh / GitHub Actions tạo ra.
// Giao thức giống bản JS cũ: nhận {id, b, size, pen, t}  ->  trả {id, m, stats}
importScripts('hell.js');

let M = null;
const ready = HellModule()
  .then(m => { M = m; })
  .catch(err => { postMessage({ fatal: String(err) }); });

function heap8(){ return M.HEAPU8 || new Uint8Array(M.wasmMemory.buffer); }
function heap16(){ return M.HEAPU16 || new Uint16Array(M.wasmMemory.buffer); }

onmessage = async (e) => {
  const d = e.data;
  try {
    await ready;
    if (!M) throw new Error('wasm chưa nạp được');
    const S = d.size, N = S * S;
    const board = new Uint8Array(d.b);
    heap8().set(board.subarray(0, N), M._hell_board_ptr());
    let usePen = 0;
    if (d.pen) {
      const base = M._hell_pen_ptr() >> 1, H = heap16();
      for (let i = 0; i < N; i++) { const v = d.pen[i] | 0; H[base + i] = v < 0 ? 0 : (v > 65535 ? 65535 : v); }
      usePen = 1;
    }
    const m = M._hell_think(S, d.t || 8000, usePen);
    postMessage({
      id: d.id, m,
      stats: { depth: M._hell_stat(0), nodes: M._hell_stat(1), ms: M._hell_stat(2),
               score: M._hell_stat(3), reason: M._hell_stat(4), engine: 'wasm' }
    });
  } catch (err) {
    postMessage({ id: d.id, m: -1, error: String(err) });
  }
};
