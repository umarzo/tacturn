// TacTurn Chess Engine v1 — game session implementation (port of Game.kt).

#include "game.h"

#include <chrono>

#include "book.h"
#include "movegen.h"

namespace tacturn {

// ------------------------------------------------------------------
// v1.3.2 TT move-key decoder (see ttable.h). from/to/promo identify a
// legal move uniquely; every other field of the 32-bit encoding is a
// pure function of the probing placement, exactly as movegen derived
// it when the move was stored:
//   piece    = the piece standing on `from` (promotions move a pawn)
//   captured = the piece standing on `to`, or a pawn for a diagonal
//              pawn move to the ep square (the only empty-target
//              diagonal a generated pawn move can have)
//   castle   = king moving exactly two files
//   double   = pawn moving exactly two ranks
// Stale entries whose `from` is empty (a torn or colliding store)
// decode to MOVE_NONE — an inert ordering hint, same as before.
// ------------------------------------------------------------------
Move ttDecodeMove(const Position &b, uint16_t moveKey) {
    if (moveKey == 0) return MOVE_NONE;
    int from = moveKey & 63;
    int to = (moveKey >> 6) & 63;
    int promo = (moveKey >> 12) & 7;

    int piece = b.pieceTypeOf(from);
    if (piece == NO_PIECE) return MOVE_NONE;  // not a move of this position

    int captured = NO_PIECE;
    uint32_t flags = 0;
    int victim = b.pieceTypeOf(to);
    if (victim != NO_PIECE) {
        captured = victim;
    } else if (piece == PAWN && fileOf(from) != fileOf(to) && to == b.epSquare) {
        captured = PAWN;
        flags = FLAG_EP;
    }
    if (piece == KING && (to == from + 2 || to == from - 2)) {
        flags = to > from ? FLAG_CASTLE_K : FLAG_CASTLE_Q;
    } else if (piece == PAWN && (to == from + 16 || to == from - 16)) {
        flags = FLAG_DOUBLE;
    }
    return encodeMove(from, to, piece, captured, promo, flags);
}

GameSession::GameSession(int ttBits, const char *initialFen)
    : searcher(ttBits),
      bookRng_((uint64_t)std::chrono::steady_clock::now().time_since_epoch().count() ^
               0x9E3779B97F4A7C15ULL) {
    reset(initialFen);
}

void GameSession::reset(const char *fen) {
    board.setFen(fen ? fen : Position::START_FEN);
    records.clear();
    legalCacheValid = false;
    resultOverride = 0;
}

bool GameSession::setFen(const char *fen) {
    if (!board.setFen(fen)) return false;
    records.clear();
    legalCacheValid = false;
    resultOverride = 0;
    return true;
}

const std::vector<Move> &GameSession::legalMoves() {
    if (!legalCacheValid) {
        legalCache.clear();
        Move buf[MAX_MOVES];
        int n = generateLegal(board, buf);
        legalCache.assign(buf, buf + n);
        legalCacheValid = true;
    }
    return legalCache;
}

std::vector<Move> GameSession::findMoves(int from, int to) const {
    // Cached list is mutable state; compute locally to stay const-safe.
    Move buf[MAX_MOVES];
    int n = generateLegal(const_cast<Position &>(board), buf);
    std::vector<Move> out;
    for (int i = 0; i < n; i++) {
        if (moveFrom(buf[i]) == from && moveTo(buf[i]) == to) out.push_back(buf[i]);
    }
    return out;
}

std::vector<Move> GameSession::legalMovesFrom(int sq) const {
    Move buf[MAX_MOVES];
    int n = generateLegal(const_cast<Position &>(board), buf);
    std::vector<Move> out;
    for (int i = 0; i < n; i++) {
        if (moveFrom(buf[i]) == sq) out.push_back(buf[i]);
    }
    return out;
}

std::string GameSession::makeMove(Move move) {
    std::string san = sanFor(move);
    board.makeMove(move);
    records.push_back({move, san});
    legalCacheValid = false;
    return san;
}

std::string GameSession::undo() {
    if (records.empty()) return "";
    std::string san = records.back().san;
    records.pop_back();
    board.unmakeMove();
    legalCacheValid = false;
    resultOverride = 0;   // taking a move back revives the game
    return san;
}

int GameSession::result() {
    if (resultOverride != 0) return resultOverride;   // resignation
    const std::vector<Move> &legal = legalMoves();
    if (legal.empty()) {
        if (board.inCheck(board.sideToMove)) {
            return board.sideToMove == WHITE ? GR_BLACK_WINS : GR_WHITE_WINS;
        }
        return GR_DRAW_STALEMATE;
    }
    if (board.halfmoveClock >= 100) return GR_DRAW_FIFTY_MOVE;
    if (board.repetitionCount() >= 3) return GR_DRAW_REPETITION;
    if (insufficientMaterial()) return GR_DRAW_INSUFFICIENT_MATERIAL;
    return GR_ONGOING;
}

bool GameSession::insufficientMaterial() const {
    if ((board.pieceBb[PAWN] | board.pieceBb[ROOK] | board.pieceBb[QUEEN]) != 0) return false;
    int whiteMinors = popcount(board.pieceBb[BISHOP] & board.colorBb[WHITE]) +
                      popcount(board.pieceBb[KNIGHT] & board.colorBb[WHITE]);
    int blackMinors = popcount(board.pieceBb[BISHOP] & board.colorBb[BLACK]) +
                      popcount(board.pieceBb[KNIGHT] & board.colorBb[BLACK]);
    return whiteMinors <= 1 && blackMinors <= 1;
}

int GameSession::checkSquare() const {
    return board.inCheck(board.sideToMove) ? board.kingSquare(board.sideToMove) : NO_SQUARE;
}

Move GameSession::bookProbe(uint64_t rng) {
    // Explicit rng (tests, reproducible walks); 0 keeps the clock-seeded
    // default so callers get varied openings.
    return Book::instance().probe(board, rng ? rng : bookRng_);
}

// TT-walk PV extraction. See game.h for the contract; every step is
// validated against the legal-move list so a torn/stale TT entry can only
// shorten the PV, never corrupt the board or crash.
int GameSession::extractPv(Move first, Move *out, int maxOut) {
    if (first == MOVE_NONE || out == nullptr || maxOut <= 0) return 0;

    // `first` must be legal in the current position.
    Move buf[MAX_MOVES];
    int legalCount = generateLegal(board, buf);
    bool legal = false;
    for (int i = 0; i < legalCount && !legal; i++) legal = (buf[i] == first);
    if (!legal) return 0;

    int n = 0;
    out[n++] = first;
    board.makeMove(first);

    while (n < maxOut) {
        // Repetition / 50-move guard: the PV stops before looping forever.
        if (board.halfmoveClock >= 100 || board.isRepetitionDraw()) break;

        TranspositionTable::ProbeResult pr = searcher.tt().probe(board, board.key);
        if (!pr.hit || pr.move == MOVE_NONE) break;

        // The TT move must be legal here.
        legalCount = generateLegal(board, buf);
        Move m = MOVE_NONE;
        for (int i = 0; i < legalCount; i++) {
            if (buf[i] == pr.move) { m = pr.move; break; }
        }
        if (m == MOVE_NONE) break;

        out[n++] = m;
        board.makeMove(m);
    }

    // Restore the board exactly.
    for (int i = 0; i < n; i++) board.unmakeMove();
    return n;
}

std::string GameSession::sanFor(Move move) {
    int from = moveFrom(move);
    int to = moveTo(move);
    int piece = movePiece(move);
    int captured = moveCaptured(move);
    int promo = movePromo(move);

    char fromName[3], toName[3];
    squareName(from, fromName);
    squareName(to, toName);

    std::string base;
    if (isCastleK(move)) {
        base = "O-O";
    } else if (isCastleQ(move)) {
        base = "O-O-O";
    } else if (piece == PAWN) {
        std::string s = captured != NO_PIECE
                            ? std::string(1, fromName[0]) + "x" + toName
                            : std::string(toName);
        if (promo != 0) {
            s += "=";
            s += sanPromoLetter(promo);
        }
        base = s;
    } else {
        static const char LETTERS[6] = {' ', 'N', 'B', 'R', 'Q', 'K'};
        std::string letter(1, LETTERS[piece]);
        // Disambiguation: other legal moves of the same piece type to `to`.
        std::vector<Move> all = legalMoves();
        bool sameFile = false, sameRank = false, anyRival = false;
        for (Move it : all) {
            if (it == move || movePiece(it) != piece || moveTo(it) != to ||
                moveFrom(it) == from)
                continue;
            anyRival = true;
            if ((moveFrom(it) & 7) == (from & 7)) sameFile = true;
            if ((moveFrom(it) >> 3) == (from >> 3)) sameRank = true;
        }
        std::string disambiguation;
        if (anyRival) {
            if (!sameFile) disambiguation = std::string(1, fromName[0]);
            else if (!sameRank) disambiguation = std::string(1, fromName[1]);
            else disambiguation = fromName;
        }
        base = letter + disambiguation + (captured != NO_PIECE ? "x" : "") + toName;
    }

    // Check / mate suffix.
    board.makeMove(move);
    std::string suffix;
    if (board.inCheck(board.sideToMove)) {
        int defender = board.sideToMove;
        Move buf[MAX_MOVES];
        int n = generate(board, buf);
        bool hasLegal = false;
        for (int i = 0; i < n && !hasLegal; i++) {
            board.makeMove(buf[i]);
            if (!board.inCheck(defender)) hasLegal = true;
            board.unmakeMove();
        }
        suffix = hasLegal ? "+" : "#";
    }
    board.unmakeMove();
    return base + suffix;
}

}  // namespace tacturn
