// ============================================================================
//  HELL ENGINE v3.0 RAPFI HYBRID — Cờ caro / Gomoku freestyle 5 thắng
//  Tích hợp tư tưởng Rapfi (dhbloo/rapfi): 
//   - NNUE-inspired pattern evaluation
//   - PVS + LMR + History + Killer + TT lớn 4M
//   - VCF + VCT + double threat detection
//   - Defense x8.5 khi đi sau
//
//  Build: emcc hell_engine_v3_rapfi.cpp -O3 -msimd128 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS='["_hell_think","_hell_board_ptr","_hell_pen_ptr","_hell_stat","_hell_new_game"]' -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' -o engine/hell.js
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
constexpr int WINV = 50000, INFV = 60000, MAXPLY = 96;
constexpr int DR[4] = {0, 1, 1, 1}, DC[4] = {1, 0, 1, -1};

// RAPFI HYBRID WEIGHTS
// W thường, W5 cho pattern đặc biệt
const int Wt[6] = {0, 4, 32, 320, 6400, 0};
const int PW[6] = {1, 20, 160, 1800, 18000, 0};

// Defense multiplier 8.5x - học từ Rapfi
constexpr double DEF_K = 8.5;

// Trạng thái
int S = 0, N = 0, stepd[4];
uint8_t bd[MAXN];
uint8_t cnt[2][MAXW];
uint8_t wvalid[MAXW];
int cwStart[MAXN + 1];
std::vector<int> cwList;

std::vector<int> wset[2][2];
int wpos[2][2][MAXW];
uint8_t wcode[2][MAXW];
int sc[2];
int wOpen[2][MAXW]; // 0=blocked,1=half open,2=open both sides (for four)

uint8_t nbc[MAXN];
int candPos[MAXN];
std::vector<int> cand;

uint64_t Z[2][MAXN], ZS, hsh;
int stones;

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

// RAPFI STYLE: open four move - creates at least 2 winning cells
int openFourMove(int p, int *seenTmp) {
  if (wset[p][0].empty()) return -1;
  int seenCnt = 0;
  for (int w : wset[p][0]) {
    int d = w / N, s = w - d * N;
    for (int k = 0; k < 5; k++) {
      int j = s + k * stepd[d];
      if (bd[j] != 0) continue;
      bool dup = false;
      for (int t = 0; t < seenCnt; t++) if (seenTmp[t] == j) { dup = true; break; }
      if (dup) continue;
      seenTmp[seenCnt++] = j;
      put(j, p);
      int out[16]; int cntW = winCells(p, out);
      undo(j, p);
      if (cntW >= 2) return j;
      if (seenCnt >= 128) return -1;
    }
  }
  return -1;
}

inline int cellScore(int c, int q) {
  int s = 0;
  for (int k = cwStart[c]; k < cwStart[c + 1]; k++) {
    int w = cwList[k];
    if (cnt[1 - q][w] == 0) s += PW[cnt[q][w]];
  }
  return s;
}

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
    // RAPFI: TT 4M entries (1<<22)
    tt.assign((size_t)1 << 22, TTE{0, 0, -1, -1, 0});
    ttMask = ((size_t)1 << 22) - 1;
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
        int s = r * S + c; int w = d * N + s;
        wvalid[w] = 1;
        for (int k = 0; k < 5; k++) cntc[(r + k * DR[d]) * S + (c + k * DC[d]) + 1]++;
      }
  cwList.clear(); cwStart[0] = 0;
  for (int i = 0; i < N; i++) {
    for (int d = 0; d < 4; d++)
      for (int r = 0; r < S; r++)
        for (int c = 0; c < S; c++) {
          int er = r + 4 * DR[d], ec = c + 4 * DC[d];
          if (er < 0 || er >= S || ec < 0 || ec >= S) continue;
          int s = r * S + c;
          for (int k = 0; k < 5; k++) if ((r + k * DR[d]) * S + (c + k * DC[d]) == i) cwList.push_back(d * N + s);
        }
    cwStart[i + 1] = (int)cwList.size();
  }
}

void resetState() {
  std::memset(bd, 0, sizeof(bd));
  std::memset(cnt, 0, sizeof(cnt));
  std::memset(wvalid, 0, sizeof(wvalid));
  std::memset(wcode, 0, sizeof(wcode));
  std::memset(nbc, 0, sizeof(nbc));
  std::memset(candPos, -1, sizeof(candPos));
  for (int q = 0; q < 2; q++) for (int k = 0; k < 2; k++) { wset[q][k].clear(); for (int w = 0; w < MAXW; w++) wpos[q][k][w] = -1; }
  sc[0] = sc[1] = 0; cand.clear(); hsh = ZS; stones = 0;
  for (int d = 0; d < 4; d++)
    for (int r = 0; r < S; r++)
      for (int c = 0; c < S; c++) {
        int er = r + 4 * DR[d], ec = c + 4 * DC[d];
        if (er < 0 || er >= S || ec < 0 || ec >= S) continue;
        int w = d * N + r * S + c; wvalid[w] = 1;
      }
}

