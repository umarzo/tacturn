// TacTurn Chess Engine v1 — search implementation.
//
// Faithful C++ port of Search.kt () with the search upgrades noted in
// search.h. Structure mirrors the Kotlin Worker design: per-thread search
// state, main worker mutates the caller's position, helpers search clones,
// all sharing the race-hardened transposition table.

#include "search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#if defined(__AVX512F__)
#include <immintrin.h>
#define TACTURN_AVX512 1
#elif defined(__AVX2__)
#include <immintrin.h>
#define TACTURN_AVX2 1
#endif

#if defined(__ANDROID__)
#include <pthread.h>
#endif

#include "movegen.h"
#include "see.h"

namespace tacturn {

namespace tuning {
Tunables g = defaults();
}  // namespace tuning

// Host verification hook (tools/uci.cpp fuzzGivesCheck): re-export of the
// anonymous-namespace quiet-check prefilter so the fuzz harness can prove
// it exact against the post-make checkers() truth. Not used by the engine.
bool hostGivesCheckQuiet(const Position &b, Move m);  // defined below
namespace {

constexpr int MAX_PLY_GUARD = MAX_PLY - 1;
// Sentinel for "no static eval this ply" (in-check plies). Any real eval
// (corrected evals are bounded well inside +/-29016) sorts above this.
constexpr int EVAL_NO_EVAL = -31000;
// Draw contempt is now per-search (SearchLimits::contemptCp): play keeps
// the historical 12 cp default, analysis passes 0 for POV-neutral evals.
// DRAW_CONTEMPT remains only as the documented historical default.

// Correction-history experiment switch (0 = disabled).
constexpr bool USE_CORRHIST = true;

// v2.5 S1: Stockfish-style update conditions — only teach the correction
// tables when the best move is quiet (or none) and the bound is coherent
// with the static eval. MEASURED at 150 ms/move (180 games, vs v2.4 search):
// 49.4% (−12 Elo point est.) — this engine's corrhist was tuned WITH
// unconditional updates, so the SF-style gating loses signal here. OFF.
constexpr bool USE_CORR_QUIET_COND = false;

// v2.5 S2: double extensions — a TT-PV singular move whose exclusion score
// is far below the singular beta is exceptionally forcing: extend by 2.
// MEASURED with S1 at 150 ms/move (180 games): neutral-negative. The
// FLAG_EXACT proxy for Stockfish's ttPv is too loose for this engine. OFF.
constexpr bool USE_DOUBLE_EXT = false;
constexpr int DOUBLE_EXT_MARGIN = 25;

// ---- tuned search constants (v2.2 ladder, verified by fixed-node matches) ----
constexpr int RFP_BASE = 76;           // reverse-futility: RFP_BASE*depth*(improving?1:1.5)
constexpr int FUT_BASE = 100;          // quiet futility base
constexpr int FUT_MG_IMPROVING = 80;   // + per depth
constexpr int FUT_MG_NONIMPROVING = 115;
constexpr int NMP_EVAL_SLACK = 60;     // staticEval >= beta - slack (baseline)

int lmrTable[65][65];

void fillLmrTable(const tuning::Tunables &tp) {
    // Filled per search from the worker's parameter snapshot (defaults are
    // bit-identical to the shipped constants; the base/divisor read as
    // 800/1000.0 = 0.8 and 2250/1000.0 = 2.25 exactly).
    //
    // v1.4.3: compute the table only when the (base, divisor) pair actually
    // changed. Every Worker::prepare() used to re-run the 4,096 std::log()
    // calls and rewrite the global table — once per worker per MOVE, with
    // helper workers writing the same values to the same addresses
    // concurrently (benign on every shipped target, since all writers store
    // identical bytes, but pointless). The latched pair keeps later
    // prepares O(1); the written values are identical, so the search tree
    // is bit-identical. UCI parameter changes simply produce a new pair and
    // refill (idempotent — concurrent identical refills store identical
    // values, the same exposure the sequential version had across moves).
    static std::atomic<int> latchedBase{-1};
    static std::atomic<int> latchedDiv{-1};
    int lb = latchedBase.load(std::memory_order_relaxed);
    int ld = latchedDiv.load(std::memory_order_relaxed);
    if (lb == tp.lmr_base_m && ld == tp.lmr_div_m) return;
    for (int d = 1; d < 65; d++)
        for (int m = 1; m < 65; m++) {
            int r = (int)(tp.lmr_base_m / 1000.0 +
                         std::log((double)d) * std::log((double)m) / (tp.lmr_div_m / 1000.0));
            if (r < 0) r = 0;
            if (r > 8) r = 8;
            lmrTable[d][m] = r;
        }
    latchedBase.store(tp.lmr_base_m, std::memory_order_relaxed);
    latchedDiv.store(tp.lmr_div_m, std::memory_order_relaxed);
}

inline int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------------
// v1.4.8: bit-exact SIMD argmax for the selection sort.
//
// The scalar loop picks the FIRST occurrence of the maximum (bi is only
// displaced by a STRICTLY greater score), so a two-pass vector scan —
// (1) reduce the max value, (2) find the first index equal to it —
// returns the identical index on every input, including every tie
// pattern. Bit-exactness is not approximate here: same bi => same swap
// => identical displacement dynamics => identical tree (the §41.2
// selection-sort law is preserved because the ARRAY CONTENTS and the
// chosen index sequence are byte-identical to the scalar loop's).
//
// The argmax scan is the O(n) inner loop of an O(n^2) selection sort run
// once per searched move per node (~150-400 int32 compares per interior
// node at typical branching); vectorizing it is pure speed at equal
// depth. Bench identity is the proof obligation, not a gate.
// ------------------------------------------------------------------
static inline int argMaxScores(const int32_t *s, int from, int count) {
#if defined(TACTURN_AVX512)
    if (count - from > 16) {
        // Pass 1: max value over [from, count).
        __m512i vmax = _mm512_loadu_si512((const __m512i *)(s + from));
        int i = from + 16;
        for (; i + 16 <= count; i += 16)
            vmax = _mm512_max_epi32(vmax, _mm512_loadu_si512((const __m512i *)(s + i)));
        int32_t m = (int32_t)_mm512_reduce_max_epi32(vmax);
        for (; i < count; i++)
            if (s[i] > m) m = s[i];
        // Pass 2: first index in [from, count) equal to m.
        int j = from;
        const __m512i vm = _mm512_set1_epi32(m);
        for (; j + 16 <= count; j += 16) {
            __mmask16 eq = _mm512_cmpeq_epi32_mask(
                _mm512_loadu_si512((const __m512i *)(s + j)), vm);
            if (eq) return j + __builtin_ctz((unsigned)eq);
        }
        for (; j < count; j++)
            if (s[j] == m) return j;
        return from;  // unreachable (m is attained by construction)
    }
#else
    (void)s; (void)from; (void)count;
#endif
    int bi = from;
    for (int j = from + 1; j < count; j++)
        if (s[j] > s[bi]) bi = j;
    return bi;
}



inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline std::chrono::steady_clock::time_point msToDeadline(int64_t ms) {
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}

// v1.4.11 CRITICAL FIX: an ABSOLUTE wall-clock deadline at epoch-ms
// `absMs` (a nowMs()-based timestamp). msToDeadline() builds a deadline
// RELATIVE to now(); feeding it `startMs + cap` (an epoch timestamp plus
// a duration) produced now() + startMs + cap — a deadline one full
// steady_clock uptime in the future. Every post-first-iteration re-arm of
// the hard deadline used that form, so the hard wall NEVER fired after
// the first iteration completed: an iteration whose cost estimate missed
// (tactical explosions — aspiration fail cascades, sacrifice chains, deep
// forcing lines: exactly the sharp positions that decide winning games)
// ran unbounded, and the engine flagged. This helper restores the wall.
inline std::chrono::steady_clock::time_point deadlineAt(int64_t absMs) {
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(absMs));
}

// ------------------------------------------------------------------
// v1.1 quiet-check prefilter — does a QUIET (non-capture, non-promo,
// non-castle) move give check, computed on the PRE-move board?
//
// Direct check: the moved piece attacks the enemy king from its landing
// square, with the post-move occupancy (the vacated `from` no longer
// blocks, the landed `to` blocks lines as it will in reality).
//
// Discovered check: `from` was blocking a slider ray to their king. With
// the post-move occupancy, "our rook/queen (or bishop/queen) attacks the
// king" can only fire if removing `from` (or landing `to`) opened the
// line — when this fires at all, the enemy king IS attacked by that
// slider in the post-move position, so reporting "check" is exact. (The
// pre-condition !inCheck guarantees no own slider attacks the king before
// the move, so there is no stale-positive source.) Exact for quiets:
// a knight can never land on a line through its own origin; a pawn that
// stays on the discovered ray still blocks it (it is ON `to` in occAfter).
// Castling is handled separately (rook-landing + king-vacation checks).
// ------------------------------------------------------------------
inline bool givesCheckQuiet(const Position &b, Move m) {
    const int us = b.sideToMove, them = us ^ 1;
    const int from = moveFrom(m), to = moveTo(m), pc = movePiece(m);
    const int ksq = b.kingSquare(them);
    const Bitboard kingBb = bitOf(ksq);
    const Bitboard occAfter = (b.occupied & ~bitOf(from)) | bitOf(to);

    switch (pc) {
    case PAWN:
        if (PAWN_ATT[us][to] & kingBb) return true;
        break;
    case KNIGHT:
        if (KNIGHT_ATT[to] & kingBb) return true;
        break;
    case BISHOP:
        if (bishopAttacks(to, occAfter) & kingBb) return true;
        break;
    case ROOK:
        if (rookAttacks(to, occAfter) & kingBb) return true;
        break;
    case QUEEN:
        if ((bishopAttacks(to, occAfter) | rookAttacks(to, occAfter)) & kingBb)
            return true;
        break;
    case KING:
        break;  // a king never directly checks
    }

    Bitboard sliders = b.pieceBb[QUEEN] & b.colorBb[us];
    if (rookAttacks(ksq, occAfter) & ((b.pieceBb[ROOK] & b.colorBb[us]) | sliders))
        return true;
    if (bishopAttacks(ksq, occAfter) & ((b.pieceBb[BISHOP] & b.colorBb[us]) | sliders))
        return true;
    return false;
}

}  // namespace

// ==================================================================
// Worker — one per search thread
// ==================================================================

struct Searcher::Worker {
    int id = 0;
    Searcher *owner = nullptr;
    Position *pos = nullptr;         // main: caller's board; helper: own clone
    Position ownPos;                 // helper clone storage

    int maxDepth = 64;
    int64_t moveTimeMs = 0;
    int64_t hardMs = 0;
    SearchLimits limits;
    TimeManager *tm = nullptr;
    int jitter = 0;
    uint64_t rng = 0;
    std::chrono::steady_clock::time_point hardDeadline;
    int64_t startMs = 0;

    int evalStack[MAX_PLY];
    tuning::Tunables p;  // parameter snapshot, taken at prepare()
    Move prevIterBest = MOVE_NONE;
    int prevIterScore = -INFINITE;
    int aspirationFails = 0;
    uint64_t nodeLimit = 0;

    uint64_t nodes_ = 0;
    std::atomic<uint64_t> nodesShared{0};

    // ---- ordering tables (side-aware) ----
    // v1.1: int16 storage. The gravity update already clamps every entry to
    // [-32768, 32767], so halving the element width is bit-identical while
    // halving the table footprint (the four continuation tables drop from
    // 4.7 MB to 2.4 MB per worker) — measurably better cache behavior at
    // zero behavioral cost.
    Move killers[MAX_PLY * 2];
    int16_t mainHist[2 * 384];
    // v1.4.0 lever: from-to history. [side][from*64+to] — the square-pair
    // signal complements the piece-to main history (a knight to e5 and a
    // bishop to e5 learn separate cells there; here Nf3-e5 and Ng1-e5
    // share one cell). Fed into quiet ordering (weight /4, same family
    // as the main history) and the LMR history value (weight /8 — the
    // quietHistoryValue distribution is calibrated to the LMR gates, so
    // the new term enters the calibrated signal at half weight).
    int16_t fromToHist[2 * 4096];
    // v1.4.0 lever: low-ply history. [ply bucket 0: ply 1-2, 1: ply 3-4]
    // [from*64+to] — from-to at the very top of the tree, where move
    // ordering is worth the most (the root's first quiet choices decide
    // the whole iteration's shape). Updated with the standard gravity
    // bonus/malus on cutoffs at ply 1-4, read in ordering at ply <= 4.
    int16_t lowPlyHist[2][4096];
    // v1.3.1 lever: pawn-structure history. [side][pawn-key bucket (512)]
    // [piece type x to square] — the main-history feature space refined by
    // the node's pawn structure (pawn-key hash bucket). Fed into quiet
    // ordering, the LMR history value (quietHistoryValue) and the cutoff
    // bonus/malus updates, keyed by the pawn key of the node where the move
    // is made. 2 x 512 x 384 x 2B = ~0.8 MB per worker.
    // Measured: +25.7 ± 9.2 over 984 games (5k nodes).
    static constexpr int PAWN_HIST_BUCKETS = 512;
    static constexpr int PAWN_HIST_MASK = PAWN_HIST_BUCKETS - 1;
    static constexpr int PAWN_HIST_STRIDE = PAWN_HIST_BUCKETS * 384;
    int16_t pawnHist[2 * PAWN_HIST_STRIDE];
    int16_t followupHist[2 * 147456];
    int16_t counterHist[2 * 147456];
    int16_t contHist3[2 * 147456];  // 3 plies back (opponent)
    int16_t contHist4[2 * 147456];  // 4 plies back (own)
    int16_t captureHist[2 * 2304];
    Move counterMoves[2 * 384];

    // ---- evaluation correction history ----
    // Buckets are keyed by the pawn placement / own non-pawn placement;
    // entries are indexed by (piece type, square) of the side to move.
    // Applied as a small correction on top of the static eval, learned
    // from how far the search disagrees with the eval at cutoffs.
    // v1.4.7 fix (POV aliasing): the tables now carry a leading
    // side-to-move dimension. The old corrPawn[CORR_BUCKETS][384] and
    // corrNonPawn[color][...] received writes from BOTH colors' searches
    // in the WRITER's POV — one ply apart, White's "+diff is good for
    // White" and Black's "+diff is good for Black" landed in the SAME
    // cell with OPPOSITE world meanings (the pawn key does not change
    // across quiet non-pawn plies, and corrNonPawn's color dimension is
    // the KEY OWNER, not the side to move). Every entry was an aliasing
    // soup of two signed signals. Keying by [stm] makes every entry
    // single-POV: written by `us`, read only when `us` is to move.
    // CORR_BUCKETS halves (4096 -> 2048) so the per-worker footprint is
    // unchanged at 9.4 MB; each [stm] table now sees ~half the write
    // traffic, so per-table collision load matches the old shared table.
    static constexpr int CORR_BUCKETS = 2048;
    static constexpr int CORR_MASK = CORR_BUCKETS - 1;
    static constexpr int CORR_LIMIT = 1024;
    int16_t corrPawn[2][CORR_BUCKETS][384];
    int16_t corrNonPawn[2][2][CORR_BUCKETS][384];
    // v1.4.10 lever: CONTINUATION correction history. The pawn/non-pawn
    // tables above key the correction by material PLACEMENT (pawn key /
    // non-pawn keys); this table keys it by the opponent's last move
    // (piece-to of stack[ply-1], the exact key space the continuation
    // ORDERING histories use). The same structural error (static eval
    // systematically off in a recognizable context) often depends on what
    // just happened, not only on where the pieces sit: a recapture context,
    // a piece landing on a fresh outpost square, a threat the eval has not
    // priced. [stm][prevMovePieceTo * 384 + ownPieceTo] — 2 x 147456 x 2B
    // = 576 KB per worker. Read in corrSumOnly (all own pieces, like the
    // other corr tables), taught in updateCorr (same cells, same gravity,
    // same bonus). POV-clean by the v1.4.7 [stm] keying. Never aged (the
    // v1.4.5 persistence regime the other corr tables follow).
    int16_t corrCont[2 * 147456];

    // ---- per-ply buffers ----
    Move movesBuf[MAX_PLY][MAX_MOVES];
    int32_t scoresBuf[MAX_PLY][MAX_MOVES];
    Move triedBuf[MAX_PLY][MAX_MOVES];
    Move stack[MAX_PLY + 4];

    // ---- root state ----
    Move rootMoves[MAX_MOVES];
    int32_t rootScores[MAX_MOVES];
    // v1.4.0: per-root-move node spend of the CURRENT iteration (swapped
    // alongside rootMoves by sortRootMoves so it stays keyed to the move).
    // Feeds the time manager's root node-fraction scaling.
    uint64_t rootMoveNodes[MAX_MOVES];
    int bestMoveNodesPm = -1;  // best move's per-mille share, -1 = unknown
    int rootCount = 0;
    Move iterationBestMove = MOVE_NONE;
    int iterationBestScore = -INFINITE;
    // v1.4.2: set by the aspiration loop before a FAIL-HIGH re-search so
    // searchRoot does not wipe the proven >= beta publication of the pass
    // that caused it (salvage-wipeout fix — see searchRoot's reset below).
    bool iterationBestPreserve = false;
    // v1.1: final per-worker result (for the Lazy-SMP best-thread pick)
    SearchResult lastResult;

    PawnHashTable pawnHash;
    bool prepared = false;

    // ---- raw-eval cache (direct-mapped; value is pre-corrhist eval) ----
    // evaluate() is a pure function of the placement (key covers side to
    // move), so a hit skips the whole term sweep. The corrhist correction
    // is applied on top of the cached raw score every call.
    // v2.4: 64k -> 256k entries (3 MB/worker) — the richer v2.4 eval costs
    // more per miss, so a higher hit rate pays for the extra memory.
    // v5: each entry also caches the NNUE residual (same miss fills both;
    // the residual is a pure function of placement+stm too). PRUNING PATHS
    // read the HCE score only — the search constants were tuned around the
    // HCE signal. The residual is applied exclusively at quiescence
    // stand-pat (leafEval), the horizon where its positional knowledge pays
    // and its tactical noise costs least.
    static constexpr int EC_BITS = 18;
    static constexpr int EC_MASK = (1 << EC_BITS) - 1;
    static constexpr int32_t RES_INVALID = 0x7FFFFFFF;
    // v1.4.6: 8-byte entry (was 16). The index already carries the low 18
    // key bits; storing the HIGH 32 bits gives 50-bit effective
    // verification — stronger than the TT's own 16-bit check. |score| is
    // bounded by the HCE material+term ceiling (< 10k) and |res| by
    // 1 + RES_CLAMP*blend/100 (< 470); both fit int16 with RES_INVALID16
    // = 32767 as the never-real marker. Halves the eval cache's footprint
    // (4 MB -> 2 MB per worker): twice the entries per L2/L3 line.
    static constexpr int16_t RES_INVALID16 = (int16_t)0x7FFF;
    struct ECEntry {
        uint32_t keyHi;
        int16_t score;
        int16_t res;     // cached NNUE residual, mover POV (blend applied)
    };
    static inline bool ecMatch(const ECEntry &e, Bitboard key) {
        return e.keyHi == (uint32_t)(key >> 32);
    }
    ECEntry evalCache[1 << EC_BITS];

