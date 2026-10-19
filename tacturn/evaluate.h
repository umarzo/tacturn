// TacTurn Chess Engine — evaluation.
//
// Tapered (midgame/endgame) evaluation, parameterized for Texel tuning:
//   - PeSTO-derived piece-square tables (maintained incrementally in
//     Position) — retuned by the tools/tuner pipeline
//   - material + tempo (tuned)
//   - pawn structure: doubled, isolated, backward, islands, phalanx /
//     supported (connected), passed by rank (+ connected passers, king
//     proximity, blocked passers in the endgame) — cached in a per-worker
//     pawn hash
//   - bishop pair, bad bishop (own pawns on the bishop's colour)
//   - rooks on open/semi-open files, the 7th rank, behind passed pawns
//   - knight/bishop outposts (pawn-protected, beyond enemy pawn reach)
//   - king shelter (per-file distance/rank table) + pawnless-flank penalty
//   - king-attack danger model (weighted attacker units + king-zone hits,
//     quadratic safety table, separate MG/EG scaling)
//   - safe mobility (squares attacked by enemy pawns excluded), tapered
//   - threats: pawn attacks on minors/majors, safe minor attacks on majors,
//     hanging (attacked & undefended) pieces
//   - endgame material scaling (opposite-colour bishops, pawnless leaders)
// All terms are centipawns from White's POV, converted to side-to-move POV
// at the very end (negamax convention).
//
// Every tunable term contributes  params.mg * amount  to the midgame score
// and  params.eg * amount  to the endgame score, where `amount` is a pure
// positional feature count. tools/tuner.cpp extracts those amounts (see
// EvalTrace) and fits the parameter vector by logistic regression on
// self-play data — the engine and the tuner share this exact code path, so
// the fitted model can never drift from what the engine computes.
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include "bitboard.h"
#include "evaltables.h"
#include "position.h"

namespace tacturn {

/** Per-thread pawn hash entry (16K entries; keyed by pawnKey).
 *  v1.4.3: also caches the KING-POSITION eval terms (shelter, pawn storm,
 *  king-to-pawn file proximity, king on open file) — every one of those
 *  depends only on (wp, bp, wk, bk, phase>6), so an entry whose key, king
 *  squares and phase gate all match lets evaluate() skip ~100 lines of
 *  bitboard work. F_KING_DEFENDERS (knight/bishop placement) and
 *  F_PAWN_BLOCKED (full occupancy) are NOT king-position terms and stay
 *  live in evaluate(). */
struct PawnEntry {
    Bitboard key = 0;
    int mg = 0, eg = 0;
    Bitboard wPass = 0, bPass = 0;
    uint8_t wkSq = 0, bkSq = 0;   // king squares the cached terms used
    uint8_t kingGate = 0;         // 1 = the phase > 6 shelter gate was ON
    int16_t kpMg = 0, kpEg = 0;   // king-position tapered term sum
    bool kingValid = false;
};

class PawnHashTable {
public:
    static constexpr int BITS = 14;
    static constexpr int SIZE = 1 << BITS;
    static constexpr int MASK = SIZE - 1;

    PawnEntry *probe(Bitboard pawnKey) { return &table_[pawnKey & MASK]; }

private:
    PawnEntry table_[SIZE];
};

/** Feature amounts emitted for the tuner (white POV). */
struct EvalTrace {
    int32_t mgAmt[EV_NUM_FEATURES];
    int32_t egAmt[EV_NUM_FEATURES];
    int preScaleScore = 0;  // linear (pre-scaling) tapered score, white POV
    bool scaled = false;    // endgame material scaling applied (tuner skips)
};

/** Set by the tuner: bypass the pawn hash so amounts are always emitted. */
extern bool evalPawnHashDisabled;

/** Evaluates the position from the side-to-move's POV (centipawns).
 *  When `trace` is non-null, accumulate tuner feature amounts. */
int evaluate(const Position &b, PawnHashTable &pawnHash, EvalTrace *trace = nullptr);

/** Mirrors a FEN (flip ranks + swap case + swap side) for symmetry tests. */
std::string mirrorFen(const std::string &fen);

}  // namespace tacturn
