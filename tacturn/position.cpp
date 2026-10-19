// TacTurn Chess Engine v1 — position implementation (port of Board.kt).

#include "position.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include "evaltables.h"

namespace tacturn {

const char *Position::START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Position::Position() {
    initTables();
    initEvalTables();
    clear();
}

Position::Position(const Position &other) { *this = other; }

Position &Position::operator=(const Position &other) {
    if (this != &other) {
        memcpy(pieceBb, other.pieceBb, sizeof(pieceBb));
        memcpy(colorBb, other.colorBb, sizeof(colorBb));
        occupied = other.occupied;
        memcpy(pieceAt, other.pieceAt, sizeof(pieceAt));
        sideToMove = other.sideToMove;
        castlingRights = other.castlingRights;
        epSquare = other.epSquare;
        halfmoveClock = other.halfmoveClock;
        fullmoveNumber = other.fullmoveNumber;
        key = other.key;
        pawnKey = other.pawnKey;
        nonPawnKey[0] = other.nonPawnKey[0];
        nonPawnKey[1] = other.nonPawnKey[1];
        mg[0] = other.mg[0]; mg[1] = other.mg[1];
        eg[0] = other.eg[0]; eg[1] = other.eg[1];
        phase = other.phase;
        // v6: do NOT deep-copy the 132 KB accumulator ring — invalidate it.
        // The first reader (nnueEnsureFresh) finds no valid slot and does a
        // single from-scratch refresh from the already-copied placement.
        memset(nnueRingValid, 0, sizeof(nnueRingValid));
        // v1.4.7: reuse the existing state-stack buffer whenever it is
        // already large enough — the old unconditional delete/new churned
        // one heap allocation per helper clone per move.
        if (stCap < other.stCount + MAX_PLY + 256) {
            delete[] st;
            stCap = other.stCount + MAX_PLY + 256;
            st = new StateInfo[stCap];
        }
        stCount = other.stCount;
        if (stCount > 0) memcpy(st, other.st, sizeof(StateInfo) * (size_t)stCount);
        startKey = other.startKey;
        checkersRoot_ = other.checkersRoot_;
    }
    return *this;
}

// v1.4.7: see position.h — steal the source's state stack instead of
// deep-copying it. Every other member is copied exactly as the copy-assign
// copies it (ring invalidated, Finny untouched), so a moved-into Position
// is bit-identical to a copy-assigned one.
Position::Position(Position &&other) noexcept { *this = std::move(other); }

Position &Position::operator=(Position &&other) noexcept {
    if (this != &other) {
        memcpy(pieceBb, other.pieceBb, sizeof(pieceBb));
        memcpy(colorBb, other.colorBb, sizeof(colorBb));
        occupied = other.occupied;
        memcpy(pieceAt, other.pieceAt, sizeof(pieceAt));
        sideToMove = other.sideToMove;
        castlingRights = other.castlingRights;
        epSquare = other.epSquare;
        halfmoveClock = other.halfmoveClock;
        fullmoveNumber = other.fullmoveNumber;
        key = other.key;
        pawnKey = other.pawnKey;
        nonPawnKey[0] = other.nonPawnKey[0];
        nonPawnKey[1] = other.nonPawnKey[1];
        mg[0] = other.mg[0]; mg[1] = other.mg[1];
        eg[0] = other.eg[0]; eg[1] = other.eg[1];
        phase = other.phase;
        memset(nnueRingValid, 0, sizeof(nnueRingValid));
        delete[] st;
        st = other.st;
        stCount = other.stCount;
        stCap = other.stCap;
        other.st = nullptr;
        other.stCount = 0;
        other.stCap = 0;
        startKey = other.startKey;
        checkersRoot_ = other.checkersRoot_;
    }
    return *this;
}

Position::~Position() { delete[] st; st = nullptr; }

// ------------------------------------------------------------------
// Piece placement helpers (keep all state coherent)
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// v1.4.1 bitboard-only placement helpers (unmakeMove fast path).
// See position.h — the derived scalars they skip are restored from the
// StateInfo snapshot immediately after, so the skip is bit-exact.
// ------------------------------------------------------------------

inline void Position::addPieceBb(int side, int type, int sq) {
    Bitboard b = bitOf(sq);
    pieceBb[type] |= b;
    colorBb[side] |= b;
    occupied |= b;
    pieceAt[sq] = (int8_t)((side << 3) | type);
}

inline void Position::removePieceBb(int side, int type, int sq) {
    Bitboard b = bitOf(sq);
    pieceBb[type] &= ~b;
    colorBb[side] &= ~b;
    occupied &= ~b;
    pieceAt[sq] = -1;
}

inline void Position::shiftPieceBb(int side, int type, int from, int to) {
    Bitboard b = bitOf(from) | bitOf(to);
    pieceBb[type] ^= b;
    colorBb[side] ^= b;
    occupied ^= b;
    pieceAt[from] = -1;
    pieceAt[to] = (int8_t)((side << 3) | type);
}

inline void Position::addPiece(int side, int type, int sq) {
    Bitboard b = bitOf(sq);
    pieceBb[type] |= b;
    colorBb[side] |= b;
    occupied |= b;
    pieceAt[sq] = (int8_t)((side << 3) | type);
    key ^= Z_PIECE[side * 6 + type][sq];
    if (type == PAWN) pawnKey ^= Z_PIECE[side * 6 + PAWN][sq];
    else nonPawnKey[side] ^= Z_PIECE[side * 6 + type][sq];
    if (side == WHITE) {
        mg[side] += PST_MG[type][sq];
        eg[side] += PST_EG[type][sq];
    } else {
        mg[side] += PST_MG_BLACK[type][sq];
        eg[side] += PST_EG_BLACK[type][sq];
    }
    phase += PHASE_WEIGHT[type];
    // v6: no accumulator maintenance here — the NNUE ops are recorded in
    // makeMove (see the lazy ring in position.h). setFen does its own full
    // refresh.
}

inline void Position::removePiece(int side, int type, int sq) {
    Bitboard b = bitOf(sq);
    pieceBb[type] &= ~b;
    colorBb[side] &= ~b;
    occupied &= ~b;
    pieceAt[sq] = -1;
    key ^= Z_PIECE[side * 6 + type][sq];
    if (type == PAWN) pawnKey ^= Z_PIECE[side * 6 + PAWN][sq];
    else nonPawnKey[side] ^= Z_PIECE[side * 6 + type][sq];
    if (side == WHITE) {
        mg[side] -= PST_MG[type][sq];
        eg[side] -= PST_EG[type][sq];
    } else {
        mg[side] -= PST_MG_BLACK[type][sq];
        eg[side] -= PST_EG_BLACK[type][sq];
    }
    phase -= PHASE_WEIGHT[type];
    // v6: no accumulator maintenance (lazy ring — see addPiece).
}

inline void Position::shiftPiece(int side, int type, int from, int to) {
    Bitboard b = bitOf(from) | bitOf(to);
    pieceBb[type] ^= b;
    colorBb[side] ^= b;
    occupied ^= b;
    pieceAt[from] = -1;
    pieceAt[to] = (int8_t)((side << 3) | type);
    key ^= Z_PIECE[side * 6 + type][from] ^ Z_PIECE[side * 6 + type][to];
    if (type == PAWN) {
        pawnKey ^= Z_PIECE[side * 6 + PAWN][from] ^ Z_PIECE[side * 6 + PAWN][to];
    } else {
        nonPawnKey[side] ^= Z_PIECE[side * 6 + type][from] ^ Z_PIECE[side * 6 + type][to];
    }
    if (side == WHITE) {
        mg[side] += PST_MG[type][to] - PST_MG[type][from];
        eg[side] += PST_EG[type][to] - PST_EG[type][from];
    } else {
        mg[side] += PST_MG_BLACK[type][to] - PST_MG_BLACK[type][from];
        eg[side] += PST_EG_BLACK[type][to] - PST_EG_BLACK[type][from];
    }
    // v6: no accumulator maintenance — king-move handling (bucket check,
    // op recording) lives in makeMove where the full move context exists.
}

// v6: materialize the accumulator for the current state-stack depth.
// Walk the ring DOWN from the current depth to the nearest slot whose key
// matches the position at that depth, then copy+apply the recorded ops
// down to the current ply. No valid ancestor within the ring -> one full
// refresh from the current placement (correct, rare). Dirty perspectives
// are refreshed from the placement on return (the reader semantics of
// v5.3's nnueEnsureFresh, unchanged).
// v9: prime the ring slot at the current depth from the placement. The
// search calls this once at the start of runThink (root). After priming,
// every nnueEnsureFresh walk terminates at the root instead of falling
// through to a full 64-row refresh when no ancestor was ever materialized
// (eval-cache-hit ancestors never fill their slot).
void Position::nnuePrimeRoot() {
    if (!nnue::enabled) return;
    const int slot = stCount & NNUE_RING_MASK;
    nnue::refresh(nnueRing[slot], *this);
    nnueRingValid[slot] = 1;
    nnueRingKey[slot] = keyAfter(stCount);
    nnueRingDirty[slot] = 0;
}

nnue::Accumulator *Position::nnueEnsureFresh() {
    if (!nnue::enabled) return nullptr;
    const int cur = stCount;
    const int slot = cur & NNUE_RING_MASK;
    const Bitboard curKey = keyAfter(cur);
    if (nnueRingValid[slot] && nnueRingKey[slot] == curKey) {
        if (nnueRingDirty[slot]) {
            if (nnueRingDirty[slot] & 1u)
                nnue::refreshPerspective(nnueRing[slot], *this, WHITE);
            if (nnueRingDirty[slot] & 2u)
                nnue::refreshPerspective(nnueRing[slot], *this, BLACK);
            nnueRingDirty[slot] = 0;
        }
        return &nnueRing[slot];
    }
    int base = -1;
    int lo = cur - (NNUE_RING - 1);
    if (lo < 0) lo = 0;
    for (int k = cur; k >= lo; k--) {
        int s = k & NNUE_RING_MASK;
        if (nnueRingValid[s] && nnueRingKey[s] == keyAfter(k)) {
            base = k;
            break;
        }
    }
    if (base < 0) {
        nnue::refresh(nnueRing[slot], *this);
        nnueRingValid[slot] = 1;
        nnueRingKey[slot] = curKey;
        nnueRingDirty[slot] = 0;
        return &nnueRing[slot];
    }
    uint8_t dirty = nnueRingDirty[base & NNUE_RING_MASK];
    for (int k = base; k < cur; k++) {
        int d = (k + 1) & NNUE_RING_MASK;
        nnue::applyMoveAcc(nnueRing[d], nnueRing[k & NNUE_RING_MASK], st[k], dirty);
        nnueRingValid[d] = 1;
        nnueRingKey[d] = keyAfter(k + 1);
        nnueRingDirty[d] = dirty;
    }
    if (dirty) {
        if (dirty & 1u) nnue::refreshPerspective(nnueRing[slot], *this, WHITE);
        if (dirty & 2u) nnue::refreshPerspective(nnueRing[slot], *this, BLACK);
        nnueRingDirty[slot] = 0;
    }
    return &nnueRing[slot];
}

// ------------------------------------------------------------------
// FEN
// ------------------------------------------------------------------

void Position::clear() {
    memset(pieceBb, 0, sizeof(pieceBb));
    memset(colorBb, 0, sizeof(colorBb));
    occupied = 0;
    memset(pieceAt, -1, sizeof(pieceAt));
    sideToMove = WHITE;
    castlingRights = 0;
    epSquare = NO_SQUARE;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    key = 0;
    pawnKey = 0;
    nonPawnKey[0] = nonPawnKey[1] = 0;
    mg[0] = mg[1] = eg[0] = eg[1] = 0;
    phase = 0;
    stCount = 0;
    startKey = 0;
    checkersRoot_ = 0;
    memset(nnueRingValid, 0, sizeof(nnueRingValid));
}

bool Position::setFen(const char *fenStr) {
    // Parse into a scratch copy; only commit on success.
    Position tmp;
    tmp.clear();

    // Tokenize on whitespace.
    char buf[256];
    size_t len = strlen(fenStr);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, fenStr, len + 1);
    char *parts[8];
    int nparts = 0;
    char *p = strtok(buf, " \t\r\n");
    while (p && nparts < 8) {
        parts[nparts++] = p;
        p = strtok(nullptr, " \t\r\n");
    }
    if (nparts < 4) return false;