    bool isMain() const { return id == 0; }

    Worker() {
        // v5.1 determinism fix: the ordering tables were previously left
        // uninitialized (only AGED in prepare(), which preserves garbage).
        // A Worker built on recycled heap memory then searched with garbage
        // history — nondeterministic trees across worker reuse (bench
        // fingerprints only reproduced on a fresh process). Zero the tables
        // here so every Worker starts from the intended clean state; the
        // fresh-process fingerprints are unchanged.
        memset(mainHist, 0, sizeof(mainHist));
        memset(fromToHist, 0, sizeof(fromToHist));
        memset(lowPlyHist, 0, sizeof(lowPlyHist));
        memset(pawnHist, 0, sizeof(pawnHist));
        memset(followupHist, 0, sizeof(followupHist));
        memset(counterHist, 0, sizeof(counterHist));
        memset(contHist3, 0, sizeof(contHist3));
        memset(contHist4, 0, sizeof(contHist4));
        memset(captureHist, 0, sizeof(captureHist));
        memset(counterMoves, 0, sizeof(counterMoves));
        memset(corrPawn, 0, sizeof(corrPawn));
        memset(corrNonPawn, 0, sizeof(corrNonPawn));
        memset(corrCont, 0, sizeof(corrCont));
        memset(evalCache, 0, sizeof(evalCache));
    }

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void prepare(Position &board, const SearchLimits &lim, int jitterCp) {
        prepareSetup(board, lim, jitterCp);
        prepareAge();
    }

    /** v1.4.3: the clone/limits half of prepare(). Runs on the caller's
     *  thread — it READS the caller's board (cloneForSearch), which is only
     *  safe before the main worker starts mutating it. */
    void prepareSetup(Position &board, const SearchLimits &lim, int jitterCp) {
        p = tuning::g;  // snapshot tunables (stable for the whole search)
        fillLmrTable(p);
        if (isMain()) {
            pos = &board;
        } else {
            ownPos = board.cloneForSearch();
            pos = &ownPos;
        }
        maxDepth = lim.maxDepth;
        moveTimeMs = lim.moveTimeMs;
        hardMs = lim.hardMoveTimeMs > 0 ? lim.hardMoveTimeMs : lim.moveTimeMs;
        limits = lim;
        tm = nullptr;
        jitter = jitterCp;
        rng = lim.rngSeed != 0 ? (lim.rngSeed + 0x9E3779B9ULL * (id + 1)) : nowMs() * 2654435761ULL + id;
        nodes_ = 0;
        nodesShared.store(0, std::memory_order_relaxed);
        hardDeadline = std::chrono::steady_clock::time_point::max();
        nodeLimit = lim.nodeLimit;
        prevIterBest = MOVE_NONE;
        prevIterScore = -INFINITE;
        aspirationFails = 0;
        memset(killers, 0, sizeof(killers));
        // (v1.4.3 note: RETAINING counterMoves across moves — the
        // Stockfish-standard treatment, the third-dev audit's claim 7 —
        // was tested and could not be distinguished from noise here:
        // +44 in one 95-game run, then -6.4 and -13.9 in the two 600-game
        // assemblies that contained it, -23 in the 287-game assembly
        // without it. Unlike every other history table, a counterMove
        // carries the ABSOLUTE 2880000 priority band, so a stale
        // refutation outranks every quiet the current position actually
        // earned — and this engine's ordering was tuned with the per-move
        // wipe. Dropped from the release; the wipe stays. See HISTORY.md
        // §40.)
        memset(counterMoves, 0, sizeof(counterMoves));
        std::fill(stack, stack + MAX_PLY + 4, MOVE_NONE);
        std::fill(evalStack, evalStack + MAX_PLY, 0);
        prepared = true;
    }

    /** v1.4.3: the history-aging half of prepare(), split out so helper
     *  workers can run it INSIDE their own thread (see think()). The sweeps
     * touch only worker-local tables — no shared state, no board access —
     *  so they are always safe to parallelize; the clone/limits half
     *  (prepareSetup) stays on the caller's thread because it READS the
     *  caller's board while it must not yet be mutated. */
    void prepareAge() {
        // Age (soft-decay) the histories instead of wiping them.
        // v1.3: halve instead of quartering. >>= 2 every move discarded
        // 75% of what the tables learned between moves of a game — most
        // of the ordering signal died before it could be reused on the
        // next move. Halving keeps half the signal (the standard aging
        // choice) while still bounding staleness.
        for (size_t i = 0; i < sizeof(mainHist) / sizeof(int16_t); i++) mainHist[i] >>= 1;
        for (size_t i = 0; i < sizeof(fromToHist) / sizeof(int16_t); i++) fromToHist[i] >>= 1;
        for (size_t i = 0; i < sizeof(lowPlyHist) / sizeof(int16_t); i++)
            lowPlyHist[i / 4096][i % 4096] >>= 1;
        for (size_t i = 0; i < sizeof(pawnHist) / sizeof(int16_t); i++) pawnHist[i] >>= 1;
        for (size_t i = 0; i < sizeof(followupHist) / sizeof(int16_t); i++) followupHist[i] >>= 1;
        for (size_t i = 0; i < sizeof(counterHist) / sizeof(int16_t); i++) counterHist[i] >>= 1;
        for (size_t i = 0; i < sizeof(contHist3) / sizeof(int16_t); i++) contHist3[i] >>= 1;
        for (size_t i = 0; i < sizeof(contHist4) / sizeof(int16_t); i++) contHist4[i] >>= 1;
        for (size_t i = 0; i < sizeof(captureHist) / sizeof(int16_t); i++) captureHist[i] >>= 1;
        // v1.4.5: corrPawn/corrNonPawn are NO LONGER aged here. Correction
        // history learns slowly across a game and Stockfish never ages it
        // between moves — gravity alone bounds the entries. Halving every
        // move discarded half of the learned eval corrections before they
        // could be reused, and the 9.4 MB sweep was 75% of prepareAge's
        // memory traffic. Tables are still zeroed on ucinewgame (prepare()).
    }

    // ------------------------------------------------------------------
    // Evaluation correction history
    // ------------------------------------------------------------------

    /** Static eval + learned correction (pawn bucket + own + enemy non-pawn
     *  buckets). The enemy-non-pawn table lets the correction react to the
     *  OPPONENT's piece placement too — the same structure Stockfish uses
     *  (pawn / non-pawn-own / non-pawn-their). */
    int rawEvalCached() {
        static constexpr bool USE_EVAL_CACHE = true;  // C1 ablation switch
        if (!USE_EVAL_CACHE) return evaluate(*pos, pawnHash);
        ECEntry &e = evalCache[pos->key & EC_MASK];
        if (!ecMatch(e, pos->key)) {
            e.keyHi = (uint32_t)(pos->key >> 32);
            e.score = (int16_t)evaluate(*pos, pawnHash);
            e.res = RES_INVALID16;  // lazy: computed only when a leaf reads it
            // Datagen hook (never active in the app): report eval-cache
            // misses — the positions the search actually evaluates.
            if (nnue::dataSink && nnue::dataSinkMask &&
                (nodes_ & nnue::dataSinkMask) == 0)
                nnue::dataSink(nnue::dataSinkCtx, *pos, e.score);
        }
        return e.score;
    }

    /** v1.1: leaf-eval variants that take the ALREADY-COMPUTED corrected
     *  eval — quiesce computed correctedEval() for standEval and then the
     *  leaf helper recomputed the whole corrhist sum a second time. The
     *  correction sum is the expensive part (3 tables x every piece); the
     *  pass-through is bit-identical and saves one full sum per eval. */
    int leafEvalFrom(int corrected) {
        int v = corrected;
        if (!nnue::enabled) return v;
        ECEntry &e = evalCache[pos->key & EC_MASK];
        if (ecMatch(e, pos->key)) {
            if (e.res == RES_INVALID16) e.res = (int16_t)nnue::residual(*pos);
            return v + decayResidual(e.res);
        }
        // (correctedEval() just filled the entry; the guard is defensive)
        return v + decayResidual(nnue::residual(*pos));
    }

    /** Stand-pat with the residual computed only when it can affect the
     *  cutoff decision. The residual is clamped to +-RES_CLAMP BEFORE the
     *  blend is applied inside nnue::residual(), so the effective bound is
     *  RES_CLAMP*blend/100 (+1 for rounding) — far-boundary nodes skip the
     *  inference entirely. Cutoff decisions are exactly preserved;
     *  far-boundary fail-soft values are returned in HCE terms. */
    int leafEvalBoundedFrom(int alpha, int beta, int corrected) {
        int v = corrected;
        if (!nnue::enabled) return v;
        // v8: the skip bound must use the bucket's effective blend (the
        // endgame phase blend can exceed blendPct; blendForBucket returns
        // blendPct when the phase blend is off).
        int bp = nnue::blendForBucket(
            nnue::bucketOf(popcount(pos->occupied)));
        int bound = 1 + (nnue::RES_CLAMP * bp + 99) / 100;
        if (v >= beta + bound || v <= alpha - bound) return v;
        ECEntry &e = evalCache[pos->key & EC_MASK];
        if (ecMatch(e, pos->key)) {
            if (e.res == RES_INVALID16) e.res = (int16_t)nnue::residual(*pos);
            return v + decayResidual(e.res);
        }
        return v + decayResidual(nnue::residual(*pos));
    }

    int leafEval() { return leafEvalFrom(correctedEval()); }

    /** v1.4.5: 50-move residual decay. The HCE side already damps endgame
     *  scores as the halfmove clock runs out (evaluate.cpp's long-hmc
     *  decay), but the NNUE residual kept full strength all the way to the
     *  rule — so deep into a drawn-out game the engine kept playing for
     *  phantom advantages. Decay the residual linearly past halfmove 64
     *  (x(102-hmc)/38 — full strength at 64, ~0 at 100). Applied at the
     *  USE sites with the CURRENT clock so the eval cache stores the raw
     *  residual and the decay never goes stale. */
    inline int decayResidual(int r) const {
        int hmc = pos->halfmoveClock;
        if (hmc > 64) r = r * (102 - hmc) / 38;
        return r;
    }

    int leafEvalBounded(int alpha, int beta) {
        return leafEvalBoundedFrom(alpha, beta, correctedEval());
    }

    /** v6: the static-eval residual injection (staticBlendPct scaling of
     *  the cached full-blend residual). Used by BOTH the main-search static
     *  eval AND the qsearch TT store, so the TT eval field carries ONE
     *  semantics everywhere (v5.3 stored main-search evals WITH the
     *  residual but qsearch evals WITHOUT — qsearch stand-pats then read
     *  mixed-semantics values depending on which path stored the entry).
     *  Pure-HCE (UseNNUE=false) returns 0: the baseline is unchanged. */
    // (v1.4.4 final note: a 50-move dampening of the NNUE residual
    //  (r * (102 - hmc) / 38 beyond halfmove 64, mirroring the HCE decay)
    //  was implemented and STRIPPED before release together with the
    //  L5/L9 tree levers after the 210-game bundle probe read
    //  -28.2 ± 19.1 vs the -15.0 ± 28.0 no-op control band. The premise
    //  (HCE decays, residual does not) is real and worth a dedicated
    //  long-time-control gate someday; it could not prove itself inside
    //  this host's fixed-movetime noise. See HISTORY §41.)

    int staticResidualInject() {
        if (!nnue::enabled || nnue::staticBlendPct == 0) return 0;
        // (v1.4.3 note: caching the RAW residual and applying
        // blendResidual() per use site — including re-scaling the
        // quiescence TT-eval-hit stand-pat from the static blend to the
        // leaf blend — was tested and REJECTED: a ~100-game bisect
        // measured it at roughly -40 Elo. The 72%-vs-77% "mismatch" the
        // third-dev audit flagged is load-bearing: the pruning constants
        // were tuned with TT-hit stand-pats at the static-blend scale.
        // See HISTORY.md §40.)
        ECEntry &e = evalCache[pos->key & EC_MASK];
        int r;
        if (ecMatch(e, pos->key)) {
            if (e.res == RES_INVALID16) e.res = (int16_t)nnue::residual(*pos);
            r = e.res;
        } else {
            r = nnue::residual(*pos);
        }
        r = decayResidual(r);
        int bp = nnue::blendForBucket(
            nnue::bucketOf(popcount(pos->occupied)));
        if (nnue::staticBlendPct == bp) return r;
        // e.res carries the qsearch blend; rescale for the static injection.
        int t = r * 100 / (bp == 0 ? 1 : bp);
        t = t * nnue::staticBlendPct;
        return (t >= 0 ? (t + 50) : (t - 50)) / 100;
    }

    int correctedEval() {
        int raw = rawEvalCached();
        if (!USE_CORRHIST) return raw;
        return raw + corrSumOnly(corrPly_);
    }

    // v1.4.10: the ply of the innermost search frame (set at every
    // alphaBeta/quiesce entry) — the continuation corr key reads
    // stack[ply-1], the move that led into this node.
    int corrPly_ = 0;

    /** v1.4.10: the continuation-corr base index for the current node
     *  (opponent's last move piece-to key), or -1 when there is no
     *  usable context (root, null-move parent, exotic). */
    inline int corrContBase(int ply) const {
        if (ply < 1) return -1;
        Move p = stack[ply - 1];
        if (p == MOVE_NONE || movePiece(p) >= 6) return -1;
        return pos->sideToMove * 147456 +
               (movePiece(p) * 64 + moveTo(p)) * 384;
    }

    /** v1.4.0 lever: the live correction-history sum alone (no raw eval).
     *  The TT eval field now stores the corrhist-FREE static signal
     *  (raw eval + static-blend residual); readers re-apply THIS sum so
     *  the eval a TT entry lends to a later visit always carries the
     *  CURRENT correction state instead of the state at store time —
     *  corrections are learned all search long, so a stored corrected
     *  eval goes stale within seconds (and across moves of a game the
     *  table outlives, it is stale by construction). */
    int corrSumOnly(int ply) {
        if (!USE_CORRHIST) return 0;
        int us = pos->sideToMove;
        int them = us ^ 1;
        // v1.4.10: continuation table base (opponent's last move context).
        const int16_t *cT = nullptr;
        int cBase = corrContBase(ply);
        if (cBase >= 0) cT = corrCont + cBase;
        // v1.4.10: the cont correction sums SEPARATELY at HALF the corr
        // weight — the pawn/non-pawn divisor was tuned for a two-table
        // sum, and folding a third full-amplitude table into the same
        // sums over-corrects (bench d9 +23% nodes: evals noisier, pruning
        // less effective). Half weight keeps the total correction
        // magnitude inside the tuned envelope while the new context
        // signal still lands.
        int contSum = 0;
        // v1.4.7: [stm]-keyed tables (see the declaration note) — a cell
        // written by White's search is now only ever read by White's
        // search, in White's POV.
        const int16_t *pT = corrPawn[us][pos->pawnKey & CORR_MASK];
        const int16_t *nT = corrNonPawn[us][us][pos->nonPawnKey[us] & CORR_MASK];
        const int16_t *eT = corrNonPawn[us][them][pos->nonPawnKey[them] & CORR_MASK];
        int pawnSum = 0, npSum = 0;
        // v1.4.7: branchless pawn/non-pawn split — pawns and non-pawns
        // iterate separate bitboards instead of testing `pt != PAWN` on
        // every piece. Same cells, same sums (int addition commutes);
        // PAWN == 0, so a pawn's cell index pt*64 + sq is just sq.
        Bitboard pw = pos->colorBb[us] & pos->pieceBb[PAWN];
        while (pw) {
            int sq = lsb(pw);
            pw &= pw - 1;
            pawnSum += pT[sq];
            if (cT) contSum += cT[sq];
        }
        Bitboard np = pos->colorBb[us] & ~pos->pieceBb[PAWN];
        while (np) {
            int sq = lsb(np);
            np &= np - 1;
            int idx = (pos->pieceAt[sq] & 7) * 64 + sq;
            pawnSum += pT[idx];
            npSum += nT[idx] + eT[idx];
            if (cT) contSum += cT[idx];
        }
        // v5.3: divisor exposed as tuning::g.corrWeight (UCI CorrWeight);
        // 1024 = the shipped constant (probed 512 at 50 ms: -26.5 ± 25.7,
        // rejected — the weight is already at its optimum).
        return pawnSum / p.corrWeight + npSum / p.corrWeight +
               contSum / (p.corrWeight * 2);
    }

    static inline int16_t corrGravity(int16_t h, int b) {
        // v1.4.5: signed-h gravity (the old |h| * |b| damping broke both
        // negative quadrants: a positive cutoff bonus on a negative entry
        // subtracted from it once |h| > 16384, trapping cells at the clamp
        // until aging; a malus on a negative entry amplified itself).
        // Standard formula: h + b - h * |b| / 16384.
        int64_t ab = b < 0 ? -(int64_t)b : (int64_t)b;
        int64_t v = (int64_t)h + b - ((int64_t)h * ab) / 16384;
        if (v > CORR_LIMIT) v = CORR_LIMIT;
        if (v < -CORR_LIMIT) v = -CORR_LIMIT;
        return (int16_t)v;
    }

