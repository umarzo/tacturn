// TacTurn Chess Engine v1 — search (port of Search.kt).
//
// Iterative-deepening PVS (fail-soft alpha-beta) with:
//   - Lazy SMP over std::thread, sharing the race-hardened TT
//   - aspiration windows, IIR, singular extensions / multi-cut
//   - razoring, reverse futility, null-move (adaptive R), futility/LMP
//   - late move reductions (log formula + PV/cut-node/history adjustments)
//   - move ordering: TT move, MVV-LVA + capture history, killers,
//     counter-moves, side-aware piece-to history + two continuation
//     histories, gravity-scaled bonus/malus updates
//   - quiescence with TT probing, SEE filtering, delta pruning, full
//     evasions in check, checking moves at the first qsearch ply
//   - draw awareness (twofold repetition, 50-move, 12 cp contempt)
//   - the adaptive time manager
// an "improving" flag (static-eval trend) widens/narrows the
// futility & RFP margins — a well-established Elo gainer on top of the
// proven Kotlin heuristics.
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "evaluate.h"
#include "position.h"
#include "timeman.h"
#include "ttable.h"

namespace tacturn {

// ---- runtime-tunable search constants (SPSA interface) ----
// Global copy is written by UCI setoption BETWEEN searches; every worker
// snapshots it at prepare() time, so a running search always sees a stable
// parameter set. Defaults are bit-identical to the shipped constants.
namespace tuning {
struct Tunables {
    int lmr_base_m;  // LMR formula base, milliunits (0.80)
    int lmr_div_m;   // LMR formula divisor, milliunits (2.25)
    int rfp_imp;     // reverse-futility per-depth margin, improving
    int rfp_non;     // reverse-futility per-depth margin, non-improving
    int fut_base;    // quiet-futility base
    int fut_imp;    // quiet-futility per-depth, improving
    int fut_non;     // quiet-futility per-depth, non-improving
    int nmp_slack;   // null-move eval slack
    int nmp_rdiv;    // null-move depth divisor (x10)
    int razor;       // razoring per-depth margin
    int see_coeff;   // SEE-prune per-depth coefficient
    int asp_delta;   // initial aspiration half-window
    int corrWeight;  // correction-history divisor (1024 = shipped)
};

inline Tunables defaults() {
    Tunables t;
    t.lmr_base_m = 800;
    t.lmr_div_m = 2250;
    t.rfp_imp = 70;
    t.rfp_non = 105;
    t.fut_base = 95;
    t.fut_imp = 80;
    t.fut_non = 110;
    t.nmp_slack = 60;
    t.nmp_rdiv = 60;
    t.razor = 220;
    // v1.3.1: 60 -> 75. Coefficient 45 measured -16 (z -1.8): pruning MORE
    // was clearly wrong, so the untested direction (prune less — the
    // threshold moves further negative) is the one worth taking.
    t.see_coeff = 75;
    // v1.3: 12 -> 27. Measured on 12 positions at 1 s (two runs each):
    // start delta 12 reached 13.79 plies average, 24 -> 14.21, 40 -> 14.25.
    // The old +-12 window failed on 50-62% of root-search nodes / 72-79% of
    // iterations failed at least once; the score moves > 12 cp between
    // iterations 42% of the time (> 30 cp 13%). The 24-40 range is flat, so
    // 27 sits mid-curve while keeping the re-search ladder affordable.
    t.asp_delta = 27;
    t.corrWeight = 1536; // v1.4.6: persistence regime — entries no longer halved
    return t;
}

extern Tunables g;
}  // namespace tuning

struct SearchLimits {
    int maxDepth = 64;
    int64_t moveTimeMs = 5000;
    int64_t hardMoveTimeMs = 0;      // hard wall-clock cap; 0 -> moveTimeMs
    uint64_t nodeLimit = 0;          // 0 = unlimited (fixed-node testing / datagen)
    bool adaptiveTime = false;       // complexity-scaled budget + instability extension
    int depthFloor = 8;              // never stop below this completed depth (adaptive)
    int lastScore = 0;               // previous move's score (game-level context)
    int jitterCp = 0;                // root-only random noise for weaker levels
    int threads = 1;                 // Lazy-SMP thread count
    uint64_t rngSeed = 0;            // 0 -> seeded from the clock
    // Draw contempt in centipawns: the score the side to move assigns to a
    // repetition / 50-move draw inside the search tree. Play keeps the
    // historical 12 cp (bit-identical strength behavior); GAME ANALYSIS
    // passes 0 so reviewed evals are POV-neutral truth instead of carrying
    // a playing-mode "avoid draws" bias (the UCI-accepted convention:
    // Contempt 0 / SideToMove off for analysis).
    int contemptCp = 12;
    // ---- analysis extensions ----
    int interIterationDelayMs = 0;   // battery-saver sleep between ID iterations
    int rootExcludeCount = 0;        // Multi-PV via root-exclusion sequential searches
    Move rootExcludes[16] = {MOVE_NONE};

    bool isExcluded(Move m) const {
        for (int i = 0; i < rootExcludeCount; i++)
            if (rootExcludes[i] == m) return true;
        return false;
    }
};

struct IterationInfo {
    int depth;
    int score;
    Move bestMove;
    uint64_t nodes;
    int64_t timeMs;
};

struct SearchResult {
    Move bestMove = MOVE_NONE;
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
    int64_t timeMs = 0;
    int mateIn = 0;
    // Adaptive time-management record (valid when adaptiveTime was set).
    double timeComplexity = 1.0;
    int64_t timeSoftMs = 0;
    int64_t timeHardMs = 0;
    int timeUnstable = 0;
    const char *timeReason = "";
};

class Searcher {
public:
    static constexpr int MATE = 32000;
    static constexpr int MATE_BOUND = MATE - 512;
    static constexpr int INFINITE = 33000;
    static constexpr int MAX_THREADS = 8;

    explicit Searcher(int ttBits = 21) : tt_(ttBits) {}
    ~Searcher();

    void requestStop() { stopped_.store(true, std::memory_order_relaxed); }
    /** v1.4.7: ucinewgame reset — clears the TT AND every allocated
     *  worker's history/correction/eval-cache tables (the correction
     *  history has persisted across moves since v1.4.5, so a fresh game
     *  in a persistent process must wipe it explicitly). Defined in
     *  search.cpp — Worker is incomplete here. */
    void clearTable();
    /** Read-only TT access (PV extraction walks it after a search). */
    const TranspositionTable &tt() const { return tt_; }

    SearchResult think(Position &board, const SearchLimits &limits,
                       const std::function<void(const IterationInfo &)> &onIteration = nullptr);

private:
    struct Worker;

    uint64_t totalNodes();
    Worker *worker(int i);

    TranspositionTable tt_;
    std::atomic<bool> stopped_{false};
    Worker *workers_[MAX_THREADS] = {nullptr};
};

}  // namespace tacturn
