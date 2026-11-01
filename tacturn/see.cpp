// TacTurn Chess Engine v1 — Static Exchange Evaluation (port of See.kt).
//
// Recursive swap algorithm: each side may capture the piece standing on the
// target square or decline; the value for the declining side is zero. Piece
// removals update the occupancy so x-ray attackers appear automatically.
//
// v1.4.25 (B2): leastValuableAttacker gates both slider lookups on
// BISHOP_PSEUDO / ROOK_PSEUDO overlap (sliderAttacks(to, occ) is a subset of
// the empty-board ray, so an empty pseudo intersection proves the real
// intersection is empty too), reuses the computed attack bitboards for the
// queen test instead of re-invoking bishopAttacks/rookAttacks with the exact
// same (to, occ), and returns the attacker's value directly through attVal —
// the callers previously re-derived it via pieceTypeOf + PIECE_VALUES even
// though the piece type was already known here. All decisions identical.

#include "see.h"

namespace tacturn {

namespace {

constexpr int MAX_EXCHANGE_DEPTH = 32;

int leastValuableAttacker(const Position &b, int side, int to, Bitboard occ,
                          int &attVal) {
    Bitboard own = b.colorBb[side] & occ;
    Bitboard attackers;

    attackers = PAWN_ATT[side ^ 1][to] & b.pieceBb[PAWN] & own;
    if (attackers) { attVal = PIECE_VALUES[PAWN]; return lsb(attackers); }

    attackers = KNIGHT_ATT[to] & b.pieceBb[KNIGHT] & own;
    if (attackers) { attVal = PIECE_VALUES[KNIGHT]; return lsb(attackers); }

    Bitboard queens = b.pieceBb[QUEEN] & own;
    Bitboard bAtt = 0;
    if (((b.pieceBb[BISHOP] & own) | queens) & BISHOP_PSEUDO[to]) {
        bAtt = bishopAttacks(to, occ);
        attackers = bAtt & b.pieceBb[BISHOP] & own;
        if (attackers) { attVal = PIECE_VALUES[BISHOP]; return lsb(attackers); }
    }

    Bitboard rAtt = 0;
    if (((b.pieceBb[ROOK] & own) | queens) & ROOK_PSEUDO[to]) {
        rAtt = rookAttacks(to, occ);
        attackers = rAtt & b.pieceBb[ROOK] & own;
        if (attackers) { attVal = PIECE_VALUES[ROOK]; return lsb(attackers); }
    }

    attackers = (bAtt | rAtt) & queens;
    if (attackers) { attVal = PIECE_VALUES[QUEEN]; return lsb(attackers); }

    attackers = KING_ATT[to] & b.pieceBb[KING] & own;
    if (attackers) { attVal = PIECE_VALUES[KING]; return lsb(attackers); }

    return -1;
}

int captureValue(const Position &b, Bitboard occIn, int side, int to, int valueOnTo, int depth) {
    if (depth == 0) return 0;
    int attVal = 0;
    int from = leastValuableAttacker(b, side, to, occIn, attVal);
    if (from < 0) return 0;
    int attackerValue = attVal;
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
    int attVal = 0;
    int from = leastValuableAttacker(b, side, to, occ, attVal);
    if (from < 0) return true;            // CV = 0 <= limit
    int attackerValue = attVal;
    // need cont >= valueOnTo − limit (> 0, so cont's own max-with-0 never fires)
    return cvGE(b, occ & ~bitOf(from), side ^ 1, to, attackerValue, valueOnTo - limit);
}

bool cvGE(const Position &b, Bitboard occ, int side, int to, int valueOnTo, int limit) {
    if (limit <= 0) return true;          // CV >= 0 >= limit
    // v1.4.27 (B1): upper-bound early exit, the dual of cvLE's line-2 test.
    // CV = max(0, valueOnTo - cont) with cont >= 0, so CV <= valueOnTo —
    // when valueOnTo < limit the comparison is mathematically impossible.
    // The old code ran the full 6-piece leastValuableAttacker bitboard +
    // slider lookup and then handed cvLE a negative limit (valueOnTo -
    // limit < 0) which cvLE rejected on its own first line — the entire
    // attacker walk was dead work (-16% leastValuableAttacker calls on
    // bench 14). Exactness: a necessary condition, decision-identical.
    if (valueOnTo < limit) return false;  // max(0, val-cont) <= val < limit
    int attVal = 0;
    int from = leastValuableAttacker(b, side, to, occ, attVal);
    if (from < 0) return false;           // CV = 0 < limit
    int attackerValue = attVal;
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
    // v1.4.27 (B1): pre-occupancy fast path. see >= threshold is decided
    // by limit = victimValue - threshold and landedValue alone in the two
    // extreme cases, before to / from / mover / occ / EP are touched:
    //   - limit < 0: even winning the victim for free cannot clear the
    //     threshold (the old `victimValue < threshold` test, kept first);
    //   - landedValue <= limit: CV(opp, to, landed) <= landedValue <=
    //     limit always holds (CV <= the value standing on the square), so
    //     the exchange can never cost more than the threshold allows —
    //     every equal-or-winning capture at threshold <= 0 resolves here
    //     in 3 register operations with no bitboard work at all.
    // Exactness: both are necessary/sufficient bounds of the same algebra
    // (fuzzsee-proven).
    int promo = movePromo(move);
    int victimValue = moveCaptured(move) == NO_PIECE ? 0 : PIECE_VALUES[moveCaptured(move)];
    if (promo != 0) victimValue += PIECE_VALUES[promo] - PIECE_VALUES[PAWN];

    // Even winning the victim for free cannot clear the threshold.
    int limit = victimValue - threshold;
    if (limit < 0) return false;

    int landedValue = PIECE_VALUES[promo != 0 ? promo : movePiece(move)];

    // Even losing the landed piece for free still clears the threshold.
    if (landedValue <= limit) return true;

    int to = moveTo(move);
    int from = moveFrom(move);
    int mover = b.sideToMove;

    Bitboard occ = b.occupied & ~bitOf(from);
    if (isEPMove(move)) {
        int capSq = mover == WHITE ? to - 8 : to + 8;
        occ &= ~bitOf(capSq);
    }

    // see >= threshold  ⟺  CV(opp, to, landed) <= victimValue − threshold
    return cvLE(b, occ, mover ^ 1, to, landedValue, limit);
}

}  // namespace tacturn