    /** Teach the correction tables how the search differed from static eval.
     *
     * The update must touch EXACTLY the features correctedEval() sums over:
     * every piece of the side to move at its current square, in both the
     * pawn-key and own-nonpawn-key tables. (An earlier version indexed by the
     * best move's piece/to-square — cells the application never reads for
     * that position — which turned the whole mechanism into noise.) */
    void updateCorr(int ply, int depth, int diff) {
        if (!USE_CORRHIST) return;
        int bonus = clampI(diff * (depth + 4) / 8, -512, 512);
        if (bonus == 0) return;
        int us = pos->sideToMove;
        int them = us ^ 1;
        // v1.4.7: [stm]-keyed tables + branchless pawn/non-pawn split
        // (same cells touched, same gravity per cell — the per-cell
        // updates are independent, so the visit order changes nothing).
        int16_t *pT = corrPawn[us][pos->pawnKey & CORR_MASK];
        int16_t *nT = corrNonPawn[us][us][pos->nonPawnKey[us] & CORR_MASK];
        int16_t *eT = corrNonPawn[us][them][pos->nonPawnKey[them] & CORR_MASK];
        // v1.4.10: continuation cells for THIS node's context (may be null).
        int cBase = corrContBase(ply);
        int16_t *cT = cBase >= 0 ? corrCont + cBase : nullptr;
        Bitboard pw = pos->colorBb[us] & pos->pieceBb[PAWN];
        while (pw) {
            int sq = lsb(pw);
            pw &= pw - 1;
            pT[sq] = corrGravity(pT[sq], bonus);
            if (cT) cT[sq] = corrGravity(cT[sq], bonus);
        }
        Bitboard np = pos->colorBb[us] & ~pos->pieceBb[PAWN];
        while (np) {
            int sq = lsb(np);
            np &= np - 1;
            int idx = (pos->pieceAt[sq] & 7) * 64 + sq;
            pT[idx] = corrGravity(pT[idx], bonus);
            nT[idx] = corrGravity(nT[idx], bonus);
            eT[idx] = corrGravity(eT[idx], bonus);
            if (cT) cT[idx] = corrGravity(cT[idx], bonus);
        }
    }

    /** v1.4.7: correction update on the POV-fixed tables. See the body
     *  note for why the theoretically-sound "bound coherence" gates were
     *  measured and rejected for this engine. */
    void updateCorrGated(Move bestMove, int ply, int depth, int bestScore, int staticEval,
                         int alphaOrig, int beta) {
        // v1.4.7: POV-fixed tables, UNCONDITIONAL update. The bound
        // "coherence" gates that pair with this fix in theory (skip
        // fail-highs below staticEval / fail-lows above it) were
        // implemented and MEASURED as a clear loss (0.0 ± 34.7 vs
        // +46.6 ± 34.8 for the POV fix alone, 60-game paired samples):
        // this engine's correction history was tuned end-to-end with the
        // unconditional update stream, and the incoherent-bounds portion
        // of it evidently carries usable signal (or provides the damping
        // the de-noised tables no longer get from cross-POV
        // cancellation). alphaOrig is accepted for signature stability.
        (void)bestMove;
        (void)alphaOrig;
        (void)beta;
        if (!USE_CORRHIST) return;
        updateCorr(ply, depth, bestScore - staticEval);
    }

    /** v1.4.7: full history/correction/eval-cache reset for ucinewgame.
     *  Since v1.4.5 the correction tables persist across MOVES by design
     *  (prepareAge no longer ages them), so a new game in a persistent
     *  process needs an explicit wipe — clearTable() used to clear only
     *  the TT, letting one game's learned corrections leak into the next
     *  through recycled workers. Zeroes exactly the tables the Worker
     *  constructor zeroes, so a cleared worker is bit-identical to a
     *  fresh one. */
    void clearHistories() {
        memset(mainHist, 0, sizeof(mainHist));
        memset(fromToHist, 0, sizeof(fromToHist));
        memset(lowPlyHist, 0, sizeof(lowPlyHist));
        memset(pawnHist, 0, sizeof(pawnHist));
        memset(followupHist, 0, sizeof(followupHist));
        memset(counterHist, 0, sizeof(counterHist));
        memset(contHist3, 0, sizeof(contHist3));
        memset(contHist4, 0, sizeof(contHist4));
        memset(captureHist, 0, sizeof(captureHist));
        memset(counterMoves, 0, sizeof(counterMoves));
        memset(corrPawn, 0, sizeof(corrPawn));
        memset(corrNonPawn, 0, sizeof(corrNonPawn));
        memset(corrCont, 0, sizeof(corrCont));
        memset(evalCache, 0, sizeof(evalCache));
    }

    inline void checkStop() {
        ++nodes_;
        if (nodeLimit > 0 && nodes_ >= nodeLimit) {
            owner->stopped_.store(true, std::memory_order_relaxed);
            return;
        }
        if ((nodes_ & 511ULL) == 0) {
            // 512-node mask (was 2048): the hard-deadline abort latency
            // shrinks ~4x, which matters on small clock budgets and on
            // iteration caps. Cost is one relaxed store + clock read per
            // 512 nodes (< 0.1% of search time).
            if (std::chrono::steady_clock::now() >= hardDeadline)
                owner->stopped_.store(true, std::memory_order_relaxed);
            nodesShared.store(nodes_, std::memory_order_relaxed);
        }
    }

    inline bool stopped() const { return owner->stopped_.load(std::memory_order_relaxed); }

    /** v1.3: the POV-relative draw score every draw source returns (see the
     *  sign note at the repetition/50-move/dead-material branch). Kept in
     *  one place so stalemate can share it — the old hard-0 for stalemate
     *  made a clock-pressured engine PREFER stalemating (0 beats the -C
     *  draw) over riding out to the 50-move rule. */
    inline int drawScore(int ply) const {
        int ct = limits.contemptCp;
        if (ct) {
            int np = popcount(pos->occupied & ~pos->pieceBb[PAWN] &
                              ~pos->pieceBb[KING]);
            if (np < 8) ct = ct * np / 8;
        }
        return (ply & 1) ? ct : -ct;
    }

    // ------------------------------------------------------------------
    // Iterative deepening
    // ------------------------------------------------------------------

    /** v1.4.0: next-iteration cost estimate, gap-aware. Main walks every
     *  depth (gap 1: the historical x1.3); helper depth-skip ladders jump
     *  2-3 plies per iteration, and the tree grows ~1.55x per ply, so a
     *  gap-g iteration costs ~(0.55*g + 0.75)x the last completed one. */
    static int64_t estMsFor(int64_t lastIterMs, int gap) {
        if (gap < 1) gap = 1;
        int64_t est = gap == 1
                          ? lastIterMs * 13 / 10
                          : lastIterMs * (13 + 11 * (gap - 1)) / 10;
        if (est < lastIterMs + 2) est = lastIterMs + 2;
        return est < 2 ? 2 : est;
    }

    SearchResult runThink(const std::function<void(const IterationInfo &)> &onIteration) {
        SearchResult result;
        startMs = nowMs();
        // The pre-run work outside this function (worker prepare sweeps,
        // ~2-4 ms) is deliberately NOT counted against the budget here: it
        // is bounded, memory-bandwidth-bound, and rides inside the caller's
        // overhead reserve (the clock formulas reserve 60+ ms). Counting it
        // would silently tax every fixed-movetime request the same amount.
        int64_t budgetMs = hardMs > 0 ? hardMs : moveTimeMs;
        hardDeadline = budgetMs > 0 ? msToDeadline(budgetMs)
                                    : std::chrono::steady_clock::time_point::max();

        rootCount = generateLegal(*pos, rootMoves);
        if (rootCount == 0) return result;
        pos->nnuePrimeRoot();  // v9: guarantee a valid ring base for walks

        const SearchLimits &lim = limits;
        std::unique_ptr<TimeManager> tmLocal;
        // v1.4.0 Lazy-SMP fix: EVERY worker now runs the adaptive time
        // manager (was main only). Helpers used to pace against the raw
        // budgets, so whenever main extended an unstable position to
        // 1.5x soft, the helpers had already refused their next iteration
        // — the entire unstable extension ran single-threaded. Each worker
        // keeps its OWN TimeManager (the state is per-search mutable), so
        // helpers follow the same soft budget, stability scaling and
        // unstable extension as main.
        if (lim.adaptiveTime && moveTimeMs > 0) {
            tmLocal.reset(new TimeManager(moveTimeMs, hardMs > 0 ? hardMs : moveTimeMs,
                                          lim.depthFloor));
            tm = tmLocal.get();
            tm->planBudget(*pos, rootMoves, rootCount, lim.lastScore);
        } else {
            tm = nullptr;
        }

        Move bestMove = rootMoves[0];
        int bestScore = -INFINITE;
        int completedDepth = 0;
        int64_t lastIterMs = 0;

        // v1.4.0 Lazy-SMP depth ladders. Main searches every depth. Helper
        // h skips every (h+1)-th depth (helper 1: odd depths only, helper
        // 2: multiples of 3, ...) — distinct iteration ladders spread the
        // helpers' trees across different TT regions and let a helper
        // complete a strictly deeper iteration for the best-thread pick.
        // (The old 1 + id%2 start offset produced only two distinct
        // ladders — and both coincided with main's after one ply.)
        int depth = 1;
        if (!isMain())
            while (depth % (id + 1) == 0) depth++;
        while (depth <= maxDepth) {
            if (stopped()) break;

            // Battery saver: duty-cycle the CPU by sleeping briefly
            // between depth iterations (long analysis sessions). The sleep
            // is chunked and stop-aware so requestStop() reacts within 50 ms.
            if (isMain() && limits.interIterationDelayMs > 0) {
                int64_t remaining = limits.interIterationDelayMs;
                while (remaining > 0 && !stopped()) {
                    int64_t chunk = remaining < 50 ? remaining : 50;
                    std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                    remaining -= chunk;
                }
                if (stopped()) break;
            }

            if (depth > 1 && moveTimeMs > 0) {
                int64_t elapsedMs = nowMs() - startMs;
                // v1.4.0: the next-iteration cost estimate scales with the
                // DEPTH GAP to the iteration about to start (main: always 1;
                // helpers on skip ladders: 2-3). The tree grows ~1.55x per
                // ply (bench d9->d11: 2.42x over 2 plies), so a gap-2
                // iteration costs ~2.4x the last completed one, not 1.3x.
                int gap = depth - completedDepth;
                if (gap < 1) gap = 1;
                if (tm != nullptr) {
                    if (!tm->shouldStartNextIteration(elapsedMs, lastIterMs, completedDepth,
                                                     bestMove, prevIterBest, bestScore,
                                                     prevIterScore, aspirationFails >= 2,
                                                     bestMoveNodesPm, gap, lim.lastScore))
                        break;
                    // v1.4.11: the re-arm used msToDeadline(startMs + cap),
                    // which added the full steady_clock uptime to the wall —
                    // the hard deadline never fired after the first
                    // iteration. Absolute form:
                    hardDeadline = deadlineAt(startMs + tm->iterationCapMs(completedDepth));
                } else if (limits.hardMoveTimeMs <= 0) {
                    // Fixed movetime: start an iteration whenever it is
                    // expected to fit in the REMAINING budget — the full
                    // budget, not 95% of it. Unspent time cannot be banked,
                    // and the old 95% margin refused exactly the borderline
                    // deep iterations that would have completed in time,
                    // wasting 17-23% of every `go movetime` budget. Estimate
                    // misses (aspiration re-search cascades) are caught by
                    // the hard wall-clock deadline: the overrunning iteration
                    // is aborted and the last completed move is used.
                    int64_t estMs = estMsFor(lastIterMs, gap);
                    if (elapsedMs + estMs > moveTimeMs) break;
                } else {
                    // Explicit soft/hard split (Android skill levels, analysis
                    // budgets): keep the conservative soft pacing (95% of the
                    // soft budget) so the caller's split is respected.
                    int64_t estMs = estMsFor(lastIterMs, gap);
                    if (elapsedMs + estMs > moveTimeMs * 95 / 100) break;
                }
            }

            prevIterBest = bestMove;
            prevIterScore = bestScore;
            aspirationFails = 0;
            // v1.4.0: fresh per-move node spend for this iteration.
            for (int i = 0; i < rootCount; i++) rootMoveNodes[i] = 0;

            int64_t iterStartMs = nowMs();

            int score;
            bool mateish = bestScore >= MATE_BOUND || bestScore <= -MATE_BOUND;
            // v1.1: aspiration windows run on EVERY worker now (was main
            // only). Helpers iterating inside narrow windows hit different
            // re-search points than the old full-window sweeps, which both
            // speeds helpers up and spreads their trees across different
            // parts of the shared TT — the classic Lazy-SMP diversity
            // lever. The per-thread initial-delta offset (id % 3) adds a
            // little extra divergence between helpers.
            if (depth >= 5 && jitter == 0 && !mateish) {
                // ---- aspiration window with widening re-searches ----
                // Tight initial window; widen geometrically (with a floor so
                // repeated near-misses cannot loop), full window after 4 fails.
                int delta = p.asp_delta + (isMain() ? 0 : (id % 3) * 4);
                // (v1.2 tested: depth-scaled initial delta LOST -38.4
                // ± 6.5 — the SPSA-tuned fixed start is optimal here.)
                int alpha = bestScore - delta;
                int beta = bestScore + delta;
                int reSearches = 0;
                // v1.4.0 lever: aspiration fail-high depth reduction. When a
                // window fails HIGH the position proved sharper than the
                // iteration assumed; the widened-beta re-search runs one ply
                // shallower (floor depth/2) — the raised bound rarely needs
                // the full ladder to re-establish, and the saved time goes
                // into the next completed iteration. Fail-low re-searches
                // (and the full-window fallback) keep the full depth: the
                // low side is where an under-estimated bound costs moves.
                int aspDepth = depth;
                while (true) {
                    score = searchRoot(aspDepth, alpha, beta);
                    if (stopped()) break;
                    if (score <= alpha && alpha > -INFINITE) {
                        delta += 6 + delta / 2;
                        alpha = score - delta;
                        if (alpha < -INFINITE) alpha = -INFINITE;
                        aspDepth = depth;
                        reSearches++;
                    } else if (score >= beta && beta < INFINITE) {
                        delta += 6 + delta / 2;
                        beta = score + delta;
                        if (beta > INFINITE) beta = INFINITE;
                        if (aspDepth > depth / 2) aspDepth--;
                        reSearches++;
                        // v1.4.2: the next searchRoot pass re-searches a
                        // window whose BETA side widened because a move
                        // already proved >= beta — preserve that evidence
                        // against an abort (salvage-wipeout fix).
                        iterationBestPreserve = true;
                    } else {
                        break;
                    }
                    if (reSearches >= 4) {
                        alpha = -INFINITE;
                        beta = INFINITE;
                        aspDepth = depth;
                    }
                }
                aspirationFails = reSearches;
            } else {
                score = searchRoot(depth, -INFINITE, INFINITE);
            }

            if (!stopped()) {
                bestScore = score;
                if (iterationBestMove != MOVE_NONE) bestMove = iterationBestMove;
                completedDepth = depth;
                lastIterMs = nowMs() - iterStartMs;
                // v1.4.0: root node-fraction — the best move's share of this
                // iteration's root node spend (per-move sums are accumulated
                // in searchRoot and stay keyed to the moves through sorts).
                {
                    uint64_t tot = 0, bm = 0;
                    for (int i = 0; i < rootCount; i++) {
                        tot += rootMoveNodes[i];
                        if (rootMoves[i] == bestMove) bm = rootMoveNodes[i];
                    }
                    bestMoveNodesPm = (tot > 0 && bm > 0) ? (int)(bm * 1000 / tot) : -1;
                }
                if (isMain() && onIteration) {
                    IterationInfo info;
                    info.depth = depth;
                    info.score = bestScore;
                    info.bestMove = bestMove;
                    info.nodes = owner->totalNodes();
                    info.timeMs = nowMs() - startMs;
                    onIteration(info);
                }
            } else {
                // Partial-iteration salvage: an aborted iteration whose
                // first root move completed carries depth-d information.
                // When a later move overtook the incumbent before the abort,
                // that move is the deeper-informed choice; the previous
                // depth's pick remains the fallback when nothing completed
                // (or when only fail-low upper-bound evidence exists — see
                // the guard in searchRoot).
                if (iterationBestMove != MOVE_NONE) {
                    bestMove = iterationBestMove;
                    bestScore = iterationBestScore;
                }
                break;
            }

            sortRootMoves();
            // (v1.4.7: gating this sort behind jitter/Multi-PV — keeping
            // only the fail-high rotation for ordering — was implemented
            // and MEASURED as a large loss; see the searchRoot note. The
            // sort stays unconditional.)

            // A forced mate is proven — no need to search deeper.
            if (bestScore >= MATE_BOUND && depth >= MATE - bestScore + 1) break;
            if (bestScore <= -MATE_BOUND && depth >= MATE + bestScore + 1) break;

            // v1.4.0: advance to the next depth on this worker's ladder
            // (helpers skip their pattern's depths).
            do {
                depth++;
            } while (!isMain() && depth <= maxDepth && depth % (id + 1) == 0);
        }

        result.bestMove = bestMove;
        result.score = bestScore;
        result.depth = completedDepth;
        result.nodes = nodes_;
        result.timeMs = nowMs() - startMs;
        result.mateIn = mateDistance(bestScore);
        lastResult = result;
        if (tm != nullptr) {
            result.timeComplexity = tm->complexityFactor();
            result.timeSoftMs = tm->softBudgetMs();
            result.timeHardMs = hardMs > 0 ? hardMs : moveTimeMs;
            result.timeUnstable = tm->instabilityHits();
            result.timeReason = tm->lastDecisionReason();
        }
        return result;
    }

    // ------------------------------------------------------------------
    // Root
    // ------------------------------------------------------------------

