// TacTurn Chess Engine v1 — move generation implementation.
//
// v1.4.25 (B2): in capturesOnly mode the single-push set is pre-masked with
// promoRank (every non-promotion push was skipped by the loop body anyway)
// and the bishop/rook/queen capture generation is gated on the empty-board
// pseudo rays — sliderAttacks(sq, occ) is a subset of *_PSEUDO[sq], so an
// empty (targets & PSEUDO) intersection proves the generated capture set is
// empty too and the table lookup is skipped. Identical move lists.

#include "movegen.h"

namespace tacturn {

int generate(const Position &b, Move *out, bool capturesOnly) {
    int n = 0;
    int us = b.sideToMove;
    Bitboard own = b.colorBb[us];
    Bitboard opp = b.colorBb[us ^ 1];
    Bitboard occ = b.occupied;
    Bitboard notOwn = ~own;
    Bitboard empty = ~occ;
    Bitboard pawns = b.pieceBb[PAWN] & own;

    // ------------------------------------------------------------------
    // Pawns
    // ------------------------------------------------------------------
    if (us == WHITE) {
        Bitboard promoRank = RANKS_BB[7];

        // Single pushes.
        Bitboard t = (pawns << 8) & empty;
        // v1.4.25 (B2): in capturesOnly mode only promotion pushes can be
        // emitted — pre-mask instead of testing every pushed square.
        if (capturesOnly) t &= promoRank;
        while (t) {
            int to = lsb(t);
            t &= t - 1;
            int from = to - 8;
            if (promoRank & bitOf(to)) {
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, QUEEN);
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, ROOK);
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, BISHOP);
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, KNIGHT);
            } else if (!capturesOnly) {
                out[n++] = encodeMove(from, to, PAWN);
            }
        }

        // Double pushes.
        if (!capturesOnly) {
            t = (((pawns & RANKS_BB[1]) << 8) & empty) << 8 & empty;
            while (t) {
                int to = lsb(t);
                t &= t - 1;
                out[n++] = encodeMove(to - 16, to, PAWN, NO_PIECE, 0, FLAG_DOUBLE);
            }
        }

        // Captures (up-left: from = to - 7).
        Bitboard cap = ((pawns & NOT_FILE_A_BB) << 7) & opp;
        while (cap) {
            int to = lsb(cap);
            cap &= cap - 1;
            int from = to - 7;
            int victim = b.pieceTypeOf(to);
            if (promoRank & bitOf(to)) {
                out[n++] = encodeMove(from, to, PAWN, victim, QUEEN);
                out[n++] = encodeMove(from, to, PAWN, victim, ROOK);
                out[n++] = encodeMove(from, to, PAWN, victim, BISHOP);
                out[n++] = encodeMove(from, to, PAWN, victim, KNIGHT);
            } else {
                out[n++] = encodeMove(from, to, PAWN, victim);
            }
        }
        // Captures (up-right: from = to - 9).
        cap = ((pawns & NOT_FILE_H_BB) << 9) & opp;
        while (cap) {
            int to = lsb(cap);
            cap &= cap - 1;
            int from = to - 9;
            int victim = b.pieceTypeOf(to);
            if (promoRank & bitOf(to)) {
                out[n++] = encodeMove(from, to, PAWN, victim, QUEEN);
                out[n++] = encodeMove(from, to, PAWN, victim, ROOK);
                out[n++] = encodeMove(from, to, PAWN, victim, BISHOP);
                out[n++] = encodeMove(from, to, PAWN, victim, KNIGHT);
            } else {
                out[n++] = encodeMove(from, to, PAWN, victim);
            }
        }

        // En passant.
        int ep = b.epSquare;
        if (ep != NO_SQUARE) {
            Bitboard attackers = pawns & PAWN_ATT[BLACK][ep];
            while (attackers) {
                int from = lsb(attackers);
                attackers &= attackers - 1;
                out[n++] = encodeMove(from, ep, PAWN, PAWN, 0, FLAG_EP);
            }
        }
    } else {
        Bitboard promoRank = RANKS_BB[0];

        Bitboard t = (pawns >> 8) & empty;
        // v1.4.25 (B2): see the White branch.
        if (capturesOnly) t &= promoRank;
        while (t) {
            int to = lsb(t);
            t &= t - 1;
            int from = to + 8;
            if (promoRank & bitOf(to)) {
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, QUEEN);
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, ROOK);
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, BISHOP);
                out[n++] = encodeMove(from, to, PAWN, NO_PIECE, KNIGHT);
            } else if (!capturesOnly) {
                out[n++] = encodeMove(from, to, PAWN);
            }
        }

        if (!capturesOnly) {
            t = (((pawns & RANKS_BB[6]) >> 8) & empty) >> 8 & empty;
            while (t) {
                int to = lsb(t);
                t &= t - 1;
                out[n++] = encodeMove(to + 16, to, PAWN, NO_PIECE, 0, FLAG_DOUBLE);
            }
        }

        Bitboard cap = ((pawns & NOT_FILE_A_BB) >> 9) & opp;
        while (cap) {
            int to = lsb(cap);
            cap &= cap - 1;
            int from = to + 9;
            int victim = b.pieceTypeOf(to);
            if (promoRank & bitOf(to)) {
                out[n++] = encodeMove(from, to, PAWN, victim, QUEEN);
                out[n++] = encodeMove(from, to, PAWN, victim, ROOK);
                out[n++] = encodeMove(from, to, PAWN, victim, BISHOP);
                out[n++] = encodeMove(from, to, PAWN, victim, KNIGHT);
            } else {
                out[n++] = encodeMove(from, to, PAWN, victim);
            }
        }
        cap = ((pawns & NOT_FILE_H_BB) >> 7) & opp;
        while (cap) {
            int to = lsb(cap);
            cap &= cap - 1;
            int from = to + 7;
            int victim = b.pieceTypeOf(to);
            if (promoRank & bitOf(to)) {
                out[n++] = encodeMove(from, to, PAWN, victim, QUEEN);
                out[n++] = encodeMove(from, to, PAWN, victim, ROOK);
                out[n++] = encodeMove(from, to, PAWN, victim, BISHOP);
                out[n++] = encodeMove(from, to, PAWN, victim, KNIGHT);
            } else {
                out[n++] = encodeMove(from, to, PAWN, victim);
            }
        }

        int ep = b.epSquare;
        if (ep != NO_SQUARE) {
            Bitboard attackers = pawns & PAWN_ATT[WHITE][ep];
            while (attackers) {
                int from = lsb(attackers);
                attackers &= attackers - 1;
                out[n++] = encodeMove(from, ep, PAWN, PAWN, 0, FLAG_EP);
            }
        }
    }

    // ------------------------------------------------------------------
    // Knights, king, sliders
    // ------------------------------------------------------------------
    Bitboard pieces = b.pieceBb[KNIGHT] & own;
    while (pieces) {
        int sq = lsb(pieces);
        pieces &= pieces - 1;
        Bitboard att = KNIGHT_ATT[sq] & notOwn;
        if (capturesOnly) att &= opp;
        while (att) {
            int to = lsb(att);
            att &= att - 1;
            out[n++] = encodeMove(sq, to, KNIGHT, b.pieceTypeOf(to));
        }
    }

    pieces = b.pieceBb[BISHOP] & own;
    while (pieces) {
        int sq = lsb(pieces);
        pieces &= pieces - 1;
        Bitboard att;
        if (capturesOnly) {
            // v1.4.25 (B2): pseudo-ray gate — skip the lookup when no enemy
            // piece sits on this bishop's empty-board rays.
            if (!(opp & BISHOP_PSEUDO[sq])) continue;
            att = bishopAttacks(sq, occ) & notOwn & opp;
        } else {
            att = bishopAttacks(sq, occ) & notOwn;
        }
        while (att) {
            int to = lsb(att);
            att &= att - 1;
            out[n++] = encodeMove(sq, to, BISHOP, b.pieceTypeOf(to));
        }
    }

    pieces = b.pieceBb[ROOK] & own;
    while (pieces) {
        int sq = lsb(pieces);
        pieces &= pieces - 1;
        Bitboard att;
        if (capturesOnly) {
            // v1.4.25 (B2): pseudo-ray gate (see the bishop loop).
            if (!(opp & ROOK_PSEUDO[sq])) continue;
            att = rookAttacks(sq, occ) & notOwn & opp;
        } else {
            att = rookAttacks(sq, occ) & notOwn;
        }
        while (att) {
            int to = lsb(att);
            att &= att - 1;
            out[n++] = encodeMove(sq, to, ROOK, b.pieceTypeOf(to));
        }
    }

    pieces = b.pieceBb[QUEEN] & own;
    while (pieces) {
        int sq = lsb(pieces);
        pieces &= pieces - 1;
        Bitboard att;
        if (capturesOnly) {
            // v1.4.25 (B2): a queen capture needs a target on one of her
            // two pseudo rays; each lookup is gated on its own ray so a
            // blocked-line queen pays only the ray that can hit.
            Bitboard rRay = opp & ROOK_PSEUDO[sq];
            Bitboard bRay = opp & BISHOP_PSEUDO[sq];
            if (!rRay && !bRay) continue;
            att = ((rRay ? rookAttacks(sq, occ) : 0) |
                   (bRay ? bishopAttacks(sq, occ) : 0)) & notOwn & opp;
        } else {
            att = queenAttacks(sq, occ) & notOwn;
        }
        while (att) {
            int to = lsb(att);
            att &= att - 1;
            out[n++] = encodeMove(sq, to, QUEEN, b.pieceTypeOf(to));
        }
    }

    pieces = b.pieceBb[KING] & own;
    while (pieces) {
        int sq = lsb(pieces);
        pieces &= pieces - 1;
        Bitboard att = KING_ATT[sq] & notOwn;
        if (capturesOnly) att &= opp;
        while (att) {
            int to = lsb(att);
            att &= att - 1;
            out[n++] = encodeMove(sq, to, KING, b.pieceTypeOf(to));
        }
    }

    // ------------------------------------------------------------------
    // Castling (fully legal at generation time)
    // ------------------------------------------------------------------
    // v1.4.27 (Prev-B4): castling is illegal while in check — the per-wing
    // isAttacked(kingSq) tests below discovered that the expensive way
    // (after the rights/placement/occupancy checks). checkers() is a
    // stored field (computed at make time), so this gate is one load:
    // in-check positions never generated castle moves before either, so
    // the generated move set is bit-identical.
    if (!capturesOnly && b.checkers() == 0) {
        if (us == WHITE) {
            if ((b.castlingRights & CASTLE_WK) &&
                b.pieceTypeOf(4) == KING && b.pieceColorOf(4) == WHITE &&
                b.pieceTypeOf(7) == ROOK && b.pieceColorOf(7) == WHITE &&
                (occ & 0x60ULL) == 0 &&
                !b.isAttacked(4, BLACK) && !b.isAttacked(5, BLACK) && !b.isAttacked(6, BLACK)) {
                out[n++] = encodeMove(4, 6, KING, NO_PIECE, 0, FLAG_CASTLE_K);
            }
            if ((b.castlingRights & CASTLE_WQ) &&
                b.pieceTypeOf(4) == KING && b.pieceColorOf(4) == WHITE &&
                b.pieceTypeOf(0) == ROOK && b.pieceColorOf(0) == WHITE &&
                (occ & 0x0EULL) == 0 &&
                !b.isAttacked(4, BLACK) && !b.isAttacked(3, BLACK) && !b.isAttacked(2, BLACK)) {
                out[n++] = encodeMove(4, 2, KING, NO_PIECE, 0, FLAG_CASTLE_Q);
            }
        } else {
            if ((b.castlingRights & CASTLE_BK) &&
                b.pieceTypeOf(60) == KING && b.pieceColorOf(60) == BLACK &&
                b.pieceTypeOf(63) == ROOK && b.pieceColorOf(63) == BLACK &&
                (occ & 0x6000000000000000ULL) == 0 &&
                !b.isAttacked(60, WHITE) && !b.isAttacked(61, WHITE) && !b.isAttacked(62, WHITE)) {
                out[n++] = encodeMove(60, 62, KING, NO_PIECE, 0, FLAG_CASTLE_K);
            }
            if ((b.castlingRights & CASTLE_BQ) &&
                b.pieceTypeOf(60) == KING && b.pieceColorOf(60) == BLACK &&
                b.pieceTypeOf(56) == ROOK && b.pieceColorOf(56) == BLACK &&
                (occ & 0x0E00000000000000ULL) == 0 &&
                !b.isAttacked(60, WHITE) && !b.isAttacked(59, WHITE) && !b.isAttacked(58, WHITE)) {
                out[n++] = encodeMove(60, 58, KING, NO_PIECE, 0, FLAG_CASTLE_Q);
            }
        }
    }

    return n;
}

