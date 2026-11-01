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
    // v1.4.28 (B4): branchless PST indexing. side*56 is 0 for WHITE and 56
    // for BLACK, so sq ^ (side * 56) == sq for white and sq ^ 56 (the
    // vertical mirror) for black — the identical address the if-else
    // selected, without the branch. (XOR with 0 / 56: the low 3 bits are
    // untouched, bit 3..5 flip — exactly rank mirroring.)
    const int msq = sq ^ (side * 56);
    mg[side] += PST_MG[type][msq];
    eg[side] += PST_EG[type][msq];
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
    // v1.4.28 (B4): branchless PST indexing (see addPiece).
    const int msq = sq ^ (side * 56);
    mg[side] -= PST_MG[type][msq];
    eg[side] -= PST_EG[type][msq];
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
    // v1.4.28 (B4): branchless PST indexing (see addPiece). from/to share
    // the same side XOR constant, so the mirrored difference is exact.
    const int mf = from ^ (side * 56), mt = to ^ (side * 56);
    mg[side] += PST_MG[type][mt] - PST_MG[type][mf];
    eg[side] += PST_EG[type][mt] - PST_EG[type][mf];
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
    // v1.4.27 (N6): null-move zero-copy bypass. A null move changes no
    // piece: the null-moved position's accumulator IS its parent's.
    // When the current position came from a null move and the nearest
    // real-move ancestor's ring slot is fresh for its key, return that
    // slot directly — no 2 KB copy, no replay walk. (Consecutive nulls
    // climb to the first non-null ply; every ply in between shares the
    // placement.) The key check re-validates the ancestor's content on
    // every call, so a sibling overwriting the slot is caught by key
    // mismatch and falls through to the walk below. A dirty ancestor is
    // refreshed from the CURRENT placement — identical to the parent's
    // (null moves move nothing) — so the refreshed bytes are exact.
    if (cur > 0 && st[cur - 1].wasNull) {
        int p = cur - 1;
        while (p > 0 && st[p - 1].wasNull) p--;
        const int ps = p & NNUE_RING_MASK;
        if (nnueRingValid[ps] && nnueRingKey[ps] == keyAfter(p)) {
            if (nnueRingDirty[ps]) {
                if (nnueRingDirty[ps] & 1u)
                    nnue::refreshPerspective(nnueRing[ps], *this, WHITE);
                if (nnueRingDirty[ps] & 2u)
                    nnue::refreshPerspective(nnueRing[ps], *this, BLACK);
                nnueRingDirty[ps] = 0;
            }
            return &nnueRing[ps];
        }
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
    // v1.4.27 (N6): srcSlot tracks the last MATERIALIZED slot. A null
    // step (st[k].wasNull) leaves the accumulator unchanged, so its
    // destination slot is NOT materialized at all (no 2 KB applyMoveAcc
    // memcpy) — the next real move replays straight from srcSlot, and a
    // direct hit at the skipped ply resolves through the null-parent
    // bypass above. Skipped slots keep their previous (key-mismatched or
    // invalid) state, so no stale entry can ever be trusted.
    int srcSlot = base & NNUE_RING_MASK;
    for (int k = base; k < cur; k++) {
        if (st[k].wasNull) continue;  // zero pieces moved: no accumulator delta
        int d = (k + 1) & NNUE_RING_MASK;
        nnue::applyMoveAcc(nnueRing[d], nnueRing[srcSlot], st[k], dirty);
        nnueRingValid[d] = 1;
        nnueRingKey[d] = keyAfter(k + 1);
        nnueRingDirty[d] = dirty;
        srcSlot = d;
    }
    // The effective accumulator slot: the walk's last materialized slot.
    // Equals `slot` unless the tail step(s) were null — in that case the
    // current position's content IS srcSlot's (null identity), and its
    // own slot stays unmaterialized.
    const int effSlot = srcSlot;
    if (dirty) {
        if (dirty & 1u) nnue::refreshPerspective(nnueRing[effSlot], *this, WHITE);
        if (dirty & 2u) nnue::refreshPerspective(nnueRing[effSlot], *this, BLACK);
        nnueRingDirty[effSlot] = 0;
        // v1.4.26 (B3): dirty-ancestor back-healing. The replay loop above
        // left EVERY intermediate slot (base+1 .. cur-1) tagged with the
        // dirty-perspective mask it had at that step; only slot `cur` was
        // repaired. When the search unmakes back into that subtree and a
        // sibling line evaluates at one of those plies, the direct-hit path
        // at the top of this function finds a valid-but-dirty slot and pays
        // ANOTHER refreshPerspective — over and over, once per sibling walk,
        // for the same king-bucket crossing (29,086 refreshes on bench 14
        // before this fix). Instead, walk BACKWARD from cur and reconstruct
        // each ancestor's freshly-refreshed perspective by applying the
        // recorded ops in reverse (nnue::healMoveAcc — the exact modular
        // int16 inverse), clearing the dirty bit.
        //
        // CRITICAL (the reason this is per-perspective): a perspective
        // whose OWN king crossed a quadrant boundary at ply k0 changes
        // feature-index SPACE (every one of its rows re-keys to the new
        // bucket) — the recorded op stream never describes that update
        // (that is exactly why the perspective goes dirty). Row-op
        // reversal therefore CANNOT cross k0 for the mover's perspective:
        // healing stops there (the slot keeps its dirty bit; a later visit
        // pays one refreshPerspective, as before). The OTHER perspective
        // (whose bucket did not change) is a plain row shift at the
        // crossing and heals straight through it. The per-slot dirty masks
        // only accumulate downward within a replay walk, so once a slot
        // needs no healing for any surviving perspective, every slot above
        // it is clean too — the walk stops. Every healed value is bit-exact
        // (group inverse from an authoritative refresh), so the search tree
        // is unchanged — only the work is.
        uint8_t healing = dirty;
        for (int h = cur; h > base + 1 && healing; --h) {
            const StateInfo &si = st[h - 1];
            // v1.4.27 (N6): a null step's destination slot was never
            // materialized (the zero-copy replay above skips it), so the
            // heal source ring[sH] is unmaterialized — and the heal pair
            // across a null step is the identity anyway. Stop the chain
            // here; shallower slots keep their dirty bits and pay one
            // refreshPerspective on a later visit, exactly as pre-B3.
            // (v1.4.28 B3 audit: a "climb past the null" variant was
            // implemented and REJECTED by the bench fingerprint — the
            // shallower slot's OTHER perspective is not covered by the
            // per-perspective inverse, so healing through the skip is not
            // provably exact. The conservative break stays.)
            if (si.wasNull) break;
            // Stop healing a perspective across its OWN king-bucket
            // crossing (SHIFT_P marks the MOVER's perspective dirty; the
            // mover is op.persp ^ 1).
            for (int i = 0; i < si.nnueNops; i++)
                if (si.nnueOp[i].kind == nnue::NNUE_OP_SHIFT_P)
                    healing &= (uint8_t)~(1u << (si.nnueOp[i].persp ^ 1));
            const int sH = h & NNUE_RING_MASK;
            const int sP = (h - 1) & NNUE_RING_MASK;
            const uint8_t dd = nnueRingDirty[sP];
            const uint8_t todo = (uint8_t)(dd & healing);
            if (todo == 0) break;  // clean from here up: nothing to heal
            if (todo & 1u)
                nnue::healMoveAcc(nnueRing[sP], nnueRing[sH], si, WHITE);
            if (todo & 2u)
                nnue::healMoveAcc(nnueRing[sP], nnueRing[sH], si, BLACK);
            nnueRingDirty[sP] = (uint8_t)(dd & ~todo);
        }
    }
    return &nnueRing[effSlot];
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
    // v1.4.25 (B2): pseudo-ray gating — sliderAttacks(sq, occ) is a subset
    // of the empty-board ray, so an empty pseudo intersection proves the
    // real intersection is empty too and the table lookup is skipped.
    // Bit-exact for every (sq, occ); skips both slider lookups in the
    // common no-aligned-slider case.
    Bitboard rq = (pieceBb[ROOK] | pieceBb[QUEEN]) & byPieces;
    if (rq & ROOK_PSEUDO[sq]) {
        if (rookAttacks(sq, occupied) & rq) return true;
    }
    Bitboard bq = (pieceBb[BISHOP] | pieceBb[QUEEN]) & byPieces;
    if (bq & BISHOP_PSEUDO[sq]) {
        if (bishopAttacks(sq, occupied) & bq) return true;
    }
    return false;
}