    int searchRoot(int depth, int alphaIn, int beta) {
        int alpha = alphaIn;
        int best = -INFINITE;
        Move bestLocal = MOVE_NONE;
        bool firstSearched = true;  // full window for the first non-excluded move
        // Clear the previous call's publication: an earlier (completed but
        // fail-low) re-search of this same iteration must not leak its result
        // into a later aborted re-search's salvage path.
        // v1.4.2 exception: a FAIL-HIGH re-search keeps it — the pass that
        // failed high already proved a move >= beta at this depth (and the
        // v1.4.1 rotation put it at rootMoves[0]); if THIS pass aborts
        // before completing any move, the salvage path must still publish
        // that move instead of falling back to the previous depth's pick.
        // Early completion of rootMoves[0] re-publishes naturally.
        if (!iterationBestPreserve) iterationBestMove = MOVE_NONE;
        iterationBestPreserve = false;

        for (int i = 0; i < rootCount; i++) {
            Move move = rootMoves[i];
            // Multi-PV: skip root moves excluded by earlier lines (and clear
            // their scores so stale values can't leak into later sorting).
            if (limits.rootExcludeCount > 0 && limits.isExcluded(move)) {
                rootScores[i] = -INFINITE;
                rootMoveNodes[i] = 0;
                continue;
            }
            pos->makeMove(move);
            stack[0] = move;

            // v1.4.0: per-move node spend (root node-fraction input).
            uint64_t nodesBefore = nodes_;

            int score;
            // v1.4.7: scout-salvage slot — a COMPLETED zero-window scout
            // that beat alpha proved a genuine lower bound (above every
            // earlier root move's upper bound). If the full-window PVS
            // confirmation is then aborted by the deadline, the raw return
            // (0) must not discard that evidence: the move stays the
            // best-so-far candidate for the abort path's publication.
            int scoutProof = -INFINITE;
            bool fullWindow = firstSearched || jitter != 0;
            if (fullWindow) {
                score = -alphaBeta(depth - 1, -beta, -alpha, 1, true, false, MOVE_NONE);
            } else {
                score = -alphaBeta(depth - 1, -alpha - 1, -alpha, 1, true, true, MOVE_NONE);
                if (!stopped() && score > alpha && score < beta) {
                    scoutProof = score;
                    score = -alphaBeta(depth - 1, -beta, -alpha, 1, true, false, MOVE_NONE);
                    if (stopped()) score = scoutProof;  // salvage the proven fail-high
                }
            }
            firstSearched = false;
            pos->unmakeMove();
            rootMoveNodes[i] += nodes_ - nodesBefore;
            if (stopped()) {
                // Partial-iteration salvage: at least one root move fully
                // searched at this depth means bestLocal is depth-d
                // evidence. Publish it so the ID loop's abort path can
                // prefer it over the previous depth's pick. When nothing
                // completed at this depth (bestLocal == MOVE_NONE) the
                // previous iteration's values stay in place.
                //
                // SOUNDNESS GUARD: publish only when the best move's score
                // genuinely beat the window floor (best > alphaIn). PVS only
                // produces in-window exact scores above alpha; below it,
                // every score is a fail-low UPPER bound, and comparing upper
                // bounds across root moves can flip bestLocal to a junk move.
                // (Full-window iterations have alphaIn = -INFINITE, so the
                // guard is vacuous there — every completed move publishes.)
                //
                // v1.4.7: a completed scout's proof counts as "fully
                // searched" depth-d evidence for exactly the same reason —
                // it is a LOWER bound above every earlier move's bound.
                if (scoutProof > best) {
                    best = scoutProof;
                    bestLocal = move;
                }
                if (bestLocal != MOVE_NONE && best > alphaIn) {
                    iterationBestMove = bestLocal;
                    iterationBestScore = best;
                }
                return best;
            }

            if (jitter != 0) score += (int)(rng % (uint64_t)(2 * jitter + 1)) - jitter;
            // (v1.4.3 note: storing -INFINITE for fail-low scouts was
            // tested and REJECTED — a ~150-game bisect measured it at
            // roughly -36 Elo. The bounds are inexact but they carry real
            // ordering information; nulling them collapses the root order
            // to sticky previous-iteration order, starving the true
            // second-best move of early full-window searches. The PV-raise
            // rotation below is the sound half of the claim. See
            // HISTORY.md §40.)
            rootScores[i] = score;

            if (score > best) {
                best = score;
                bestLocal = move;
                if (score > alpha) alpha = score;
                // v1.4.1 fix (fail-high break + root promotion): rotate the
                // fail-high move to rootMoves[0] so the widened-beta
                // re-search starts from it with the full window.
                //
                // (v1.4.7 note: generalizing this rotation to EVERY PV raise
                // and disabling the end-of-iteration sortRootMoves() — the
                // "fail-low upper-bound sorting corruption" fix — was
                // implemented and MEASURED as a disaster: -76.5 ± 34.5 vs
                // the sort-keeping build at 60 games. The score-based sort
                // IS load-bearing in this engine (the v1.4.3 null-scores
                // experiment already showed -36 for collapsing to sticky
                // root order); the scout upper bounds evidently carry more
                // usable ordering signal than they corrupt. Rotation stays
                // fail-high-only; the sort stays unconditional.)
                if (score >= beta && beta < INFINITE) {
                    uint64_t mvNodes = rootMoveNodes[i];
                    for (int j = i; j > 0; j--) {
                        rootMoves[j] = rootMoves[j - 1];
                        rootScores[j] = rootScores[j - 1];
                        rootMoveNodes[j] = rootMoveNodes[j - 1];
                    }
                    rootMoves[0] = move;
                    rootScores[0] = score;
                    rootMoveNodes[0] = mvNodes;
                    iterationBestMove = bestLocal;
                    iterationBestScore = best;
                    if (jitter == 0) {
                        owner->tt_.store(pos->key, bestLocal, scoreToTT(best, 0), depth,
                                         TranspositionTable::FLAG_LOWER,
                                         TranspositionTable::EVAL_NONE,
                                         /*pv=*/beta - alphaIn > 1);
                    }
                    return best;
                }
            }
        }

        iterationBestMove = bestLocal;
        iterationBestScore = best;
        int flag = best >= beta   ? TranspositionTable::FLAG_LOWER
                   : best <= alphaIn ? TranspositionTable::FLAG_UPPER
                                     : TranspositionTable::FLAG_EXACT;
        // Never store jittered root scores into the shared TT: the noise
        // would pollute later searches.
        if (jitter == 0) {
            owner->tt_.store(pos->key, bestLocal, scoreToTT(best, 0), depth, flag,
                             TranspositionTable::EVAL_NONE,
                             /*pv=*/beta - alphaIn > 1);
        }
        return best;
    }

    void sortRootMoves() {
        for (int i = 1; i < rootCount; i++) {
            Move m = rootMoves[i];
            int32_t s = rootScores[i];
            uint64_t n = rootMoveNodes[i];  // v1.4.0: stays keyed to the move
            int j = i - 1;
            while (j >= 0 && rootScores[j] < s) {
                rootMoves[j + 1] = rootMoves[j];
                rootScores[j + 1] = rootScores[j];
                rootMoveNodes[j + 1] = rootMoveNodes[j];
                j--;
            }
            rootMoves[j + 1] = m;
            rootScores[j + 1] = s;
            rootMoveNodes[j + 1] = n;
        }
    }

    // ------------------------------------------------------------------
    // Main alpha-beta
    // ------------------------------------------------------------------

