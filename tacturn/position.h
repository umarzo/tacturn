// TacTurn Chess Engine v1 — bitboard position with incremental state.
//
// Faithful C++ port of the Kotlin Board.kt:
//   - pieceBb[6] / colorBb[2] / occupied + a mailbox (pieceAt: -1 empty,
//     else (color<<3)|type — the same encoding the Android UI expects)
//   - incremental Zobrist key, pawn key, mg/eg PST scores and game phase
//   - fully reversible make/unmake via a StateInfo stack; the state stack
//     doubles as the repetition-detection history
// checkers of the side to move are computed in make() and kept
// in the state (in-check and gives-check queries become O(1) in search).
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include "bitboard.h"
#include "nnue.h"
#include "types.h"

#include <string>
#include <vector>

namespace tacturn {

struct StateInfo {
    Move move = MOVE_NONE;        // 0 + wasNull covers null moves
    int captured = NO_PIECE;
    Bitboard key = 0;             // key AFTER this move (repetition history)
    // ---- pre-move snapshot, restored on unmake ----
    int castling = 0;
    int ep = NO_SQUARE;
    int halfmove = 0;
    Bitboard preKey = 0;
    Bitboard prePawnKey = 0;
    int preMg[2] = {0, 0};
    int preEg[2] = {0, 0};
    int prePhase = 0;
    Bitboard preNonPawnKey[2] = {0, 0};
    Bitboard checkers = 0;        // checkers of the side to move AFTER the move
    bool wasNull = false;
    // ---- v6 lazy NNUE replay ops (recorded at make, applied on demand) ----
    uint8_t nnueNops = 0;
    nnue::NnueOp nnueOp[3];       // capture / promo / mover / castle-rook ops
};

class Position {
public:
    Bitboard pieceBb[6] = {0, 0, 0, 0, 0, 0};
    Bitboard colorBb[2] = {0, 0};
    Bitboard occupied = 0;
    int8_t pieceAt[64];           // -1 empty, else (color<<3)|type

    int sideToMove = WHITE;
    int castlingRights = 0;
    int epSquare = NO_SQUARE;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    Bitboard key = 0;
    Bitboard pawnKey = 0;
    Bitboard nonPawnKey[2] = {0, 0};  // per-colour, non-pawn pieces only
    int mg[2] = {0, 0};
    int eg[2] = {0, 0};
    int phase = 0;

    // NNUE accumulator ring (v6). Each RING slot caches the accumulator
    // for one state-stack depth (tagged by the position's Zobrist key);
    // makeMove records the feature-row ops in the StateInfo, unmake is
    // free, and the accumulator for the current ply is materialized
    // lazily by nnueEnsureFresh() — walk to the nearest valid ancestor,
    // then copy+apply the recorded ops down. Slots survive sibling
    // re-searches and transpositions (key match); different lines
    // invalidate by key mismatch. This replaces v5.x's eager per-make
    // accumulator maintenance, which cost 2xL1 row traffic on EVERY make
    // AND unmake while ~2/3 of makes are never followed by an eval below.
    static constexpr int NNUE_RING_BITS = 7;              // 128 slots
    static constexpr int NNUE_RING = 1 << NNUE_RING_BITS; // ~132 KB at L1=256
    static constexpr int NNUE_RING_MASK = NNUE_RING - 1;
    nnue::Accumulator nnueRing[NNUE_RING];
    Bitboard nnueRingKey[NNUE_RING];   // key of the position this slot holds
    uint8_t nnueRingValid[NNUE_RING];  // 0 = slot invalid
    uint8_t nnueRingDirty[NNUE_RING];  // per-perspective stale mask
    // v1.4.2 Finny tables: per-(perspective, own-king-bucket) accumulator
    // cache consulted by nnue::refreshPerspective (see nnue.h). mutable:
    // the cache is internal memoization for a const refresh call.
    mutable nnue::FinnyEntry finny[2][nnue::KING_BUCKETS];

    // State stack: st[i] = the i-th half-move (stCount == undoCount).
    // Grow-only manual array (the search pushes/pops millions of times —
    // std::vector reallocation churn is measurably slower).
    StateInfo *st = nullptr;
    int stCount = 0;
    int stCap = 0;
    Bitboard startKey = 0;        // key of the position set via setFen

    Position();
    Position(const Position &other);            // deep copy (search helpers)
    Position &operator=(const Position &other);  // deep copy
    // v1.4.7: move semantics — helper workers re-clone the root board
    // every move (`ownPos = board.cloneForSearch()`); the copy-assign used
    // to deep-copy the state stack a SECOND time out of the returned
    // temporary (clone -> operator= -> new StateInfo[]), doubling the
    // per-move heap traffic of every Lazy-SMP helper. The move-assign
    // steals the temporary's stack instead. Placement semantics match the
    // copy-assign exactly: the NNUE ring is invalidated (not copied) and
    // the Finny caches are left as-is — the delta path keeps them correct
    // across placements by design.
    Position(Position &&other) noexcept;
    Position &operator=(Position &&other) noexcept;
    ~Position();

