// TacTurn host UCI harness — desktop testing wrapper around the engine core.
//
// This tool is NOT part of the Android build (CMakeLists builds only the
// tacturn core + JNI). It exposes the engine over the standard UCI protocol
// for engine-vs-engine testing (cutechess-style), plus host utilities:
//
//   bench [depth]  — deterministic fingerprint suite (fresh searcher per
//                    position, fixed depth, 1 thread, contempt 12)
//   perft <depth>  — perft from the current position
//   evalpos        — print HCE eval / NNUE residual / corrected eval
//   isover         — print game-over state of the current game session
//                    (used by the match runner for adjudication)
//
// All original code — MIT licensed, Umar Khan.
//
// ============================================================================
// CCRL-KIT BUILD — UCI compliance upgrade for public engine testing
// ============================================================================
// Changes vs the v1.4.9 harness (engine core UNTOUCHED — bit-identical
// search/eval; all changes are protocol-layer only):
//
//  1. [THREADING] The search now runs on a worker thread while the main
//     thread keeps reading stdin, so `stop`, `isready` and `quit` are
//     honored DURING a search (UCI requires this; `go infinite` + `stop`
//     previously deadlocked the GUI).
//  2. [IDENTITY] `id name` is a single clean string ("TacTurn V1") —
//     GUIs previously showed the doubled "TacTurn TacTurn 1.4.9 (host UCI)".
//     TACTURN_AUTHOR below is the public author handle (Umar Khan).
//  3. [HASH] The Hash option is now declared in MEGABYTES (default 32,
//     min 1, max 32768) as GUIs and rating-list testers expect. The old
//     declaration ("default 86016 min 12 max 30") made GUIs clamp/reject
//     standard values like 256/512 MB. (CCRL uses 256 or 512 MB.)
//  4. [MOVESTOGO] `go ... movestogo N` is parsed and used for the clock
//     allocation (repeating time controls such as CCRL's 40/15). The
//     legacy no-movestogo allocation is kept bit-identically for
//     increment-only clocks.
//  5. [OWNBOOK] `OwnBook` check option, default FALSE. The engine's
//     embedded book was never used by the UCI harness (it is an Android
//     app feature) — the option now makes that explicit and verifiable,
//     as rating lists require the engine's own book to be disabled.
//  6. [MULTIPV] `MultiPV` spin option (1..8) via sequential root-exclusion
//     searches — lets testers cross-check lines in analysis GUIs.
//  7. [SCORE] Mate scores are reported as `score mate N` (not raw ~32000
//     centipawns); `nps` is included in info lines; the final info line
//     carries the full PV from the transposition table.
//  8. [CLEAR HASH] `Clear Hash` button option (UCI convention between
//     games; clears the TT and learned histories — same state as
//     ucinewgame).
//  9. [WIN32] Portable to Windows (MinGW-w64 / MSVC): unistd.h ->
//     io.h guarded; the pthread stack-size thread launch in search.cpp
//     works unchanged under MinGW posix-threads.
// 10. [PARSING] setoption handles multi-word option names and values;
//     unknown/edge input never derails the parser.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <type_traits>
#include <chrono>

#ifdef _WIN32
#include <io.h>  // _isatty
#ifndef isatty
#define isatty _isatty
#endif
#else
#include <unistd.h>
#endif

#include "book.h"
#include "game.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "evaluate.h"
#include "ttable.h"
#include "see.h"
#include "api.h"

using namespace tacturn;

// Edit this to your public author name/handle before a public release —
// rating lists credit this name (the id author line and the README).
#ifndef TACTURN_AUTHOR
#define TACTURN_AUTHOR "Umar Khan"
#endif

namespace tacturn {
// exposed from search.cpp for host verification only
bool hostGivesCheckQuiet(const Position &b, Move m);  // (defined in search.cpp)
}

namespace {

// ----------------------------------------------------------------
// Bench suite (fixed, deterministic) — UNCHANGED from v1.4.9
// ----------------------------------------------------------------
const char *BENCH_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
};
const int BENCH_COUNT = (int)(sizeof(BENCH_FENS) / sizeof(BENCH_FENS[0]));