    int alphaBeta(int depthIn, int alphaIn, int betaIn, int ply, bool allowNull,
                  bool cutNode, Move excludedMove) {
        int depth = depthIn;
        int alpha = alphaIn;
        int beta = betaIn;
        corrPly_ = ply;  // v1.4.10: continuation-corr context ply

        checkStop();
        if (stopped()) return 0;
        if (ply >= MAX_PLY_GUARD) return correctedEval();

        // Draws inside the search tree. The score is the CURRENT side to
        // move's contempt (play: 12 cp "avoid draws"; analysis: 0 = truth).
        // v1.3: dead material (no P/R/Q, <=1 minor each) joins the draw
        // family — the app adjudicates those instantly, so scoring them as
        // live evals made a clock-pressured engine prefer sacrificing into
        // a mutilated dead draw over accepting the 50-move draw.
        if (ply > 0 && (pos->halfmoveClock >= 100 || pos->isDeadMaterialDraw() ||
                        pos->isRepetitionDraw())) {
            // v1.2 dynamic contempt: full strength in the middlegame,
            // fading to zero in bare endgames — over-pressing a dead-drawn
            // ending converts half-points into zero-point losses. Scale by
            // non-pawn, non-king material (cheap; only on draw hits).
            // Gate: +17.4 ± 6.9 (100 games, 100 ms) on the 1.1.1 stack.
            // v1.3: sign fix + POV-relative (see drawScore).
            return drawScore(ply);
        }

        // Mate-distance pruning.
        if (alpha < -MATE + ply) alpha = -MATE + ply;
        if (beta > MATE - ply - 1) beta = MATE - ply - 1;
        if (alpha >= beta) return alpha;

        bool pvNode = beta - alpha > 1;
        bool inCheck = pos->checkers() != 0;

        // Check-evasion depth: no unconditional check extension — singular
        // extensions carry forced lines; unconditional extension mostly
        // explodes the tree. (v2.3 ladder test: removed.)
        if (depth <= 0) return quiesce(alpha, beta, ply, 0);

        // ---- Transposition table ----
        Move ttMove = MOVE_NONE;
        int ttEval = TranspositionTable::EVAL_NONE;
        int ttScoreRaw = 0, ttFlag = 0, ttDepth = 0;
        bool ttHit = false;
        bool ttPv = false;  // v1.4.0: the position was stored from a PV window
        if (excludedMove == MOVE_NONE) {
            TranspositionTable::ProbeResult pr = owner->tt_.probe(*pos, pos->key);
            if (pr.hit) {
                ttHit = true;
                ttMove = pr.move;
                ttEval = pr.eval;
                ttScoreRaw = pr.score;
                ttFlag = pr.flag;
                ttDepth = pr.depth;
                ttPv = pr.pv;
                int tScore = scoreFromTT(ttScoreRaw, ply);
                if (ttDepth >= depth) {
                    if (ttFlag == TranspositionTable::FLAG_EXACT) return tScore;
                    if (ttFlag == TranspositionTable::FLAG_LOWER && !pvNode && tScore >= beta)
                        return tScore;
                    if (ttFlag == TranspositionTable::FLAG_UPPER && !pvNode && tScore <= alpha)
                        return tScore;
                }
            }
        }

        // Internal iterative reduction: no TT move -> search shallower first.
        // v5.3: also when the TT entry is much shallower than the remaining
        // depth (its move ordering hint is stale) — the deeper search starts
        // from a cheaper probe instead of a full-width miss.
        if (excludedMove == MOVE_NONE && depth >= 4 &&
            (ttMove == MOVE_NONE || ttDepth < depth - 6))
            depth--;

        // ---- Singular extension / multi-cut ----
        bool singular = false;
        int singularExt = 0;
        if (excludedMove == MOVE_NONE && depth >= 4 && ttMove != MOVE_NONE && !inCheck &&
            (ttFlag == TranspositionTable::FLAG_LOWER ||
             ttFlag == TranspositionTable::FLAG_EXACT) &&
            ttDepth >= depth - 4) {
            int ttScoreAdj = scoreFromTT(ttScoreRaw, ply);
            // v1.4.7 fix (mate-bound guard): a mate-score TT entry makes
            // singularBeta = ttScore - 2*depth a nonsense mate-distance
            // threshold, firing spurious excluded verification searches
            // throughout mate trees (and very rarely a bogus singular
            // extension when the exclusion is even more mate-ish). Gate
            // the whole singular block on a non-mate TT score, the
            // Stockfish treatment.
            if (ttScoreAdj > -Searcher::MATE_BOUND && ttScoreAdj < Searcher::MATE_BOUND) {
            int singularBeta = ttScoreAdj - 2 * depth;
            // v1.1: the excluded verification now also runs when the TT
            // score sits far ABOVE beta (singularBeta >= beta) — the
            // precondition under which the multi-cut refutation is
            // reachable. The old code gated the whole block on
            // beta > singularBeta, which made the multi-cut branch below
            // DEAD CODE for its entire life (mutually exclusive
            // conditions — see HISTORY v4.0 note). The re-gated form:
            //   - beta > singularBeta: singular-extension verification
            //     (unchanged behavior);
            //   - !pvNode && singularBeta >= beta: multi-cut branch. v1.1.0
            //     made this reachable for the first time; v1.1.1 disables it
            //     again after direct measurement: on the full 1.1.0 stack the
            //     multi-cut return cost ~41 Elo of head-to-head swing (the
            //     TT age-wrap fix was in/out at the same time, isolated by
            //     A/B matches). The excluded search still runs below for the
            //     singularBeta >= beta case via singularSearch only when
            //     beta > singularBeta; the >= beta case now skips the block
            //     entirely (identical to the pre-1.1 unreachable form).
            bool singularSearch = beta > singularBeta;
            bool multicutSearch = false;  // v1.1.1: measured regression, off
            if (singularSearch || multicutSearch) {
                int excludedScore =
                    alphaBeta((depth - 1) / 2, singularBeta - 1, singularBeta, ply, false,
                              cutNode, ttMove);
                if (stopped()) return 0;
                if (excludedScore < singularBeta) {
                    if (singularSearch) {
                        singular = true;
                        if (USE_DOUBLE_EXT && ttFlag == TranspositionTable::FLAG_EXACT &&
                            excludedScore < singularBeta - DOUBLE_EXT_MARGIN && !isCapture(ttMove))
                            singularExt = 2;
                        else
                            singularExt = 1;
                    }
                } else if (false && multicutSearch && excludedScore < MATE_BOUND) {
                    return singularBeta;  // multi-cut: dead again (v1.1.1)
                }
                // (v1.2 tested: TT-move negative extension when not
                // singular & score >= beta LOST -17.4 ± 6.4 — the 48%
                // tree shrink cost more tactical depth than it saved.)
            }
            }
        }

        int staticEval;
        // v1.4.0 lever: what this node's TT stores carry as the eval — the
        // RAW eval + static-blend residual, WITHOUT the correction-history
        // term (corrections are re-learned all search long; a stored
        // corrected eval would lend STALE corrections to every later
        // visit — the TT outlives each corrhist state by construction).
        // Readers of a TT eval re-apply the LIVE corrhist sum (below).
        int ttEvalStore;
        if (inCheck) {
            staticEval = -INFINITE;
            ttEvalStore = TranspositionTable::EVAL_NONE;
        } else if (ttEval != TranspositionTable::EVAL_NONE) {
            // v1.4.0: stored raw+residual + the CURRENT correction sum.
            staticEval = ttEval + corrSumOnly(ply);
            ttEvalStore = ttEval;  // already the store semantics — pass through
        } else if (nnue::enabled && nnue::staticBlendPct > 0) {
            // v5.1 experiment: inject the residual into the MAIN-SEARCH
            // static eval (and the TT store) so RFP/NMP/razoring/improving
            // see a consistent signal with the qsearch stand-pat.
            // v1.4.2: each primitive computed once (correctedEval() + a
            // second rawEvalCached() + a second staticResidualInject()
            // re-ran their EC probes on the very next line). All three
            // helpers are pure/idempotent, so this is bit-identical.
            int rawV = rawEvalCached();
            int resV = staticResidualInject();
            staticEval = (USE_CORRHIST ? rawV + corrSumOnly(ply) : rawV) + resV;
            ttEvalStore = rawV + resV;
            owner->tt_.storeEval(pos->key, ttEvalStore);
        } else {
            int rawV = rawEvalCached();
            staticEval = USE_CORRHIST ? rawV + corrSumOnly(ply) : rawV;
            ttEvalStore = rawV;
            owner->tt_.storeEval(pos->key, ttEvalStore);
        }
        // In-check plies carry no eval: store a sentinel instead of
        // -INFINITE (v4.0). The old -INFINITE store made any later finite
        // eval compare as "improving" over a checked ply spuriously —
        // right after a check the side to move is often doing WORSE.
        evalStack[ply] = inCheck ? EVAL_NO_EVAL : staticEval;

        // is the position getting better for the side to move?
        // Sentinel plies (in check) are skipped by the guard terms.
        // (v1.3.1: improving keeps the RAW static eval, per the lever spec.)
        bool improving =
            (ply >= 2 && evalStack[ply - 2] != EVAL_NO_EVAL &&
             staticEval > evalStack[ply - 2]) ||
            (ply >= 4 && evalStack[ply - 4] != EVAL_NO_EVAL &&
             staticEval > evalStack[ply - 4]);

        // v1.3.1 lever: TT-score refinement of the eval used for pruning.
        // A TT bound that contradicts the raw static eval in its own
        // direction carries strictly more information than the static
        // eval: a LOWER entry above the eval means the true value is at
        // least that high, an UPPER entry below it means at most that low,
        // and an EXACT entry replaces outright. Used ONLY by the pruning
        // decisions below (razoring, RFP, the NMP eval gate and its
        // reduction ramp, quiet futility); the improving flag and the
        // correction-history teacher keep the raw static eval.
        // Measured: +30 ± 9 (5k nodes) alone; +36 ± 9 combined with the
        // LMR depth floor.
        int evalPrune = staticEval;
        if (ttHit && !inCheck) {
            int tv = scoreFromTT(ttScoreRaw, ply);
            if (ttFlag == TranspositionTable::FLAG_EXACT) {
                evalPrune = tv;
            } else if (ttFlag == TranspositionTable::FLAG_LOWER && tv > evalPrune) {
                evalPrune = tv;
            } else if (ttFlag == TranspositionTable::FLAG_UPPER && tv < evalPrune) {
                evalPrune = tv;
            }
        }

        // Razoring: hopeless-looking non-PV nodes drop into quiescence.
        if (!pvNode && !inCheck && depth <= 3 && evalPrune + p.razor * depth < alpha) {
            return quiesce(alpha, beta, ply, 0);
        }

        // Reverse futility pruning (wider margin when not improving).
        if (!pvNode && !inCheck && depth <= 8 &&
            evalPrune - (improving ? p.rfp_imp : p.rfp_non) * depth >= beta) {
            return evalPrune;
        }

        // Null-move pruning (adaptive R). Non-PV only: cutting PV nodes
        // interacts badly with aspiration windows.
        if (allowNull && !inCheck && !pvNode && depth >= 2 &&
            evalPrune >= beta - p.nmp_slack &&
            pos->hasNonPawnMaterial(pos->sideToMove)) {
            int r = 3 + depth * 10 / p.nmp_rdiv;
            // v1.3.1 lever: scale the null-move reduction with the eval
            // margin over beta instead of a single +1 step at 250: the
            // larger the static advantage, the safer a deeper null
            // verification cut. Clamped to 0-3 extra plies.
            // Measured: ≈ +13 ± 9 (5k nodes).
            r += clampI((evalPrune - beta) / 128, 0, 3);
            if (cutNode) r++;
            pos->makeNullMove();
            stack[ply] = MOVE_NONE;
            // v1.4.2 fix (cutNode alternation): the null-move child was
            // hardcoded cutNode=false. PVS topology: the child of an
            // All-node is an expected Cut-node and vice versa, so the
            // child flag must be !cutNode — the same convention the
            // ProbCut verification below already follows. With false, the
            // entire null subtree ran with the LMR/NMP cutNode aggression
            // disabled whenever the parent was an All-node.
            int score = -alphaBeta(depth - 1 - r, -beta, -beta + 1, ply + 1, false,
                                   !cutNode, MOVE_NONE);
            pos->unmakeNullMove();
            if (stopped()) return 0;
            if (score >= beta) {
                return score >= MATE_BOUND ? beta : score;
            }
        }

        int us = pos->sideToMove;

        // ---- ProbCut ----
        // With a comfortable static advantage in non-PV nodes, a capture
        // that still beats a raised beta after a reduced verification
        // search proves the cutoff cheaply, before the full move loop.
        if (!pvNode && !inCheck && depth >= 5 && staticEval >= beta &&
            excludedMove == MOVE_NONE) {
            int probBeta = beta + 100;
            int probDepth = depth - 4;
            int seeThreshold = probBeta - staticEval;

            Move *pcMoves = movesBuf[ply];
            int32_t *pcScores = scoresBuf[ply];

            // v1.4.8 lever (ProbCut try-cap): after three verified captures
            // failed to prove the cutoff, further captures rarely will —
            // the node is not a cheap-capture node and each extra try pays
            // a full reduced verification search before the real move loop
            // runs. Cap the attempts at 3 (SF-family engines cap ProbCut
            // tries the same way). Only moves that PASS the promo/SEE
            // filters and are actually searched count.
            int pcTried = 0;

            // v1.4.4: king-ray mask for the ProbCut legality gate below.
            Bitboard pcKingRays =
                rookAttacks(pos->kingSquare(us), 0) | bishopAttacks(pos->kingSquare(us), 0);

            int pcCount = generate(*pos, pcMoves, true);
            scoreQMoves(pcMoves, pcScores, pcCount, ttMove, ply);

            for (int i = 0; i < pcCount; i++) {
                // Selection sort step — highest scored capture first.
                // v1.4.8: bit-exact SIMD argmax (same first-max index).
                int bi = argMaxScores(pcScores, i, pcCount);

                if (bi != i) {
                    Move tm = pcMoves[i];
                    pcMoves[i] = pcMoves[bi];
                    pcMoves[bi] = tm;

                    int32_t ts = pcScores[i];
                    pcScores[i] = pcScores[bi];
                    pcScores[bi] = ts;
                }

                Move m = pcMoves[i];

                // Only consider captures and queen promotions in ProbCut.
                if (movePromo(m) != 0 && movePromo(m) != QUEEN) {
                    continue;
                }

                // v1.3.2: seeGE sign test — same exact decision as the full
                // see() walk (proved by the fuzzsee battery), resolved in
                // 1-2 attacker lookups instead of the whole exchange.
                if (seeThreshold > 0 && !seeGE(*pos, m, seeThreshold)) {
                    continue;
                }

                if (pcTried >= 3) break;

                pos->makeMove(m);
                // v1.4.4 ray gate (ProbCut runs only when !inCheck, so the
                // mask is computed unconditionally here; castling never
                // appears in a captures-only list, king captures and EP do).
                if ((movePiece(m) == KING || isEPMove(m) ||
                     (pcKingRays & bitOf(moveFrom(m)))) &&
                    pos->inCheck(us)) {
                    pos->unmakeMove();
                    continue;
                }
                stack[ply] = m;

                int score = -alphaBeta(probDepth, -probBeta, -probBeta + 1, ply + 1, true,
                                       !cutNode, MOVE_NONE);
                pcTried++;

                pos->unmakeMove();

                if (stopped()) return 0;

                if (score >= probBeta) {
                    // v1.3.1 lever: publish the ProbCut refutation as a TT
                    // lower bound at depth-3 — later visits to this node can
                    // cut (or probe the move) before the full move loop.
                    // Measured: ≈ +13 ± 9 (5k nodes).
                    owner->tt_.store(pos->key, m, scoreToTT(score, ply), depth - 3,
                                     TranspositionTable::FLAG_LOWER, ttEvalStore,
                                     /*pv=*/false);
                    return score;
                }
            }
        }

        // ---- Move loop ----
        Move *moves = movesBuf[ply];
        int32_t *scores = scoresBuf[ply];
        Move *tried = triedBuf[ply];
        int count = generate(*pos, moves);

        int alphaOrig = alpha;
        int bestScore = -INFINITE;
        Move bestMoveLocal = MOVE_NONE;
        int legalCount = 0;
        int quietCount = 0;
        int triedCount = 0;
        Move counterMove = MOVE_NONE;

        // v1.4.9 lever: TT-bound-aware quiet LMR. An UPPER (true value <=
        // bound) or EXACT TT score sitting far below alpha means this node
        // is genuinely bad for the side to move — the remaining quiets are
        // searched only to prove they fail, so they can afford one more ply
        // of reduction. LOWER bounds say nothing (the true value only sits
        // above them), so they are excluded; the gate is non-PV only (PV
        // nodes carry the aspiration window's exactness requirements) and
        // mate scores are excluded (the margin comparison is meaningless
        // there). Computed once before the loop; the comparison uses the
        // live alpha (as it rises past earlier moves' raises the gate only
        // fires more — the remaining moves face a higher bar, never a
        // lower one).
        int ttValLmr = 0;
        bool ttValLmrValid = false;
        if (ttHit && !pvNode &&
            ttFlag != TranspositionTable::FLAG_LOWER) {
            int tv = scoreFromTT(ttScoreRaw, ply);
            if (tv > -Searcher::MATE_BOUND && tv < Searcher::MATE_BOUND) {
                ttValLmr = tv;
                ttValLmrValid = true;
            }
        }

        // v1.4.1: king-ray mask for the pre-make quiet-prune fast path and
        // (v1.4.4) the legality ray gate in the TT fast path, ProbCut and
        // the general loop. Moving a piece that is NOT on any
        // rank/file/diagonal through our king can never expose the king
        // (we were not in check, the piece was not blocking any slider
        // line to the king) — so such a move is provably legal without the
        // post-make inCheck(us) test (an EP capture vacates a SECOND
        // square and always keeps the full test; king moves likewise).
        Bitboard kingRays = 0;
        if (!inCheck) {
            int ksqUs = pos->kingSquare(us);
            kingRays = rookAttacks(ksqUs, 0) | bishopAttacks(ksqUs, 0);
        }

        // v1.4.4: in-check evasion target mask. While in check, a move can
        // only be legal if it moves the king, captures the (single)
        // checker, blocks on the king-checker line, or is an en-passant
        // capture (an EP pawn can capture the double-pushed checking pawn
        // or block a slider check from the ep square). In double check only
        // king moves survive (mask 0). Used by the provably-illegal skip
        // in the loop below.
        Bitboard evasionMask = 0;
        if (inCheck) {
            Bitboard ck = pos->checkers();
            if (!(ck & (ck - 1))) {  // single check
                int csq = lsb(ck);
                evasionMask = bitOf(csq) | BETWEEN_BB[pos->kingSquare(us)][csq];
            }
        }

        // v1.4.10 lever: SINGLE-REPLY EXTENSION. Every legal evasion of a
        // check is a king move, an en-passant capture, or a move landing on
        // the checker's square / a between-square (the evasionMask set —
        // the exact superset the v1.4.4 provably-illegal skip uses). When
        // the pseudo-legal list contains EXACTLY ONE such candidate, the
        // node has at most one legal move: if the candidate is legal the
        // reply is FORCED, and the forced line can afford to keep its full
        // depth (childDepth = depth instead of depth-1) — the classic
        // single-reply extension. Mutually exclusive with the singular
        // extension by construction (singular is gated on !inCheck), so
        // the two never stack.
        Move singleReplyMove = MOVE_NONE;
        if (inCheck) {
            int candidates = 0;
            for (int i = 0; i < count && candidates <= 1; i++) {
                Move m = moves[i];
                if (movePiece(m) != KING && !isEPMove(m) &&
                    !(evasionMask & bitOf(moveTo(m))))
                    continue;
                candidates++;
                singleReplyMove = m;
            }
            if (candidates != 1) singleReplyMove = MOVE_NONE;
        }

        // v1.4.1 staged ordering (bit-exact): when a TT move is present in
        // the generated list, try it FIRST — before scoring the whole list.
        // The selection sort always picks the ttMove first anyway (its
        // 8000000 band is the strict maximum), so skipping scoreMoves' full
        // history sweep (8 table lookups per quiet across ~2.5 MB of tables,
        // plus a seeGE per suspicious capture) on TT-move cutoff nodes
        // (~75-85% of cut nodes cut on the TT move) changes NOTHING about
        // the move sequence — only the work spent producing it. When the TT
        // move does NOT cut off, the list is scored and the general loop
        // resumes at slot 1 exactly where the original loop would have been
        // after consuming the TT move at slot 0.
        int ttIdx = -1;
        if (ttMove != MOVE_NONE) {
            for (int i = 0; i < count; i++)
                if (moves[i] == ttMove) { ttIdx = i; break; }
        }
        bool ttHandled = false;
        if (ttIdx >= 0 && moves[ttIdx] != excludedMove) {
            if (ttIdx != 0) {
                Move tm = moves[0];
                moves[0] = moves[ttIdx];
                moves[ttIdx] = tm;
            }
            Move move = moves[0];
            bool quiet = isQuiet(move);
            bool capture = isCapture(move);
            // (The TT move is exempt from SEE pruning by the move != ttMove
            // guard in the original loop; the three quiet-prune gates and
            // the LMR gate are all provably inert at legalCount == 1:
            // hist-prune needs legalCount > 3, futility needs legalCount > 1,
            // LMP needs quietCount > limit >= 3, LMR needs legalCount > 3.)
            pos->makeMove(move);
            // v1.4.4 ray gate: when not in check, only king moves (castling
            // included — the king is the mover), en-passant captures (two
            // squares vacate) and moves whose FROM square sits on a king ray
            // (a potential pin line) can possibly leave us in check — a
            // capture ON a ray keeps that ray blocked (the capturer occupies
            // the victim's square), so the from-square test is sufficient.
            // Everything else is provably legal: skip the 5-lookup
            // inCheck(us) attack test entirely. Bit-exact (the skipped call
            // would have returned false).
            if ((inCheck || movePiece(move) == KING || isEPMove(move) ||
                 (kingRays & bitOf(moveFrom(move)))) &&
                pos->inCheck(us)) {
                pos->unmakeMove();
                // Illegal TT move: consumed at slot 0, never re-examined
                // (identical to the original loop's i == 0 iteration).
            } else {
                legalCount = 1;
                stack[ply] = move;
                if (quiet) quietCount = 1;
                if (quiet || capture) tried[triedCount++] = move;
                // Singular extension applies to the TT move; the
                // single-reply extension applies at in-check nodes (the
                // two are mutually exclusive — singular needs !inCheck).
                int childDepth =
                    depth - 1 + (singular ? singularExt : 0) +
                    (singleReplyMove != MOVE_NONE && move == singleReplyMove ? 1 : 0);
                // v1.4.2 fix (cutNode alternation): the first legal child
                // of a non-PV node was hardcoded cutNode=false, so every
                // All-node's first child — the expected CUTTER — was
                // mislabeled, and cutNode stayed false down the whole
                // leftmost line of every All-node (disabling the NMP
                // extra reduction and the LMR cut-node bonus there). PV
                // nodes keep false (a PV child is never a cut node).
                int score = -alphaBeta(childDepth, -beta, -alpha, ply + 1, true,
                                       pvNode ? false : !cutNode, MOVE_NONE);
                pos->unmakeMove();
                if (stopped()) return 0;
                if (score > bestScore) {
                    bestScore = score;
                    bestMoveLocal = move;
                    if (score > alpha) {
                        alpha = score;
                        if (score >= beta) {
                            if (excludedMove == MOVE_NONE) {
                                if (quiet || capture) {
                                    updateCutoffStats(move, quiet, tried, triedCount,
                                                      depth, ply);
                                }
                                if (!inCheck && abs(bestScore) < Searcher::MATE_BOUND) {
                                    updateCorrGated(move, ply, depth, bestScore, staticEval,
                                                    alphaOrig, beta);
                                }
                                owner->tt_.store(pos->key, move, scoreToTT(bestScore, ply),
                                                 depth, TranspositionTable::FLAG_LOWER,
                                                 ttEvalStore, pvNode);
                            }
                            return bestScore;
                        }
                    }
                }
            }
            ttHandled = true;
            // The TT move occupies slot 0 in every respect the original loop
            // gave it; score the remaining moves for the general loop.
            counterMove = scoreMoves(moves, scores, count, ttMove, ply);
        } else {
            counterMove = scoreMoves(moves, scores, count, ttMove, ply);
        }

        for (int i = ttHandled ? 1 : 0; i < count; i++) {
            // Selection sort step — pick the highest scored remaining move.
            // (v1.4.3 note: deferring the quiet scoring until the selection
            // reaches the quiet band was tested and REJECTED — bench
            // 219,136 -> 233,472. The history tables are LIVE state during
            // this loop: the LMR re-search feedback and updateCutoffStats
            // bump table cells between iterations, so a deferred read sees
            // different values than the upfront read. Lazy quiet scoring is
            // only bit-exact in engines whose move loop never mutates the
            // ordering tables mid-loop. See HISTORY.md §40.)
            int bi = argMaxScores(scores, i, count);
            if (bi != i) {
                Move tm = moves[i]; moves[i] = moves[bi]; moves[bi] = tm;
                int32_t ts = scores[i]; scores[i] = scores[bi]; scores[bi] = ts;
            }
            Move move = moves[i];
            if (move == excludedMove) continue;
            bool quiet = isQuiet(move);
            bool capture = isCapture(move);

            // SEE pruning of clearly losing moves (non-PV, shallow): losing
            // captures AND quiets that hang the moved piece. The TT move is
            // exempt: pruning it costs cutoffs and pollutes the table; never
            // prune before one legal move is secured. Promotions stay (queen
            // promos are rarely SEE-prunable in practice).
            // v1.4.1 fix: the margin is now SPLIT by move class. The old
            // single linear -75*depth threshold let quiets that hang a whole
            // minor piece (SEE -320/-330) survive at depths 5-6 (-375/-450)
            // — exactly the depth range where shallow tactics decide games —
            // while HISTORY §35.1 showed the CAPTURE side needs the wide
            // margin (see_coeff 45 over-pruned captures and lost Elo).
            // Quiets: -50*depth (a hung minor dies by depth 6: -300).
            // Captures: -25*depth*depth (d4 -400, d5 -625, d6 -900) — the
            // tactical slack the exchange sequence needs.
            if (movePromo(move) == 0 && !pvNode && !inCheck && depth <= 6 &&
                legalCount > 0 && move != ttMove) {
                int seeMargin = quiet ? (-50 * depth) : (-25 * depth * depth);
                if (!seeGE(*pos, move, seeMargin)) {
                    continue;
                }
            }

            // v1.3.1: pawn-key bucket of THIS node (pre-move pawnKey) for the
            // pawn-structure history reads that run after makeMove.
            int pawnBucket = (int)(pos->pawnKey & PAWN_HIST_MASK);

            // v1.4.1 pre-make quiet pruning (bit-exact): when one of the
            // three post-make quiet prunes would fire, the original flow
            // still paid seeGE (above), makeMove, attackersTo (inside
            // inCheck) and unmakeMove just to hit `continue`. For a quiet
            // move that (a) cannot be illegal — it is not a king, castle or
            // EP move and its origin is not on any ray through our king, so
            // it cannot expose the king — and (b) does not give check
            // (givesCheckQuiet, fuzz-proven exact over 355M moves), the
            // prune decision can be taken BEFORE makeMove. legalCount is
            // incremented exactly as the post-make path did (pruned quiets
            // were legal and counted; quietCount and tried[] were not).
            // Moves failing any gate fall through to the original path,
            // which reaches the identical decision after makeMove.
            if (quiet && !pvNode && !inCheck && movePiece(move) != KING &&
                !isCastleMove(move) && !isEPMove(move) &&
                (kingRays & bitOf(moveFrom(move))) == 0 &&
                !givesCheckQuiet(*pos, move)) {
                int lc = legalCount + 1;
                bool pruned = false;
                if (depth <= 3 && lc > 3 &&
                    quietHistoryValue(move, ply, us, pawnBucket) < -100) {
                    pruned = true;
                }
                if (!pruned && depth <= 6 && lc > 1) {
                    int margin = p.fut_base + (improving ? p.fut_imp : p.fut_non) * depth;
                    if (evalPrune + margin <= alpha) pruned = true;
                }
                if (!pruned && depth <= 8) {
                    int limit = 3 + depth * depth / (improving ? 1 : 2);
                    if (quietCount > limit) pruned = true;
                }
                if (pruned) {
                    legalCount = lc;
                    continue;
                }
            }

            // v1.4.4: provably-illegal in-check skip (bit-exact). A move
            // that is not a king move, not EP, and lands neither on the
            // checker's square nor on a between-square cannot be legal —
            // the original flow proved it the expensive way (makeMove +
            // inCheck + unmakeMove, all side-effect-free). Skipping straight
            // to the continue preserves the exact array contents, selection
            // swaps, scores and searched sequence. (COMPACTING these moves
            // out of the array instead is NOT bit-exact: removing elements
            // changes the selection-sort displacement dynamics — the
            // displaced slot-i element re-enters the suffix at the argmax
            // position — and permutes tied move groups. Root-caused via a
            // TT-store trace bisection this campaign; the same mechanism
            // explains the v1.4.2/v1.4.3 compaction/sentinel divergences.)
            if (inCheck && movePiece(move) != KING && !isEPMove(move) &&
                !(evasionMask & bitOf(moveTo(move)))) {
                continue;
            }

            pos->makeMove(move);
            // v1.4.4 ray gate (see the TT-move site above for the soundness
            // argument). kingRays is 0 while in check; the || chain
            // short-circuits so the gate is simply always-on when in check.
            if ((inCheck || movePiece(move) == KING || isEPMove(move) ||
                 (kingRays & bitOf(moveFrom(move)))) &&
                pos->inCheck(us)) {
                pos->unmakeMove();
                continue;
            }
            legalCount++;
            stack[ply] = move;
            bool givesCheck = pos->checkers() != 0;

            if (quiet) {
                // History-gated pruning: at shallow depth skip quiets the
                // engine itself has learned are bad (deeply negative
                // combined history), after the good moves have failed.
                // (The TT move is never touched: it is ordered first, so
                // legalCount is 1 when it is tried and these gates require
                // legalCount > 1 — ordering IS the TT-move protection.)
                // v1.3: threshold rescaled to the measured distribution —
                // quietHistoryValue spans roughly [-368, +4653]; the old
                // -9000 gate never fired (0 hits in 5M nodes). Moves with
                // deeply negative combined history fail high after a
                // reduction only ~1.3% of the time (hist <= 0 bucket), so
                // pruning the extreme negative tail at shallow depth is safe.
                if (!pvNode && !inCheck && !givesCheck && depth <= 3 &&
                    legalCount > 3 && quietHistoryValue(move, ply, us, pawnBucket) < -100) {
                    pos->unmakeMove();
                    continue;
                }
                // Futility pruning of quiet moves at shallow depths
                // (tighter when improving, looser otherwise).
                if (!pvNode && !inCheck && !givesCheck && depth <= 6 && legalCount > 1) {
                    int margin = p.fut_base + (improving ? p.fut_imp : p.fut_non) * depth;
                    if (evalPrune + margin <= alpha) {
                        pos->unmakeMove();
                        continue;
                    }
                }
                // Late move pruning: at low depth, late quiets rarely matter
                // (tighter when not improving).
                if (!pvNode && !inCheck && !givesCheck && depth <= 8) {
                    int limit = 3 + depth * depth / (improving ? 1 : 2);
                    if (quietCount > limit) {
                        pos->unmakeMove();
                        continue;
                    }
                }
                quietCount++;
            }
            if (quiet || capture) tried[triedCount++] = move;

            bool singularMove = singular && move == ttMove;

            // Late move reductions (log formula). Quiets get the full
            // adjustment set; captures with poor capture history also get a
            // mild reduction at non-PV nodes (bad captures ordered late are
            // almost never best). (The TT move is effectively exempt: it is
            // ordered first, so legalCount is 1 when it is tried and this
            // branch requires legalCount > 3.)
            int reduce = 0;
            // v1.4.1 fix: the gate also admits LOSING captures (SEE < 0 —
            // the score band below every quiet) at legalCount > 1: with the
            // bad-captures-last ordering they usually sit after the quiets,
            // but in capture-heavy positions they still arrive at low
            // legalCount and must not search at full depth.
            if (depth >= 3 && !givesCheck && !inCheck && !singularMove &&
                (legalCount > 3 ||
                 (capture && legalCount > 1 && scores[i] < -100000))) {
                int d = depth < 63 ? depth : 63;
                int m = legalCount < 63 ? legalCount : 63;

                int r;
                if (quiet) {
                    r = lmrTable[d][m];

                    if (pvNode)
                        r--;
                    else
                        r++;

                    // v1.4.0 LMR-context: cut nodes reduce harder, and a
                    // cut node with NO TT move (no ordering hint at all —
                    // typically an IIR-reduced first visit) is exactly where
                    // a late quiet is least likely to matter: +2 instead
                    // of +1.
                    if (cutNode) r += (ttMove != MOVE_NONE) ? 1 : 2;

                    // v1.4.0 LMR-context: ex-PV positions (the TT's pv bit)
                    // hold the remembered best line; their quiets get one
                    // ply of reduction back.
                    if (ttPv) r--;

                    if (move == killers[ply * 2] || move == killers[ply * 2 + 1] ||
                        move == counterMove) {
                        r--;
                    }

                    // v1.2 tactical-context LMR: the TT move is a capture ->
                    // the position is tactically loaded; quiets rarely
                    // matter here, reduce them one ply harder.
                    // Gate: +38.4 ± 6.6 alone (100 games, 100 ms),
                    // +27.9 ± 6.6 combined with dynamic contempt.
                    if (ttMove != MOVE_NONE && moveCaptured(ttMove) != NO_PIECE) r++;

                    // v1.4.9 lever: the TT-bound-aware reduction (see the
                    // hoisted ttValLmr note above the move loop). Fires for
                    // non-PV quiets when a genuine TT upper/exact bound sits
                    // >= 200 cp below alpha.
                    if (ttValLmrValid && ttValLmr < alpha - 200) r++;

                    if (improving)
                        r--;

                    int hist = quietHistoryValue(move, ply, us, pawnBucket);
                    // v1.3: rescaled to the measured quietHistoryValue
                    // distribution (740k reductions: [-368, +4653]); the old
                    // 6000/12000 gates could never fire. Fail-high rate after
                    // a reduction: 1.3% at hist <= 0, 4.2% at 250-1000 and
                    // 6-10% above 2000 — so bad-history quiets take an extra
                    // ply of reduction (they almost never need the depth) and
                    // strong-history quiets earn up to two plies back.
                    if (hist <= 0)
                        r++;
                    if (hist >= 2500)
                        r--;
                    if (hist >= 4200)
                        r--;

                    if (legalCount > 12)
                        r++;
                } else {
                    // Captures/promotions: reduce only clearly-bad CAPTURES
                    // (quiet promotions have no victim cell and are exempt).
                    // v4.0 fix: `us`, not pos->sideToMove — this block runs
                    // after makeMove, so pos->sideToMove is the opponent and
                    // the old read indexed the wrong side's capture table.
                    r = 0;
                    // v1.4.1 fix: losing captures (SEE < 0) were searched at
                    // FULL depth at legalCount <= 6 (the old gate below
                    // needed legalCount > 6) while inflating legalCount so
                    // the engine's best quiets took the heaviest LMR and
                    // history pruning. Reduce them as soon as legalCount > 1
                    // — deepening to 2 with a deeply negative capture
                    // history (the losing sacrifice is then context-bad too).
                    if (!pvNode && legalCount > 1 && scores[i] < -100000) {
                        r = 1;
                        if (moveCaptured(move) <= 5) {
                            int capHist = captureHist[us * 2304 +
                                                      (movePiece(move) * 64 + moveTo(move)) * 6 +
                                                      moveCaptured(move)];
                            if (capHist < -8000) r = 2;
                        }
                    } else if (!pvNode && legalCount > 6 && moveCaptured(move) <= 5) {
                        int capHist = captureHist[us * 2304 +
                                                  (movePiece(move) * 64 + moveTo(move)) * 6 +
                                                  moveCaptured(move)];
                        if (capHist < 0) r = 1 + (capHist < -8000 ? 1 : 0);
                    }
                }

                reduce = clampI(r, 0, 8);
            }

            int childDepth = depth - 1 + (singularMove ? singularExt : 0) +
                             (singleReplyMove != MOVE_NONE && move == singleReplyMove
                                  ? 1
                                  : 0);
            // (v1.2 tested: passed-pawn-to-7th extension LOST -24.4 ± 6.4
            // on this stack — extensions keep losing to this search's
            // LMR ordering; consistent with the project's history.)

            // (v1.3.1: the "reduce <= childDepth-1" LMR depth floor lever was
            // tested and DROPPED: instrumentation shows 90% of this engine's
            // LMR calls deliberately reduce to depth-0 (qsearch) — capping
            // them at depth-1 explodes the tree 7.5x and measured -155 Elo
            // at 5k nodes / -200 at 10k nodes. The engine's aggressive
            // shallow LMR already re-searches on any positive reduced
            // verdict, so the floor's premise doesn't apply here.)

            int score;
            if (legalCount == 1) {
                // v1.4.2 fix (cutNode alternation): see the TT-move fast
                // path above — first legal child of a non-PV node is an
                // expected Cut-node (!cutNode), not false.
                score = -alphaBeta(childDepth, -beta, -alpha, ply + 1, true,
                                   pvNode ? false : !cutNode, MOVE_NONE);
            } else {
                score = -alphaBeta(childDepth - reduce, -alpha - 1, -alpha, ply + 1, true,
                                   !cutNode, MOVE_NONE);
                if (score > alpha && reduce > 0) {
                    // (v1.4.0 tested: verifying the LMR fail-high one ply
                    // DEEPER at non-PV nodes measured neutral-at-best on
                    // this stack — the +2-4% tree it buys did not convert
                    // to gate Elo; the neutral full-depth verification is
                    // kept. The re-search history feedback below stays.)
                    score = -alphaBeta(childDepth, -alpha - 1, -alpha, ply + 1, true,
                                       !cutNode, MOVE_NONE);
                    // v1.4.0 LMR-context: history feedback for the re-searched
                    // move. A quiet whose reduced fail-high SURVIVES the
                    // full-depth verification earned its early ordering —
                    // small bonus. One whose fail-high was a false positive
                    // ordered itself early and then wasted a whole re-search
                    // — small malus. (b is 1/3 of the cutoff bonus: the move
                    // gets the full bonus/malus later if it goes on to cut
                    // or to be malused as a tried-and-failed move.)
                    if (quiet) {
                        int b = depth * depth / 2;
                        bumpQuiet(move, us, score > alpha ? b : -b, ply, pawnBucket);
                    }
                }
                if (score > alpha && score < beta) {
                    score = -alphaBeta(childDepth, -beta, -alpha, ply + 1, true, false,
                                       MOVE_NONE);
                }
            }
            pos->unmakeMove();
            if (stopped()) return 0;

            if (score > bestScore) {
                bestScore = score;
                bestMoveLocal = move;
                if (score > alpha) {
                    alpha = score;
                    if (score >= beta) {
                        if (excludedMove == MOVE_NONE) {
                            if (quiet || capture) {
                                updateCutoffStats(move, quiet, tried, triedCount, depth, ply);
                            }
                            if (!inCheck && abs(bestScore) < Searcher::MATE_BOUND) {
                                updateCorrGated(move, ply, depth, bestScore, staticEval,
                                                alphaOrig, beta);
                            }
                            owner->tt_.store(pos->key, move, scoreToTT(bestScore, ply), depth,
                                             TranspositionTable::FLAG_LOWER, ttEvalStore,
                                             pvNode);
                        }
                        return bestScore;
                    }
                }
            }
        }

        if (legalCount == 0) {
            if (excludedMove != MOVE_NONE) return alpha;
            if (inCheck) return -MATE + ply;
            // v1.3: stalemate is a draw — same POV-relative score as every
            // other draw (was a hard 0, which ranked ABOVE the -C draw
            // value and made a clock-pressured winner deliver stalemate
            // instead of shuffling into the 50-move draw).
            return drawScore(ply);
        }

        if (excludedMove == MOVE_NONE) {
            // Fail-low / exact nodes: teach the correction tables when the
            // static eval was too optimistic about this structure.
            if (!inCheck && bestMoveLocal != MOVE_NONE &&
                abs(bestScore) < Searcher::MATE_BOUND) {
                updateCorrGated(bestMoveLocal, ply, depth, bestScore, staticEval, alphaOrig,
                                beta);
            }
            int flag = bestScore >= beta   ? TranspositionTable::FLAG_LOWER
                       : bestScore > alphaOrig ? TranspositionTable::FLAG_EXACT
                                               : TranspositionTable::FLAG_UPPER;
            // v1.3.1 lever: don't store a best move on fail-low nodes. On an
            // upper bound every move failed to raise alpha — the "best" of
            // them is not proven good, and storing it as the TT move pollutes
            // ordering / singular / IIR decisions at later visits. (The TT
            // store keeps any previously-saved move for the same key.)
            // Measured: pooled +9.8 ± 6.4 over two batches.
            Move ttStoreMove = flag == TranspositionTable::FLAG_UPPER
                                   ? (Move)MOVE_NONE
                                   : bestMoveLocal;
            owner->tt_.store(pos->key, ttStoreMove, scoreToTT(bestScore, ply), depth, flag,
                             ttEvalStore, pvNode);
        }
        return bestScore;
    }

