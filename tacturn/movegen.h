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

}  // namespace tacturn
