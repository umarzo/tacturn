// TacTurn Chess Engine v1 — precomputed attack tables and magic bitboards.
//
// Ported from Tables.kt, upgraded: sliding attacks now use magic bitboards
// (generated deterministically at init with splitmix64 — no hand-maintained
// constants, identical results on every platform/build). Zobrist keys come
// from the same deterministic PRNG, so keys are stable across processes
// (needed by the opening book, which is position-keyed).
//
// v1.4.25 (B1): the per-square magic descriptors (mask / magic / table
// pointer / shift) now live in ONE alignas(32) 32-byte MagicSlot instead of
// four separate global arrays — the magic lookup on the non-PEXT path
// (Android arm64, pre-BMI2 x86) touches a single cache line instead of
// four, and the tables are backed by contiguous alignas(64) static pools
// instead of 128 fragmented new[] blocks.
//
// v1.4.25 (B1): whenever the compiler has BMI2 enabled globally
// (-march=native on a BMI2 host), the PEXT lookup is inlined directly into
// rookAttacks()/bishopAttacks() — the old shape routed every one of the
// ~30M slider calls per million nodes through an out-of-line
// __attribute__((target("bmi2"))) thunk that GCC's LTO inliner refuses to
// inline into callers without the matching target attribute. Builds
// without global BMI2 keep the exact runtime dispatch.
//
// v1.4.25 (B2): ROOK_PSEUDO[64] / BISHOP_PSEUDO[64] hold the empty-board
// sliding rays. sliderAttacks(sq, occ) is a subset of the empty-board ray,
// so "does bitboard X intersect the real attacks?" can be pre-gated on
// "does X intersect the pseudo ray?" — a pure table AND with no occupancy
// dependence. Call sites: isAttacked, attackersTo, makeMove checker
// detection, givesCheckQuiet, SEE's leastValuableAttacker, and
// capturesOnly move generation.
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
// (v1.4.27 B3 note: moreThanOne already lives in types.h — reused by the
// evaluate.cpp long-diagonal test; no new helper needed.)
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

// ---- v1.4.25 (B2): empty-board pseudo rays ----
// sliderAttacks(sq, occ) ⊆ *_PSEUDO[sq] for every occupancy, so a nonzero
// intersection with the real attacks implies a nonzero intersection with
// the pseudo ray. Pure lookup — no occupancy needed.
extern Bitboard ROOK_PSEUDO[64];
extern Bitboard BISHOP_PSEUDO[64];

// ---- Magic bitboards ----
// v1.4.25 (B1): one 32-byte aligned descriptor per square (mask, magic,
// table pointer, shift + pad) over contiguous static pools.
struct alignas(32) MagicSlot {
    Bitboard mask;
    Bitboard magic;
    Bitboard *attacks;
    unsigned shift;
    unsigned pad;
};
extern MagicSlot ROOK_MAGICS[64];
extern MagicSlot BISHOP_MAGICS[64];

// ---- BMI2 (PEXT) fast path ----
// On x86-64 with BMI2, dense PEXT-indexed attack tables replace the
// magic multiply/shift lookup (no masked occupancy AND, one instruction
// index computation, better table locality). With global __BMI2__ the
// lookup is inlined below; otherwise dispatch is resolved once at
// initTables() — the branch is perfectly predicted. Non-x86
// (Android arm64 etc.) always uses the magic path, unchanged.
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define TACTURN_PEXT_DISPATCH 1
#include <immintrin.h>
struct alignas(16) PextSlot {
    Bitboard mask;
    Bitboard *attacks;
};
extern PextSlot PEXT_ROOK[64];
extern PextSlot PEXT_BISHOP[64];
extern bool pextAttacksEnabled;
Bitboard pextRookAttacks(int sq, Bitboard occ);
Bitboard pextBishopAttacks(int sq, Bitboard occ);
#endif

inline Bitboard rookAttacks(int sq, Bitboard occ) {
#if defined(TACTURN_PEXT_DISPATCH)
#if defined(__BMI2__)
    // v1.4.25 (B1): compile-time BMI2 — direct table lookup, no call.
    const PextSlot &ps = PEXT_ROOK[sq];
    return ps.attacks[_pext_u64(occ, ps.mask)];
#else
    if (pextAttacksEnabled) return pextRookAttacks(sq, occ);
#endif
#endif
    const MagicSlot &ms = ROOK_MAGICS[sq];
    return ms.attacks[(unsigned)(((occ & ms.mask) * ms.magic) >> ms.shift)];
}

inline Bitboard bishopAttacks(int sq, Bitboard occ) {
#if defined(TACTURN_PEXT_DISPATCH)
#if defined(__BMI2__)
    // v1.4.25 (B1): compile-time BMI2 — direct table lookup, no call.
    const PextSlot &ps = PEXT_BISHOP[sq];
    return ps.attacks[_pext_u64(occ, ps.mask)];
#else
    if (pextAttacksEnabled) return pextBishopAttacks(sq, occ);
#endif
#endif
    const MagicSlot &ms = BISHOP_MAGICS[sq];
    return ms.attacks[(unsigned)(((occ & ms.mask) * ms.magic) >> ms.shift)];
}

inline Bitboard queenAttacks(int sq, Bitboard occ) {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}

/** Idempotent init of every table (cheap after the first call). */
void initTables();

}  // namespace tacturn
