// TacTurn Chess Engine v1 — bitboard table generation (magic bitboards).
//
// v1.4.25 (B1): magicMask edge-stripping fixed. The old formula
//     edges = (R1|R8|FA|FH) & ~(R[own] | F[own])
// intersected the edge set with the complement of the rook's own rank ∪
// own file. Every square of a ROOK ray lies on the rook's rank or file,
// so `ray & ~edges == ray` for every rook square: no edge was ever
// stripped and ROOK_MASK kept all 14 bits — 64 × 2^14 = 1,048,576 table
// entries (8.39 MB) per table instead of 102,400 (800 KB), blowing out
// L2/L3 on every slider lookup. The Stockfish form,
//     edges = ((R1|R8) & ~R[own]) | ((FA|FH) & ~F[own]),
// strips exactly the ray endpoints a blocker can never hide behind.
// Both ROOK_ATT and PEXT_ROOK shrink 10.24x (16.78 MB -> 1.64 MB total).
// Bishop masks were already correct under the old formula (a bishop ray
// never touches its own rank/file), so bishop tables are unchanged in
// size; the magic search consumes a different number of PRNG draws after
// the rook fix, so the found bishop magics differ — the ATTACK SETS every
// lookup returns are bit-identical for every (sq, occ), which is the
// contract the rest of the engine relies on (bench-verified bit-exact:
// 196,608 / 1,881,088 nodes on benches 9 / 14).
//
// v1.4.25 (B1): all four slider tables are backed by contiguous
// alignas(64) static pools (no 256 fragmented new[] allocations, no
// allocator round-trip at startup) and indexed through MagicSlot /
// PextSlot descriptors.
//
// v1.4.25 (B2): ROOK_PSEUDO / BISHOP_PSEUDO (empty-board rays) are built
// here and exported for the pseudo-ray gating at the engine's hottest
// slider call sites.
//
// All original code — MIT licensed, TacTurn contributors.

#include "bitboard.h"

#include <algorithm>
#include <mutex>
#include <vector>

#if defined(TACTURN_PEXT_DISPATCH)
#include <immintrin.h>
#endif