Bitboard Position::attackersTo(int sq, Bitboard occ) const {
    Bitboard knights = KNIGHT_ATT[sq] & pieceBb[KNIGHT];
    Bitboard kings = KING_ATT[sq] & pieceBb[KING];
    Bitboard pawns = (PAWN_ATT[BLACK][sq] & pieceBb[PAWN] & colorBb[WHITE]) |
                     (PAWN_ATT[WHITE][sq] & pieceBb[PAWN] & colorBb[BLACK]);
    // v1.4.25 (B2): pseudo-ray gating (see isAttacked) — same sets.
    Bitboard sliders = 0;
    Bitboard rq = pieceBb[ROOK] | pieceBb[QUEEN];
    if (rq & ROOK_PSEUDO[sq]) sliders |= rookAttacks(sq, occ) & rq;
    Bitboard bq = pieceBb[BISHOP] | pieceBb[QUEEN];
    if (bq & BISHOP_PSEUDO[sq]) sliders |= bishopAttacks(sq, occ) & bq;
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
    // v1.4.27 (Prev-B4): featureIndex() unwound with the two king-bucket
    // bases hoisted — 2 kingBucketOf calls instead of 4 per recorded move
    // (the bucket depends only on the perspective's OWN king, which is
    // constant across the 4 row computations). Identical arithmetic:
    // WHITE rows key on (bucket(wk), sq); BLACK rows on (bucket(bk^56),
    // sq^56) — exactly nnue::featureIndex unwrapped.
    const int pc64 = (color * 6 + type) * 64;
    const int wBase = nnue::kingBucketOf(wk) * 768 + pc64;
    const int bBase = nnue::kingBucketOf(bk ^ 56) * 768 + pc64;
    op.wF = (uint16_t)(wBase + from);
    op.wT = (uint16_t)(wBase + to);
    op.bF = (uint16_t)(bBase + (from ^ 56));
    op.bT = (uint16_t)(bBase + (to ^ 56));
}