    // ------------------------------------------------------------------
    // Quiescence
    // ------------------------------------------------------------------

    int quiesce(int alphaIn, int beta, int ply, int qply) {
        corrPly_ = ply;  // v1.4.10: continuation-corr context ply
        checkStop();
        if (stopped()) return 0;
        if (ply >= MAX_PLY_GUARD) return leafEval();

        bool inCheck = pos->checkers() != 0;
        int alpha = alphaIn;

        // v1.4.0 lever: mate-distance pruning (the main search has had it
        // since v1; qsearch lines could climb above/below achievable
        // mates). The best this node can do is mate the opponent at
        // ply+1 (beta cap) and the worst is being mated right here
        // (alpha floor) — identical bounds to the main search's block.
        if (alpha < -MATE + ply) alpha = -MATE + ply;
        if (beta > MATE - ply - 1) beta = MATE - ply - 1;
        if (alpha >= beta) return alpha;

        // v1.4.5: draw adjudication at qsearch entry (mirrors the main
        // search's block). Without it, capture chains that hit a
        // repetition, the 50-move rule, or a dead-drawn material tail
        // (KNvK / KBvK / KNNvK) stand-pat on a full eval — the classic
        // qsearch draw blindness. The repetition scan is guarded by
        // hmc >= 4 (a repetition needs 4 reversible plies; SF-standard)
        // so qsearch pays nothing on the common short-clock path.
        if (ply > 0 && (pos->halfmoveClock >= 100 || pos->isDeadMaterialDraw() ||
                        (pos->halfmoveClock >= 4 && pos->isRepetitionDraw()))) {
            return drawScore(ply);
        }

        // ---- TT (cutoffs are safe: bound semantics hold at any depth) ----
        Move ttMove = MOVE_NONE;
        int ttEval = TranspositionTable::EVAL_NONE;
        {
            TranspositionTable::ProbeResult pr = owner->tt_.probe(*pos, pos->key);
            if (pr.hit) {
                int tScore = scoreFromTT(pr.score, ply);
                if (pr.flag == TranspositionTable::FLAG_EXACT) return tScore;
                if (pr.flag == TranspositionTable::FLAG_LOWER && tScore >= beta) return tScore;
                if (pr.flag == TranspositionTable::FLAG_UPPER && tScore <= alphaIn) return tScore;
                ttMove = pr.move;
                ttEval = pr.eval;
            }
        }

        int best;
        // v1.4.0: the store-side eval — raw+residual, NO corrhist (readers
        // re-apply the live correction sum; see correctedEval's note).
        int ttEvalStore;
        Move bestQMove = MOVE_NONE;
        if (inCheck) {
            best = -INFINITE;
            ttEvalStore = TranspositionTable::EVAL_NONE;
        } else {
            // Leaf hybrid: stand-pat cutoff decision uses HCE+corrhist+NNUE;
            // the value recorded in the TT uses the raw+residual semantics
            // of the main search's stores (v1.4.0 uniform store — the
            // correction term is re-applied fresh on every read), so
            // TT-eval readers never see mixed semantics for the same
            // position.
            if (ttEval != TranspositionTable::EVAL_NONE) {
                // v1.4.0: stored raw+residual + the CURRENT correction sum.
                ttEvalStore = ttEval;  // pass-through (already store semantics)
                best = ttEval + corrSumOnly(ply);
            } else {
                // v1.4.1 fix: compute the bounded leaf eval FIRST so the
                // far-boundary NNUE skip inside leafEvalBoundedFrom is
                // actually reachable. v1.4.0 computed ttEvalStore — whose
                // staticResidualInject() runs the full NNUE forward pass
                // whenever the residual is not yet cached — BEFORE calling
                // the bounded helper, so e.res was always materialized and
                // the skip was dead code (HISTORY §23.2: resCalls ==
                // evalCalls). §23.2's earlier attempt at restoring the skip
                // stored the HCE-only value into the TT on skipped nodes
                // (mixed semantics, -3.4 Elo); here skipped nodes store
                // EVAL_NONE instead — the TT's publish() preserves any
                // previously stored valid eval, so every stored eval keeps
                // the uniform raw+static-residual semantics.
                int rawV = rawEvalCached();
                int corrected = USE_CORRHIST ? rawV + corrSumOnly(ply) : rawV;
                best = leafEvalBoundedFrom(alphaIn, beta, corrected);
                ECEntry &ec = evalCache[pos->key & EC_MASK];
                ttEvalStore =
                    (!nnue::enabled || nnue::staticBlendPct == 0 ||
                     !ecMatch(ec, pos->key) || ec.res != RES_INVALID16)
                        ? (rawV + staticResidualInject())
                        : TranspositionTable::EVAL_NONE;
            }
            if (best >= beta) {
                storeQ(alphaIn, beta, best, MOVE_NONE, ttEvalStore, ply);
                return best;
            }
            if (best > alpha) alpha = best;
        }

        Move *moves = movesBuf[ply];
        int32_t *scores = scoresBuf[ply];
        // At the first quiescence ply of a non-check node, generate all moves
        // so checking moves can be searched too.
        int count = generate(*pos, moves, !inCheck && qply > 0);
        // (v1.4.2 note: compacting the skipped qply-0 moves out of this
        // list before scoreQMoves was tested and REJECTED — the lazy
        // selection sort below is not stable for tied moves, so removing
        // list elements permutes tied capture/quiet groups through the
        // swap mechanics (a tied pawn-capture trio rotated when the list
        // was compacted). The compaction changes the tree while saving
        // only a couple of history lookups per skipped quiet; not worth a
        // tree change. The verbatim-gate variant additionally hoisted the
        // loop's running-best/legal-dependent capture gates, which is
        // unsound outright. See HISTORY.md §39.)
        //
        // v1.4.3 history note (recorded straight): the "sound version" —
        // scoreQMoves marking never-searched qply-0 quiets with a
        // -2000000 sentinel below every producible score, with the
        // selection loop breaking on the sentinel — was implemented,
        // proved bench-identical at four depths, passed a 96-position
        // game-derived hunt at depths 9/11, and STILL diverged in the
        // fixed-depth self-play sequence comparison (2/8 openings at
        // depth 12, deep endgames under warm TT). The quiesce package was
        // dropped wholesale (HISTORY.md §40.4) and the marker is NOT in
        // the code — scoreQMoves scores every quiet as before. Re-trying
        // the same marker without a new root-cause analysis is off-limits.
        int us = pos->sideToMove;
        int legal = 0;
        // v1.4.4: king-ray mask for the legality gate below (same
        // soundness argument as the alphaBeta sites — quiesce pays
        // makeMove + a 5-lookup inCheck(us) per move, most of which the
        // gate now provably skips), plus the in-check evasion target mask
        // for the provably-illegal skip.
        Bitboard kingRaysQ = 0;
        Bitboard qEvasionMask = 0;
        if (!inCheck) {
            int ksqUs = pos->kingSquare(us);
            kingRaysQ = rookAttacks(ksqUs, 0) | bishopAttacks(ksqUs, 0);
        } else {
            Bitboard ck = pos->checkers();
            if (!(ck & (ck - 1))) {  // single check
                int csq = lsb(ck);
                qEvasionMask = bitOf(csq) |
                               BETWEEN_BB[pos->kingSquare(us)][csq];
            }
        }

        // ------------------------------------------------------------------
        // v1.4.7: TT-move-first staging + quietChecksLeft early break
        // (bit-exact; the slot-0 swap mirrors alphaBeta's v1.4.1 staging).
        //
        // The three earlier quiesce-staging attempts (HISTORY §39.3/§40.4/
        // §41.2) diverged because they COMPACTED moves[] (changing the
        // selection-sort displacement dynamics) or searched the TT move
        // in place without swapping it to slot 0 (displacing a different
        // move into the suffix). Here the TT move is swapped to slot 0 —
        // exactly the permutation the selection sort's argmax step would
        // have performed (its 8000000 band is the strict maximum) — and
        // searched with the loop's own gates before scoreQMoves ever
        // runs. On TT cutoffs the whole scoring pass is skipped; when the
        // TT move does not cut off, the suffix loop from slot 1 sees the
        // identical array state the original loop had after consuming
        // slot 0.
        //
        // quietChecksLeft: at qply 0 non-checking quiets are ALL skipped
        // by the prefilter below, so when no remaining quiet can be
        // searched (no castles, no checking quiets) the first quiet the
        // selection sort picks ends the loop — every capture/promo
        // outranks every quiet by >= 2.99M, so a selected quiet implies
        // nothing searchable remains. skipQuietHist then also zeroes the
        // quiet scores in scoreQMoves (~60 table lookups saved), which
        // cannot change capture order (all captures/promos score
        // >= 2996604 > any quiet score) and only reorders moves that are
        // all skipped with no side effects.
        // ------------------------------------------------------------------
        int qTtIdx = -1;
        if (ttMove != MOVE_NONE) {
            for (int i = 0; i < count; i++)
                if (moves[i] == ttMove) { qTtIdx = i; break; }
        }
        bool qTtHandled = false;
        int quietChecksLeft = -1;  // -1 = not counted (qply > 0 or in check)
        if (qTtIdx >= 0) {
            if (qTtIdx != 0) {
                Move tm = moves[0];
                moves[0] = moves[qTtIdx];
                moves[qTtIdx] = tm;
            }
            Move move = moves[0];
            int victim = moveCaptured(move);
            int promo = movePromo(move);
            bool skipTt = false;
            // ---- the loop's own slot-0 gates, verbatim ----
            if (!inCheck && qply == 0 && victim == NO_PIECE && promo == 0) {
                if (isCastleMove(move)) {
                    pos->makeMove(move);
                    bool castleChecks = pos->checkers() != 0;
                    pos->unmakeMove();
                    if (!castleChecks || !seeGE(*pos, move, 0)) skipTt = true;
                } else {
                    if (!givesCheckQuiet(*pos, move)) skipTt = true;
                    else if (!seeGE(*pos, move, 0)) skipTt = true;
                }
            }
            if (!skipTt && !inCheck && victim != NO_PIECE && promo == 0) {
                if (!seeGE(*pos, move, -50)) skipTt = true;
                else if (best + PIECE_VALUES[victim] + 250 <= alpha) skipTt = true;
            }
            if (!skipTt && inCheck && movePiece(move) != KING && !isEPMove(move) &&
                !(qEvasionMask & bitOf(moveTo(move)))) skipTt = true;
            if (!skipTt) {
                pos->makeMove(move);
                if ((inCheck || movePiece(move) == KING || isEPMove(move) ||
                     (kingRaysQ & bitOf(moveFrom(move)))) &&
                    pos->inCheck(us)) {
                    pos->unmakeMove();
                } else {
                    legal++;
                    stack[ply] = move;
                    int score = -quiesce(-beta, -alpha, ply + 1, qply + 1);
                    pos->unmakeMove();
                    if (stopped()) return 0;
                    if (score > best) {
                        best = score;
                        bestQMove = move;
                        if (score > alpha) {
                            alpha = score;
                            if (alpha >= beta) {
                                storeQ(alphaIn, beta, best, move, ttEvalStore, ply);
                                return best;
                            }
                        }
                    }
                }
            }
            qTtHandled = true;
        }
        if (!inCheck && qply == 0) {
            quietChecksLeft = 0;
            for (int i = qTtHandled ? 1 : 0; i < count; i++) {
                Move m = moves[i];
                if (moveCaptured(m) != NO_PIECE || movePromo(m) != 0) continue;
                if (isCastleMove(m) || givesCheckQuiet(*pos, m)) quietChecksLeft++;
            }
        }
        scoreQMoves(moves, scores, count, ttMove, ply, quietChecksLeft == 0);

        for (int i = qTtHandled ? 1 : 0; i < count; i++) {
            int bi = argMaxScores(scores, i, count);
            if (bi != i) {
                Move tm = moves[i]; moves[i] = moves[bi]; moves[bi] = tm;
                int32_t ts = scores[i]; scores[i] = scores[bi]; scores[bi] = ts;
            }
            Move move = moves[i];
            int victim = moveCaptured(move);
            int promo = movePromo(move);

            // v1.4.7: with no searchable quiet left at qply 0, the first
            // selected quiet ends the loop (see the staging note above) —
            // everything behind it is prefilter-skipped with no side
            // effects, and the buffer permutation is dead state (the next
            // node at this ply regenerates it).
            if (quietChecksLeft == 0 && victim == NO_PIECE && promo == 0) break;

            // ---- v1.1 quiet-check prefilter (qsearch ply 0) ----
            // First-ply quiets are only searched when they give check (and
            // are SEE-safe). The old flow made EVERY quiet to test
            // checkers() and scored the survivors with a POST-move SEE
            // — which was computed on the wrong board state (side to move
            // already flipped, mover already on `to`), i.e. a scrambled
            // exchange. Now: non-checking quiets (the overwhelming
            // majority) are skipped BEFORE makeMove, and checking quiets
            // get an exact PRE-move SEE. Castling keeps the exact post-make
            // checkers() test (rook checks + exotic king-vacation lines) but
            // also with a pre-move SEE.
            if (!inCheck && qply == 0 && victim == NO_PIECE && promo == 0) {
                if (isCastleMove(move)) {
                    pos->makeMove(move);
                    bool castleChecks = pos->checkers() != 0;
                    pos->unmakeMove();
                    if (!castleChecks || !seeGE(*pos, move, 0)) continue;
                } else {
                    if (!givesCheckQuiet(*pos, move)) continue;
                    if (!seeGE(*pos, move, 0)) continue;
                }
            }

            // v1.4.7 fix: the old `legal >= 1` guard disabled BOTH the SEE
            // and the delta prune for the FIRST capture at every quiescence
            // node — a holdover of the main search's "never prune before
            // one legal move is secured" rule, which has no analogue here:
            // when !inCheck the mover already holds a legal stand-pat score
            // in `best`, so pruning every capture simply returns the
            // stand-pat (the intended qsearch semantics). The guard made
            // scoreQMoves' pure MVV-LVA ordering search the #1 LOSING
            // sacrifice (e.g. QxR into a defended pawn, SEE -425) at full
            // cost in every single node.
            if (!inCheck && victim != NO_PIECE && promo == 0) {
                // Skip clearly losing captures.
                if (!seeGE(*pos, move, -50)) continue;
                // Delta pruning.
                // v1.3.1 lever: margin 150 -> 250 — the tighter margin
                // dropped borderline-winning captures whose victim value
                // straddled the alpha gap. Measured: ≈ +13 ± 9 (5k nodes).
                if (best + PIECE_VALUES[victim] + 250 <= alpha) continue;
            }

            // v1.4.4: provably-illegal in-check skip (bit-exact, same
            // argument as the alphaBeta loop: the skipped makeMove +
            // inCheck + unmakeMove triplet is side-effect-free, and the
            // array/scores/selection mechanics are untouched).
            if (inCheck && movePiece(move) != KING && !isEPMove(move) &&
                !(qEvasionMask & bitOf(moveTo(move)))) {
                continue;
            }

            pos->makeMove(move);
            // v1.4.4 ray gate (same condition as alphaBeta; kingRaysQ is 0
            // while in check and the || chain short-circuits).
            if ((inCheck || movePiece(move) == KING || isEPMove(move) ||
                 (kingRaysQ & bitOf(moveFrom(move)))) &&
                pos->inCheck(us)) {
                pos->unmakeMove();
                continue;
            }
            legal++;
            // v1.3: publish the move so continuation-history ordering at
            // deeper qsearch plies sees the true previous move. quiesce
            // never wrote stack[ply], so capture ordering below the first
            // qsearch ply used a stale move from an earlier search path.
            stack[ply] = move;

            int score = -quiesce(-beta, -alpha, ply + 1, qply + 1);
            pos->unmakeMove();
            if (stopped()) return 0;

            if (score > best) {
                best = score;
                bestQMove = move;
                if (score > alpha) {
                    alpha = score;
                    if (alpha >= beta) {
                        storeQ(alphaIn, beta, best, move, ttEvalStore, ply);
                        return best;
                    }
                }
            }
        }

        if (inCheck && legal == 0) return -MATE + ply;
        storeQ(alphaIn, beta, best, bestQMove, ttEvalStore, ply);
        return best;
    }