int generateLegal(Position &b, Move *out) {
    Move buf[MAX_MOVES];
    int n = generate(b, buf);
    int m = 0;
    int mover = b.sideToMove;
    for (int i = 0; i < n; i++) {
        b.makeMove(buf[i]);
        if (!b.inCheck(mover)) out[m++] = buf[i];
        b.unmakeMove();
    }
    return m;
}

// v1.4.28 (B1): O(1) membership test of the pseudo-legal generator's
// output set (see movegen.h). Every rule below mirrors generate()'s exact
// emission conditions, including capturesOnly's stricter shape (quiet
// pushes / non-promotion quiets / castling excluded; promotion pushes and
// all captures kept). Sliding geometry uses the empty-board pseudo rays +
// the BETWEEN blocker mask — no magic/PEXT lookups.
namespace {
inline bool pawnPromoRank(int us, int to) {
    return us == WHITE ? (to >> 3) == 7 : (to >> 3) == 0;
}
}  // namespace

bool isPseudoLegal(const Position &b, Move m, bool capturesOnly) {
    if (m == MOVE_NONE) return false;
    const int us = b.sideToMove;
    const int them = us ^ 1;
    const int from = moveFrom(m);
    const int to = moveTo(m);
    const int pc = movePiece(m);
    const int cap = moveCaptured(m);
    const int promo = movePromo(m);
    const uint32_t flags = m & (FLAG_EP | FLAG_CASTLE_K | FLAG_CASTLE_Q | FLAG_DOUBLE);
    const Bitboard fromBb = bitOf(from);
    const Bitboard toBb = bitOf(to);

    // The mover must be OUR piece of the encoded type.
    if (!(b.colorBb[us] & b.pieceBb[pc] & fromBb)) return false;

    // ---- Castling (generated fully legal) ----
    if (flags & (FLAG_CASTLE_K | FLAG_CASTLE_Q)) {
        if (capturesOnly || pc != KING || cap != NO_PIECE || promo != 0)
            return false;
        if (b.checkers() != 0) return false;
        if (flags == FLAG_CASTLE_K) {
            if (us == WHITE) {
                if (from != 4 || to != 6) return false;
                if (!(b.castlingRights & CASTLE_WK)) return false;
                if (b.occupied & ((1ULL << 5) | (1ULL << 6))) return false;
                if (b.isAttacked(4, BLACK) || b.isAttacked(5, BLACK) ||
                    b.isAttacked(6, BLACK))
                    return false;
                return true;
            } else {
                if (from != 60 || to != 62) return false;
                if (!(b.castlingRights & CASTLE_BK)) return false;
                if (b.occupied & ((1ULL << 61) | (1ULL << 62))) return false;
                if (b.isAttacked(60, WHITE) || b.isAttacked(61, WHITE) ||
                    b.isAttacked(62, WHITE))
                    return false;
                return true;
            }
        } else if (flags == FLAG_CASTLE_Q) {
            if (us == WHITE) {
                if (from != 4 || to != 2) return false;
                if (!(b.castlingRights & CASTLE_WQ)) return false;
                if (b.occupied & ((1ULL << 1) | (1ULL << 2) | (1ULL << 3))) return false;
                if (b.isAttacked(4, BLACK) || b.isAttacked(3, BLACK) ||
                    b.isAttacked(2, BLACK))
                    return false;
                return true;
            } else {
                if (from != 60 || to != 58) return false;
                if (!(b.castlingRights & CASTLE_BQ)) return false;
                if (b.occupied & ((1ULL << 57) | (1ULL << 58) | (1ULL << 59)))
                    return false;
                if (b.isAttacked(60, WHITE) || b.isAttacked(59, WHITE) ||
                    b.isAttacked(58, WHITE))
                    return false;
                return true;
            }
        }
        return false;
    }

    // ---- En passant ----
    if (flags & FLAG_EP) {
        if (pc != PAWN || promo != 0 || cap != PAWN || flags != FLAG_EP)
            return false;
        if (to != b.epSquare || b.epSquare == NO_SQUARE) return false;
        return (PAWN_ATT[us][from] & toBb) != 0;
    }

    // ---- Plain moves (no castle/EP flags; DOUBLE only on pawn pushes) ----
    if (flags & FLAG_DOUBLE) {
        if (pc != PAWN || cap != NO_PIECE || promo != 0) return false;
    }

    // ---- Target occupancy / victim consistency ----
    if (cap != NO_PIECE) {
        // A capture must land on an ENEMY piece of the encoded type.
        if (!(b.colorBb[them] & b.pieceBb[cap] & toBb)) return false;
    } else {
        // A quiet must land on an EMPTY square.
        if (b.occupied & toBb) return false;
        // capturesOnly emits only captures, EP and PROMOTIONS; a quiet
        // survives only as a promotion push (pawn, promo-rank target).
        if (capturesOnly && !(pc == PAWN && promo != 0 && pawnPromoRank(us, to)))
            return false;
    }

    // ---- Per-piece attack geometry ----
    if (pc == PAWN) {
        const bool promoRank = pawnPromoRank(us, to);
        if (promoRank) {
            if (promo != QUEEN && promo != ROOK && promo != BISHOP && promo != KNIGHT)
                return false;
        } else {
            if (promo != 0) return false;
        }
        if (cap != NO_PIECE) {
            return (PAWN_ATT[us][from] & toBb) != 0;
        }
        const int push1 = (us == WHITE ? from + 8 : from - 8);
        if (to == push1 && !(flags & FLAG_DOUBLE)) return true;  // empty: above
        const int startRank = (us == WHITE ? 1 : 6);
        const int push2 = (us == WHITE ? from + 16 : from - 16);
        if ((flags & FLAG_DOUBLE) && (from >> 3) == startRank && to == push2) {
            // generate() emits the double push only when BOTH squares were
            // empty; the target's emptiness is already verified above.
            return (b.occupied & bitOf(push1)) == 0;
        }
        return false;
    }
    if (promo != 0) return false;
    if (flags & FLAG_DOUBLE) return false;
    switch (pc) {
    case KNIGHT: return (KNIGHT_ATT[from] & toBb) != 0;
    case KING:   return (KING_ATT[from] & toBb) != 0;
    case BISHOP: return (BISHOP_PSEUDO[from] & toBb) &&
                          !(BETWEEN_BB[from][to] & b.occupied);
    case ROOK:   return (ROOK_PSEUDO[from] & toBb) &&
                          !(BETWEEN_BB[from][to] & b.occupied);
    case QUEEN:  return ((BISHOP_PSEUDO[from] | ROOK_PSEUDO[from]) & toBb) &&
                          !(BETWEEN_BB[from][to] & b.occupied);
    default: return false;
    }
}

