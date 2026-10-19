// TacTurn Chess Engine v1 — opening book implementation (port of Book.kt).

#include "book.h"

#include <cstdlib>
#include <cstring>

#include "movegen.h"

namespace tacturn {

Book &Book::instance() {
    static Book book;
    return book;
}

namespace {

Move parseUci(Position &board, const char *uci) {
    size_t len = strlen(uci);
    if (len < 4) return MOVE_NONE;
    char fromName[3] = {uci[0], uci[1], 0};
    char toName[3] = {uci[2], uci[3], 0};
    int from = squareFromName(fromName);
    int to = squareFromName(toName);
    if (from == NO_SQUARE || to == NO_SQUARE) return MOVE_NONE;
    int promo = 0;
    if (len >= 5) {
        switch (uci[4]) {
            case 'q': promo = QUEEN; break;
            case 'r': promo = ROOK; break;
            case 'b': promo = BISHOP; break;
            case 'n': promo = KNIGHT; break;
            default: promo = 0; break;
        }
    }
    Move buf[MAX_MOVES];
    int n = generateLegal(board, buf);
    for (int i = 0; i < n; i++) {
        Move m = buf[i];
        if (moveFrom(m) == from && moveTo(m) == to) {
            if (promo == 0 && movePromo(m) != 0) continue;
            if (promo != 0 && movePromo(m) != promo) continue;
            return m;
        }
    }
    return MOVE_NONE;
}

}  // namespace

void Book::ensureInit() {
    if (initialized_) return;
    std::lock_guard<std::mutex> lock(initMutex_);
    if (initialized_) return;

    Position board;
    for (size_t li = 0; li < BOOK_LINE_COUNT; li++) {
        // Split "moves|weight".
        char line[512];
        strncpy(line, BOOK_LINES[li], sizeof(line) - 1);
        line[sizeof(line) - 1] = 0;
        char *bar = strchr(line, '|');
        int weight = 1;
        if (bar) {
            *bar = 0;
            weight = atoi(bar + 1);
            if (weight < 1) weight = 1;
        }
        board.setFen(Position::START_FEN);

        // Tokenize moves on whitespace.
        char *saveptr = nullptr;
        for (char *tok = strtok_r(line, " \t", &saveptr); tok;
             tok = strtok_r(nullptr, " \t", &saveptr)) {
            Move move = parseUci(board, tok);
            if (move == MOVE_NONE) break;
            int ply = board.undoCount();
            // Store the move as a candidate for the position BEFORE it.
            Bitboard key = board.key;
            Entry &e = map_[key];
            bool merged = false;
            for (size_t i = 0; i < e.moves.size(); i++) {
                if (e.moves[i] == move) {
                    e.weights[i] += weight;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                e.moves.push_back(move);
                e.weights.push_back(weight);
            }
            board.makeMove(move);
            if (ply + 1 >= MAX_BOOK_PLY) break;
        }
    }
    initialized_ = true;
}

Move Book::probe(const Position &pos, uint64_t &rng) {
    ensureInit();
    if (pos.undoCount() >= MAX_BOOK_PLY) return MOVE_NONE;
    auto it = map_.find(pos.key);
    if (it == map_.end()) return MOVE_NONE;
    const Entry &e = it->second;
    if (e.moves.empty()) return MOVE_NONE;
    int total = 0;
    for (int w : e.weights) total += w;
    // xorshift64 — deterministic per-rng-state, matches app expectations.
    uint64_t pick;
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    pick = rng % (uint64_t)total;
    for (size_t i = 0; i < e.moves.size(); i++) {
        if (pick < (uint64_t)e.weights[i]) return e.moves[i];
        pick -= (uint64_t)e.weights[i];
    }
    return e.moves[0];
}

std::vector<std::pair<Move, int>> Book::probeAll(const Position &pos) {
    ensureInit();
    std::vector<std::pair<Move, int>> out;
    if (pos.undoCount() >= MAX_BOOK_PLY) return out;
    auto it = map_.find(pos.key);
    if (it == map_.end()) return out;
    const Entry &e = it->second;
    for (size_t i = 0; i < e.moves.size(); i++) out.push_back({e.moves[i], e.weights[i]});
    return out;
}

bool Book::contains(const Position &pos) {
    if (pos.undoCount() >= MAX_BOOK_PLY) return false;
    ensureInit();
    auto it = map_.find(pos.key);
    return it != map_.end() && !it->second.moves.empty();
}

bool Book::containsMove(const Position &pos, Move move) {
    if (move == MOVE_NONE) return false;
    if (pos.undoCount() >= MAX_BOOK_PLY) return false;
    ensureInit();
    auto it = map_.find(pos.key);
    if (it == map_.end()) return false;
    for (Move m : it->second.moves)
        if (m == move) return true;
    return false;
}

int Book::size() {
    ensureInit();
    return (int)map_.size();
}

}  // namespace tacturn