    // Placement.
    const char *place = parts[0];
    int rank = 7, file = 0;
    for (const char *c = place; *c; c++) {
        if (*c == '/') {
            if (file != 8 || rank == 0) return false;
            rank--;
            file = 0;
        } else if (*c >= '1' && *c <= '8') {
            file += *c - '0';
            if (file > 8) return false;
        } else {
            int type = pieceTypeFromFen(*c);
            if (type == NO_PIECE || file >= 8) return false;
            int side = pieceSideFromFen(*c);
            int sq = rank * 8 + file;
            tmp.pieceBb[type] |= bitOf(sq);
            tmp.colorBb[side] |= bitOf(sq);
            tmp.occupied |= bitOf(sq);
            tmp.pieceAt[sq] = (int8_t)((side << 3) | type);
            file++;
        }
    }
    if (file != 8 || rank != 0) return false;
    if (!tmp.pieceBb[KING] || popcount(tmp.pieceBb[KING]) != 2) return false;
    // Side to move.
    if (strlen(parts[1]) != 1) return false;
    if (parts[1][0] == 'w') tmp.sideToMove = WHITE;
    else if (parts[1][0] == 'b') tmp.sideToMove = BLACK;
    else return false;

    // Castling.
    tmp.castlingRights = 0;
    if (strcmp(parts[2], "-") != 0) {
        for (const char *c = parts[2]; *c; c++) {
            switch (*c) {
                case 'K': tmp.castlingRights |= CASTLE_WK; break;
                case 'Q': tmp.castlingRights |= CASTLE_WQ; break;
                case 'k': tmp.castlingRights |= CASTLE_BK; break;
                case 'q': tmp.castlingRights |= CASTLE_BQ; break;
                default: return false;
            }
        }
    }

