// TacTurn Chess Engine v1 — core types and move encoding.
//
// Ported from the original Kotlin engine (Types.kt). The 32-bit move encoding
// is kept BIT-IDENTICAL so the Kotlin UI layer (moveFrom/moveTo/... helpers)
// works unchanged on values returned across the JNI boundary:
//
//   bits  0.. 5 : from square
//   bits  6..11 : to square
//   bits 12..14 : moving piece type
//   bits 15..17 : captured piece type (NO_PIECE when quiet)
//   bits 18..20 : promotion piece type (0 = none)
//   bit  21     : en-passant capture
//   bit  22     : castling (king side)
//   bit  23     : castling (queen side)
//   bit  24     : double pawn push
//
// Board layout: square 0 = a1, 63 = h8, sq = rank*8 + file.
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include <cstdint>
#include <string>

namespace tacturn {

constexpr int WHITE = 0, BLACK = 1;
constexpr int PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5;
constexpr int NO_PIECE = 6;
constexpr int NO_SQUARE = -1;

constexpr int MAX_MOVES = 256;
constexpr int MAX_PLY = 128;

// Castling right bits.
constexpr int CASTLE_WK = 1, CASTLE_WQ = 2, CASTLE_BK = 4, CASTLE_BQ = 8;

// Move flag bits.
constexpr uint32_t FLAG_EP = 1u << 21;
constexpr uint32_t FLAG_CASTLE_K = 1u << 22;
constexpr uint32_t FLAG_CASTLE_Q = 1u << 23;
constexpr uint32_t FLAG_DOUBLE = 1u << 24;

// Sentinel meaning "no move" (a1 -> a1 cannot be a legal move).
constexpr uint32_t MOVE_NONE = 0;

typedef uint32_t Move;
typedef uint64_t Bitboard;

constexpr int SQUARE_OF(int file, int rank) { return rank * 8 + file; }

inline int fileOf(int sq) { return sq & 7; }
inline int rankOf(int sq) { return sq >> 3; }

inline constexpr Move encodeMove(int from, int to, int piece,
                                 int captured = NO_PIECE, int promo = 0,
                                 uint32_t flags = 0) {
    return Move(from | (to << 6) | (piece << 12) | (captured << 15) | (promo << 18) | flags);
}

inline int moveFrom(Move m) { return m & 63; }
inline int moveTo(Move m) { return (m >> 6) & 63; }
inline int movePiece(Move m) { return (m >> 12) & 7; }
inline int moveCaptured(Move m) { return (m >> 15) & 7; }
inline int movePromo(Move m) { return (m >> 18) & 7; }
inline bool isEPMove(Move m) { return (m & FLAG_EP) != 0; }
inline bool isCastleK(Move m) { return (m & FLAG_CASTLE_K) != 0; }
inline bool isCastleQ(Move m) { return (m & FLAG_CASTLE_Q) != 0; }
inline bool isCastleMove(Move m) { return (m & (FLAG_CASTLE_K | FLAG_CASTLE_Q)) != 0; }
inline bool isDoublePush(Move m) { return (m & FLAG_DOUBLE) != 0; }
inline bool isCapture(Move m) { return moveCaptured(m) != NO_PIECE; }
inline bool isQuiet(Move m) { return moveCaptured(m) == NO_PIECE && movePromo(m) == 0; }

extern const int PIECE_VALUES[7];  // P N B R Q K NO_PIECE (centipawns)

char pieceFenChar(int type);
int pieceTypeFromFen(char c);
int pieceSideFromFen(char c);
char sanPromoLetter(int type);

/** "e4"-style square name into out (out must have >= 3 bytes). */
void squareName(int sq, char out[3]);
int squareFromName(const char *name);

/** UCI string ("e2e4", "a7a8q") for a move; out must hold >= 8 bytes. */
void moveToUci(Move m, char out[8]);
std::string moveToUciStr(Move m);

// Bit helpers — portable compiler builtins (clang/gcc, x86 & ARM).
inline int lsb(Bitboard b) { return __builtin_ctzll(b); }
inline int msb(Bitboard b) { return 63 - __builtin_clzll(b); }
inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline Bitboard bitOf(int sq) { return 1ULL << sq; }
inline bool moreThanOne(Bitboard b) { return b & (b - 1); }

/** Deterministic 64-bit PRNG (splitmix64) — used for tables & Zobrist keys. */
inline uint64_t splitmix64(uint64_t &state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace tacturn
