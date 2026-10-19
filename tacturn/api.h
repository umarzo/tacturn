// TacTurn Chess Engine v1 — stable flat C API.
//
// The boundary between the Kotlin facade (JNI) and the C++ engine. Every
// type crossing this line is a C type; handles are opaque pointers. The API
// mirrors exactly what the Android app's GameViewModel needs, with the same
// semantics as the original Kotlin engine's Game/Searcher/Book classes.

#ifndef TACTURN_API_H
#define TACTURN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** API/protocol version for defensive checks on the Kotlin side.
 *  v4: TacSearchLimits gained contemptCp; book-contains queries added. */
#define TACTURN_API_VERSION 4

/** Search limits — mirrors the Kotlin SearchLimits data class.
 *  rootExcludes[] + rootExcludeCount enable Multi-PV by sequential
 *  root-exclusion searches; interIterationDelayMs duty-cycles the CPU
 *  (battery saver for long analysis sessions); contemptCp is the draw
 *  contempt used inside the search tree (play: 12, analysis: 0). */
typedef struct {
    int32_t maxDepth;        // default 64
    int64_t moveTimeMs;      // default 5000
    int64_t hardMoveTimeMs;  // 0 -> moveTimeMs
    int32_t adaptiveTime;
    int32_t depthFloor;      // default 8
    int32_t lastScore;
    int32_t jitterCp;
    int32_t contemptCp;      // draw contempt: play 12, analysis 0
    int32_t threads;         // default 1
    int32_t interIterationDelayMs;              // 0 = none
    int32_t rootExcludeCount;                   // moves to skip at root 
    int32_t rootExcludes[16];                   // UI move ints 
} TacSearchLimits;

/** Max length of an extracted principal variation. */
#define TACTURN_MAX_PV 24

/** v1.1: PGO training entry point. Runs a deterministic engine workload
 *  (bench-suite searches at increasing depths, plus fixed-time searches on
 *  the same positions) for `seconds` wall-clock seconds, exercising the
 *  full hot path (search, quiescence, NNUE inference, HCE eval, movegen,
 *  SEE, TT). Used ONLY to generate profile data for a -fprofile-use
 *  rebuild (see scripts/pgo/README-PGO.md). Returns total nodes searched. */
uint64_t tacturn_pgo_train(int32_t seconds);

/** One analysis line: score + PV as UI move ints, best first. */
typedef struct {
    int32_t score;    // centipawns (or mate scores) from mover POV
    int32_t depth;    // completed depth of the search that produced it
    int32_t mateIn;   // moves to mate (positive: we deliver)
    int32_t pvLen;    // number of moves in pv[] (>= 1)
    int32_t pv[TACTURN_MAX_PV];
} TacPvLine;

/** Search output — mirrors the Kotlin SearchResult data class. */
typedef struct {
    int32_t bestMove;    // UI move int (MOVE_NONE = 0 when no move)
    int32_t score;       // centipawns (or mate scores) from mover's POV
    int32_t depth;
    int64_t nodes;
    int64_t timeMs;
    int32_t mateIn;      // moves to mate (positive: we deliver)
} TacSearchResult;

/** Progress callback: called from the search thread once per iteration. */
typedef void (*TacIterationCallback)(void *userdata, int32_t depth, int32_t score,
                                     int32_t bestMove, int64_t nodes, int64_t timeMs);

/** Engine + game session handle. */
typedef void *TacHandle;

const char *tacturn_version(void);
int32_t tacturn_api_version(void);

TacHandle tacturn_create(int32_t ttBits);
void tacturn_destroy(TacHandle h);

void tacturn_reset(TacHandle h);
/** 1 on success (records cleared); 0 on invalid FEN (state unchanged). */
int32_t tacturn_set_fen(TacHandle h, const char *fen);
/** Current position FEN (written into out, incl. terminating NUL). */
int32_t tacturn_fen(TacHandle h, char *out, int32_t outLen);