    // En passant.
    if (strcmp(parts[3], "-") == 0) {
        tmp.epSquare = NO_SQUARE;
    } else {
        tmp.epSquare = squareFromName(parts[3]);
        if (tmp.epSquare == NO_SQUARE) return false;
    }

    tmp.halfmoveClock = nparts > 4 ? atoi(parts[4]) : 0;
    tmp.fullmoveNumber = nparts > 5 ? atoi(parts[5]) : 1;
    if (tmp.halfmoveClock < 0) tmp.halfmoveClock = 0;
    if (tmp.fullmoveNumber < 1) tmp.fullmoveNumber = 1;

    // Reject illegal positions: the side NOT to move may not be in check
    // (its king would be capturable, which every layer assumes impossible).
    if (tmp.isAttacked(tmp.kingSquare(tmp.sideToMove ^ 1), tmp.sideToMove)) return false;

    tmp.computeDerived();
    tmp.startKey = tmp.key;

    // Move-assign: take the parsed state (buffer transfer is fine — tmp dies).
    // Do it field by field; the parsed stack is empty so a fresh buffer works.
    *this = tmp;
    computeRootCheckers();
    return true;
}

void Position::computeDerived() {
    key = 0;
    pawnKey = 0;
    mg[0] = mg[1] = eg[0] = eg[1] = 0;
    phase = 0;
    for (int sq = 0; sq < 64; sq++) {
        int type = pieceTypeOf(sq);
        if (type != NO_PIECE) {
            int side = pieceColorOf(sq);
            key ^= Z_PIECE[side * 6 + type][sq];
            if (type == PAWN) pawnKey ^= Z_PIECE[side * 6 + PAWN][sq];
            if (side == WHITE) {
                mg[WHITE] += PST_MG[type][sq];
                eg[WHITE] += PST_EG[type][sq];
            } else {
                mg[BLACK] += PST_MG_BLACK[type][sq];
                eg[BLACK] += PST_EG_BLACK[type][sq];
            }
            phase += PHASE_WEIGHT[type];
        }
    }
    key ^= Z_CASTLE[castlingRights];
    if (epSquare != NO_SQUARE) key ^= Z_EP_FILE[epSquare & 7];
    if (sideToMove == BLACK) key ^= Z_SIDE;
    nonPawnKey[0] = 0;
    nonPawnKey[1] = 0;
    for (int sq = 0; sq < 64; sq++) {
        int type = pieceTypeOf(sq);
        if (type != NO_PIECE && type != PAWN) {
            int side = pieceColorOf(sq);
            nonPawnKey[side] ^= Z_PIECE[side * 6 + type][sq];
        }
    }
    // setFen commits via operator= (which invalidates the ring), so the
    // first nnueEnsureFresh() after a setFen does one full refresh from
    // the placement. When NNUE is off, nothing is ever computed.
    memset(nnueRingValid, 0, sizeof(nnueRingValid));
}