inline void recShiftPOp(StateInfo &si, int color, int type, int from, int to,
                        int persp, int wk, int bk) {
    nnue::NnueOp &op = si.nnueOp[si.nnueNops++];
    op.kind = nnue::NNUE_OP_SHIFT_P;
    op.persp = (uint8_t)persp;
    // v1.4.27 (Prev-B4): the same hoisted-base unwind as recShiftOp.
    const int pc64 = (color * 6 + type) * 64;
    const int wBase = nnue::kingBucketOf(wk) * 768 + pc64;
    const int bBase = nnue::kingBucketOf(bk ^ 56) * 768 + pc64;
    op.wF = (uint16_t)(wBase + from);
    op.wT = (uint16_t)(wBase + to);
    op.bF = (uint16_t)(bBase + (from ^ 56));
    op.bT = (uint16_t)(bBase + (to ^ 56));
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
    // v1.4.25 (B2): single-side checker computation with pseudo-ray
    // gating. v1.4.24 called the two-color attackersTo(kingSquare(them),
    // occupied) and ANDed colorBb[us] afterwards — paying both slider
    // lookups on EVERY move. Computing the mover-side attackers directly
    // is the same set (the & colorBb[us] filter is folded into each term),
    // and the ROOK_PSEUDO / BISHOP_PSEUDO gates skip both lookups on the
    // ~82% of moves where no mover R/Q (B/Q) is aligned with the enemy
    // king. Bit-exact: pieceBb terms are subsets of occupied, so the old
    // trailing `& occ` was redundant for this call site.
    {
        int ksq = kingSquare(them);
        Bitboard usBb = colorBb[us];
        Bitboard att = (KNIGHT_ATT[ksq] & pieceBb[KNIGHT] & usBb) |
                       (PAWN_ATT[them][ksq] & pieceBb[PAWN] & usBb) |
                       (KING_ATT[ksq] & pieceBb[KING] & usBb);
        Bitboard rq = (pieceBb[ROOK] | pieceBb[QUEEN]) & usBb;
        if (rq & ROOK_PSEUDO[ksq]) att |= rookAttacks(ksq, occupied) & rq;
        Bitboard bq = (pieceBb[BISHOP] | pieceBb[QUEEN]) & usBb;
        if (bq & BISHOP_PSEUDO[ksq]) att |= bishopAttacks(ksq, occupied) & bq;
        si.checkers = att;
    }
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
    // v1.4.28 (B4): 3-field restore. makeNullMove() modifies ONLY epSquare,
    // key and sideToMove (pushState snapshots the rest, but nothing else is
    // touched between make and unmake) — the old 10-field restore copied
    // castlingRights, halfmoveClock, pawnKey, nonPawnKey[2], mg[2], eg[2]
    // and phase back onto their own current values. Value-identical.
    const StateInfo &u = st[--stCount];
    epSquare = u.ep;
    key = u.preKey;
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
    // v1.4.25 (B4): a repetition needs 4 reversible halfmoves, and the
    // n - 2 candidate is mathematically unreachable — the side to move
    // cannot restore a square the opponent's last move vacated (the last
    // two plies are provably non-captures when halfmoveClock >= 2), and
    // two consecutive null moves are structurally impossible (the NMP
    // child is searched with allowNull = false). Gate on the clock and
    // start the scan at n - 4: >60% of calls return before the loop.
    if (halfmoveClock < 4) return false;
    int n = undoCount();
    int start = n - halfmoveClock;
    if (start < 0) start = 0;
    for (int i = n - 4; i >= start; i -= 2) {
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
