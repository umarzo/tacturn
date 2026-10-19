// TacTurn Chess Engine v1 — precomputed attack tables and magic bitboards.
//
// Ported from Tables.kt, upgraded: sliding attacks now use magic bitboards
// (generated deterministically at init with splitmix64 — no hand-maintained
// constants, identical results on every platform/build). Zobrist keys come
// from the same deterministic PRNG, so keys are stable across processes
// (needed by the opening book, which is position-keyed).
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include "types.h"

namespace tacturn {

extern Bitboard KNIGHT_ATT[64];
extern Bitboard KING_ATT[64];
extern Bitboard PAWN_ATT[2][64];       // squares attacked by a pawn of `color` on `sq`
extern Bitboard FILES_BB[8];
extern Bitboard RANKS_BB[8];
extern Bitboard ADJACENT_FILES[8];
extern Bitboard RANKS_LE[8];   // ranks 0..r inclusive
extern Bitboard RANKS_GE[8];   // ranks r..7 inclusive
constexpr Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;
constexpr Bitboard DARK_SQUARES = ~LIGHT_SQUARES;

/** Squares of the same colour as `sq`. */
inline Bitboard colorSquaresOf(int sq) {
    return (((sq >> 3) + (sq & 7)) & 1) ? LIGHT_SQUARES : DARK_SQUARES;
}
extern Bitboard PASSED_MASK[2][64];    // squares that must be free of enemy pawns
extern Bitboard SHIELD_MASK[2][64];    // two ranks directly in front of a king
extern Bitboard BETWEEN_BB[64][64];    // strictly between aligned squares (else 0)
extern int CASTLE_RIGHTS_MASK[64];

extern Bitboard Z_PIECE[12][64];
extern Bitboard Z_CASTLE[16];
extern Bitboard Z_EP_FILE[8];
extern Bitboard Z_SIDE;

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = 0x8080808080808080ULL;
constexpr Bitboard NOT_FILE_A_BB = ~FILE_A_BB;
constexpr Bitboard NOT_FILE_H_BB = ~FILE_H_BB;

// ---- Magic bitboards ----
extern Bitboard ROOK_MASK[64];
extern Bitboard BISHOP_MASK[64];
extern Bitboard ROOK_MAGIC_N[64];      // magic multiplier
extern Bitboard BISHOP_MAGIC_N[64];
extern int ROOK_SHIFT[64];
extern int BISHOP_SHIFT[64];
extern Bitboard *ROOK_ATT[64];         // per-square table, pool-backed
extern Bitboard *BISHOP_ATT[64];

// ---- BMI2 (PEXT) fast path ----
// On x86-64 with BMI2, dense PEXT-indexed attack tables replace the
// magic multiply/shift lookup (no masked occupancy AND, one instruction
// index computation, better table locality). Dispatch is resolved once
// at initTables() — the branch below is perfectly predicted. Non-x86
// (Android arm64 etc.) always uses the magic path, unchanged.
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define TACTURN_PEXT_DISPATCH 1
extern bool pextAttacksEnabled;
Bitboard pextRookAttacks(int sq, Bitboard occ);
Bitboard pextBishopAttacks(int sq, Bitboard occ);
#endif

inline Bitboard rookAttacks(int sq, Bitboard occ) {
#if defined(TACTURN_PEXT_DISPATCH)
    if (pextAttacksEnabled) return pextRookAttacks(sq, occ);
#endif
    Bitboard o = occ & ROOK_MASK[sq];
    return ROOK_ATT[sq][(unsigned)((o * ROOK_MAGIC_N[sq]) >> ROOK_SHIFT[sq])];
}

inline Bitboard bishopAttacks(int sq, Bitboard occ) {
#if defined(TACTURN_PEXT_DISPATCH)
    if (pextAttacksEnabled) return pextBishopAttacks(sq, occ);
#endif
    Bitboard o = occ & BISHOP_MASK[sq];
    return BISHOP_ATT[sq][(unsigned)((o * BISHOP_MAGIC_N[sq]) >> BISHOP_SHIFT[sq])];
}

inline Bitboard queenAttacks(int sq, Bitboard occ) {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}

/** Idempotent init of every table (cheap after the first call). */
void initTables();

}  // namespace tacturn