std::string Position::fen() const {
    std::string out;
    out.reserve(96);
    char sqbuf[3];
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int type = pieceTypeOf(sq);
            if (type == NO_PIECE) {
                empty++;
            } else {
                if (empty > 0) { out += char('0' + empty); empty = 0; }
                char c = pieceFenChar(type);
                out += (pieceColorOf(sq) == WHITE) ? c : char(c - 'A' + 'a');
            }
        }
        if (empty > 0) out += char('0' + empty);
        if (rank > 0) out += '/';
    }
    out += ' ';
    out += (sideToMove == WHITE) ? 'w' : 'b';
    out += ' ';
    if (castlingRights == 0) {
        out += '-';
    } else {
        if (castlingRights & CASTLE_WK) out += 'K';
        if (castlingRights & CASTLE_WQ) out += 'Q';
        if (castlingRights & CASTLE_BK) out += 'k';
        if (castlingRights & CASTLE_BQ) out += 'q';
    }
    out += ' ';
    if (epSquare == NO_SQUARE) out += '-';
    else { squareName(epSquare, sqbuf); out += sqbuf; }
    char nums[24];
    snprintf(nums, sizeof(nums), " %d %d", halfmoveClock, fullmoveNumber);
    out += nums;
    return out;
}

// ------------------------------------------------------------------
// Attacks
// ------------------------------------------------------------------