int32_t tacturn_side_to_move(TacHandle h);
/** Fills out[64]: -1 empty, else (color<<3)|type. */
void tacturn_board_pieces(TacHandle h, int32_t *out64);
int32_t tacturn_king_square(TacHandle h, int32_t color);
int32_t tacturn_in_check(TacHandle h);
int32_t tacturn_halfmove_clock(TacHandle h);

/** Fully-legal moves (UI move ints); returns count (<= maxOut). */
int32_t tacturn_legal_moves(TacHandle h, int32_t *out, int32_t maxOut);
/** Legal moves matching from/to (promotion candidates). */
int32_t tacturn_find_moves(TacHandle h, int32_t from, int32_t to, int32_t *out,
                           int32_t maxOut);

/** Applies a legal UI move int; returns its SAN length (0 if not applied). */
int32_t tacturn_make_move(TacHandle h, int32_t uiMove, char *sanOut, int32_t sanOutLen);
/** Undoes the last move; returns 1 when something was undone. */
int32_t tacturn_undo(TacHandle h);

/** Number of recorded moves. */
int32_t tacturn_move_count(TacHandle h);
/** Record i: {move, capturedPieceType, san}. Returns 0 if i out of range. */
int32_t tacturn_move_record(TacHandle h, int32_t index, int32_t *moveOut,
                            int32_t *capturedOut, char *sanOut, int32_t sanOutLen);

/** GameResultValue (see game.h numbering — mirrored in Kotlin). */
int32_t tacturn_result(TacHandle h);

/** Ends the game immediately as a draw by mutual agreement. */
void tacturn_agree_draw(TacHandle h);

/** Weighted-random book move (UI move int, 0 when not in book). */
int32_t tacturn_book_probe(TacHandle h);

/** 1 when the current position is known opening theory (book), else 0.
 *  Game review uses this to label "Book" moves and exclude them from
 *  accuracy aggregates (the industry convention). */
int32_t tacturn_book_contains(TacHandle h);

/** 1 when `uiMove` is one of the book continuations of the CURRENT
 *  position (a "book move"), else 0. */
int32_t tacturn_book_contains_move(TacHandle h, int32_t uiMove);

/** Runs the search on the calling thread; blocks until done. */
void tacturn_think(TacHandle h, const TacSearchLimits *limits, TacIterationCallback cb,
                   void *userdata, TacSearchResult *outResult);
void tacturn_request_stop(TacHandle h);
void tacturn_clear_search_table(TacHandle h);

/** Static evaluation of the current position (mover POV, centipawns). */
int32_t tacturn_evaluate(TacHandle h);

/* ================= analysis API ================= */

/**
 * Multi-PV analysis of the current position.
 *
 * Runs up to `multiPv` sequential searches, each excluding the best moves
 * found by the previous ones (root-exclusion Multi-PV), sharing the
 * transposition table so later lines stay cheap. PVs are extracted by
 * walking the TT after each search. Lines are written best-first into
 * outLines (at most maxLines).
 *
 * The progress callback fires only for the FIRST line (live depth/score
 * for the main PV); subsequent lines are reported via the returned array.
 *
 * Returns the number of lines written (0 when the position has no legal
 * moves or the search was stopped before any line completed).
 */
int32_t tacturn_analyze(TacHandle h, const TacSearchLimits *limits, int32_t multiPv,
                        TacIterationCallback cb, void *userdata,
                        TacPvLine *outLines, int32_t maxLines);

/**
 * SAN (Standard Algebraic Notation) for a legal move in the CURRENT
 * position, without applying it. Writes into sanOut (incl. NUL) and
 * returns the length; returns 0 when the move is not legal here.
 */
int32_t tacturn_san_for(TacHandle h, int32_t uiMove, char *sanOut, int32_t sanOutLen);

/** ends the game immediately — winnerColor 0/1 (ignored when 0 means
 *  White; pass the WINNING color). Undone by reset/undo/setFen. */
void tacturn_resign(TacHandle h, int32_t winnerColor);

#ifdef __cplusplus
}
#endif

#endif  // TACTURN_API_H
