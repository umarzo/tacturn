// TacTurn Chess Engine v1 — Static Exchange Evaluation (port of See.kt).
//
// Recursive swap algorithm: each side may capture the piece standing on the
// target square or decline; the value for the declining side is zero. Piece
// removals update the occupancy so x-ray attackers appear automatically.

#include "see.h"

namespace tacturn {

namespace {

constexpr int MAX_EXCHANGE_DEPTH = 32;

int leastValuableAttacker(const Position &b, int side, int to, Bitboard occ) {
    Bitboard own = b.colorBb[side] & occ;
    Bitboard attackers;

    attackers = PAWN_ATT[side ^ 1][to] & b.pieceBb[PAWN] & own;
    if (attackers) return lsb(attackers);

    attackers = KNIGHT_ATT[to] & b.pieceBb[KNIGHT] & own;
    if (attackers) return lsb(attackers);

    attackers = bishopAttacks(to, occ) & b.pieceBb[BISHOP] & own;
    if (attackers) return lsb(attackers);

    attackers = rookAttacks(to, occ) & b.pieceBb[ROOK] & own;
    if (attackers) return lsb(attackers);

    attackers = (bishopAttacks(to, occ) | rookAttacks(to, occ)) & b.pieceBb[QUEEN] & own;
    if (attackers) return lsb(attackers);

    attackers = KING_ATT[to] & b.pieceBb[KING] & own;
    if (attackers) return lsb(attackers);

    return -1;
}

int captureValue(const Position &b, Bitboard occIn, int side, int to, int valueOnTo, int depth) {
    if (depth == 0) return 0;
    int from = leastValuableAttacker(b, side, to, occIn);
    if (from < 0) return 0;
    int attackerValue = PIECE_VALUES[b.pieceTypeOf(from)];
    int cont = captureValue(b, occIn & ~bitOf(from), side ^ 1, to, attackerValue, depth - 1);
    int v = valueOnTo - cont;
    return v > 0 ? v : 0;
}

// ------------------------------------------------------------------
// v1.1: sign-only SEE with early exit (move-ordering helper).
//
// captureValue (above) is the negamax exchange recursion
//     CV(side, to, valOnTo) = max(0, valOnTo − CV(side^1, to, attVal))
// and see(move) = victimValue − CV(opp, to, landedValue). For ordering we
// only need "see(move) >= threshold", i.e.
//     CV(opp, to, landedValue) <= victimValue − threshold.
// The dual predicates below prove <= / >= WITHOUT materializing the value:
// every level eliminates the recursion as soon as the comparison is
// decided (free capture, cheap-enough landing piece, no recapturer, or an
// impossible requirement), so the common cases resolve in 1-2 attacker
// lookups instead of a full exchange walk. Exactness: each level applies
// the same max-with-0/decline algebra as captureValue, so the decision is
// provably identical to `see(b, m) >= threshold`.
// ------------------------------------------------------------------
bool cvGE(const Position &b, Bitboard occ, int side, int to, int valueOnTo, int limit);

bool cvLE(const Position &b, Bitboard occ, int side, int to, int valueOnTo, int limit) {
    if (limit < 0) return false;          // CV >= 0 > limit
    if (valueOnTo <= limit) return true;  // max(0, val−cont) <= val <= limit
    int from = leastValuableAttacker(b, side, to, occ);
    if (from < 0) return true;            // CV = 0 <= limit
    int attackerValue = PIECE_VALUES[b.pieceTypeOf(from)];
    // need cont >= valueOnTo − limit (> 0, so cont's own max-with-0 never fires)
    return cvGE(b, occ & ~bitOf(from), side ^ 1, to, attackerValue, valueOnTo - limit);
}

bool cvGE(const Position &b, Bitboard occ, int side, int to, int valueOnTo, int limit) {
    if (limit <= 0) return true;          // CV >= 0 >= limit
    int from = leastValuableAttacker(b, side, to, occ);
    if (from < 0) return false;           // CV = 0 < limit
    int attackerValue = PIECE_VALUES[b.pieceTypeOf(from)];
    // need max(0, valueOnTo − cont) >= limit  (limit > 0 → need valueOnTo − cont >= limit)
    return cvLE(b, occ & ~bitOf(from), side ^ 1, to, attackerValue, valueOnTo - limit);
}

}  // namespace

int see(const Position &b, Move move) {
    int to = moveTo(move);
    int from = moveFrom(move);
    int promo = movePromo(move);
    int mover = b.sideToMove;

    Bitboard occ = b.occupied & ~bitOf(from);
    if (isEPMove(move)) {
        int capSq = mover == WHITE ? to - 8 : to + 8;
        occ &= ~bitOf(capSq);
    }

    int victimValue = moveCaptured(move) == NO_PIECE ? 0 : PIECE_VALUES[moveCaptured(move)];
    if (promo != 0) victimValue += PIECE_VALUES[promo] - PIECE_VALUES[PAWN];

    int landedValue = PIECE_VALUES[promo != 0 ? promo : movePiece(move)];

    return victimValue - captureValue(b, occ, mover ^ 1, to, landedValue, MAX_EXCHANGE_DEPTH);
}

bool seeGE(const Position &b, Move move, int threshold) {
    int to = moveTo(move);
    int from = moveFrom(move);
    int promo = movePromo(move);
    int mover = b.sideToMove;

    int victimValue = moveCaptured(move) == NO_PIECE ? 0 : PIECE_VALUES[moveCaptured(move)];
    if (promo != 0) victimValue += PIECE_VALUES[promo] - PIECE_VALUES[PAWN];
    // Even winning the victim for free cannot clear the threshold.
    if (victimValue < threshold) return false;

    Bitboard occ = b.occupied & ~bitOf(from);
    if (isEPMove(move)) {
        int capSq = mover == WHITE ? to - 8 : to + 8;
        occ &= ~bitOf(capSq);
    }

    int landedValue = PIECE_VALUES[promo != 0 ? promo : movePiece(move)];

    // see >= threshold  ⟺  CV(opp, to, landed) <= victimValue − threshold
    return cvLE(b, occ, mover ^ 1, to, landedValue, victimValue - threshold);
}

}  // namespace tacturn