bool Position::isAttacked(int sq, int by) const {
    Bitboard byPieces = colorBb[by];
    if (KNIGHT_ATT[sq] & pieceBb[KNIGHT] & byPieces) return true;
    if (KING_ATT[sq] & pieceBb[KING] & byPieces) return true;
    if (PAWN_ATT[by ^ 1][sq] & pieceBb[PAWN] & byPieces) return true;
    Bitboard rq = (pieceBb[ROOK] | pieceBb[QUEEN]) & byPieces;
    if (rookAttacks(sq, occupied) & rq) return true;
    Bitboard bq = (pieceBb[BISHOP] | pieceBb[QUEEN]) & byPieces;
    if (bishopAttacks(sq, occupied) & bq) return true;
    return false;
}

Bitboard Position::attackersTo(int sq, Bitboard occ) const {
    Bitboard knights = KNIGHT_ATT[sq] & pieceBb[KNIGHT];
    Bitboard kings = KING_ATT[sq] & pieceBb[KING];
    Bitboard pawns = (PAWN_ATT[BLACK][sq] & pieceBb[PAWN] & colorBb[WHITE]) |
                     (PAWN_ATT[WHITE][sq] & pieceBb[PAWN] & colorBb[BLACK]);
    Bitboard sliders = (rookAttacks(sq, occ) & (pieceBb[ROOK] | pieceBb[QUEEN])) |
                       (bishopAttacks(sq, occ) & (pieceBb[BISHOP] | pieceBb[QUEEN]));
    return (knights | kings | pawns | sliders) & occ;
}

void Position::computeRootCheckers() {
    int ksq = kingSquare(sideToMove);
    checkersRoot_ = attackersTo(ksq, occupied) & colorBb[sideToMove ^ 1];
}

namespace {

// ---- v6 NNUE op recording helpers (need complete StateInfo) ----

inline void recAddOp(StateInfo &si, int color, int type, int sq, int wk, int bk) {
    nnue::NnueOp &op = si.nnueOp[si.nnueNops++];
    op.kind = nnue::NNUE_OP_ADD;
    op.persp = 0;
    op.wF = (uint16_t)nnue::rowIndexOf(color, type, sq, WHITE, wk, bk);
    op.bF = (uint16_t)nnue::rowIndexOf(color, type, sq, BLACK, wk, bk);
}

inline void recSubOp(StateInfo &si, int color, int type, int sq, int wk, int bk) {
    nnue::NnueOp &op = si.nnueOp[si.nnueNops++];
    op.kind = nnue::NNUE_OP_SUB;
    op.persp = 0;
    op.wF = (uint16_t)nnue::rowIndexOf(color, type, sq, WHITE, wk, bk);
    op.bF = (uint16_t)nnue::rowIndexOf(color, type, sq, BLACK, wk, bk);
}

inline void recShiftOp(StateInfo &si, int color, int type, int from, int to, int wk,
                       int bk) {
    nnue::NnueOp &op = si.nnueOp[si.nnueNops++];
    op.kind = nnue::NNUE_OP_SHIFT;
    op.persp = 0;
    op.wF = (uint16_t)nnue::rowIndexOf(color, type, from, WHITE, wk, bk);
    op.wT = (uint16_t)nnue::rowIndexOf(color, type, to, WHITE, wk, bk);
    op.bF = (uint16_t)nnue::rowIndexOf(color, type, from, BLACK, wk, bk);
    op.bT = (uint16_t)nnue::rowIndexOf(color, type, to, BLACK, wk, bk);
}

inline void recShiftPOp(StateInfo &si, int color, int type, int from, int to,
                        int persp, int wk, int bk) {
    nnue::NnueOp &op = si.nnueOp[si.nnueNops++];
    op.kind = nnue::NNUE_OP_SHIFT_P;
    op.persp = (uint8_t)persp;
    op.wF = (uint16_t)nnue::rowIndexOf(color, type, from, WHITE, wk, bk);
    op.wT = (uint16_t)nnue::rowIndexOf(color, type, to, WHITE, wk, bk);
    op.bF = (uint16_t)nnue::rowIndexOf(color, type, from, BLACK, wk, bk);
    op.bT = (uint16_t)nnue::rowIndexOf(color, type, to, BLACK, wk, bk);
}

}  // namespace

