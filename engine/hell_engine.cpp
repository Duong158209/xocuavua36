// ============================================================================
//  HELL ENGINE v2.0  —  Cờ caro / Gomoku freestyle (5 quân liên tiếp trở lên = thắng)
//  Viết bằng C++17, build ra WebAssembly bằng Emscripten (xem build.sh).
//
//  Quy ước:   ô trống = 0,  người/đối thủ = 1,  engine = 2   (giống bản JS cũ)
//  Bàn cờ tối đa 64x64 (SIZE do giao diện truyền vào).
//
//  Kỹ thuật:
//   - Cửa sổ 5 ô đếm quân tăng dần (incremental) -> eval + phát hiện đe doạ O(1)
//   - Negamax alpha-beta + PVS + LMR + killer + history + bảng băm Zobrist (TT)
//   - Iterative deepening theo thời gian
//   - Ép nước khi bị đối thủ có "tứ" (không giảm depth => tự mở rộng chuỗi chiếu)
//   - VCF (thắng bằng chuỗi liên tiếp tạo tứ) ở gốc, ở lá và để lọc nước phòng thủ
// ============================================================================
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define HELL_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define HELL_EXPORT extern "C"
#endif

namespace {

constexpr int MAXS = 64, MAXN = MAXS * MAXS, MAXW = 4 * MAXN;
constexpr int WINV = 30000, INFV = 32000, MAXPLY = 80;
constexpr int DR[4] = {0, 1, 1, 1}, DC[4] = {1, 0, 1, -1};

// ---------------------------------------------------------------- trạng thái
int S = 0, N = 0, stepd[4];
uint8_t bd[MAXN];                 // 0 trống, 1 = player0, 2 = player1
uint8_t cnt[2][MAXW];             // số quân mỗi bên trong cửa sổ 5 ô
uint8_t wvalid[MAXW];
int cwStart[MAXN + 1];
std::vector<int> cwList;          // cell -> các cửa sổ chứa nó (CSR)

// tập cửa sổ "ba" (3 quân của mình, 0 của địch) và "tứ" (4 quân, 0 của địch)
std::vector<int> wset[2][2];
int wpos[2][2][MAXW];
uint8_t wcode[2][MAXW];           // 0 không, 1 ba, 2 tứ
int sc[2];                        // điểm cửa sổ mỗi bên
const int Wt[6] = {0, 2, 12, 130, 3200, 0};

// ứng viên nước đi = ô trống trong bán kính 2 quanh quân đã đánh
uint8_t nbc[MAXN];
int candPos[MAXN];
std::vector<int> cand;

uint64_t Z[2][MAXN], ZS, hsh;
int stones;

// ---------------------------------------------------------------- tiện ích
inline void setAdd(int q, int k, int w) {
  wpos[q][k][w] = (int)wset[q][k].size();
  wset[q][k].push_back(w);
}
inline void setDel(int q, int k, int w) {
  auto &v = wset[q][k];
  int p = wpos[q][k][w], last = v.back();
  v[p] = last;
  wpos[q][k][last] = p;
  v.pop_back();
}
inline void wRemove(int w) {
  for (int q = 0; q < 2; q++) {
    if (cnt[1 - q][w] == 0) sc[q] -= Wt[cnt[q][w]];
    if (wcode[q][w]) { setDel(q, wcode[q][w] - 1, w); wcode[q][w] = 0; }
  }
}
inline void wAdd(int w) {
  for (int q = 0; q < 2; q++) {
    if (cnt[1 - q][w] == 0) {
      int m = cnt[q][w];
      sc[q] += Wt[m];
      if (m == 3) { wcode[q][w] = 1; setAdd(q, 0, w); }
      else if (m == 4) { wcode[q][w] = 2; setAdd(q, 1, w); }
    }
  }
}
inline void addCand(int j) { candPos[j] = (int)cand.size(); cand.push_back(j); }
inline void delCand(int j) {
  int p = candPos[j], last = cand.back();
  cand[p] = last; candPos[last] = p; cand.pop_back(); candPos[j] = -1;
}

void put(int i, int p) {
  bd[i] = (uint8_t)(p + 1); hsh ^= Z[p][i]; stones++;
  for (int k = cwStart[i]; k < cwStart[i + 1]; k++) {
    int w = cwList[k];
    wRemove(w); cnt[p][w]++; wAdd(w);
  }
  int r = i / S, c = i % S;
  for (int dr = -2; dr <= 2; dr++) {
    int rr = r + dr; if (rr < 0 || rr >= S) continue;
    for (int dc = -2; dc <= 2; dc++) {
      int cc = c + dc; if (cc < 0 || cc >= S) continue;
      int j = rr * S + cc;
      if (++nbc[j] == 1 && bd[j] == 0) addCand(j);
    }
  }
  if (candPos[i] >= 0) delCand(i);
}
void undo(int i, int p) {
  int r = i / S, c = i % S;
  for (int dr = -2; dr <= 2; dr++) {
    int rr = r + dr; if (rr < 0 || rr >= S) continue;
    for (int dc = -2; dc <= 2; dc++) {
      int cc = c + dc; if (cc < 0 || cc >= S) continue;
      int j = rr * S + cc;
      if (--nbc[j] == 0 && candPos[j] >= 0) delCand(j);
    }
  }
  bd[i] = 0; hsh ^= Z[p][i]; stones--;
  for (int k = cwStart[i]; k < cwStart[i + 1]; k++) {
    int w = cwList[k];
    wRemove(w); cnt[p][w]--; wAdd(w);
  }
  if (nbc[i] > 0) addCand(i);
}

// các ô mà p đặt vào sẽ thành 5 (khác nhau); trả về số lượng
int winCells(int p, int *out) {
  int n = 0;
  for (int w : wset[p][1]) {
    int d = w / N, s = w - d * N;
    for (int k = 0; k < 5; k++) {
      int j = s + k * stepd[d];
      if (bd[j] == 0) {
        bool dup = false;
        for (int t = 0; t < n; t++) if (out[t] == j) { dup = true; break; }
        if (!dup && n < 16) out[n++] = j;
        break;
      }
    }
  }
  return n;
}

const int PW[6] = {1, 10, 80, 800, 9000, 0};
inline int cellScore(int c, int q) {
  int s = 0;
  for (int k = cwStart[c]; k < cwStart[c + 1]; k++) {
    int w = cwList[k];
    if (cnt[1 - q][w] == 0) s += PW[cnt[q][w]];
  }
  return s;
}

// ---------------------------------------------------------------- khởi tạo
uint64_t rngState = 0x9E3779B97F4A7C15ULL;
uint64_t rnd64() {
  uint64_t z = (rngState += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

struct TTE { uint64_t key; int16_t score; int16_t move; int8_t depth; uint8_t flag; };
std::vector<TTE> tt;
size_t ttMask = 0;
int16_t history_[2][MAXN];
int16_t killers[MAXPLY + 4][2];
bool zInit = false;

void buildBoard(int size) {
  if (!zInit) {
    for (int p = 0; p < 2; p++) for (int i = 0; i < MAXN; i++) Z[p][i] = rnd64();
    ZS = rnd64();
    tt.assign((size_t)1 << 20, TTE{0, 0, -1, -1, 0});
    ttMask = ((size_t)1 << 20) - 1;
    zInit = true;
  }
  if (size == S) return;
  S = size; N = S * S;
  stepd[0] = 1; stepd[1] = S; stepd[2] = S + 1; stepd[3] = S - 1;
  std::memset(wvalid, 0, sizeof(wvalid));
  std::vector<int> cntc(N + 1, 0);
  for (int d = 0; d < 4; d++)
    for (int r = 0; r < S; r++)
      for (int c = 0; c < S; c++) {
        int er = r + 4 * DR[d], ec = c + 4 * DC[d];
        if (er < 0 || er >= S || ec < 0 || ec >= S) continue;
        int w = d * N + r * S + c;
        wvalid[w] = 1;
        for (int k = 0; k < 5; k++) cntc[(r + k * DR[d]) * S + (c + k * DC[d])]++;
      }
  cwStart[0] = 0;
  for (int i = 0; i < N; i++) cwStart[i + 1] = cwStart[i] + cntc[i];
  cwList.assign(cwStart[N], 0);
  std::vector<int> fill(N, 0);
  for (int d = 0; d < 4; d++)
    for (int r = 0; r < S; r++)
      for (int c = 0; c < S; c++) {
        int w = d * N + r * S + c;
        if (!wvalid[w]) continue;
        for (int k = 0; k < 5; k++) {
          int i = (r + k * DR[d]) * S + (c + k * DC[d]);
          cwList[cwStart[i] + fill[i]++] = w;
        }
      }
}

void resetState() {
  std::memset(bd, 0, sizeof(bd));
  std::memset(cnt, 0, sizeof(cnt));
  std::memset(wcode, 0, sizeof(wcode));
  std::memset(nbc, 0, sizeof(nbc));
  for (int i = 0; i < MAXN; i++) candPos[i] = -1;
  for (int q = 0; q < 2; q++) for (int k = 0; k < 2; k++) wset[q][k].clear();
  cand.clear();
  sc[0] = sc[1] = 0; hsh = 0; stones = 0;
}

// ---------------------------------------------------------------- thời gian
using Clock = std::chrono::steady_clock;
Clock::time_point t0;
double limitMs = 3000;
bool aborted = false;
long long nodes = 0, vcfNodes = 0, vcfLimit = 0;
inline double elapsed() { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
inline bool timeUp() { return elapsed() >= limitMs; }

// ---------------------------------------------------------------- VCF
int vcfMove = -1;
bool vcf(int p, int depth, bool top) {
  if (++vcfNodes > vcfLimit) return false;
  if ((++nodes & 1023) == 0 && timeUp()) aborted = true;
  if (aborted) return false;
  if (!wset[p][1].empty()) return true;
  int o = 1 - p;
  if (!wset[o][1].empty()) return false;
  if (depth <= 0) return false;
  int cells[48], n = 0;
  for (int w : wset[p][0]) {
    int d = w / N, s = w - d * N;
    for (int k = 0; k < 5 && n < 48; k++) {
      int j = s + k * stepd[d];
      if (bd[j] == 0) {
        bool dup = false;
        for (int t = 0; t < n; t++) if (cells[t] == j) { dup = true; break; }
        if (!dup) cells[n++] = j;
      }
    }
  }
  int key[48];
  for (int i = 0; i < n; i++) key[i] = cellScore(cells[i], p);
  for (int i = 1; i < n; i++) {           // sắp xếp chèn giảm dần
    int c = cells[i], kk = key[i], j = i - 1;
    while (j >= 0 && key[j] < kk) { cells[j + 1] = cells[j]; key[j + 1] = key[j]; j--; }
    cells[j + 1] = c; key[j + 1] = kk;
  }
  for (int i = 0; i < n; i++) {
    int c = cells[i];
    put(c, p);
    int ow[16], wn = winCells(p, ow);
    bool res = false;
    if (wn >= 2) res = true;
    else if (wn == 1) {
      int e = ow[0];
      put(e, o);
      res = vcf(p, depth - 1, false);
      undo(e, o);
    }
    undo(c, p);
    if (res) { if (top) vcfMove = c; return true; }
    if (aborted) return false;
  }
  return false;
}
int vcfFind(int p, int depth, long long limit) {
  vcfNodes = 0; vcfLimit = limit; vcfMove = -1;
  int ow[16];
  if (winCells(p, ow) > 0) return ow[0];
  if (vcf(p, depth, true)) return vcfMove;
  return -1;
}
bool vcfQuick(int p, int depth, long long limit) {
  vcfNodes = 0; vcfLimit = limit;
  return vcf(p, depth, false);
}

// ---------------------------------------------------------------- tìm kiếm
inline int toTT(int v, int ply) { return v > WINV - 1000 ? v + ply : (v < -WINV + 1000 ? v - ply : v); }
inline int fromTT(int v, int ply) { return v > WINV - 1000 ? v - ply : (v < -WINV + 1000 ? v + ply : v); }

int negamax(int depth, int alpha, int beta, int ply, int me) {
  if ((++nodes & 2047) == 0 && timeUp()) aborted = true;
  if (aborted) return 0;
  int opp = 1 - me;
  if (!wset[me][1].empty()) return WINV - ply;
  int ow[16], on = winCells(opp, ow);
  if (on >= 2) return -(WINV - ply - 2);
  if (stones >= N) return 0;
  if (ply >= MAXPLY) return sc[me] - sc[opp];
  if (on == 1) {                                   // bắt buộc chặn, không giảm depth
    put(ow[0], me);
    int v = -negamax(depth, -beta, -alpha, ply + 1, opp);
    undo(ow[0], me);
    return v;
  }
  uint64_t key = hsh ^ (me ? ZS : 0);
  TTE &e = tt[key & ttMask];
  int ttm = -1;
  if (e.key == key) {
    ttm = e.move;
    if (e.depth >= depth) {
      int v = fromTT(e.score, ply);
      if (e.flag == 0) return v;
      if (e.flag == 1 && v > alpha) alpha = v;
      if (e.flag == 2 && v < beta) beta = v;
      if (alpha >= beta) return v;
    }
  }
  if (depth <= 0) {
    if (!wset[me][0].empty() && vcfQuick(me, 8, 700)) return WINV - ply - 8;
    if (aborted) return 0;
    return sc[me] - sc[opp];
  }

  int mv[160], ms[160], n = 0;
  for (int j : cand) {
    if (n >= 160) break;
    int a = cellScore(j, me), d = cellScore(j, opp);
    mv[n] = j;
    ms[n] = a + d + (a >= 800 ? 400 : 0) + history_[me][j] / 8;
    n++;
  }
  if (!n) return 0;
  for (int i = 0; i < n; i++) {
    if (mv[i] == ttm) ms[i] += 1000000;
    else if (mv[i] == killers[ply][0]) ms[i] += 5000;
    else if (mv[i] == killers[ply][1]) ms[i] += 3000;
  }
  int K = depth >= 6 ? 10 : (depth >= 4 ? 13 : 17);
  if (K > n) K = n;
  for (int i = 0; i < K; i++) {                     // chọn K nước tốt nhất
    int b = i;
    for (int j = i + 1; j < n; j++) if (ms[j] > ms[b]) b = j;
    std::swap(ms[i], ms[b]); std::swap(mv[i], mv[b]);
  }

  int origAlpha = alpha, best = -INFV, bestm = -1;
  for (int i = 0; i < K; i++) {
    int m = mv[i];
    put(m, me);
    int v;
    if (i == 0) v = -negamax(depth - 1, -beta, -alpha, ply + 1, opp);
    else {
      int red = (depth >= 3 && i >= 5 && ms[i] < 800) ? 1 : 0;
      v = -negamax(depth - 1 - red, -alpha - 1, -alpha, ply + 1, opp);
      if (v > alpha && (red || v < beta)) v = -negamax(depth - 1, -beta, -alpha, ply + 1, opp);
    }
    undo(m, me);
    if (aborted) return 0;
    if (v > best) { best = v; bestm = m; }
    if (v > alpha) alpha = v;
    if (alpha >= beta) {
      if (killers[ply][0] != m) { killers[ply][1] = killers[ply][0]; killers[ply][0] = (int16_t)m; }
      int h = history_[me][m] + depth * depth;
      history_[me][m] = (int16_t)(h > 16000 ? 16000 : h);
      break;
    }
  }
  e.key = key; e.score = (int16_t)toTT(best, ply); e.move = (int16_t)bestm; e.depth = (int8_t)depth;
  e.flag = best <= origAlpha ? 2 : (best >= beta ? 1 : 0);
  return best;
}

// ---------------------------------------------------------------- gốc
struct Stats { int depth = 0; long long nodes = 0; int ms = 0; int score = 0; int reason = 0; } stats;
// reason: 0 search, 1 center, 2 win, 3 forced-block, 4 vcf-win, 5 fallback

int think(const uint8_t *board, int size, int timeMs, const uint16_t *pen) {
  buildBoard(size);
  resetState();
  t0 = Clock::now();
  limitMs = timeMs < 50 ? 50 : timeMs;
  aborted = false; nodes = 0;
  stats = Stats();
  std::memset(history_, 0, sizeof(history_));
  std::memset(killers, -1, sizeof(killers));
  for (int i = 0; i < N; i++) if (board[i] == 1) put(i, 0); else if (board[i] == 2) put(i, 1);
  const int me = 1, opp = 0;

  if (stones == 0) { stats.reason = 1; return (S / 2) * S + S / 2; }
  if (stones >= N) return -1;
  int ow[16];
  if (winCells(me, ow) > 0) { stats.reason = 2; return ow[0]; }
  int on = winCells(opp, ow);
  if (on >= 1) {                                    // phải chặn (nếu >=2 thì chọn ô tốt nhất)
    int bi = ow[0], bs = -1;
    for (int t = 0; t < on; t++) { int s = cellScore(ow[t], me); if (s > bs) { bs = s; bi = ow[t]; } }
    stats.reason = 3; return bi;
  }

  // 1) tôi có chuỗi VCF thắng không?
  double full = limitMs;
  limitMs = std::min(full, elapsed() + full * 0.25);
  int pv = vcfFind(me, 40, 3000000);
  aborted = false; limitMs = full;
  if (pv >= 0) { stats.reason = 4; stats.ms = (int)elapsed(); return pv; }

  // 2) danh sách nước ở gốc
  std::vector<std::pair<int, int>> rc;
  for (int j : cand) rc.push_back({cellScore(j, me) + cellScore(j, opp), j});
  std::sort(rc.begin(), rc.end(), [](auto &a, auto &b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
  if (rc.size() > 40) rc.resize(40);
  std::vector<int> order;
  for (auto &x : rc) order.push_back(x.second);
  if (order.empty()) { for (int i = 0; i < N; i++) if (!bd[i]) return i; return -1; }

  if (pen) {                                        // trí nhớ: tránh nước từng thua
    std::vector<int> lo, rest;
    for (int i : order) (pen[i] >= 500 ? lo : rest).push_back(i);
    if (!lo.empty() && !rest.empty()) {
      if (rest.size() >= 3) { for (int i : lo) if (pen[i] < 900) rest.push_back(i); }
      else for (int i : lo) rest.push_back(i);
      order = rest;
    }
  }

  // 3) đối thủ đang có VCF (nếu tôi bỏ qua lượt)? -> chỉ giữ nước khiến nó mất VCF
  limitMs = std::min(full, elapsed() + full * 0.30);
  bool threat = vcfFind(opp, 24, 400000) >= 0;
  aborted = false;
  if (threat) {
    std::vector<int> safe, unsafe;
    size_t k = 0;
    for (; k < order.size(); k++) {
      int m = order[k];
      put(m, me);
      bool bad = !wset[me][1].empty() ? false : vcfQuick(opp, 24, 120000);
      undo(m, me);
      if (aborted) break;
      (bad ? unsafe : safe).push_back(m);
    }
    for (; k < order.size(); k++) safe.push_back(order[k]);
    aborted = false;
    if (!safe.empty()) order = safe;
  }
  limitMs = full;

  int best = order[0], bestScore = -INFV, completed = 0, stable = 0;
  double lastIter = 0;
  for (int depth = 1; depth <= 40; depth++) {
    double now = elapsed();
    if (depth >= 5 && lastIter > 0 && now + lastIter * 2.2 > limitMs) break;
    if (order.size() == 1 && depth >= 3) break;
    double ti = now;
    int alpha = -INFV, beta = INFV, itBest = -1, itScore = -INFV;
    std::vector<std::pair<int, int>> scored;
    bool ok = true;
    for (size_t i = 0; i < order.size(); i++) {
      int m = order[i];
      put(m, me);
      int v;
      if (i == 0) v = -negamax(depth - 1, -beta, -alpha, 1, opp);
      else {
        v = -negamax(depth - 1, -alpha - 1, -alpha, 1, opp);
        if (v > alpha && v < beta) v = -negamax(depth - 1, -beta, -alpha, 1, opp);
      }
      undo(m, me);
      if (aborted) { ok = false; break; }
      scored.push_back({v, m});
      if (v > itScore) { itScore = v; itBest = m; }
      if (v > alpha) alpha = v;
    }
    if (!ok) break;
    std::stable_sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first > b.first; });
    order.clear();
    for (auto &x : scored) order.push_back(x.second);
    stable = (itBest == best) ? stable + 1 : 0;
    best = itBest; bestScore = itScore; completed = depth;
    lastIter = elapsed() - ti;
    if (bestScore > WINV - 1000) break;                    // thấy thắng cưỡng ép
    if (bestScore < -WINV + 1000 && depth >= 4) break;     // thua chắc, khỏi tốn giờ
    if (depth >= 8 && stable >= 3 && elapsed() > limitMs * 0.55) break;
  }
  stats.depth = completed; stats.nodes = nodes; stats.ms = (int)elapsed(); stats.score = bestScore;
  return best;
}

uint8_t inBoard[MAXN];
uint16_t inPen[MAXN];

}  // namespace

// ---------------------------------------------------------------- API cho WASM
HELL_EXPORT uint8_t *hell_board_ptr() { return inBoard; }
HELL_EXPORT uint16_t *hell_pen_ptr() { return inPen; }
HELL_EXPORT int hell_think(int size, int timeMs, int usePen) {
  if (size < 5 || size > MAXS) return -1;
  return think(inBoard, size, timeMs, usePen ? inPen : nullptr);
}
HELL_EXPORT int hell_stat(int i) {
  switch (i) {
    case 0: return stats.depth;
    case 1: return (int)std::min<long long>(stats.nodes, 2000000000LL);
    case 2: return stats.ms;
    case 3: return stats.score;
    case 4: return stats.reason;
  }
  return 0;
}
HELL_EXPORT void hell_new_game() {
  if (zInit) std::fill(tt.begin(), tt.end(), TTE{0, 0, -1, -1, 0});
}

// ---------------------------------------------------------------- test native
#ifdef HELL_TEST
#include <cstdio>
// stdin:  "S T\n" rồi S dòng, mỗi dòng S ký tự 0/1/2.  stdout: chỉ số ô
int main() {
  int s, t;
  if (std::scanf("%d %d", &s, &t) != 2) return 1;
  for (int r = 0; r < s; r++) {
    char line[256];
    if (std::scanf("%255s", line) != 1) return 1;
    for (int c = 0; c < s; c++) inBoard[r * s + c] = (uint8_t)(line[c] - '0');
  }
  int m = hell_think(s, t, 0);
  std::printf("%d %d %d %d %d\n", m, hell_stat(0), hell_stat(1), hell_stat(2), hell_stat(4));
  return 0;
}
#endif