// ------------------------------------------------------------------
// Perft (declared in position.h)
// ------------------------------------------------------------------

namespace {
Move perftBuffers[32][MAX_MOVES];
}

uint64_t perft(Position &b, int depth) {
    if (depth <= 0) return 1;
    if (depth == 1) {
        // Bulk-count at depth 1 via the legality filter (fast path).
        int n = generate(b, perftBuffers[0]);
        int mover = b.sideToMove;
        uint64_t nodes = 0;
        for (int i = 0; i < n; i++) {
            b.makeMove(perftBuffers[0][i]);
            if (!b.inCheck(mover)) nodes++;
            b.unmakeMove();
        }
        return nodes;
    }
    Move *buffer = perftBuffers[depth];
    int n = generate(b, buffer);
    uint64_t nodes = 0;
    int mover = b.sideToMove;
    for (int i = 0; i < n; i++) {
        b.makeMove(buffer[i]);
        if (!b.inCheck(mover)) nodes += perft(b, depth - 1);
        b.unmakeMove();
    }
    return nodes;
}

void perftDivide(Position &b, int depth, std::vector<PerftDivideEntry> &out) {
    out.clear();
    Move buffer[MAX_MOVES];
    int n = generate(b, buffer);
    int mover = b.sideToMove;
    for (int i = 0; i < n; i++) {
        b.makeMove(buffer[i]);
        if (!b.inCheck(mover)) {
            PerftDivideEntry e;
            e.move = buffer[i];
            e.nodes = depth <= 1 ? 1 : perft(b, depth - 1);
            out.push_back(e);
        }
        b.unmakeMove();
    }
}

}  // namespace tacturn