// ------------------------------------------------------------------
// Make / unmake
// ------------------------------------------------------------------

void Position::pushState() {
    if (stCount >= stCap) {
        int newCap = stCap == 0 ? (MAX_PLY + 256) : stCap * 2;
        StateInfo *ns = new StateInfo[newCap];
        if (stCount > 0) memcpy(ns, st, sizeof(StateInfo) * (size_t)stCount);
        delete[] st;
        st = ns;
        stCap = newCap;
    }
    StateInfo &si = st[stCount++];
    si.move = MOVE_NONE;
    si.captured = NO_PIECE;
    si.castling = castlingRights;
    si.ep = epSquare;
    si.halfmove = halfmoveClock;
    si.preKey = key;
    si.prePawnKey = pawnKey;
    si.preMg[0] = mg[0]; si.preMg[1] = mg[1];
    si.preEg[0] = eg[0]; si.preEg[1] = eg[1];
    si.prePhase = phase;
    si.preNonPawnKey[0] = nonPawnKey[0]; si.preNonPawnKey[1] = nonPawnKey[1];
    si.wasNull = true;
}

void Position::makeMove(Move move) {
    int from = moveFrom(move), to = moveTo(move);
    int piece = movePiece(move), captured = moveCaptured(move), promo = movePromo(move);
    int us = sideToMove, them = us ^ 1;

    pushState();
    StateInfo &si = st[stCount - 1];
    si.move = move;
    si.captured = captured;
    si.wasNull = false;
    si.nnueNops = 0;

    key ^= Z_SIDE;
    if (epSquare != NO_SQUARE) {
        key ^= Z_EP_FILE[epSquare & 7];
        epSquare = NO_SQUARE;
    }

    // v6: record the victim op with PRE-move king squares (the eager v5.x
    // order removed the victim row before the mover's placement changed).
    if (captured != NO_PIECE) {
        int capSq = isEPMove(move) ? (us == WHITE ? to - 8 : to + 8) : to;
        if (nnue::enabled)
            recSubOp(si, them, captured, capSq, kingSquare(WHITE), kingSquare(BLACK));
        removePiece(them, captured, capSq);
    }

    if (promo != 0) {
        removePiece(us, PAWN, from);
        addPiece(us, promo, to);
    } else {
        shiftPiece(us, piece, from, to);
    }

    if (isCastleK(move)) {
        if (us == WHITE) shiftPiece(WHITE, ROOK, 7, 5); else shiftPiece(BLACK, ROOK, 63, 61);
    } else if (isCastleQ(move)) {
        if (us == WHITE) shiftPiece(WHITE, ROOK, 0, 3); else shiftPiece(BLACK, ROOK, 56, 59);
    }

    // v6: record the mover / promotion / castle-rook ops with POST-move
    // king squares (mirroring the v5.x eager order exactly).
    if (nnue::enabled) {
        int wk = kingSquare(WHITE), bk = kingSquare(BLACK);
        if (promo != 0) {
            recSubOp(si, us, PAWN, from, wk, bk);
            recAddOp(si, us, promo, to, wk, bk);
        } else if (piece == KING) {
            // Own-quadrant check in the MOVER's perspective coordinates:
            // same quadrant -> a normal 4-row shift (both perspectives) is
            // exact and saves the v5.x unconditional 32-row refresh;
            // cross-quadrant -> the other perspective shifts, own goes dirty.
            int f = us == WHITE ? from : (from ^ 56);
            int t = us == WHITE ? to : (to ^ 56);
            if (nnue::kingBucketOf(f) != nnue::kingBucketOf(t))
                recShiftPOp(si, us, KING, from, to, us ^ 1, wk, bk);
            else
                recShiftOp(si, us, KING, from, to, wk, bk);
        } else {
            recShiftOp(si, us, piece, from, to, wk, bk);
        }
        if (isCastleK(move)) {
            if (us == WHITE) recShiftOp(si, WHITE, ROOK, 7, 5, wk, bk);
            else recShiftOp(si, BLACK, ROOK, 63, 61, wk, bk);
        } else if (isCastleQ(move)) {
            if (us == WHITE) recShiftOp(si, WHITE, ROOK, 0, 3, wk, bk);
            else recShiftOp(si, BLACK, ROOK, 56, 59, wk, bk);
        }
    }

    key ^= Z_CASTLE[castlingRights];
    castlingRights &= CASTLE_RIGHTS_MASK[from] & CASTLE_RIGHTS_MASK[to];
    key ^= Z_CASTLE[castlingRights];

    if (isDoublePush(move)) {
        epSquare = us == WHITE ? from + 8 : from - 8;
        key ^= Z_EP_FILE[epSquare & 7];
    }

    halfmoveClock = (piece == PAWN || captured != NO_PIECE) ? 0 : halfmoveClock + 1;
    if (us == BLACK) fullmoveNumber++;
    sideToMove = them;

    // Checkers of the new side to move (also = "this move gives check").
    si.key = key;
    si.checkers = attackersTo(kingSquare(them), occupied) & colorBb[us];
}