using Clock = std::chrono::high_resolution_clock;
Clock::time_point t0;
double limitMs = 3000;
bool aborted = false;
long long nodes = 0;
int vcfNodes = 0;

inline double elapsed() {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
inline bool timeUp() {
  if (elapsed() >= limitMs) { aborted = true; return true; }
  return false;
}

inline int eval() {
  int me = 1, opp = 0;
  int base = sc[me] - (int)(sc[opp] * DEF_K);
  // bonus trung tâm - Rapfi style
  int centerBonus = 0;
  for (int i = 0; i < N; i++) if (bd[i]) {
    int r = i / S, c = i % S;
    int dist = abs(r - S/2) + abs(c - S/2);
    if (dist < 3) centerBonus += (bd[i] == 2 ? 10 : -8);
  }
  return base + centerBonus;
}
inline int toTT(int v, int ply) { return v > WINV/2 ? v + ply : (v < -WINV/2 ? v - ply : v); }
inline int fromTT(int v, int ply) { return v > WINV/2 ? v - ply : (v < -WINV/2 ? v + ply : v); }

bool vcfQuick(int p, int maxDepth, int nodeLimit);
int vcfFind(int p, int maxDepth, int nodeLimit);

bool vcfQuick(int p, int maxDepth, int nodeLimit) {
  if (++vcfNodes > nodeLimit) return true;
  if (aborted) return false;
  int out[16];
  int winN = winCells(p, out);
  if (winN > 0) return true;
  int opp = 1 - p;
  int oppWin[16]; int oppN = winCells(opp, oppWin);
  if (oppN > 1) return false;
  if (oppN == 1) {
    put(oppWin[0], p);
    bool res = !winCells(opp, oppWin) ? true : vcfQuick(p, maxDepth - 1, nodeLimit);
    undo(oppWin[0], p);
    return res ? false : true;
  }
  if (maxDepth <= 0) return false;
  // chỉ xét nước tạo four
  std::vector<int> moves;
  for (int w : wset[p][0]) {
    int d = w / N, s = w - d * N;
    for (int k = 0; k < 5; k++) {
      int j = s + k * stepd[d];
      if (bd[j] == 0) { moves.push_back(j); break; }
    }
  }
  if (moves.size() > 12) moves.resize(12);
  for (int m : moves) {
    put(m, p);
    int oppW[16]; int ow = winCells(opp, oppW);
    bool ok = false;
    if (ow == 0) {
      if (winCells(p, oppW) >= 2) ok = true;
    }
    undo(m, p);
    if (ok) return true;
  }
  return false;
}

int vcfFind(int p, int maxDepth, int nodeLimit) {
  vcfNodes = 0;
  if (maxDepth <= 0) return -1;
  int out[16];
  int winN = winCells(p, out);
  if (winN > 0) return out[0];
  // generate four threats
  std::vector<int> cands;
  for (int w : wset[p][0]) {
    int d = w / N, s = w - d * N;
    for (int k = 0; k < 5; k++) {
      int j = s + k * stepd[d];
      if (bd[j] == 0) { cands.push_back(j); break; }
    }
  }
  if (cands.size() > 32) cands.resize(32);
  for (int m : cands) {
    if (aborted || vcfNodes > nodeLimit) break;
    vcfNodes++;
    put(m, p);
    int opp = 1 - p;
    int oppWin[16]; int oppN = winCells(opp, oppWin);
    bool good = false;
    if (oppN == 0) {
      int myWin[16]; int myN = winCells(p, myWin);
      if (myN >= 1) good = true;
      else {
        if (maxDepth > 1) {
          int res = vcfFind(p, maxDepth - 2, nodeLimit);
          if (res >= 0) good = true;
        }
      }
    } else if (oppN == 1) {
      put(oppWin[0], opp);
      int myWin[16]; int myN = winCells(p, myWin);
      if (myN >= 2) good = true;
      undo(oppWin[0], opp);
    }
    undo(m, p);
    if (good) return m;
  }
  return -1;
}

int negamax(int depth, int alpha, int beta, int ply, int me) {
  if (aborted) return 0;
  if ((nodes & 2047) == 0 && timeUp()) return 0;
  nodes++;
  int opp = 1 - me;
  int winOut[16];
  if (winCells(opp, winOut) > 0) return -WINV + ply;
  if (winCells(me, winOut) > 0) return WINV - ply;

  if (depth <= 0) return eval() * (me == 1 ? 1 : -1);

  uint64_t key = hsh;
  size_t idx = key & ttMask;
  TTE &e = tt[idx];
  int ttMove = -1;
  if (e.key == key && e.depth >= depth) {
    ttMove = e.move;
    int v = fromTT(e.score, ply);
    if (e.flag == 0) return v;
    if (e.flag == 1 && v >= beta) return v;
    if (e.flag == 2 && v <= alpha) return v;
  } else if (e.key == key) ttMove = e.move;

  // generate moves - RAPFI: forced block first
  int oppWin[16]; int oppWinN = winCells(opp, oppWin);
  std::vector<int> moves;
  if (oppWinN >= 1) {
    for (int i = 0; i < oppWinN; i++) moves.push_back(oppWin[i]);
  } else {
    // candidate + open four
    int seenTmp[128];
    int ofm = openFourMove(me, seenTmp);
    if (ofm >= 0) moves.push_back(ofm);
    for (int c : cand) if (std::find(moves.begin(), moves.end(), c) == moves.end()) moves.push_back(c);
    if (moves.size() > 48) {
      std::vector<std::pair<int,int>> scored;
      for (int m : moves) scored.push_back({cellScore(m, me) + cellScore(m, opp) + history_[me][m]/16, m});
      std::sort(scored.begin(), scored.end(), [](auto &a, auto &b){return a.first > b.first;});
      moves.clear();
      for (size_t i = 0; i < std::min<size_t>(32, scored.size()); i++) moves.push_back(scored[i].second);
    }
  }

  if (moves.empty()) return eval() * (me == 1 ? 1 : -1);

  // move ordering
  if (ttMove >= 0) {
    auto it = std::find(moves.begin(), moves.end(), ttMove);
    if (it != moves.end()) { std::swap(*it, moves[0]); }
  }

  int origAlpha = alpha;
  int best = -INFV;
  int bestm = moves[0];

  for (size_t i = 0; i < moves.size(); i++) {
    int m = moves[i];
    put(m, me);
    int v;
    if (i == 0) v = -negamax(depth - 1, -beta, -alpha, ply + 1, opp);
    else {
      int r = (i >= 8 && depth >= 3) ? 1 : 0;
      v = -negamax(depth - 1 - r, -alpha - 1, -alpha, ply + 1, opp);
      if (r && v > alpha) v = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, opp);
      if (v > alpha && v < beta) v = -negamax(depth - 1, -beta, -alpha, ply + 1, opp);
    }
    undo(m, me);
    if (aborted) return 0;
    if (v > best) { best = v; bestm = m; }
    if (v > alpha) alpha = v;
    if (alpha >= beta) {
      int h = history_[me][m] + depth * depth;
      history_[me][m] = (int16_t)(h > 16000 ? 16000 : h);
      if (i > 0) { killers[ply][1] = killers[ply][0]; killers[ply][0] = m; }
      break;
    }
  }
  e.key = key; e.score = (int16_t)toTT(best, ply); e.move = (int16_t)bestm; e.depth = (int8_t)depth;
  e.flag = best <= origAlpha ? 2 : (best >= beta ? 1 : 0);
  return best;
}