    static const char *START_FEN;

    // ---- FEN ----
    void clear();
    bool setFen(const char *fen);         // false on invalid input (state untouched)
    std::string fen() const;

    // ---- Square queries ----
    inline int pieceTypeOf(int sq) const { return pieceAt[sq] < 0 ? NO_PIECE : (pieceAt[sq] & 7); }
    inline int pieceColorOf(int sq) const { return pieceAt[sq] < 0 ? -1 : (pieceAt[sq] >> 3); }
    inline int kingSquare(int side) const { return lsb(pieceBb[KING] & colorBb[side]); }

    // ---- Attacks ----
    bool isAttacked(int sq, int by) const;
    Bitboard attackersTo(int sq, Bitboard occ) const;
    inline bool inCheck(int side) const { return isAttacked(kingSquare(side), side ^ 1); }
    /** Checkers of the CURRENT side to move (state-cached after make). */
    inline Bitboard checkers() const {
        return stCount == 0 ? checkersRoot_ : st[stCount - 1].checkers;
    }
    void computeRootCheckers();           // call after setFen
    /** v6: materialize the accumulator for the CURRENT state-stack depth
     *  (lazy ring replay) and return it with both perspectives valid.
     *  Returns nullptr when NNUE is disabled. The pointer is valid until
     *  the next make/unmake. The only readers are nnue::residual and the
     *  verification battery. */
    nnue::Accumulator *nnueEnsureFresh();

    /** v9: materialize the ring slot for the CURRENT depth (the search
     *  root when called at search start). Guarantees every nnueEnsureFresh
     *  walk finds a valid ancestor instead of doing a from-scratch full
     *  refresh mid-tree (34% of all materializations on bench 8 paid the
     *  64-row full refresh because no ancestor slot had ever been filled
     *  — eval-cache-hit ancestors never materialize). Bit-exact: the
     *  accumulator is a pure function of the placement. */
    void nnuePrimeRoot();
    bool hasNonPawnMaterial(int side) const {
        return (colorBb[side] & ~pieceBb[PAWN] & ~pieceBb[KING]) != 0;
    }

    /** The most recent move (MOVE_NONE after a null move or at the root). */
    Move lastMoveOrNull() const {
        if (stCount == 0 || st[stCount - 1].wasNull) return MOVE_NONE;
        return st[stCount - 1].move;
    }

    // ---- Make / unmake ----
    void makeMove(Move move);
    void unmakeMove();
    void makeNullMove();
    void unmakeNullMove();

    // ---- Repetition ----
    /** Key after `i` half-moves (i == 0 -> startKey). */
    inline Bitboard keyAfter(int i) const { return i == 0 ? startKey : st[i - 1].key; }
    inline int undoCount() const { return stCount; }
    bool isRepetitionDraw() const;
    int repetitionCount() const;
    /** v1.3: neither side can ever force mate with the remaining material
     *  (no pawns/rooks/queens, at most one minor per side — the app's
     *  insufficient-material adjudication rule). The search treats these
     *  like any other dead draw instead of evaluating phantom material
     *  edges; this also removes the incentive to hang a piece on the
     *  50-move cliff to reset the clock into a dead-drawn mutilation. */
    bool isDeadMaterialDraw() const;

    // ---- Search helpers ----
    Position cloneForSearch() const;

    // ---- Test hooks (recompute from scratch) ----
    Bitboard computeKeyForTest() const;
    Bitboard computePawnKeyForTest() const;
    void computeScoresForTest(int &mgOut, int &egOut, int &phaseOut) const;
    /** NNUE fuzz hook: incremental accumulator == full refresh? */
    // v5.3: non-const — the verification materializes lazy refreshes.
    bool nnueAccumulatorValid() { return nnue::verifyIncremental(*this); }

private:
    Bitboard checkersRoot_ = 0;   // checkers at the setFen root position

    inline void addPiece(int side, int type, int sq);
    inline void removePiece(int side, int type, int sq);
    inline void shiftPiece(int side, int type, int from, int to);
    // v1.4.1 fix (dead-work strip): bitboard/mailbox-only variants for
    // unmakeMove(). The full helpers below also XOR the Zobrist keys and
    // apply the PST/phase deltas — every one of those derived scalars is
    // then OVERWRITTEN from the StateInfo snapshot at the end of unmake,
    // so computing them on the unmake path was pure dead work (~15 table
    // lookups + XORs per unmake). Bit-exact: the restored state is
    // identical (the snapshot values are authoritative either way).
    inline void addPieceBb(int side, int type, int sq);
    inline void removePieceBb(int side, int type, int sq);
    inline void shiftPieceBb(int side, int type, int from, int to);
    void computeDerived();
    void pushState();
};

/** Perft node count (legal move tree). */
uint64_t perft(Position &b, int depth);
/** Per-root-move divide — key: uci move string, value: node count. */
struct PerftDivideEntry { Move move; uint64_t nodes; };
void perftDivide(Position &b, int depth, std::vector<PerftDivideEntry> &out);

}  // namespace tacturn