void Position::unmakeMove() {
    // Copy first: pop destroys nothing (manual stack), but keep it explicit.
    StateInfo &u = st[stCount - 1];
    stCount--;
    Move move = u.move;
    int from = moveFrom(move), to = moveTo(move);
    int piece = movePiece(move), captured = u.captured, promo = movePromo(move);
    int us = sideToMove ^ 1;  // the side that made the move
    sideToMove = us;

    // v1.4.1 fix: reverse bitboards / mailbox with the BB-only helpers.
    // The full addPiece/removePiece/shiftPiece also maintained key /
    // pawnKey / nonPawnKey / mg / eg / phase — and every one of those was
    // then overwritten from the snapshot below, so the derived-scalar work
    // was dead (~15 lookups + XORs per unmake across the whole search).
    // The snapshot restore is the authoritative semantics either way, so
    // the resulting state is bit-identical.
    if (promo != 0) {
        removePieceBb(us, promo, to);
        addPieceBb(us, PAWN, from);
    } else {
        shiftPieceBb(us, piece, to, from);
    }

    if (isCastleK(move)) {
        if (us == WHITE) shiftPieceBb(WHITE, ROOK, 5, 7); else shiftPieceBb(BLACK, ROOK, 61, 63);
    } else if (isCastleQ(move)) {
        if (us == WHITE) shiftPieceBb(WHITE, ROOK, 3, 0); else shiftPieceBb(BLACK, ROOK, 59, 56);
    }

    if (captured != NO_PIECE) {
        int capSq = isEPMove(move) ? (us == WHITE ? to - 8 : to + 8) : to;
        addPieceBb(us ^ 1, captured, capSq);
    }

    // Restore derived scalars from the pre-move snapshot.
    castlingRights = u.castling;
    epSquare = u.ep;
    halfmoveClock = u.halfmove;
    if (us == BLACK) fullmoveNumber--;
    key = u.preKey;
    pawnKey = u.prePawnKey;
    nonPawnKey[0] = u.preNonPawnKey[0];
    nonPawnKey[1] = u.preNonPawnKey[1];
    mg[0] = u.preMg[0]; mg[1] = u.preMg[1];
    eg[0] = u.preEg[0]; eg[1] = u.preEg[1];
    phase = u.prePhase;
}