    void storeQ(int alphaIn, int beta, int best, Move move, int eval, int ply) {
        int flag = best >= beta   ? TranspositionTable::FLAG_LOWER
                   : best > alphaIn ? TranspositionTable::FLAG_EXACT
                                    : TranspositionTable::FLAG_UPPER;
        // v1.3.1 lever: no best move on fail-low nodes (qsearch included —
        // it is where most upper bounds are produced).
        Move m = flag == TranspositionTable::FLAG_UPPER ? (Move)MOVE_NONE : move;
        owner->tt_.store(pos->key, m, scoreToTT(best, ply), 0, flag, eval,
                         /*pv=*/beta - alphaIn > 1);
    }

    // ------------------------------------------------------------------
    // Move ordering
    // ------------------------------------------------------------------

    Move scoreMoves(Move *moves, int32_t *scores, int count, Move ttMove, int ply) {
        int us = pos->sideToMove;
        Move k1 = killers[ply * 2];
        Move k2 = killers[ply * 2 + 1];
        Move prevOpp = ply >= 1 ? stack[ply - 1] : MOVE_NONE;
        Move prevOwn = ply >= 2 ? stack[ply - 2] : MOVE_NONE;
        int prevOppIdx = (prevOpp != MOVE_NONE && movePiece(prevOpp) < 6)
                             ? (movePiece(prevOpp) * 64 + moveTo(prevOpp))
                             : -1;
        int prevOwnIdx = (prevOwn != MOVE_NONE && movePiece(prevOwn) < 6)
                             ? (movePiece(prevOwn) * 64 + moveTo(prevOwn))
                             : -1;
        Move counterMove = prevOppIdx >= 0 ? counterMoves[(us ^ 1) * 384 + prevOppIdx]
                                           : MOVE_NONE;
        int followBase = prevOwnIdx >= 0 ? us * 147456 + prevOwnIdx * 384 : -1;
        int counterBase = prevOppIdx >= 0 ? us * 147456 + prevOppIdx * 384 : -1;
        Move prev3 = ply >= 3 ? stack[ply - 3] : MOVE_NONE;
        Move prev4 = ply >= 4 ? stack[ply - 4] : MOVE_NONE;
        int prev3Idx = (prev3 != MOVE_NONE && movePiece(prev3) < 6)
                           ? (movePiece(prev3) * 64 + moveTo(prev3))
                           : -1;
        int prev4Idx = (prev4 != MOVE_NONE && movePiece(prev4) < 6)
                           ? (movePiece(prev4) * 64 + moveTo(prev4))
                           : -1;
        int c3Base = prev3Idx >= 0 ? us * 147456 + prev3Idx * 384 : -1;
        int c4Base = prev4Idx >= 0 ? us * 147456 + prev4Idx * 384 : -1;

        // (v1.4.7: the enemyPawns/enemyPawnAtt precompute that used to live
        // here was dead work — six lines of shifts/masks per main-search
        // node whose results no scoring branch ever read. Removed.)

        for (int i = 0; i < count; i++) {
            Move m = moves[i];
            int pieceIdx = movePiece(m) * 64 + moveTo(m);
            if (m == ttMove) {
                scores[i] = 8000000;
            } else if (movePromo(m) != 0) {
                scores[i] = 4000000 + 16 * PIECE_VALUES[movePromo(m)] +
                            (isCapture(m) ? PIECE_VALUES[moveCaptured(m)] : 0);
            } else if (isCapture(m)) {
                // Continuation context ranks equal-value captures: a knight
                // recapture that answers the threat is usually right.
                int ch = captureHist[us * 2304 + pieceIdx * 6 + moveCaptured(m)] / 16;
                if (counterBase >= 0) ch += counterHist[counterBase + pieceIdx] / 16;
                if (followBase >= 0) ch += followupHist[followBase + pieceIdx] / 16;
                // v1.1: proper MVV-LVA (victim-major — the old
                // 16*victim − 40*attacker let the attacker term dominate
                // and misordered mixed captures like NxQ vs PxR) plus the
                // good/bad split: victim >= attacker implies SEE >= 0
                // (the exchange starts non-negative and every later own
                // decision keeps it there), otherwise an exact early-exit
                // SEE sign test decides. Losing captures drop behind the
                // killers/counter-move but stay ahead of the quiets.
                if (PIECE_VALUES[moveCaptured(m)] >= PIECE_VALUES[movePiece(m)] ||
                    seeGE(*pos, m, 0)) {
                    scores[i] = 3000000 + 16 * PIECE_VALUES[moveCaptured(m)] -
                                PIECE_VALUES[movePiece(m)] + ch;
                } else {
                    // v1.4.1 fix (ordering): losing captures (SEE < 0) were
                    // ranked ABOVE every quiet (2.5M band vs the clamped
                    // [-100k, +100k] quiets) and searched before them —
                    // inflating legalCount so the engine's best quiets took
                    // the heaviest LMR / history pruning, while clearly
                    // losing sacrifices (QxP into a defended pawn) were
                    // searched at FULL depth at legalCount <= 6 (the
                    // capture-LMR gate needed legalCount > 6). They now
                    // rank BELOW all quiets — the Stockfish-standard
                    // bad-captures-last order — staying victim-major with
                    // capture history among themselves. Quiets are clamped
                    // to >= -100000, so scores < -100000 uniquely identify
                    // this class for the LMR fix below.
                    scores[i] = -200000 + 4 * PIECE_VALUES[moveCaptured(m)] + ch / 4;
                }
            } else if (m == k1) {
                scores[i] = 2900000;
            } else if (m == k2) {
                scores[i] = 2890000;
            } else if (m == counterMove) {
                scores[i] = 2880000;
            } else {
                int32_t s = mainHist[us * 384 + pieceIdx] / 4;
                // v1.4.0 lever: from-to history (square-pair signal — same
                // /4 weight family as the main history) and low-ply history
                // (from-to at the top of the tree, where ordering is worth
                // the most; /2 — the signal is sparser and sharper there).
                int32_t ft = moveFrom(m) * 64 + moveTo(m);
                s += fromToHist[us * 4096 + ft] / 4;
                if (ply <= 4)
                    s += lowPlyHist[ply <= 2 ? 0 : 1][ft] / 2;
                // v1.3.1 lever: pawn-structure history — same feature space
                // as the main history (piece x to, mover side), refined by
                // the node's pawn-key bucket. Weighted like the main
                // history (the /4 family), with the same gravity
                // bonus/malus updates.
                s += pawnHist[us * PAWN_HIST_STRIDE +
                              (int)(pos->pawnKey & PAWN_HIST_MASK) * 384 + pieceIdx] / 4;
                if (followBase >= 0) s += followupHist[followBase + pieceIdx] / 4;
                if (counterBase >= 0) s += counterHist[counterBase + pieceIdx] / 4;
                if (c3Base >= 0) s += contHist3[c3Base + pieceIdx] / 8;
                if (c4Base >= 0) s += contHist4[c4Base + pieceIdx] / 8;
                scores[i] = clampI(s, -100000, 100000);
            }
        }
        return counterMove;
    }


    void scoreQMoves(Move *moves, int32_t *scores, int count, Move ttMove, int ply,
                     bool skipQuietHist = false) {
        int us = pos->sideToMove;
        // v5.3: continuation context (the opponent's previous move) ranks
        // equal-value captures in qsearch too — the main search gains from
        // this ordering signal; the qsearch loop is where most leaf nodes
        // are ordered. Measured +6.4 ± 17.8 (50 ms, SPRT-neutral but
        // consistently non-negative; principled and free).
        int counterBase = -1;
        if (ply >= 1) {
            Move prevOpp = stack[ply - 1];
            if (prevOpp != MOVE_NONE && movePiece(prevOpp) < 6)
                counterBase = us * 147456 + (movePiece(prevOpp) * 64 + moveTo(prevOpp)) * 384;
        }
        for (int i = 0; i < count; i++) {
            Move m = moves[i];
            if (m == ttMove) {
                scores[i] = 8000000;
            } else if (movePromo(m) != 0) {
                scores[i] = 4000000 + 16 * PIECE_VALUES[movePromo(m)] +
                            (isCapture(m) ? PIECE_VALUES[moveCaptured(m)] : 0);
            } else if (isCapture(m)) {
                // pure MVV-LVA + capture history in qsearch ordering (SEE
                // stays in the *filtering* path, where it pays for itself).
                // v1.1: victim-major MVV-LVA (was 16*victim − 40*attacker).
                int ch = captureHist[us * 2304 +
                                     (movePiece(m) * 64 + moveTo(m)) * 6 +
                                     moveCaptured(m)] / 16;
                int pieceIdx = movePiece(m) * 64 + moveTo(m);
                if (counterBase >= 0) ch += counterHist[counterBase + pieceIdx] / 16;
                scores[i] = 3000000 + 16 * PIECE_VALUES[moveCaptured(m)] -
                            PIECE_VALUES[movePiece(m)] + ch;
            } else {
                // v1.4.7: skipQuietHist — set when the caller proved no
                // quiet in this list can be searched (quietChecksLeft == 0
                // at qply 0). Every capture/promo scores >= 2996604 and
                // every quiet would score within [-10240, 10238], so the
                // zeroed quiets keep their place after all captures, and
                // their relative order is irrelevant (all are skipped by
                // the prefilter with no side effects). Saves ~2 table
                // lookups per quiet on the hottest node type in the tree.
                if (skipQuietHist) {
                    scores[i] = 0;
                    continue;
                }
                int s = mainHist[us * 384 + movePiece(m) * 64 + moveTo(m)] / 4;
                if (counterBase >= 0)
                    s += counterHist[counterBase + movePiece(m) * 64 + moveTo(m)] / 16;
                scores[i] = s;
            }
        }
    }