namespace tacturn {

Bitboard KNIGHT_ATT[64];
Bitboard KING_ATT[64];
Bitboard PAWN_ATT[2][64];
Bitboard FILES_BB[8];
Bitboard RANKS_BB[8];
Bitboard ADJACENT_FILES[8];
Bitboard RANKS_LE[8];
Bitboard RANKS_GE[8];
Bitboard PASSED_MASK[2][64];
Bitboard SHIELD_MASK[2][64];
Bitboard BETWEEN_BB[64][64];
int CASTLE_RIGHTS_MASK[64];

Bitboard Z_PIECE[12][64];
Bitboard Z_CASTLE[16];
Bitboard Z_EP_FILE[8];
Bitboard Z_SIDE;

// v1.4.25 (B2): empty-board pseudo rays (occupancy-independent).
Bitboard ROOK_PSEUDO[64];
Bitboard BISHOP_PSEUDO[64];

// v1.4.25 (B1): packed per-square magic descriptors over static pools.
MagicSlot ROOK_MAGICS[64];
MagicSlot BISHOP_MAGICS[64];

namespace {

constexpr int DIR_DR[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int DIR_DF[8] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr int DIRS_R[4] = {0, 1, 2, 3};   // N, S, E, W
constexpr int DIRS_B[4] = {4, 5, 6, 7};   // NE, NW, SE, SW

// Pool totals with the FIXED relevant-occupancy masks:
//   rook   = 36*2^10 + 24*2^11 + 4*2^12 = 102,400 entries (800 KB)
//   bishop = sum of 2^bits                          =  5,248 entries (41 KB)
constexpr size_t ROOK_POOL_SIZE = 102400;
constexpr size_t BISHOP_POOL_SIZE = 5248;

alignas(64) Bitboard ROOK_ATT_POOL[ROOK_POOL_SIZE];
alignas(64) Bitboard BISHOP_ATT_POOL[BISHOP_POOL_SIZE];

inline bool onBoard(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }

/** Sliding attacks from sq along the 4 dirs, honoring occupancy `occ`. */
Bitboard slidingAttacks(int sq, Bitboard occ, const int *dirs) {
    Bitboard att = 0;
    int r = rankOf(sq), f = fileOf(sq);
    for (int d = 0; d < 4; d++) {
        int dr = DIR_DR[dirs[d]], df = DIR_DF[dirs[d]];
        int rr = r + dr, ff = f + df;
        while (onBoard(rr, ff)) {
            int s = rr * 8 + ff;
            att |= bitOf(s);
            if (occ & bitOf(s)) break;  // blocker included, stop behind it
            rr += dr;
            ff += df;
        }
    }
    return att;
}

/** Relevant-occupancy mask: ray squares excluding edges not on the rays' lines.
 *  v1.4.25 (B1): correct edge stripping (see the file header). A blocker on
 *  a stripped square can never hide anything — the attack set for any
 *  occupancy is identical with or without it — so the smaller mask is
 *  bit-exact AND 10.24x smaller for the rook tables. */
Bitboard magicMask(int sq, bool rook) {
    Bitboard ray = slidingAttacks(sq, 0, rook ? DIRS_R : DIRS_B);
    Bitboard edges = ((RANKS_BB[0] | RANKS_BB[7]) & ~RANKS_BB[rankOf(sq)]) |
                     ((FILES_BB[0] | FILES_BB[7]) & ~FILES_BB[fileOf(sq)]);
    return ray & ~edges;
}

/** Enumerate all subsets of `mask` (Carry-Rippler). */
std::vector<Bitboard> allSubsets(Bitboard mask) {
    std::vector<Bitboard> out;
    out.reserve(1ULL << popcount(mask));
    Bitboard b = 0;
    do {
        out.push_back(b);
        b = (b - mask) & mask;
    } while (b);
    return out;
}

bool findMagic(int sq, bool rook, uint64_t &rng, MagicSlot &slot,
               Bitboard *pool, size_t &poolCursor, size_t poolSize) {
    Bitboard mask = magicMask(sq, rook);
    int bits = popcount(mask);
    int shift = 64 - bits;
    std::vector<Bitboard> subsets = allSubsets(mask);
    size_t size = 1ULL << bits;

    std::vector<Bitboard> ref(subsets.size());
    for (size_t i = 0; i < subsets.size(); i++)
        ref[i] = slidingAttacks(sq, subsets[i], rook ? DIRS_R : DIRS_B);

    std::vector<Bitboard> used(size);
    for (long attempt = 0; attempt < 100000000; attempt++) {
        Bitboard magic = splitmix64(rng) & splitmix64(rng) & splitmix64(rng);
        if (popcount((mask * magic) >> 56) < 6) continue;
        std::fill(used.begin(), used.end(), 0ULL);
        bool ok = true;
        for (size_t i = 0; i < subsets.size(); i++) {
            unsigned idx = unsigned((subsets[i] * magic) >> shift);
            if (used[idx] == 0) {
                used[idx] = ref[i];
            } else if (used[idx] != ref[i]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            // v1.4.25 (B1): copy into the contiguous pool instead of a
            // fragmented new[] block (unreachable overflow guard).
            if (poolCursor + size > poolSize) __builtin_trap();
            slot.mask = mask;
            slot.magic = magic;
            slot.shift = (unsigned)shift;
            slot.pad = 0;
            slot.attacks = pool + poolCursor;
            std::copy(used.begin(), used.end(), slot.attacks);
            poolCursor += size;
            return true;
        }
    }
    return false;
}

}  // namespace

#if defined(TACTURN_PEXT_DISPATCH)
// ---- BMI2 / PEXT dense attack tables ----
// v1.4.25 (B1): PextSlot descriptors over ONE contiguous static pool per
// piece (the old layout held 128 separate new[] blocks + four parallel
// per-square arrays). With global __BMI2__ the inline lookups in
// bitboard.h read these slots directly; without it, the out-of-line
// target("bmi2") thunks below do (runtime-dispatch builds only).

namespace {

alignas(64) Bitboard PEXT_ROOK_POOL[ROOK_POOL_SIZE];
alignas(64) Bitboard PEXT_BISHOP_POOL[BISHOP_POOL_SIZE];

/** Build the dense PEXT tables (needs the BMI2 target so _pext_u64 compiles
 *  without global -mbmi2). One entry per relevant-occupancy subset.
 *  v1.4.25 (B1): writes the exported PextSlot descriptors directly (the
 *  whole init runs under std::call_once, so concurrent first-use Position
 *  constructors are already serialized). */
__attribute__((target("bmi2")))
void buildPextTables() {
    size_t rCursor = 0, bCursor = 0;
    for (int sq = 0; sq < 64; sq++) {
        Bitboard rMask = ROOK_MAGICS[sq].mask, bMask = BISHOP_MAGICS[sq].mask;
        int rBits = popcount(rMask), bBits = popcount(bMask);
        if (rCursor + (size_t)(1ULL << rBits) > ROOK_POOL_SIZE) __builtin_trap();
        if (bCursor + (size_t)(1ULL << bBits) > BISHOP_POOL_SIZE) __builtin_trap();
        Bitboard *rTab = PEXT_ROOK_POOL + rCursor;
        Bitboard *bTab = PEXT_BISHOP_POOL + bCursor;
        PEXT_ROOK[sq].mask = rMask;
        PEXT_ROOK[sq].attacks = rTab;
        PEXT_BISHOP[sq].mask = bMask;
        PEXT_BISHOP[sq].attacks = bTab;
        rCursor += 1ULL << rBits;
        bCursor += 1ULL << bBits;
        Bitboard sub = 0;
        do {
            rTab[_pext_u64(sub, rMask)] = slidingAttacks(sq, sub, DIRS_R);
            sub = (sub - rMask) & rMask;
        } while (sub);
        sub = 0;
        do {
            bTab[_pext_u64(sub, bMask)] = slidingAttacks(sq, sub, DIRS_B);
            sub = (sub - bMask) & bMask;
        } while (sub);
    }
}

}  // namespace

PextSlot PEXT_ROOK[64];
PextSlot PEXT_BISHOP[64];

bool pextAttacksEnabled = false;

__attribute__((target("bmi2")))
Bitboard pextRookAttacks(int sq, Bitboard occ) {
    return PEXT_ROOK[sq].attacks[_pext_u64(occ, PEXT_ROOK[sq].mask)];
}

__attribute__((target("bmi2")))
Bitboard pextBishopAttacks(int sq, Bitboard occ) {
    return PEXT_BISHOP[sq].attacks[_pext_u64(occ, PEXT_BISHOP[sq].mask)];
}
#endif  // TACTURN_PEXT_DISPATCH

void initTables() {
    // Thread-safe one-shot init (call_once): every Position constructor
    // lands here, potentially from different threads on first use.
    static std::once_flag once;
    std::call_once(once, []() {
    const int KNIGHT_DELTAS[8][2] = {{2, 1}, {2, -1}, {-2, 1}, {-2, -1},
                                     {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};
    const int KING_DELTAS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                   {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for (int sq = 0; sq < 64; sq++) {
        int r = rankOf(sq), f = fileOf(sq);

        FILES_BB[f] |= bitOf(sq);
        RANKS_BB[r] |= bitOf(sq);

        for (int d = 0; d < 8; d++) {
            Bitboard acc = 0;
            int rr = r + DIR_DR[d], ff = f + DIR_DF[d];
            while (onBoard(rr, ff)) {
                BETWEEN_BB[sq][rr * 8 + ff] = acc;
                acc |= bitOf(rr * 8 + ff);
                rr += DIR_DR[d];
                ff += DIR_DF[d];
            }
        }

        for (auto &kd : KNIGHT_DELTAS) {
            int rr = r + kd[0], ff = f + kd[1];
            if (onBoard(rr, ff)) KNIGHT_ATT[sq] |= bitOf(rr * 8 + ff);
        }
        for (auto &kd : KING_DELTAS) {
            int rr = r + kd[0], ff = f + kd[1];
            if (onBoard(rr, ff)) KING_ATT[sq] |= bitOf(rr * 8 + ff);
        }

        int up = r + 1;
        if (up <= 7) {
            if (f > 0) PAWN_ATT[WHITE][sq] |= bitOf(up * 8 + f - 1);
            if (f < 7) PAWN_ATT[WHITE][sq] |= bitOf(up * 8 + f + 1);
        }
        int down = r - 1;
        if (down >= 0) {
            if (f > 0) PAWN_ATT[BLACK][sq] |= bitOf(down * 8 + f - 1);
            if (f < 7) PAWN_ATT[BLACK][sq] |= bitOf(down * 8 + f + 1);
        }

        for (int ff = (f > 0 ? f - 1 : 0); ff <= (f < 7 ? f + 1 : 7); ff++) {
            for (int rr = r + 1; rr <= 7; rr++) PASSED_MASK[WHITE][sq] |= bitOf(rr * 8 + ff);
            for (int rr = r - 1; rr >= 0; rr--) PASSED_MASK[BLACK][sq] |= bitOf(rr * 8 + ff);
        }

        for (int ff = (f > 0 ? f - 1 : 0); ff <= (f < 7 ? f + 1 : 7); ff++) {
            if (r + 1 <= 7) SHIELD_MASK[WHITE][sq] |= bitOf((r + 1) * 8 + ff);
            if (r + 2 <= 7) SHIELD_MASK[WHITE][sq] |= bitOf((r + 2) * 8 + ff);
            if (r - 1 >= 0) SHIELD_MASK[BLACK][sq] |= bitOf((r - 1) * 8 + ff);
            if (r - 2 >= 0) SHIELD_MASK[BLACK][sq] |= bitOf((r - 2) * 8 + ff);
        }

        CASTLE_RIGHTS_MASK[sq] = 15;

        // v1.4.25 (B2): empty-board pseudo rays for the gating call sites.
        ROOK_PSEUDO[sq] = slidingAttacks(sq, 0, DIRS_R);
        BISHOP_PSEUDO[sq] = slidingAttacks(sq, 0, DIRS_B);
    }

    CASTLE_RIGHTS_MASK[4] = 15 & ~(CASTLE_WK | CASTLE_WQ);   // e1
    CASTLE_RIGHTS_MASK[0] = 15 & ~CASTLE_WQ;                 // a1
    CASTLE_RIGHTS_MASK[7] = 15 & ~CASTLE_WK;                 // h1
    CASTLE_RIGHTS_MASK[60] = 15 & ~(CASTLE_BK | CASTLE_BQ);  // e8
    CASTLE_RIGHTS_MASK[56] = 15 & ~CASTLE_BQ;                // a8
    CASTLE_RIGHTS_MASK[63] = 15 & ~CASTLE_BK;                // h8

    for (int f = 0; f < 8; f++) {
        ADJACENT_FILES[f] = 0;
        if (f > 0) ADJACENT_FILES[f] |= FILES_BB[f - 1];
        if (f < 7) ADJACENT_FILES[f] |= FILES_BB[f + 1];
    }

    for (int r = 0; r < 8; r++) {
        RANKS_LE[r] = 0;
        RANKS_GE[r] = 0;
        for (int k = 0; k <= r; k++) RANKS_LE[r] |= RANKS_BB[k];
        for (int k = r; k < 8; k++) RANKS_GE[r] |= RANKS_BB[k];
    }

    // ---- Zobrist keys (deterministic; independent of the magic search
    // stream, so the table fix cannot move a single key) ----
    uint64_t rng = 0x5EEDBEEF1234C0DEULL;
    for (int p = 0; p < 12; p++)
        for (int sq = 0; sq < 64; sq++) Z_PIECE[p][sq] = splitmix64(rng);
    for (int i = 0; i < 16; i++) Z_CASTLE[i] = splitmix64(rng);
    for (int i = 0; i < 8; i++) Z_EP_FILE[i] = splitmix64(rng);
    Z_SIDE = splitmix64(rng);

    // ---- Magic bitboards (deterministic search; v1.4.25 B1 pools) ----
    uint64_t magicRng = 0x1F2E3D4C5B6A7988ULL;
    size_t rCursor = 0, bCursor = 0;
    for (int sq = 0; sq < 64; sq++) {
        if (!findMagic(sq, true, magicRng, ROOK_MAGICS[sq],
                       ROOK_ATT_POOL, rCursor, ROOK_POOL_SIZE)) {
            __builtin_trap();  // unreachable in practice
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        if (!findMagic(sq, false, magicRng, BISHOP_MAGICS[sq],
                       BISHOP_ATT_POOL, bCursor, BISHOP_POOL_SIZE)) {
            __builtin_trap();
        }
    }

    // ---- BMI2 / PEXT dispatch (x86-64 only; decided once) ----
    // v1.4.25 (B1): with compile-time __BMI2__ the inline lookups in
    // bitboard.h consume these slots directly, so the tables are built
    // unconditionally; runtime dispatch builds keep the cpu_supports gate.
#if defined(TACTURN_PEXT_DISPATCH)
#if defined(__BMI2__)
    buildPextTables();
    pextAttacksEnabled = true;
#else
    if (__builtin_cpu_supports("bmi2")) {
        buildPextTables();
        pextAttacksEnabled = true;
    }
#endif
#endif
    });  // call_once
}

}  // namespace tacturn
