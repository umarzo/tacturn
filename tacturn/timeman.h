// TacTurn Chess Engine v1 — adaptive time allocation (port of TimeManager.kt).
//
// Splits the per-move budget into a SOFT budget (what the engine aims to
// spend) and a HARD cap (wall-clock safety). The soft budget is scaled by a
// position complexity factor computed from cheap root features, with safety
// rails: a depth floor, a hard cap and iteration-cost prediction.
//
// v1.4.0: mid-search stability scaling. The soft budget can only SHRINK
// (the framework's 0.2-1.0 factor range is untouched); two new shrink
// signals and one new extension trigger:
//   - best-move stability: consecutive completed iterations that kept the
//     same best move (a settled position does not need the full budget);
//   - root node-fraction: the best move's share of the iteration's root
//     node spend (one move dominating = decided position);
//   - falling eval: the score dropping across iterations joins the
//     "unstable" triggers (the only budget increase the framework allows
//     is the boolean unstable flag, capped at 1.5x the pacing budget) —
//     a position that is getting worse for the mover deserves the extra
//     look, mirroring the main-search improving flag's asymmetry.

#pragma once

#include <cstdint>

#include "position.h"

namespace tacturn {

class TimeManager {
public:
    TimeManager(int64_t baseMs, int64_t hardCapMs, int depthFloor)
        : baseMs_(baseMs), hardCapMs_(hardCapMs), depthFloor_(depthFloor) {}

    const char *lastDecisionReason() const { return reason_; }
    bool unstable() const { return unstable_; }
    double complexityFactor() const { return factor_; }
    int64_t softBudgetMs() const { return softBudgetMs_; }
    int64_t pacingBudgetMs() const { return paceMs_; }
    int instabilityHits() const { return instabilityHits_; }
    int bestMoveStability() const { return stability_; }

    /** Pre-search complexity analysis; returns the soft budget in ms. */
    int64_t planBudget(const Position &board, const Move *legalMoves, int legalCount,
                       int lastScore) {
        double factor = 1.0;

        // --- Branching: very few legal moves -> close to forced ---
        if (legalCount <= 1) factor *= 0.05;
        else if (legalCount <= 2) factor *= 0.40;
        else if (legalCount <= 4) factor *= 0.55;
        else if (legalCount <= 8) factor *= 0.70;
        else if (legalCount <= 16) factor *= 0.85;
        else factor *= 1.05;
        // v1.4.7 fix: the normal-middlegame base was 0.95 — every wide
        // position's soft budget was pre-shrunk 5% before the stability /
        // node-fraction pacing shrank it further (0.70 x 0.65 = 0.455 of an
        // already-reduced base), so clocked play routinely spent only
        // 35-50% of the per-move allocation and banked time it never
        // used. 1.05 re-centers the base; the shrink signals below and the
        // hard cap still bound the spend.

        // --- In check: evasion quality is safety-critical ---
        if (board.inCheck(board.sideToMove)) factor *= 1.20;

        // --- Captures at root: tactical positions deserve more ---
        int captures = 0;
        for (int i = 0; i < legalCount; i++)
            if (isCapture(legalMoves[i])) captures++;
        if (captures > 0) factor *= 1.08;

        // --- Forced recapture ---
        Move last = board.lastMoveOrNull();
        bool forcedRecapture = false;
        if (last != MOVE_NONE && isCapture(last)) {
            int to = moveTo(last);
            int recaps = 0;
            for (int i = 0; i < legalCount; i++) {
                if (isCapture(legalMoves[i]) && moveTo(legalMoves[i]) == to) recaps++;
            }
            if (recaps == 1) {
                forcedRecapture = true;
                factor *= 0.60;
            }
        }

        // --- Phase-aware scaling ---
        int men = popcount(board.occupied);
        int moveNo = board.undoCount() / 2 + 1;
        if (men <= 6) factor *= 0.70;
        else if (men <= 10) factor *= 0.88;
        if (moveNo <= 10) factor *= 0.92;

        // --- Drawish / repetitive territory ---
        if (board.repetitionCount() >= 2) factor *= 0.65;
        // (v1.4.7: the dead-equal middlegame penalty — lastScore in
        // [-20, 20] after move 25 shrank the budget another 15% — was
        // removed. Dead-equal positions late in the game are exactly
        // where the engine must out-search the opponent to grind out a
        // win or hold a draw; the stability pacing already handles
        // genuinely settled positions.)

        // --- Clamp: never below 20% of base. v1.4.2: the ceiling rises
        // 1.0 -> 1.14 so the in-check (x1.20) and tactical-root (x1.08)
        // multipliers above can actually reach their intended effect —
        // the old 1.0 cap neutralized them to at most +5% (0.95 -> 1.0).
        // 1.14 is exactly 0.95 x 1.20: a check-evasion move in a wide
        // position may spend up to 14% over the base soft budget; the
        // hard budget and the iteration pacing caps still bound the
        // total spend, and the banking factors below 1.0 dominate. ---
        if (factor < 0.20) factor = 0.20;
        if (factor > 1.14) factor = 1.14;

        factor_ = factor;
        int64_t soft = (int64_t)(baseMs_ * factor);
        // Soft-budget floor: never search pathologically short — but on tiny
        // clocks (small base budgets) a flat 40 ms floor would burn the whole
        // clock and flag, so the floor scales down with the base budget.
        int64_t softFloor = baseMs_ < 40 ? baseMs_ : 40;
        if (soft < softFloor) soft = softFloor;
        if (soft > hardCapMs_) soft = hardCapMs_;
        softBudgetMs_ = soft;
        paceMs_ = soft;
        reason_ = legalCount <= 1 ? "forced" : (forcedRecapture ? "forced-recapture" : "budget");
        return softBudgetMs_;
    }

