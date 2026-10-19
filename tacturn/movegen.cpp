// TacTurn Chess Engine v1 — move generation implementation.

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
        Bitboard att = bishopAttacks(sq, occ) & notOwn;
        if (capturesOnly) att &= opp;
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
        Bitboard att = rookAttacks(sq, occ) & notOwn;
        if (capturesOnly) att &= opp;
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
        Bitboard att = queenAttacks(sq, occ) & notOwn;
        if (capturesOnly) att &= opp;
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
    if (!capturesOnly) {
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
