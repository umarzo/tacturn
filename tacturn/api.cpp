// TacTurn Chess Engine v1 — C API implementation.

#include "api.h"

#include <chrono>
#include <cstring>
#include <new>
#include <string>

#include "book.h"
#include "game.h"
#include "search.h"

using namespace tacturn;

// ---- PGO training workload (see api.h contract) ----
// A fixed, deterministic position suite searched at a depth ladder with a
// fresh session per position, plus fixed-movetime searches on the same
// positions. This is the same code-path mix real games hit (search +
// quiescence + NNUE + HCE + movegen + SEE + TT).
static const char *PGO_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
    "rnbqkb1r/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq e6 0 2",
    "8/5nk1/6p1/p1P2p2/P6P/5PK1/8/6R1 w - - 0 1",
    "4k3/1q3p2/4p3/4P2p/2p5/2Q5/1P4PP/4K3 w - - 0 1",
};
static const int PGO_FEN_COUNT = (int)(sizeof(PGO_FENS) / sizeof(PGO_FENS[0]));

uint64_t tacturn_pgo_train(int32_t seconds) {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::seconds(seconds > 0 ? seconds : 60);
    uint64_t totalNodes = 0;
    int cycle = 0;
    while (clock::now() < deadline) {
        int depth = 8 + (cycle % 4) * 3;   // 8, 11, 14, 17 ladder
        int movetime = 300 + (cycle % 3) * 250;
        for (int i = 0; i < PGO_FEN_COUNT; i++) {
            if (clock::now() >= deadline) break;
            GameSession g(18 /*small TT: exercises replace paths too*/, PGO_FENS[i]);
            SearchLimits lim;
            lim.maxDepth = depth;
            lim.moveTimeMs = movetime;
            lim.hardMoveTimeMs = movetime;
            lim.threads = 1;
            lim.contemptCp = 12;
            lim.rngSeed = 1;
            SearchResult r = g.think(lim, nullptr);
            totalNodes += r.nodes;
        }
        cycle++;
    }
    return totalNodes;
}

const char *tacturn_version(void) { return "TacTurn V1"; }

int32_t tacturn_api_version(void) { return TACTURN_API_VERSION; }

TacHandle tacturn_create(int32_t ttBits) {
    if (ttBits < 12) ttBits = 12;
    if (ttBits > 30) ttBits = 30;
    GameSession *g = new (std::nothrow) GameSession((int)ttBits);
    return (TacHandle)g;
}

void tacturn_destroy(TacHandle h) {
    delete (GameSession *)h;
}

static GameSession *G(TacHandle h) { return (GameSession *)h; }

void tacturn_reset(TacHandle h) { G(h)->reset(); }

int32_t tacturn_set_fen(TacHandle h, const char *fen) {
    return G(h)->setFen(fen) ? 1 : 0;
}

int32_t tacturn_fen(TacHandle h, char *out, int32_t outLen) {
    std::string f = G(h)->fen();
    if (!out || outLen <= 0) return (int32_t)f.size();
    int32_t len = (int32_t)f.size() < outLen - 1 ? (int32_t)f.size() : outLen - 1;
    memcpy(out, f.c_str(), len);
    out[len] = 0;
    return len;
}

int32_t tacturn_side_to_move(TacHandle h) { return G(h)->board.sideToMove; }

void tacturn_board_pieces(TacHandle h, int32_t *out64) {
    const Position &b = G(h)->board;
    for (int sq = 0; sq < 64; sq++) {
        int8_t p = b.pieceAt[sq];
        out64[sq] = p < 0 ? -1 : (int32_t)p;
    }
}

int32_t tacturn_king_square(TacHandle h, int32_t color) {
    return G(h)->board.kingSquare(color);
}

int32_t tacturn_in_check(TacHandle h) { return G(h)->inCheck() ? 1 : 0; }

int32_t tacturn_halfmove_clock(TacHandle h) { return G(h)->board.halfmoveClock; }

int32_t tacturn_legal_moves(TacHandle h, int32_t *out, int32_t maxOut) {
    const std::vector<Move> &moves = G(h)->legalMoves();
    int32_t n = 0;
    for (Move m : moves) {
        if (n >= maxOut) break;
        out[n++] = (int32_t)m;
    }
    return (int32_t)moves.size();
}