    /** Decides whether to start iteration (depth+1).
     *
     *  v1.4.0 parameters:
     *   - bestMoveNodesPerMille: the best move's share (per-mille) of the
     *     last completed iteration's root node spend; -1 = unknown.
     *   - depthGap: plies between the last COMPLETED iteration and the one
     *     about to start (main: 1; helper depth-skip ladders: 2-3) — the
     *     cost estimate scales accordingly. */
    bool shouldStartNextIteration(int64_t elapsedMs, int64_t lastIterMs, int completedDepth,
                                  Move bestMove, Move prevBestMove, int score, int prevScore,
                                  bool aspirationResearched, int bestMoveNodesPerMille = -1,
                                  int depthGap = 1, int lastScore = 0) {
        int64_t estMs = estMsFor(lastIterMs, depthGap);

        bool moveChanged = bestMove != MOVE_NONE && bestMove != prevBestMove &&
                           prevBestMove != MOVE_NONE;
        // v1.4.4 fix (depth-1 swing guard): the caller's prevIterScore is
        // initialized to -INFINITE, so at completedDepth == 1 the swing
        // computation read score - (-33000) = ~+33k and set unstable_ =
        // true on the FIRST depth of every search — a phantom instability
        // hit (and a forced full-pace bypass) before any real signal
        // exists. Guard both the swing and the falling-eval test on the
        // score being a real one.
        int scoreSwing = 0;
        bool fallingEval = false;
        if (prevScore > -30000) {  // a real previous score, not the init
            scoreSwing = score - prevScore;
            if (scoreSwing < 0) scoreSwing = -scoreSwing;
            // v1.4.4: cross-move drop trigger. The old test only compared
            // iteration-to-iteration: a position that worsened by 45 cp
            // since the engine's PREVIOUS MOVE but is flat across
            // iterations reads as settled and gets shrunk — exactly when
            // the unstable extension should engage.
            fallingEval = prevScore - score > 15 || lastScore - score > 30;
        }

        // v1.4.0: best-move stability — consecutive completed iterations
        // that kept the same best move.
        // v1.4.3 fix (free-depth guard): depths 1-6 complete in microseconds
        // and almost always repeat the obvious root move — counting them let
        // stability_ reach its deepest shrink tier (0.70) after ~3 ms, before
        // the time-consuming iterations where the signal is actually earned
        // began, pre-shrinking paceMs_ for the whole real search. Only
        // iterations at completedDepth >= 6 INCREMENT now; a best-move
        // change at ANY depth still resets (a flip-flop is instability
        // evidence even when cheap). Fixed-movetime searches never reach
        // this code (no TimeManager); the change affects clocked play only.
        if (bestMove != MOVE_NONE && bestMove == prevBestMove) {
            if (completedDepth >= 6) stability_++;
        } else {
            stability_ = 0;
        }

        // v1.4.0: falling eval — the score is dropping for the mover. The
        // position turned sharper; that is exactly the "unstable" situation
        // the 1.1x/1.5x extension exists for (a rising or flat score is
        // not an extension trigger).
        // (v1.4.4: fallingEval is computed under the real-score guard above.)
        unstable_ = moveChanged || scoreSwing > 30 || aspirationResearched || fallingEval;
        if (unstable_) instabilityHits_++;

        // v1.4.0: the PACING budget — the soft budget shrunk by the settled-
        // position signals. Both scalers are shrink-only; their product is
        // floored at 0.5 so the pacing budget can never halve the planned
        // soft budget (which itself is already floored at 20% of base).
        double stab = 1.0;
        if (stability_ >= 2) stab = 0.85;
        if (stability_ >= 3) stab = 0.75;
        if (stability_ >= 5) stab = 0.70;
        // v1.4.1 fix: TWO-SIDED node fraction. The old scaler only shrank
        // (>= 550/700/850 per-mille on the best move = decided position);
        // a CONTESTED root (< 350 per-mille — over 65% of the search effort
        // on rival moves) is exactly where time banked on easy decided
        // positions pays. Pace up to 1.15-1.30x the soft budget there,
        // walled by the hard cap below.
        double frac = 1.0;
        if (bestMoveNodesPerMille >= 0) {
            if (bestMoveNodesPerMille < 200) frac = 1.30;
            else if (bestMoveNodesPerMille < 350) frac = 1.15;
            else if (bestMoveNodesPerMille >= 850) frac = 0.65;
            else if (bestMoveNodesPerMille >= 700) frac = 0.75;
            else if (bestMoveNodesPerMille >= 550) frac = 0.85;
        }
        double scale = stab * frac;
        // v1.4.1 fix (unstable-shrink contradiction): stability_ counted
        // consecutive same-best-move iterations EVEN when the position had
        // just turned unstable (score plunge > 30, aspiration re-search,
        // falling eval, best-move change). A position stable for 5
        // iterations then collapsing kept paceMs_ = 0.70*soft*frac — so
        // the "1.5x unstable extension" (150% of paceMs_) still stopped
        // BELOW the plain soft budget: the extension could never extend.
        // When unstable, bypass the shrink entirely so paceMs_ = soft and
        // the extension genuinely reaches 1.5x soft (hard cap still walls
        // everything).
        if (unstable_) scale = 1.0;
        if (scale < 0.50) scale = 0.50;
        paceMs_ = (int64_t)(softBudgetMs_ * scale);
        // v1.4.11 fix (order): apply the pacing floor BEFORE the hard-cap
        // clamp. The old order could lift paceMs_ back above hardCapMs_
        // whenever the floor exceeded the cap (small base budgets), letting
        // iteration-start decisions reference a pacing budget the hard wall
        // would immediately abort — wasted budget that could not be banked.
        int64_t softFloor = baseMs_ < 40 ? baseMs_ : 40;
        if (paceMs_ < softFloor) paceMs_ = softFloor;
        if (paceMs_ > hardCapMs_) paceMs_ = hardCapMs_;  // never pace past the wall

        if (elapsedMs + estMs <= paceMs_) {
            // v1.4.7: the redundant 95% cut on paceMs_ is gone (the same
            // fix the v2.8.0 fixed-movetime path took when it recovered
            // 17-23% budget utilization — unspent clock cannot be banked,
            // and the hard wall catches estimate misses).
            reason_ = stability_ >= 3 ? "budget-stable" : "budget";
            return true;
        }
        if (completedDepth < depthFloor_ && elapsedMs + estMs <= hardCapMs_ * 98 / 100) {
            reason_ = "depth-floor";
            return true;
        }
        if (unstable_ && elapsedMs < paceMs_ * 110 / 100 &&
            elapsedMs + estMs <= paceMs_ * 150 / 100) {
            reason_ = "unstable";
            return true;
        }
        reason_ = "stop-budget";
        return false;
    }