    int quietHistoryValue(Move move, int ply, int moverSide, int pawnBucket) {
        // moverSide: the color making `move` (the node's side to move).
        // v4.0 fix: both call sites run AFTER makeMove, where
        // pos->sideToMove is the OPPONENT — the old internal read indexed
        // the wrong side's tables, turning the history-gated pruning and
        // the LMR history adjustment into noise.
        // v1.3.1: pawnBucket is the pre-move (node's) pawn-key bucket, so
        // the pawn-structure history is keyed by the node the move is
        // made in, exactly like the update path.
        int us = moverSide;
        int pieceIdx = movePiece(move) * 64 + moveTo(move);
        int v = mainHist[us * 384 + pieceIdx] / 4;
        // (v1.4.0 tested: from-to + low-ply history INSIDE the LMR value
        // measured neutral-at-best — the quietHistoryValue distribution is
        // calibrated to the LMR gates (hist<=0 extra reduction,
        // >=2500/4200 de-reductions, the -200 prune gate), and the extra
        // terms shift the profile. The new histories stay in the ORDERING
        // path (scoreMoves), where they are a pure signal add.)
        v += pawnHist[us * PAWN_HIST_STRIDE + pawnBucket * 384 + pieceIdx] / 4;
        if (ply >= 2) {
            Move p = stack[ply - 2];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                v += followupHist[us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 +
                                  pieceIdx] /
                     8;
            }
        }
        if (ply >= 1) {
            Move p = stack[ply - 1];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                v += counterHist[us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 +
                                  pieceIdx] /
                     8;
            }
        }
        if (ply >= 3) {
            Move p = stack[ply - 3];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                v += contHist3[us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 +
                               pieceIdx] /
                     8;
            }
        }
        if (ply >= 4) {
            Move p = stack[ply - 4];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                v += contHist4[us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 +
                               pieceIdx] /
                     8;
            }
        }
        return v;
    }

    // ------------------------------------------------------------------
    // History updates (gravity formula keeps values bounded)
    // ------------------------------------------------------------------

    static int gravity(int h, int b) {
        // v1.4.5: signed-h gravity (same fix as corrGravity; see the
        // negative-quadrant analysis there).
        int64_t ab = b < 0 ? -(int64_t)b : (int64_t)b;
        int64_t v = (int64_t)h + b - ((int64_t)h * ab) / 16384;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        return (int)v;
    }

    void bumpQuiet(Move move, int us, int b, int ply, int pawnBucket) {
        int pieceIdx = movePiece(move) * 64 + moveTo(move);
        int ft = moveFrom(move) * 64 + moveTo(move);
        mainHist[us * 384 + pieceIdx] =
            (int16_t)gravity(mainHist[us * 384 + pieceIdx], b);
        // v1.4.0 lever: from-to history (+ low-ply refinement at ply 1-4)
        // — same gravity update as every other quiet table.
        fromToHist[us * 4096 + ft] =
            (int16_t)gravity(fromToHist[us * 4096 + ft], b);
        if (ply >= 1 && ply <= 4)
            lowPlyHist[ply <= 2 ? 0 : 1][ft] =
                (int16_t)gravity(lowPlyHist[ply <= 2 ? 0 : 1][ft], b);
        // v1.3.1 lever: pawn-structure history update — keyed by the pawn-key
        // bucket of the node the move was made in (the caller passes the
        // pre-move bucket; pos is back at the node when this runs).
        {
            int idx = us * PAWN_HIST_STRIDE + pawnBucket * 384 + pieceIdx;
            pawnHist[idx] = (int16_t)gravity(pawnHist[idx], b);
        }
        if (ply >= 2) {
            Move p = stack[ply - 2];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                int idx = us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 + pieceIdx;
                followupHist[idx] = (int16_t)gravity(followupHist[idx], b);
            }
        }
        if (ply >= 1) {
            Move p = stack[ply - 1];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                int idx = us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 + pieceIdx;
                counterHist[idx] = (int16_t)gravity(counterHist[idx], b);
            }
        }
        if (ply >= 3) {
            Move p = stack[ply - 3];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                int idx = us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 + pieceIdx;
                contHist3[idx] = (int16_t)gravity(contHist3[idx], b);
            }
        }
        if (ply >= 4) {
            Move p = stack[ply - 4];
            if (p != MOVE_NONE && movePiece(p) < 6) {
                int idx = us * 147456 + (movePiece(p) * 64 + moveTo(p)) * 384 + pieceIdx;
                contHist4[idx] = (int16_t)gravity(contHist4[idx], b);
            }
        }
    }

    void bumpCapture(Move move, int us, int b) {
        int victim = moveCaptured(move);
        if (victim >= 6) return;
        int idx = us * 2304 + (movePiece(move) * 64 + moveTo(move)) * 6 + victim;
        captureHist[idx] = (int16_t)gravity(captureHist[idx], b);
    }

    void updateCutoffStats(Move move, bool quiet, Move *tried, int triedCount, int depth,
                           int ply) {
        // v1.4.6: 1.5*d^2 -> 2*d^2. With the v1.4.5 signed gravity the
        // negative tail no longer amplifies itself toward the clamp, so the
        // usable learning band narrowed — the bonus side carries the whole
        // signal now and can afford SF-grade energy. (E7's old +10k-bonus
        // rejection was measured under the broken |h|*|b| gravity.)
        int b = 2 * depth * depth;
        int us = pos->sideToMove;
        // v1.3.1: the node's pawn-key bucket for the pawn-history updates
        // (pos is back at this node — the cutoff store runs after unmake).
        int pawnBucket = (int)(pos->pawnKey & PAWN_HIST_MASK);
        if (quiet) {
            // Killers.
            if (killers[ply * 2] != move) {
                killers[ply * 2 + 1] = killers[ply * 2];
                killers[ply * 2] = move;
            }
            bumpQuiet(move, us, b, ply, pawnBucket);
            // Counter-move: this quiet move refuted the parent's move.
            if (ply >= 1) {
                Move prev = stack[ply - 1];
                if (prev != MOVE_NONE && movePiece(prev) < 6) {
                    counterMoves[(us ^ 1) * 384 + movePiece(prev) * 64 + moveTo(prev)] = move;
                }
            }
        } else {
            bumpCapture(move, us, b);
        }
        // Malus for every earlier move that failed to refute.
        // v1.4.7 fix (malus isolation): when the CUTOFF move is a capture,
        // only earlier CAPTURES are malused. A tactical capture refutation
        // (often a deliberate losing sacrifice — the SEE < 0 band ordered
        // after the quiets since v1.4.1) says nothing about the quiets that
        // preceded it: they failed against a different class of threat, and
        // damping their ordering signal on top of the capture-history
        // learning was pure noise (the Stockfish treatment maluses quiets
        // only when the cutoff itself was quiet-or-check).
        for (int q = 0; q < triedCount; q++) {
            Move tm = tried[q];
            if (tm == move) continue;
            if (isQuiet(tm)) {
                if (quiet) bumpQuiet(tm, us, -b, ply, pawnBucket);
            } else if (isCapture(tm)) {
                bumpCapture(tm, us, -(b / 2));
            }
        }
    }

    // ------------------------------------------------------------------
    // TT score conversion
    // ------------------------------------------------------------------

    inline int scoreToTT(int score, int ply) const {
        if (score >= MATE_BOUND) return score + ply;
        if (score <= -MATE_BOUND) return score - ply;
        return score;
    }
    inline int scoreFromTT(int score, int ply) const {
        if (score >= MATE_BOUND) return score - ply;
        if (score <= -MATE_BOUND) return score + ply;
        return score;
    }
    static int mateDistance(int score) {
        if (score >= MATE_BOUND) return (MATE - score + 1) / 2;
        if (score <= -MATE_BOUND) return -((MATE - (-score) + 1) / 2);
        return 0;
    }
};

uint64_t Searcher::totalNodes() {
    uint64_t t = 0;
    for (Worker *w : workers_)
        if (w) t += w->nodesShared.load(std::memory_order_relaxed);
    return t;
}

// v1.4.7: see search.h — the ucinewgame reset wipes the TT and every
// allocated worker's learned tables.
void Searcher::clearTable() {
    tt_.clear();
    for (Worker *w : workers_)
        if (w) w->clearHistories();
}

namespace {

// Search worker thread wrapper. On Android the Lazy-SMP helpers are started
// with an explicit 8 MB stack: bionic's default thread stack can be as small
// as ~1 MB (and JVM-spawned threads even smaller on some OEM builds), and a
// guard-page hit inside the recursion shows up as SIGSEGV/SEGV_ACCERR. The
// desktop path keeps plain std::thread (host default stack is 8 MB).
class HelperThread {
public:
    using Fn = std::function<void()>;

    explicit HelperThread(Fn fn) : fn_(std::move(fn)) {}
    ~HelperThread() { join(); }
    HelperThread(const HelperThread &) = delete;
    HelperThread &operator=(const HelperThread &) = delete;

    bool start() {
#if defined(__ANDROID__)
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 8u << 20);
        int rc = pthread_create(&tid_, &attr, &HelperThread::run, this);
        pthread_attr_destroy(&attr);
        if (rc == 0) {
            running_ = true;
            return true;
        }
        return false;
#else
        stdThread_ = std::thread(&HelperThread::runStd, this);
        return true;
#endif
    }

    void join() {
#if defined(__ANDROID__)
        if (running_) {
            pthread_join(tid_, nullptr);
            running_ = false;
        }
#else
        if (stdThread_.joinable()) stdThread_.join();
#endif
    }

private:
    static void *run(void *self) {
        static_cast<HelperThread *>(self)->fn_();
        return nullptr;
    }
    static void runStd(HelperThread *self) { self->fn_(); }

    Fn fn_;
#if defined(__ANDROID__)
    pthread_t tid_ = 0;
    bool running_ = false;
#else
    std::thread stdThread_{};
#endif
};

}  // namespace

// ==================================================================
// Public entry point
// ==================================================================

SearchResult Searcher::think(
    Position &board, const SearchLimits &limits,
    const std::function<void(const IterationInfo &)> &onIteration) {
    stopped_.store(false, std::memory_order_relaxed);
    tt_.nextSearch();

    // ---- forced-move shortcut (adaptive mode only) ----
    SearchLimits effLimits = limits;
    if (limits.adaptiveTime && limits.jitterCp == 0 && limits.moveTimeMs > 0) {
        Move tmp[MAX_MOVES];
        int legal = generateLegal(board, tmp);
        if (legal == 1) {
            SearchLimits cut;
            cut.maxDepth = limits.maxDepth < 10 ? limits.maxDepth : 10;
            // v1.4.11: 120 -> 30 ms cap, single thread. With exactly one
            // legal move the search cannot change the choice — the whole
            // call exists only to publish a score/PV for the caller. The
            // old 120 ms / full-threads shortcut burned a forced move's
            // entire budget (sweep + 7 helper spawns included) on clocked
            // games — pure clock bleed exactly in the forcing lines that
            // winning endgames are made of.
            cut.moveTimeMs = limits.moveTimeMs < 30 ? limits.moveTimeMs : 30;
            cut.threads = 1;
            cut.contemptCp = limits.contemptCp;  // keep analysis truth (0) vs play (12)
            effLimits = cut;
        }
    }

    int nThreads = effLimits.threads;
    if (nThreads < 1) nThreads = 1;
    if (nThreads > MAX_THREADS) nThreads = MAX_THREADS;

    Worker *main = worker(0);
    main->prepare(board, effLimits, effLimits.jitterCp);

    std::vector<std::unique_ptr<HelperThread>> helpers;
    if (nThreads > 1) {
        for (int i = 1; i < nThreads; i++) {
            Worker *w = worker(i);
            // Helpers search their own clone so the caller's board is only
            // ever mutated by the main worker.
            // v1.4.3: the clone/limits half of prepare stays HERE (it reads
            // the caller's board, which must not yet be mutated), but the
            // 12.6 MB history-aging sweeps now run inside the helper's own
            // thread — they touch only worker-local memory. The helpers
            // used to be prepared sequentially on the main thread while the
            // game clock was already running (8 threads = ~100 MB of sweeps
            // before depth 1 even began); now they overlap each other AND
            // the main worker's own preparation/search start.
            w->prepareSetup(board, effLimits, 0);
            helpers.push_back(std::unique_ptr<HelperThread>(
                new HelperThread([w] {
                    w->prepareAge();
                    w->runThink(nullptr);
                })));
            if (!helpers.back()->start()) {
                helpers.pop_back();  // search with fewer helpers
                break;
            }
        }
    }

    SearchResult result = main->runThink(onIteration);
    stopped_.store(true, std::memory_order_relaxed);
    for (auto &h : helpers) h->join();

    // v1.1: best-thread pick. Helpers used to contribute only TT entries;
    // their completed iterations were discarded. A helper that COMPLETED a
    // strictly deeper iteration than the main worker holds strictly more
    // information about the root — use its move/score (ties keep main).
    // completedDepth only advances on finished iterations, so an aborted
    // iteration can never win here.
    //
    // v1.4.4: Lazy-SMP consensus vote (multi-thread only; 1T is the plain
    // main result). The v1.4.2 pairwise-overwrite guard still let a single
    // depth-skip helper "win" the pick: helpers on depth % (id+1) == 0
    // ladders complete very different depth patterns at very different
    // node counts — helper 1 skips every even depth and can present a
    // depth+2 iteration with a fraction of main's cumulative nodes,
    // overriding a move that every OTHER thread agrees on. The consensus
    // vote (the Stockfish pattern) weights every completed worker by
    // (score - minScore + 14) * depth, sums the weights per best-move,
    // and takes the move with the highest total; among the workers
    // playing that move the one with the best (score, depth) is used,
    // with main winning ties. A lone deeper-but-thinner helper can no
    // longer outvote the field.
    if (nThreads > 1) {
        int voters = 0;
        int minScore = 0;
        bool haveMin = false;
        for (int i = 0; i < nThreads; i++) {
            Worker *w = workers_[i];
            if (!w || !w->prepared) continue;
            const SearchResult &r = w->lastResult;
            if (r.bestMove == MOVE_NONE || r.depth < 1) continue;
            voters++;
            if (!haveMin || r.score < minScore) {
                minScore = r.score;
                haveMin = true;
            }
        }
        if (voters >= 2) {
            // Vote weights per worker, accumulated per best-move.
            int64_t weight[16] = {0};   // per worker index
            int64_t moveVotes[16] = {0}; // per worker index (own vote only)
            int nv = 0;
            int voterIdx[16];
            for (int i = 0; i < nThreads && nv < 16; i++) {
                Worker *w = workers_[i];
                if (!w || !w->prepared) continue;
                const SearchResult &r = w->lastResult;
                if (r.bestMove == MOVE_NONE || r.depth < 1) continue;
                int64_t wt = (int64_t)(r.score - minScore + 14) * r.depth;
                if (wt < 0) wt = 0;
                weight[nv] = wt;
                moveVotes[nv] = wt;  // provisional; accumulated below
                voterIdx[nv] = i;
                nv++;
            }
            // Accumulate votes per distinct move (O(n^2), n <= 8).
            for (int a = 0; a < nv; a++) {
                for (int b = 0; b < nv; b++) {
                    if (b == a) continue;
                    const SearchResult &ra = workers_[voterIdx[a]]->lastResult;
                    const SearchResult &rb = workers_[voterIdx[b]]->lastResult;
                    if (rb.bestMove == ra.bestMove) moveVotes[a] += weight[b];
                }
            }
            // Winning worker: max total vote; ties -> best (score, depth);
            // final ties -> main (index order, main is 0).
            int winner = -1;
            for (int a = 0; a < nv; a++) {
                if (winner < 0) { winner = a; continue; }
                const SearchResult &ra = workers_[voterIdx[a]]->lastResult;
                const SearchResult &rw = workers_[voterIdx[winner]]->lastResult;
                if (moveVotes[a] > moveVotes[winner] ||
                    (moveVotes[a] == moveVotes[winner] &&
                     (ra.score > rw.score ||
                      (ra.score == rw.score && ra.depth > rw.depth)))) {
                    winner = a;
                }
            }
            if (winner >= 0 && voterIdx[winner] != 0) {
                const SearchResult &rw = workers_[voterIdx[winner]]->lastResult;
                result.bestMove = rw.bestMove;
                result.score = rw.score;
                result.depth = rw.depth;
                result.mateIn = rw.mateIn;
            }
        } else if (voters == 1) {
            // Only one completed worker: fall back to the v1.4.2 pairwise
            // guard semantics (a lone deeper helper still wins on the
            // score-not-worse / two-deeps-deeper conditions).
            for (int i = 1; i < nThreads; i++) {
                Worker *w = workers_[i];
                if (!w || !w->prepared) continue;
                const SearchResult &r = w->lastResult;
                if (r.bestMove != MOVE_NONE && r.depth > result.depth &&
                    (r.score >= result.score || r.depth >= result.depth + 2)) {
                    result.bestMove = r.bestMove;
                    result.score = r.score;
                    result.depth = r.depth;
                    result.mateIn = r.mateIn;
                }
            }
        }
    }

    result.nodes = totalNodes() ? totalNodes() : result.nodes;
    return result;
}

Searcher::Worker *Searcher::worker(int i) {
    if (!workers_[i]) {
        workers_[i] = new Worker();
        workers_[i]->id = i;
        workers_[i]->owner = this;
    }
    return workers_[i];
}

// Host verification hook definition (see declaration above).
bool hostGivesCheckQuiet(const Position &b, Move m) {
    // Re-implemented by calling the anonymous-namespace helper via a lambda
    // is impossible from here; the helper is at file-anonymous scope ABOVE,
    // so a direct call works (same translation unit).
    return givesCheckQuiet(b, m);
}

Searcher::~Searcher() {
    for (Worker *w : workers_) delete w;
}

}  // namespace tacturn