int32_t tacturn_find_moves(TacHandle h, int32_t from, int32_t to, int32_t *out,
                           int32_t maxOut) {
    std::vector<Move> moves = G(h)->findMoves(from, to);
    int32_t n = 0;
    for (Move m : moves) {
        if (n >= maxOut) break;
        out[n++] = (int32_t)m;
    }
    return (int32_t)moves.size();
}

int32_t tacturn_make_move(TacHandle h, int32_t uiMove, char *sanOut, int32_t sanOutLen) {
    // Validate: the move must be one of the current legal moves.
    const std::vector<Move> &legal = G(h)->legalMoves();
    Move m = (Move)uiMove;
    bool found = false;
    for (Move l : legal)
        if (l == m) { found = true; break; }
    if (!found) return 0;
    std::string san = G(h)->makeMove(m);
    if (sanOut && sanOutLen > 0) {
        int32_t len = (int32_t)san.size() < sanOutLen - 1 ? (int32_t)san.size() : sanOutLen - 1;
        memcpy(sanOut, san.c_str(), len);
        sanOut[len] = 0;
        return len;
    }
    return (int32_t)san.size();
}

int32_t tacturn_undo(TacHandle h) { return G(h)->undo().empty() ? 0 : 1; }

int32_t tacturn_move_count(TacHandle h) { return (int32_t)G(h)->records.size(); }

int32_t tacturn_move_record(TacHandle h, int32_t index, int32_t *moveOut,
                            int32_t *capturedOut, char *sanOut, int32_t sanOutLen) {
    const std::vector<MoveRecord> &recs = G(h)->records;
    if (index < 0 || (size_t)index >= recs.size()) return 0;
    const MoveRecord &r = recs[index];
    if (moveOut) *moveOut = (int32_t)r.move;
    if (capturedOut) *capturedOut = moveCaptured(r.move);
    if (sanOut && sanOutLen > 0) {
        int32_t len = (int32_t)r.san.size() < sanOutLen - 1 ? (int32_t)r.san.size() : sanOutLen - 1;
        memcpy(sanOut, r.san.c_str(), len);
        sanOut[len] = 0;
    }
    return 1;
}

int32_t tacturn_result(TacHandle h) { return G(h)->result(); }

int32_t tacturn_book_probe(TacHandle h) { return (int32_t)G(h)->bookProbe(); }

int32_t tacturn_book_contains(TacHandle h) {
    return Book::instance().contains(G(h)->board) ? 1 : 0;
}

int32_t tacturn_book_contains_move(TacHandle h, int32_t uiMove) {
    return Book::instance().containsMove(G(h)->board, (Move)uiMove) ? 1 : 0;
}

void tacturn_think(TacHandle h, const TacSearchLimits *limits, TacIterationCallback cb,
                   void *userdata, TacSearchResult *outResult) {
    SearchLimits lim;
    lim.maxDepth = limits->maxDepth;
    lim.moveTimeMs = limits->moveTimeMs;
    lim.hardMoveTimeMs = limits->hardMoveTimeMs;
    lim.adaptiveTime = limits->adaptiveTime != 0;
    lim.depthFloor = limits->depthFloor;
    lim.lastScore = limits->lastScore;
    lim.jitterCp = limits->jitterCp;
    lim.contemptCp = limits->contemptCp;
    lim.threads = limits->threads;
    lim.rngSeed = 0;
    lim.interIterationDelayMs = limits->interIterationDelayMs;
    lim.rootExcludeCount = limits->rootExcludeCount;
    for (int i = 0; i < limits->rootExcludeCount && i < 16; i++)
        lim.rootExcludes[i] = (Move)limits->rootExcludes[i];

    SearchResult r = G(h)->think(lim, [cb, userdata](const IterationInfo &info) {
        if (cb) cb(userdata, info.depth, info.score, (int32_t)info.bestMove, info.nodes,
                   info.timeMs);
    });

    if (outResult) {
        outResult->bestMove = (int32_t)r.bestMove;
        outResult->score = r.score;
        outResult->depth = r.depth;
        outResult->nodes = r.nodes;
        outResult->timeMs = r.timeMs;
        outResult->mateIn = r.mateIn;
    }
}