static void fuzzSee(long iterations, uint64_t seed) {
    // For every legal capture in random playout positions, compare
    // seeGE(pos, m, T) with (see(pos, m) >= T) over thresholds {-200,-50,0,50,200}.
    uint64_t rng = seed ? seed : 1;
    auto nextRand = [&rng](int n) {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return (int)((rng >> 33) % (uint64_t)n);
    };
    const int THRS[5] = {-200, -50, 0, 50, 200};
    long tested = 0, mismatch = 0;
    Position b;
    long positions = 0;
    for (long it = 0; it < iterations && mismatch == 0; it++) {
        const char *fen = BENCH_FENS[nextRand(BENCH_COUNT)];
        b.setFen(fen);
        int ply = 0;
        while (ply < 220) {
            Move buf[MAX_MOVES];
            int n = generateLegal(b, buf);
            if (n == 0) break;
            for (int i = 0; i < n; i++) {
                Move m = buf[i];
                if (moveCaptured(m) == NO_PIECE && movePromo(m) == 0) continue;
                int exact = see(b, m);
                for (int t = 0; t < 5; t++) {
                    bool ge = seeGE(b, m, THRS[t]);
                    bool truth = exact >= THRS[t];
                    tested++;
                    if (ge != truth) {
                        mismatch++;
                        std::printf("SEE MISMATCH thr=%d ge=%d truth=%d exact=%d fen=%s move=%s\n",
                                    THRS[t], (int)ge, (int)truth, exact, b.fen().c_str(),
                                    moveToUciStr(m).c_str());
                        break;
                    }
                }
                if (mismatch) break;
            }
            if (mismatch) break;
            positions++;
            b.makeMove(buf[nextRand(n)]);
            ply++;
        }
    }
    std::printf("fuzzSee: %ld positions, %ld threshold checks, %ld mismatches\n",
                positions, tested, mismatch);
    std::fflush(stdout);
}


struct UciOptions {
    int hashBits = 21;      // 2^21 entries x 16 B = 32 MiB (the shipped default)
    int hashMb = 32;        // advertised value, MB — keep in sync with hashBits
    int threads = 1;
    int contempt = 12;
    // v1.4.11: per-move overhead reserved from the clock (safety margin of
    // the flag-proof pacing). Raise it on slow hosts / slow GUIs / network.
    int moveOverhead = 60;
    bool useNnue = true;
    int blend = -1;         // -1 = compiled default
    int staticBlend = -1;   // -1 = compiled default
    bool ownBook = false;   // CCRL: engine's own book must be OFF (it is)
    int multiPv = 1;        // analysis convenience; 1 = normal play
};

uint64_t runBench(int depth, const UciOptions &opt) {
    uint64_t totalNodes = 0;
    for (int i = 0; i < BENCH_COUNT; i++) {
        GameSession g(opt.hashBits, BENCH_FENS[i]);
        SearchLimits lim;
        lim.maxDepth = depth;
        lim.moveTimeMs = 0;       // unlimited: depth-limited
        lim.hardMoveTimeMs = 0;
        lim.threads = 1;
        lim.contemptCp = opt.contempt;
        lim.rngSeed = 1;          // deterministic seed (jitter 0 anyway)
        SearchResult r = g.think(lim, nullptr);
        totalNodes += r.nodes;
        char mv[8];
        moveToUci(r.bestMove, mv);
        std::printf("bench %d: nodes=%llu best=%s score=%d\n", i + 1,
                    (unsigned long long)r.nodes, mv, r.score);
    }
    std::printf("Total nodes: %llu\n", (unsigned long long)totalNodes);
    std::fflush(stdout);
    return totalNodes;
}

