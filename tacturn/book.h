// TacTurn Chess Engine v1 — opening book (port of Book.kt / BookLines.kt).
//
// Compact in-memory book built from 64 mainline opening lines, position-keyed
// (Zobrist) so transpositions are handled for free; weighted-random picks
// give game variety without leaving theory. Capped at MAX_BOOK_PLY plies.

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "position.h"

namespace tacturn {

/** Curated opening lines (defined in book_data.cpp): "uci1 uci2 ...|weight". */
extern const char *BOOK_LINES[];
extern const size_t BOOK_LINE_COUNT;

class Book {
public:
    static constexpr int MAX_BOOK_PLY = 20;

    static Book &instance();

    /** Weighted-random book move for the position, or MOVE_NONE. */
    Move probe(const Position &pos, uint64_t &rng);

    /** All book continuations for diagnostics: (move, weight) pairs. */
    std::vector<std::pair<Move, int>> probeAll(const Position &pos);

    /** True when the position is known opening theory (within book ply
     *  range). Game review labels such positions "Book". */
    bool contains(const Position &pos);

    /** True when `move` is one of the book continuations here. */
    bool containsMove(const Position &pos, Move move);

    int size();

private:
    Book() = default;
    void ensureInit();

    struct Entry {
        std::vector<Move> moves;
        std::vector<int> weights;
    };

    std::unordered_map<Bitboard, Entry> map_;
    bool initialized_ = false;
    std::mutex initMutex_;
};

}  // namespace tacturn
