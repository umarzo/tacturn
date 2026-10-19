// TacTurn Chess Engine v1 — Static Exchange Evaluation (port of See.kt).

#pragma once

#include "position.h"

namespace tacturn {

/** SEE score of `move` from the moving side's POV (may be negative). */
int see(const Position &b, Move move);

/** v1.1: early-exit sign test — is see(move) >= threshold? Exact decision,
 *  no full exchange walk (see see.cpp). Used by move ordering. */
bool seeGE(const Position &b, Move move, int threshold);

}  // namespace tacturn
