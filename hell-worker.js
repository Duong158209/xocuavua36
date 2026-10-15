// Web Worker FIX cho engine C++ 2.0 - Tách não hoàn toàn, fix lỗi v2.0 vs v13 đánh y hệt nhau
// Giao thức: nhận {id, b, size, pen, t, botId, gameId} -> trả {id, m, stats}
// FIX: Mỗi worker có seed riêng, TT riêng, thêm nhiễu nhỏ để không đánh trùng

importScripts('hell.js');

let M = null;
let mySeed = 0;
let workerId = Math.random().toString(36).substring(2) + Date.now();

// Khởi tạo seed khác nhau cho mỗi worker instance
mySeed = Math.floor(Math.random() * 1000000000) + (Date.now() % 100000);

const ready = HellModule().then(m => {
  M = m;
  // Nếu engine C++ có hàm set seed, gọi nó để mỗi worker khác nhau
  try {
    if (M._hell_set_seed) {
      M._hell_set_seed(mySeed);
    }
    if (M._hell_set_random) {
      M._hell_set_random(mySeed);
    }
  } catch(e) {}
}).catch(err => {
  postMessage({ fatal: String(err) });
});

function heap8(){ return M.HEAPU8 || new Uint8Array(M.wasmMemory.buffer); }
function heap16(){ return M.HEAPU16 || new Uint16Array(M.wasmMemory.buffer); }

onmessage = async (e) => {
  const d = e.data;
  try {
    await ready;
    if (!M) throw new Error('wasm chưa nạp được');

    const S = d.size, N = S * S;
    const board = new Uint8Array(d.b);

    // FIX 1: Thêm nhiễu nhỏ vào thời gian để 2 bot cùng engine không tính y hệt
    let timeWithNoise = (d.t || 8000);
    if (d.botId) {
      let hash = 0;
      for (let i = 0; i < d.botId.length; i++) hash = (hash * 31 + d.botId.charCodeAt(i)) | 0;
      timeWithNoise += (hash % 200) - 100;
      timeWithNoise += Math.floor((Math.random() - 0.5) * 100);
      if (timeWithNoise < 500) timeWithNoise = 500;
    } else {
      timeWithNoise += Math.floor((Math.random() - 0.5) * 80);
    }

    heap8().set(board.subarray(0, N), M._hell_board_ptr());

    let usePen = 0;
    if (d.pen) {
      const base = M._hell_pen_ptr() >> 1, H = heap16();
      for (let i = 0; i < N; i++) {
        let v = d.pen[i] | 0;
        // FIX 2: Thêm nhiễu nhỏ vào pen để mỗi bot nhớ khác nhau
        if (d.botId && Math.random() < 0.1) {
          v = Math.floor(v * (0.9 + Math.random()*0.2));
        }
        H[base + i] = v < 0? 0 : (v > 65535? 65535 : v);
      }
      usePen = 1;
    }

    // Gọi engine C++ 2.0
    let m = M._hell_think(S, timeWithNoise, usePen);

    // FIX 3: 25% ván đầu (<6 nước) random trong vùng center để khai cuộc khác nhau
    const stoneCount = board.reduce((c, v) => c + (v!== 0? 1 : 0), 0);
    if (stoneCount < 6 && Math.random() < 0.25) {
      const center = Math.floor(S/2);
      const candidates = [];
      for (let r = center - 2; r <= center + 2; r++) {
        for (let c = center - 2; c <= center + 2; c++) {
          if (r < 0 || r >= S || c < 0 || c >= S) continue;
          const id = r * S + c;
          if (board[id] === 0) candidates.push(id);
        }
      }
      if (candidates.length > 0) {
        m = candidates[Math.floor(Math.random() * candidates.length)];
      }
    }

    postMessage({
      id: d.id, m,
      stats: {
        depth: M._hell_stat? M._hell_stat(0) : 0,
        nodes: M._hell_stat? M._hell_stat(1) : 0,
        ms: M._hell_stat? M._hell_stat(2) : timeWithNoise,
        score: M._hell_stat? M._hell_stat(3) : 0,
        reason: M._hell_stat? M._hell_stat(4) : 0,
        engine: 'wasm-v2.0-fixed',
        seed: mySeed,
        workerId: workerId
      }
    });
  } catch (err) {
    postMessage({ id: d.id, m: -1, error: String(err) });
  }
};
