// TacTurn Chess Engine v1 — pseudo-legal move generation.
//
// Faithful port of MoveGen.kt: pseudo-legal generation with the captured
// piece encoded into every move; the search/roots validate legality via
// make + in-check test (see generateLegal for the wrapper used by the
// game layer). Castling is generated fully legal. capturesOnly mode (used
// by quiescence) emits captures, en-passant and all promotions.
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include "position.h"

namespace tacturn {

/** Pseudo-legal moves; returns count. */
int generate(const Position &b, Move *out, bool capturesOnly = false);

/** Fully-legal moves; returns count. */
int generateLegal(Position &b, Move *out);

/** v1.4.28 (B1): O(1) test that `m` is a member of the pseudo-legal set
 *  generate(b, ..., capturesOnly) would emit — WITHOUT running the
 *  generator. Validates the mover's presence, flags, target occupancy,
 *  attack geometry (sliders via the empty-board pseudo rays + the
 *  BETWEEN blocker mask — no magic/PEXT lookups), pawn push/capture
 *  shapes and promotion consistency. Used by the search to try the TT
 *  move BEFORE calling generate(); a cutoff then skips the whole
 *  generation pass (~70% of TT-move nodes). */
bool isPseudoLegal(const Position &b, Move m, bool capturesOnly = false);

}  // namespace tacturn