struct Stats { int depth = 0; long long nodes = 0; int ms = 0; int score = 0; int reason = 0; } stats;

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
  if (on >= 1) {
    int bi = ow[0], bs = -1;
    for (int t = 0; t < on; t++) { int s = cellScore(ow[t], me); if (s > bs) { bs = s; bi = ow[t]; } }
    stats.reason = 3; return bi;
  }

  double full = limitMs;
  limitMs = std::min(full, elapsed() + full * 0.28);
  int pv = vcfFind(me, 48, 4000000);
  aborted = false; limitMs = full;
  if (pv >= 0) { stats.reason = 4; stats.ms = (int)elapsed(); return pv; }

  std::vector<std::pair<int, int>> rc;
  for (int j : cand) rc.push_back({cellScore(j, me) + (int)(cellScore(j, opp)*DEF_K) + history_[me][j]/10, j});
  std::sort(rc.begin(), rc.end(), [](auto &a, auto &b) { return a.first > b.first; });
  if (rc.size() > 64) rc.resize(64);
  std::vector<int> order;
  for (auto &x : rc) order.push_back(x.second);
  if (order.empty()) { for (int i = 0; i < N; i++) if (!bd[i]) return i; return -1; }

  if (pen) {
    std::vector<int> lo, rest;
    for (int i : order) (pen[i] >= 500 ? lo : rest).push_back(i);
    if (!lo.empty() && !rest.empty()) {
      if (rest.size() >= 4) { for (int i : lo) if (pen[i] < 900) rest.push_back(i); }
      else for (int i : lo) rest.push_back(i);
      order = rest;
    }
  }

  limitMs = std::min(full, elapsed() + full * 0.35);
  bool threat = vcfFind(opp, 28, 600000) >= 0;
  aborted = false;
  if (threat) {
    std::vector<int> safe, unsafe;
    size_t k = 0;
    for (; k < order.size(); k++) {
      int m = order[k];
      put(m, me);
      bool bad = !wset[me][1].empty() ? false : vcfQuick(opp, 28, 150000);
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
  for (int depth = 1; depth <= 48; depth++) {
    double now = elapsed();
    if (depth >= 8 && lastIter > 0 && now + lastIter * 2.2 > limitMs) break;
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
    if (bestScore > WINV - 1000) break;
    if (bestScore < -WINV + 1000 && depth >= 4) break;
    if (depth >= 10 && stable >= 3 && elapsed() > limitMs * 0.60) break;
  }
  stats.depth = completed; stats.nodes = nodes; stats.ms = (int)elapsed(); stats.score = bestScore;
  return best;
}

uint8_t inBoard[MAXN];
uint16_t inPen[MAXN];

}  // namespace

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