// ----------------------------------------------------------------
// v1.3.2 fuzz: NNUE inference battery — UNCHANGED from v1.4.9
// ----------------------------------------------------------------
static void fuzzNnue(long iterations, uint64_t seed) {
    uint64_t rng = seed ? seed : 1;
    auto nextRand = [&rng](int n) {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return (int)((rng >> 33) % (uint64_t)n);
    };
    long positions = 0, mismatch = 0;
    Position b;
    for (long it = 0; it < iterations && mismatch == 0; it++) {
        const char *fen = BENCH_FENS[nextRand(BENCH_COUNT)];
        b.setFen(fen);
        int ply = 0;
        while (ply < 220 && mismatch == 0) {
            Move buf[MAX_MOVES];
            int n = generateLegal(b, buf);
            if (n == 0) break;
            positions++;
            if (!nnue::verifyIncremental(b)) {
                std::printf("NNUE INCREMENTAL MISMATCH fen=%s\n", b.fen().c_str());
                mismatch++;
                break;
            }
            int r1 = nnue::residual(b);
            int r2 = nnue::residual(b);
            long long ref = (long long)std::llround(nnue::referenceResidual(b));
            if (r1 != r2 || (long long)r1 != ref) {
                std::printf("NNUE MISMATCH int=%d int2=%d ref=%lld fen=%s\n",
                            r1, r2, ref, b.fen().c_str());
                mismatch++;
                break;
            }
            b.makeMove(buf[nextRand(n)]);
            ply++;
        }
    }
    std::printf("fuzznnue: %ld positions, %ld mismatches\n", positions, mismatch);
    std::fflush(stdout);
}

// ----------------------------------------------------------------
// Fuzz: givesCheckQuiet — UNCHANGED from v1.4.9
// ----------------------------------------------------------------
static void fuzzGivesCheck(long iterations, uint64_t seed) {
    uint64_t rng = seed ? seed : 1;
    auto nextRand = [&rng](int n) {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return (int)((rng >> 33) % (uint64_t)n);
    };
    long tested = 0, mismatch = 0;
    Position b;
    int positions = 0;
    for (long it = 0; it < iterations && mismatch == 0; it++) {
        const char *fen = BENCH_FENS[nextRand(BENCH_COUNT)];
        b.setFen(fen);
        int ply = 0;
        while (ply < 220) {
            Move buf[MAX_MOVES];
            int n = generateLegal(b, buf);
            if (n == 0) break;
            bool wasInCheck = b.checkers() != 0;
            if (!wasInCheck) {
                for (int i = 0; i < n; i++) {
                    Move m = buf[i];
                    if (moveCaptured(m) != NO_PIECE || movePromo(m) != 0) continue;
                    if (isCastleMove(m)) continue;  // exact post-make path
                    bool predicted = ::tacturn::hostGivesCheckQuiet(b, m);
                    b.makeMove(m);
                    bool truth = b.checkers() != 0;
                    b.unmakeMove();
                    tested++;
                    if (predicted != truth) {
                        mismatch++;
                        std::printf(
                            "MISMATCH pred=%d truth=%d fen=%s move=%s\n",
                            (int)predicted, (int)truth, b.fen().c_str(),
                            moveToUciStr(m).c_str());
                        break;
                    }
                }
            }
            if (mismatch) break;
            positions++;
            b.makeMove(buf[nextRand(n)]);
            ply++;
        }
    }
    std::printf("fuzzGivesCheck: %ld positions, %ld quiet moves tested, %ld mismatches\n",
                (long)positions, (long)tested, (long)mismatch);
    std::fflush(stdout);
}

/** Find the legal move matching a UCI string (e2e4, e7e8q). */
Move findUciMove(GameSession &g, const char *uci) {
    if (std::strlen(uci) < 4) return MOVE_NONE;
    int from = squareFromName(std::string(uci, 2).c_str());
    int to = squareFromName(std::string(uci + 2, 2).c_str());
    if (from < 0 || to < 0) return MOVE_NONE;
    std::vector<Move> cands = g.findMoves(from, to);
    if (cands.empty()) return MOVE_NONE;
    if (std::strlen(uci) == 4) {
        for (Move m : cands)
            if (movePromo(m) == 0) return m;
        return cands[0];
    }
    int promo = pieceTypeFromFen(uci[4]);
    promo = promo == KNIGHT ? KNIGHT : promo;
    for (Move m : cands)
        if (movePromo(m) == promo) return m;
    return MOVE_NONE;
}

/** UCI score token: "score cp N" or "score mate N" (mate in moves, signed). */
void formatScore(char *out, size_t outLen, int score) {
    constexpr int MATE = Searcher::MATE;
    constexpr int MATE_BOUND = Searcher::MATE_BOUND;
    if (score >= MATE_BOUND)
        std::snprintf(out, outLen, "score mate %d", (MATE - score + 1) / 2);
    else if (score <= -MATE_BOUND)
        std::snprintf(out, outLen, "score mate %d", -((MATE + score + 1) / 2));
    else
        std::snprintf(out, outLen, "score cp %d", score);
}