    /** Wall-clock cap for the iteration about to start.
     *
     *  Always the hard cap. Soft pacing happens only at iteration start
     *  (shouldStartNextIteration); an already-started iteration is never
     *  aborted below the hard wall-clock safety margin — the old
     *  min(soft, hard) / 1.5x-soft aborts killed healthy iterations and
     *  wasted budget that could not be banked. */
    int64_t iterationCapMs(int completedDepth) const {
        (void)completedDepth;
        return hardCapMs_;
    }

private:
    /** Next-iteration cost estimate from the last iteration's wall time and
     *  the depth gap to the iteration about to start (helpers on skip
     *  ladders jump 2-3 plies; the tree grows ~1.55x per ply — bench
     *  d9->d11 = 2.42x over 2 plies — so gap-g costs ~(0.55g+0.75)x). */
    static int64_t estMsFor(int64_t lastIterMs, int gap) {
        if (gap < 1) gap = 1;
        int64_t est = gap == 1 ? lastIterMs * 13 / 10
                               : lastIterMs * (13 + 11 * (gap - 1)) / 10;
        if (est < lastIterMs + 2) est = lastIterMs + 2;
        return est < 2 ? 2 : est;
    }

    int64_t baseMs_, hardCapMs_;
    int depthFloor_;
    const char *reason_ = "budget";
    bool unstable_ = false;
    double factor_ = 1.0;
    int64_t softBudgetMs_ = 0;
    int64_t paceMs_ = 0;      // v1.4.0: stability/node-fraction scaled soft budget
    int instabilityHits_ = 0;
    int stability_ = 0;       // v1.4.0: consecutive stable-best-move iterations
};

}  // namespace tacturn