void tacturn_request_stop(TacHandle h) { G(h)->requestStop(); }

void tacturn_clear_search_table(TacHandle h) { G(h)->clearSearchTable(); }

int32_t tacturn_evaluate(TacHandle h) {
    PawnHashTable hash;
    return evaluate(G(h)->board, hash);
}

// ---- Multi-PV analysis ----

int32_t tacturn_analyze(TacHandle h, const TacSearchLimits *limits, int32_t multiPv,
                        TacIterationCallback cb, void *userdata,
                        TacPvLine *outLines, int32_t maxLines) {
    if (!outLines || maxLines <= 0) return 0;
    GameSession *g = G(h);

    // Base limits shared by every line.
    SearchLimits base;
    base.maxDepth = limits->maxDepth;
    base.moveTimeMs = limits->moveTimeMs;
    base.hardMoveTimeMs = limits->hardMoveTimeMs;
    base.adaptiveTime = limits->adaptiveTime != 0;
    base.depthFloor = limits->depthFloor;
    base.lastScore = limits->lastScore;
    base.jitterCp = limits->jitterCp;   // analysis never jitters, but honor it
    base.contemptCp = limits->contemptCp;
    base.threads = limits->threads;
    base.rngSeed = 0;
    base.interIterationDelayMs = limits->interIterationDelayMs;
    base.rootExcludeCount = 0;

    int want = multiPv < 1 ? 1 : multiPv;
    if (want > maxLines) want = maxLines;

    int lines = 0;
    for (int k = 0; k < want; k++) {
        SearchLimits lim = base;
        // Exclude the best moves of the previous lines from the root.
        lim.rootExcludeCount = k;
        for (int e = 0; e < k; e++) {
            if (outLines[e].pvLen < 1) { lim.rootExcludeCount = e; break; }
            lim.rootExcludes[e] = (Move)outLines[e].pv[0];
        }
        if (lim.rootExcludeCount < k) break;  // earlier line had no move

        // Only the FIRST line streams live iteration progress to the UI.
        SearchResult r = g->think(lim, [cb, userdata, k](const IterationInfo &info) {
            if (cb && k == 0)
                cb(userdata, info.depth, info.score, (int32_t)info.bestMove, info.nodes,
                   info.timeMs);
        });
        if (r.bestMove == MOVE_NONE) break;  // no more legal moves / stopped early

        TacPvLine &line = outLines[lines++];
        line.score = r.score;
        line.depth = r.depth;
        line.mateIn = r.mateIn;
        // pv[] is int32_t while Move is uint32_t — same 4 bytes on every
        // supported ABI; cast keeps the C-facing struct plain int.
        line.pvLen = g->extractPv(r.bestMove, (Move *)line.pv, TACTURN_MAX_PV);
        if (line.pvLen == 0) {
            // TT walk failed (fresh table / torn entries): fall back to the
            // root move alone so the line is still usable.
            line.pv[0] = r.bestMove;
            line.pvLen = 1;
        }
    }
    return lines;
}

int32_t tacturn_san_for(TacHandle h, int32_t uiMove, char *sanOut, int32_t sanOutLen) {
    if (!sanOut || sanOutLen <= 0) return 0;
    // Validate legality before generating SAN (sanFor assumes a legal move).
    const std::vector<Move> &legal = G(h)->legalMoves();
    Move m = (Move)uiMove;
    bool found = false;
    for (Move l : legal)
        if (l == m) { found = true; break; }
    if (!found) return 0;
    std::string san = G(h)->sanFor(m);
    int32_t len = (int32_t)san.size() < sanOutLen - 1 ? (int32_t)san.size() : sanOutLen - 1;
    memcpy(sanOut, san.c_str(), len);
    sanOut[len] = 0;
    return len;
}

void tacturn_resign(TacHandle h, int32_t winnerColor) {
    G(h)->resign(winnerColor);
}

void tacturn_agree_draw(TacHandle h) {
    G(h)->agreeDraw();
}