// ----------------------------------------------------------------
// runGo — runs on the SEARCH WORKER THREAD (main thread keeps stdin)
// ----------------------------------------------------------------
int runGo(GameSession &g, const UciOptions &opt, const std::string &goArgs,
          bool printInfo, std::atomic<bool> *searchingFlag = nullptr) {
    // The "searching" flag MUST drop before bestmove is printed: a GUI sends
    // its next "position"/"go" the instant it reads bestmove, and anything
    // that arrives while the flag is still set is ignored as "mid-search".
    // (V1 cleared it after returning -> rare lost commands / GUI timeouts.)
    // Nothing below touches the GameSession after this point.
    auto searchDone = [searchingFlag]() {
        if (searchingFlag) searchingFlag->store(false, std::memory_order_release);
    };
    std::istringstream parser(goArgs);
    SearchLimits lim;
    lim.maxDepth = 64;
    lim.moveTimeMs = 0;
    lim.hardMoveTimeMs = 0;
    lim.threads = opt.threads;
    lim.contemptCp = opt.contempt;
    lim.rngSeed = 0;
    lim.nodeLimit = 0;

    std::string tok;
    int64_t wtime = 0, btime = 0, winc = 0, binc = 0, movesToGo = 0;
    bool hasW = false, hasB = false;   // clock fields actually sent by the GUI
    bool infinite = false;
    // Safe numeric read: a missing/garbled value (e.g. a bare "go depth")
    // leaves the default untouched instead of zeroing it, which used to turn
    // the search into an unlimited one.
    auto num = [&parser](auto &dst) {
        long long v;
        if (parser >> v) dst = (std::remove_reference_t<decltype(dst)>)v;
        else parser.clear();
    };
    while (parser >> tok) {
        if (tok == "depth") num(lim.maxDepth);
        else if (tok == "movetime") num(lim.moveTimeMs);
        else if (tok == "nodes") num(lim.nodeLimit);
        else if (tok == "wtime") { num(wtime); hasW = true; }
        else if (tok == "btime") { num(btime); hasB = true; }
        else if (tok == "winc") num(winc);
        else if (tok == "binc") num(binc);
        else if (tok == "movestogo") num(movesToGo);   // [CCRL] repeating TCs
        else if (tok == "infinite") infinite = true;
        // unknown tokens (ponder etc.) are ignored gracefully
    }
    if (infinite) { lim.maxDepth = 64; lim.moveTimeMs = 0; }

    if (lim.moveTimeMs == 0 && lim.nodeLimit == 0 && (hasW || hasB)) {
        int64_t t = g.board.sideToMove == WHITE ? wtime : btime;
        int64_t inc = g.board.sideToMove == WHITE ? winc : binc;
        // A negative/zero clock (GUI reporting overtime) means "move now":
        // clamp to 1 ms instead of falling through to an unlimited search.
        // If only the opponent's clock was sent, assume 60 s for ours.
        bool ownSent = g.board.sideToMove == WHITE ? hasW : hasB;
        if (!ownSent) t = 60000;
        else if (t < 1) t = 1;
        // v1.4.11: flag-proof clock pacing (two-reserve accounting).
        //   safety      - hard-cap reserve for the overhead TAIL (option
        //                 Move Overhead, default 60 ms, scaled down on tiny
        //                 clocks). `hard` never spends into it.
        //   EXPECTED_OH - the EXPECTED per-move overhead (4 ms), used in the
        //                 sustainability math so the soft budget nets to zero
        //                 drain whenever the increment covers the overhead.
        //   mtg         - moves-to-go: the GUI's real `movestogo` when sent
        //                 (repeating TCs such as 40/15), otherwise an estimate
        //                 aged through the game (45 -> 25), with an emergency
        //                 thin spread (60) on a depleted no-increment clock.
        constexpr int64_t EXPECTED_OH = 4;
        int64_t safety = opt.moveOverhead;
        if (safety > t / 4) safety = t / 4;      // tiny clocks: keep a quarter
        if (safety < 2) safety = t > 2 ? 2 : 1;
        int64_t avail = t - safety;              // what the search may spend
        if (avail < 1) avail = 1;

        int mtg;
        if (movesToGo > 0) {
            mtg = (int)(movesToGo > 60 ? 60 : movesToGo) + 2;   // [CCRL] period TC
        } else {
            int ply = (int)g.records.size();
            mtg = 45 - ply / 4;                  // ply 80 (move 40) -> 25
            if (mtg < 25) mtg = 25;
            if (t < 500 && inc < t / 4) mtg = 60; // emergency: thin spread
        }
        int64_t einc = inc - EXPECTED_OH;
        if (einc < 0) einc = 0;

        int64_t soft = avail / mtg + einc;
        int64_t cap = avail * 2 / 5;             // never dump a huge share on one move
        if (cap < einc) cap = einc;
        if (cap > avail) cap = avail;
        if (soft > cap) soft = cap;
        if (soft < 1) soft = 1;

        int64_t hard = avail / 8;                // burst headroom (~12% of clock)
        if (hard < soft * 3) hard = soft * 3;
        if (hard > avail) hard = avail;          // NEVER past the reserve
        if (hard < soft) hard = soft;

        lim.moveTimeMs = soft;
        lim.hardMoveTimeMs = hard;
        // v1.4.11: in time trouble the depth floor shrinks too.
        lim.depthFloor = soft < 120 ? 4 : 8;
        // v1.4.0: clocked play uses the adaptive time manager (the app's
        // play mode) — the soft/hard split above becomes the tm's base/cap.
        lim.adaptiveTime = true;
    }

    // ---- [CCRL] Multi-PV: sequential root-exclusion searches ----
    int npv = opt.multiPv > 1 ? opt.multiPv : 1;
    if (npv > 1) {
        // Divide timed budgets across the lines (analysis convenience).
        if (lim.moveTimeMs > 0) {
            lim.moveTimeMs = std::max<int64_t>(20, lim.moveTimeMs / npv);
            if (lim.hardMoveTimeMs > 0)
                lim.hardMoveTimeMs = std::max(lim.moveTimeMs, lim.hardMoveTimeMs / npv);
        }
        Move chosen[16] = {MOVE_NONE};
        int lines = 0;
        for (int k = 0; k < npv; k++) {
            SearchLimits lpv = lim;
            lpv.rootExcludeCount = k;
            for (int e = 0; e < k; e++) lpv.rootExcludes[e] = chosen[e];
            SearchResult r = g.think(lpv, [printInfo, k](const IterationInfo &info) {
                if (!printInfo || k > 0) return;  // stream line 1 only
                char mv[8], sc[32];
                moveToUci(info.bestMove, mv);
                formatScore(sc, sizeof sc, info.score);
                std::printf("info multipv 1 depth %d %s nodes %llu time %lld pv %s\n",
                            info.depth, sc, (unsigned long long)info.nodes,
                            (long long)info.timeMs, mv);
                std::fflush(stdout);
            });
            if (r.bestMove == MOVE_NONE) break;
            char mv[8], sc[32];
            moveToUci(r.bestMove, mv);
            formatScore(sc, sizeof sc, r.score);
            // Full PV for this line from the TT walk.
            Move pv[32];
            int pvLen = g.extractPv(r.bestMove, pv, 32);
            std::string pvStr;
            for (int i = 0; i < pvLen; i++) {
                char p[8];
                moveToUci(pv[i], p);
                if (i) pvStr += " ";
                pvStr += p;
            }
            if (pvStr.empty()) pvStr = mv;
            std::printf("info multipv %d depth %d %s nodes %llu time %lld pv %s\n",
                        k + 1, r.depth, sc, (unsigned long long)r.nodes,
                        (long long)r.timeMs, pvStr.c_str());
            std::fflush(stdout);
            chosen[lines++] = r.bestMove;
        }
        if (lines == 0) { searchDone(); std::printf("bestmove (none)\n"); std::fflush(stdout); return 0; }
        char mv[8];
        moveToUci(chosen[0], mv);
        searchDone();
        std::printf("bestmove %s\n", mv);
        std::fflush(stdout);
        return 0;
    }

    SearchResult r = g.think(lim, [printInfo](const IterationInfo &info) {
        if (!printInfo) return;
        char mv[8], sc[32];
        moveToUci(info.bestMove, mv);
        formatScore(sc, sizeof sc, info.score);
        int64_t nps = info.timeMs > 0 ? (int64_t)(info.nodes * 1000 / info.timeMs) : 0;
        std::printf("info depth %d %s nodes %llu nps %lld time %lld pv %s\n",
                    info.depth, sc, (unsigned long long)info.nodes,
                    (long long)nps, (long long)info.timeMs, mv);
        std::fflush(stdout);
    });

    char mv[8];
    if (r.bestMove == MOVE_NONE) {
        searchDone();
        std::printf("bestmove (none)\n");
    } else {
        // Final info line with the full TT-walk PV (GUIs display the
        // latest info before bestmove).
        char sc[32];
        formatScore(sc, sizeof sc, r.score);
        Move pv[64];
        int pvLen = g.extractPv(r.bestMove, pv, 64);
        std::string pvStr;
        for (int i = 0; i < pvLen; i++) {
            char p[8];
            moveToUci(pv[i], p);
            if (i) pvStr += " ";
            pvStr += p;
        }
        if (!pvStr.empty() && r.depth > 0) {
            std::printf("info depth %d %s nodes %llu time %lld pv %s\n",
                        r.depth, sc, (unsigned long long)r.nodes,
                        (long long)r.timeMs, pvStr.c_str());
        }
        moveToUci(r.bestMove, mv);
        searchDone();
        std::printf("bestmove %s\n", mv);
    }
    std::fflush(stdout);
    return 0;
}

