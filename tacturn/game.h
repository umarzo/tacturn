// TacTurn Chess Engine v1 — game-state layer (port of Game.kt).
//
// Wraps a Position with everything the app needs: full-rule result detection
// (checkmate, stalemate, 50-move, threefold repetition, insufficient
// material), Standard Algebraic Notation, move records for history/undo and
// FEN import/export.

#pragma once

#include <string>
#include <vector>

#include "position.h"
#include "search.h"

namespace tacturn {

class Book;

enum GameResultValue {
    GR_ONGOING = 0,
    GR_WHITE_WINS = 1,
    GR_BLACK_WINS = 2,
    GR_DRAW_STALEMATE = 3,
    GR_DRAW_FIFTY_MOVE = 4,
    GR_DRAW_REPETITION = 5,
    GR_DRAW_INSUFFICIENT_MATERIAL = 6,
    GR_DRAW_AGREEMENT = 7,
};

inline bool gameResultIsDraw(int r) {
    return r == GR_DRAW_STALEMATE || r == GR_DRAW_FIFTY_MOVE ||
           r == GR_DRAW_REPETITION || r == GR_DRAW_INSUFFICIENT_MATERIAL ||
           r == GR_DRAW_AGREEMENT;
}
inline bool gameResultIsGameOver(int r) { return r != GR_ONGOING; }

struct MoveRecord {
    Move move = MOVE_NONE;
    std::string san;
};

class GameSession {
public:
    explicit GameSession(int ttBits = 21, const char *initialFen = nullptr);

    Position board;
    std::vector<MoveRecord> records;

    // ---- lifecycle ----
    void reset(const char *fen = nullptr);
    std::string fen() const { return board.fen(); }
    bool setFen(const char *fen);  // resets records

    // ---- legal moves ----
    const std::vector<Move> &legalMoves();  // cached
    std::vector<Move> findMoves(int from, int to) const;
    std::vector<Move> legalMovesFrom(int sq) const;

    // ---- making moves ----
    /** Applies a legal move, records it and returns its SAN. */
    std::string makeMove(Move move);
    /** Undoes the last move; returns its SAN or "" when none to undo. */
    std::string undo();

    bool inCheck() const { return board.inCheck(board.sideToMove); }

    // ---- results ----
    int result();  // GameResultValue (computed, cached legality)
    /** immediate end by resignation (0 = clear, else winner color 0/1). */
    void resign(int winnerColor) {
        resultOverride = winnerColor == WHITE ? GR_WHITE_WINS : GR_BLACK_WINS;
        legalCacheValid = false;
    }
    /** immediate end by mutual draw agreement (clears with undo/reset). */
    void agreeDraw() {
        resultOverride = GR_DRAW_AGREEMENT;
        legalCacheValid = false;
    }
    bool insufficientMaterial() const;
    int checkSquare() const;

    // ---- search bridge ----
    SearchResult think(const SearchLimits &limits,
                       const std::function<void(const IterationInfo &)> &onIteration = nullptr) {
        return searcher.think(board, limits, onIteration);
    }
    void requestStop() { searcher.requestStop(); }
    void clearSearchTable() { searcher.clearTable(); }

    /**
     * extracts the principal variation for `first` (a legal move in the
     * current position) by walking the transposition table after a search.
     * Fills out[0..maxOut) with the PV as moves (out[0] == first) and
     * returns the length. The board is restored before returning. Guards:
     * legality checks on every step, repetition/50-move stops, maxOut cap —
     * a corrupt or missing TT entry simply truncates the PV, never crashes.
     */
    int extractPv(Move first, Move *out, int maxOut);

    // ---- book ----
    Move bookProbe(uint64_t rng = 0);  // 0 = internal clock-seeded rng

    /** SAN for a legal move in the current position (without applying it). */
    std::string sanFor(Move move);

private:
    Searcher searcher;
    std::vector<Move> legalCache;
    bool legalCacheValid = false;
    int resultOverride = 0;      // resignation (GameResultValue or 0)
    uint64_t bookRng_;
};

}  // namespace tacturn