void Position::makeNullMove() {
    pushState();
    key ^= Z_SIDE;
    if (epSquare != NO_SQUARE) {
        key ^= Z_EP_FILE[epSquare & 7];
        epSquare = NO_SQUARE;
    }
    sideToMove ^= 1;
    st[stCount - 1].key = key;
    st[stCount - 1].checkers = 0;
}

void Position::unmakeNullMove() {
    StateInfo &u = st[stCount - 1];
    stCount--;
    castlingRights = u.castling;
    epSquare = u.ep;
    halfmoveClock = u.halfmove;
    key = u.preKey;
    pawnKey = u.prePawnKey;
    nonPawnKey[0] = u.preNonPawnKey[0];
    nonPawnKey[1] = u.preNonPawnKey[1];
    mg[0] = u.preMg[0]; mg[1] = u.preMg[1];
    eg[0] = u.preEg[0]; eg[1] = u.preEg[1];
    phase = u.prePhase;
    sideToMove ^= 1;
}

// ------------------------------------------------------------------
// Repetition
// ------------------------------------------------------------------

bool Position::isDeadMaterialDraw() const {
    if ((pieceBb[PAWN] | pieceBb[ROOK] | pieceBb[QUEEN]) != 0) return false;
    Bitboard minors = pieceBb[KNIGHT] | pieceBb[BISHOP];
    int wm = popcount(minors & colorBb[WHITE]);
    int bm = popcount(minors & colorBb[BLACK]);
    if (wm <= 1 && bm <= 1) return true;
    // v1.4.5: KNN vs bare king is a dead draw — two knights cannot force
    // mate. (Only the exact knights-only case; two bishops still mate, and
    // KNN vs a defender with a minor keeps helpmate/fortress potential, so
    // it stays out of dead adjudication.)
    if (bm == 0 && wm == 2 &&
        (minors & colorBb[WHITE]) == (pieceBb[KNIGHT] & colorBb[WHITE]))
        return true;
    if (wm == 0 && bm == 2 &&
        (minors & colorBb[BLACK]) == (pieceBb[KNIGHT] & colorBb[BLACK]))
        return true;
    return false;
}

bool Position::isRepetitionDraw() const {
    int n = undoCount();
    int start = n - halfmoveClock;
    if (start < 0) start = 0;
    for (int i = n - 2; i >= start; i -= 2) {
        if (keyAfter(i) == key) return true;
    }
    return false;
}

int Position::repetitionCount() const {
    int count = 0;
    int n = undoCount();
    for (int i = n; i >= 0; i -= 2) {
        if (keyAfter(i) == key) count++;
    }
    return count;
}

Position Position::cloneForSearch() const {
    return Position(*this);  // deep copy incl. state stack
}

// ------------------------------------------------------------------
// Test hooks
// ------------------------------------------------------------------

Bitboard Position::computeKeyForTest() const {
    Bitboard k = 0;
    for (int sq = 0; sq < 64; sq++) {
        int type = pieceTypeOf(sq);
        if (type != NO_PIECE) k ^= Z_PIECE[pieceColorOf(sq) * 6 + type][sq];
    }
    k ^= Z_CASTLE[castlingRights];
    if (epSquare != NO_SQUARE) k ^= Z_EP_FILE[epSquare & 7];
    if (sideToMove == BLACK) k ^= Z_SIDE;
    return k;
}

Bitboard Position::computePawnKeyForTest() const {
    Bitboard k = 0;
    Bitboard pawns = pieceBb[PAWN];
    while (pawns) {
        int sq = lsb(pawns);
        pawns &= pawns - 1;
        k ^= Z_PIECE[pieceColorOf(sq) * 6 + PAWN][sq];
    }
    return k;
}

void Position::computeScoresForTest(int &mgOut, int &egOut, int &phaseOut) const {
    int mgScore = 0, egScore = 0, ph = 0;
    for (int sq = 0; sq < 64; sq++) {
        int type = pieceTypeOf(sq);
        if (type != NO_PIECE) {
            int side = pieceColorOf(sq);
            if (side == WHITE) {
                mgScore += PST_MG[type][sq];
                egScore += PST_EG[type][sq];
            } else {
                mgScore -= PST_MG_BLACK[type][sq];
                egScore -= PST_EG_BLACK[type][sq];
            }
            ph += PHASE_WEIGHT[type];
        }
    }
    mgOut = mgScore;
    egOut = egScore;
    phaseOut = ph;
}

}  // namespace tacturn