void applyOptions(UciOptions &opt) {
    nnue::enabled = opt.useNnue;
    if (opt.blend >= 0) nnue::blendPct = opt.blend;
    if (opt.staticBlend >= 0) nnue::staticBlendPct = opt.staticBlend;
}

// MB <-> TT bits (16 B/entry; 12..30 bits) — conversion shared by the
// Hash option handler. Same mapping as v1.4.9.
int hashMbToBits(long mb) {
    if (mb < 1) mb = 1;
    long entries = mb * 1024 * 1024 / 16;
    int bits = 12;
    // largest 2^bits entries that still fit in `mb` MB (round DOWN; the old
    // loop rounded up and allocated up to 2x the requested Hash).
    while ((1L << (bits + 1)) <= entries && bits < 30) bits++;
    return bits;
}

}  // namespace

int main(int argc, char **argv) {
    std::ios::sync_with_stdio(false);

    // Interactive console (double-click / bare launch from cmd): UCI engines
    // are headless, so show a short hint on stderr and then behave like any
    // other UCI engine -- keep reading commands until "quit" / EOF.
    // (V1 used to run a startpos "fingerprint" here and exit, which looked
    // like a crash on double-click and tripped on a stale expected value.
    // The identity check is `bench 9` -> Total nodes: 230912.)
    if (argc == 1 && isatty(0)) {
        std::fprintf(stderr,
                     "%s by %s -- UCI chess engine (api v%d).\n"
                     "Load this exe in a GUI (Cute Chess, Arena, Banksia, ...) or type UCI\n"
                     "commands here: uci, isready, position, go, bench 9, quit.\n",
                     tacturn_version(), TACTURN_AUTHOR, tacturn_api_version());
    }

    // One-shot bench mode: "tacturn-uci bench [depth]"
    if (argc >= 2 && std::strcmp(argv[1], "bench") == 0) {
        int depth = argc >= 3 ? std::atoi(argv[2]) : 9;
        UciOptions opt;
        runBench(depth, opt);
        return 0;
    }

    UciOptions opt;
    applyOptions(opt);
    GameSession *g = new GameSession(opt.hashBits);
    std::string lastFen = Position::START_FEN;
    std::vector<std::string> gameMoves;

    // [CCRL] Search worker thread. The main loop below keeps reading stdin
    // while a search runs, so stop/isready/quit are honored mid-search.
    std::thread searchThr;
    std::atomic<bool> searching{false};

    // Stop the running search reliably. The engine core clears its stop flag
    // when a search *starts*, so a single requestStop() sent right after "go"
    // (before the worker thread reaches the search) is lost and the search
    // runs forever. Keep re-sending it until the worker reports it is done.
    auto stopSearch = [&]() {
        while (searching.load(std::memory_order_acquire)) {
            g->requestStop();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream parser(line);
        std::string tok;
        if (!(parser >> tok)) continue;

        // ---- Commands honored DURING a search ----
        if (searching.load(std::memory_order_acquire)) {
            if (tok == "stop") {
                stopSearch();
            } else if (tok == "isready") {
                std::printf("readyok\n");
                std::fflush(stdout);
            } else if (tok == "quit") {
                stopSearch();
                break;
            }
            // Everything else mid-search is not sent by spec-compliant
            // GUIs; ignore it until bestmove is out.
            continue;
        }

        if (tok == "uci") {
            // [CCRL] clean identity + GUI-conventional option declarations.
            std::printf("id name %s\n", tacturn_version());
            std::printf("id author %s\n", TACTURN_AUTHOR);
            std::printf("option name Hash type spin default %d min 1 max 32768\n",
                        opt.hashMb);
            std::printf("option name Threads type spin default 1 min 1 max 8\n");
            std::printf("option name Contempt type spin default %d min -200 max 200\n",
                        opt.contempt);
            std::printf("option name Move Overhead type spin default %d min 0 max 10000\n",
                        opt.moveOverhead);
            std::printf("option name UseNNUE type check default true\n");
            std::printf("option name NNUEBlend type spin default %d min 0 max 100\n",
                        nnue::blendPct);
            std::printf("option name StaticBlend type spin default %d min 0 max 100\n",
                        nnue::staticBlendPct);
            std::printf("option name OwnBook type check default false\n");
            std::printf("option name MultiPV type spin default 1 min 1 max 8\n");
            std::printf("option name Clear Hash type button\n");
            std::printf("uciok\n");
        } else if (tok == "isready") {
            std::printf("readyok\n");
        } else if (tok == "ucinewgame") {
            // v1.4.7: reset in place instead of delete/new GameSession.
            g->reset();
            g->clearSearchTable();
            lastFen = Position::START_FEN;
            gameMoves.clear();
        } else if (tok == "setoption") {
            // [CCRL] Robust parsing: multi-word names + optional value,
            // e.g. "setoption name Clear Hash" / "setoption name OwnBook
            // value true" / "setoption name Hash value 256".
            std::string keyword;
            parser >> keyword;  // consume the "name" keyword
            std::string name, value, t;
            while (parser >> t) {
                if (t == "value") break;
                if (!name.empty()) name += " ";
                name += t;
            }
            std::getline(parser, value);
            size_t a = value.find_first_not_of(" \t\r\n");
            size_t b = value.find_last_not_of(" \t\r\n");
            value = (a == std::string::npos) ? "" : value.substr(a, b - a + 1);

            if (name == "Hash") {
                long mb = std::atol(value.c_str());
                opt.hashMb = (int)(mb < 1 ? 1 : (mb > 32768 ? 32768 : mb));
                opt.hashBits = hashMbToBits(opt.hashMb);
                delete g;
                g = new GameSession(opt.hashBits);
                g->setFen(lastFen.c_str());
                for (const std::string &mv : gameMoves) {
                    Move m = findUciMove(*g, mv.c_str());
                    if (m == MOVE_NONE) break;
                    g->makeMove(m);
                }
            } else if (name == "Threads") {
                opt.threads = std::max(1, std::min(8, std::atoi(value.c_str())));
            } else if (name == "Contempt") {
                opt.contempt = std::atoi(value.c_str());
            } else if (name == "Move Overhead" || name == "MoveOverhead") {
                int ov = std::atoi(value.c_str());
                opt.moveOverhead = ov < 0 ? 0 : (ov > 10000 ? 10000 : ov);
            } else if (name == "UseNNUE") {
                opt.useNnue = (value == "true" || value == "1");
                applyOptions(opt);
            } else if (name == "NNUEBlend") {
                opt.blend = std::atoi(value.c_str());
                applyOptions(opt);
            } else if (name == "StaticBlend") {
                opt.staticBlend = std::atoi(value.c_str());
                applyOptions(opt);
            } else if (name == "OwnBook") {
                opt.ownBook = (value == "true" || value == "1");
            } else if (name == "MultiPV") {
                opt.multiPv = std::max(1, std::min(8, std::atoi(value.c_str())));
            } else if (name == "Clear Hash") {
                g->clearSearchTable();
            }
            // unknown options are silently ignored (GUIs may probe)
        } else if (tok == "position") {
            std::string sub;
            parser >> sub;
            std::string fen;
            std::vector<std::string> moves;
            if (sub == "startpos") {
                fen = Position::START_FEN;
                parser >> sub;  // consume "moves" if present
            } else if (sub == "fen") {
                std::string f;
                while (parser >> f && f != "moves") fen += f + " ";
                if (f == "moves") sub = "moves";
            }
            if (!fen.empty()) {
                if (!g->setFen(fen.c_str())) {
                    std::printf("info string invalid FEN\n");
                    std::fflush(stdout);
                    continue;
                }
            }
            if (sub == "moves") {
                std::string mstr;
                while (parser >> mstr) moves.push_back(mstr);
            }
            for (const std::string &mstr : moves) {
                Move m = findUciMove(*g, mstr.c_str());
                if (m == MOVE_NONE) {
                    std::printf("info string illegal move %s\n", mstr.c_str());
                    std::fflush(stdout);
                    break;
                }
                g->makeMove(m);
            }
            lastFen = fen.empty() ? g->fen() : fen;
            gameMoves = moves;
        } else if (tok == "go") {
            // [CCRL] Book handling: the UCI harness never used the engine's
            // book; OwnBook (default false) keeps that explicit. When a
            // tester turns it on, a book hit answers instantly.
            if (opt.ownBook) {
                Move bm = g->bookProbe();
                if (bm != MOVE_NONE) {
                    char mv[8];
                    moveToUci(bm, mv);
                    std::printf("info string book move\nbestmove %s\n", mv);
                    std::fflush(stdout);
                    continue;
                }
            }
            std::string rest;
            std::getline(parser, rest);
            size_t a = rest.find_first_not_of(" \t");
            if (a != std::string::npos) rest = rest.substr(a);
            else rest.clear();
            // Reap the previous (finished) search thread before spawning a
            // new one — assigning over a joinable std::thread terminates.
            // Safe: we only get here when `searching` is false, i.e. runGo
            // already returned and printed bestmove.
            if (searchThr.joinable()) searchThr.join();
            searching.store(true, std::memory_order_release);
            searchThr = std::thread([&g, &opt, &searching, rest]() {
                runGo(*g, opt, rest, true, &searching);
                searching.store(false, std::memory_order_release);
            });
            continue;  // main thread keeps reading stdin during the search
        } else if (tok == "stop") {
            g->requestStop();  // no search running: harmless flag set
        } else if (tok == "perft") {
            int d = 5;
            parser >> d;
            uint64_t nodes = perft(g->board, d);
            std::printf("perft %d: %llu\n", d, (unsigned long long)nodes);
        } else if (tok == "isover") {
            int r = g->result();
            std::printf("isover %d\n", r);
        } else if (tok == "evalpos") {
            PawnHashTable ph;
            int hce = evaluate(g->board, ph);
            int res = nnue::residual(g->board);
            std::printf("evalpos hce %d nnue %d stm %d fen %s\n", hce, res,
                        g->board.sideToMove, g->fen().c_str());
        } else if (tok == "bench") {
            int d = 9;
            parser >> d;
            runBench(d, opt);
        } else if (tok == "fuzzcheck") {
            long iters = 2000;
            uint64_t seed = 1;
            parser >> iters;
            parser >> seed;
            fuzzGivesCheck(iters, seed);
        } else if (tok == "fuzzsee") {
            long iters = 3000;
            uint64_t seed = 1;
            parser >> iters;
            parser >> seed;
            fuzzSee(iters, seed);
        } else if (tok == "fuzznnue") {
            long iters = 2000;
            uint64_t seed = 1;
            parser >> iters;
            parser >> seed;
            fuzzNnue(iters, seed);
        } else if (tok == "pgotrain") {
            int secs = 60;
            parser >> secs;
            uint64_t nodes = tacturn_pgo_train(secs);
            std::printf("pgotrain: %llu nodes in ~%d s\n",
                        (unsigned long long)nodes, secs);
            std::fflush(stdout);
        } else if (tok == "quit") {
            break;
        }
        std::fflush(stdout);
    }
    // stdin closed (GUI died) or "quit": never wait on a running search
    // (e.g. "go infinite") -- ask it to stop, then join.
    stopSearch();
    if (searchThr.joinable()) searchThr.join();
    delete g;
    return 0;
}
