// TacTurn Chess Engine v1 — bitboard table generation (magic bitboards).

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

Bitboard ROOK_MASK[64];
Bitboard BISHOP_MASK[64];
Bitboard ROOK_MAGIC_N[64];
Bitboard BISHOP_MAGIC_N[64];
int ROOK_SHIFT[64];
int BISHOP_SHIFT[64];
Bitboard *ROOK_ATT[64];
Bitboard *BISHOP_ATT[64];

namespace {

constexpr int DIR_DR[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int DIR_DF[8] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr int DIRS_R[4] = {0, 1, 2, 3};   // N, S, E, W
constexpr int DIRS_B[4] = {4, 5, 6, 7};   // NE, NW, SE, SW

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

/** Relevant-occupancy mask: ray squares excluding edges not on the rays' lines. */
Bitboard magicMask(int sq, bool rook) {
    Bitboard ray = slidingAttacks(sq, 0, rook ? DIRS_R : DIRS_B);
    Bitboard edges = (RANKS_BB[0] | RANKS_BB[7] | FILES_BB[0] | FILES_BB[7]) &
                     ~(RANKS_BB[rankOf(sq)] | FILES_BB[fileOf(sq)]);
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

bool findMagic(int sq, bool rook, uint64_t &rng,
               Bitboard &maskOut, Bitboard &magicOut, int &shiftOut,
               Bitboard *&tableOut) {
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
            maskOut = mask;
            magicOut = magic;
            shiftOut = shift;
            tableOut = new Bitboard[size];
            std::copy(used.begin(), used.end(), tableOut);
            return true;
        }
    }
    return false;
}

}  // namespace

#if defined(TACTURN_PEXT_DISPATCH)
// ---- BMI2 / PEXT dense attack tables ----
// These live at `tacturn::` scope: bitboard.h declares them and the inline
// attack getters call them directly.
namespace {

Bitboard *PEXT_ROOK_TAB[64];
Bitboard *PEXT_BISHOP_TAB[64];

/** Build the dense PEXT tables (needs the BMI2 target so _pext_u64 compiles
 *  without global -mbmi2). One entry per relevant-occupancy subset. */
__attribute__((target("bmi2")))
void buildPextTables() {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard rMask = ROOK_MASK[sq], bMask = BISHOP_MASK[sq];
        int rBits = popcount(rMask), bBits = popcount(bMask);
        PEXT_ROOK_TAB[sq] = new Bitboard[1ULL << rBits];
        PEXT_BISHOP_TAB[sq] = new Bitboard[1ULL << bBits];
        Bitboard sub = 0;
        do {
            PEXT_ROOK_TAB[sq][_pext_u64(sub, rMask)] =
                slidingAttacks(sq, sub, DIRS_R);
            sub = (sub - rMask) & rMask;
        } while (sub);
        sub = 0;
        do {
            PEXT_BISHOP_TAB[sq][_pext_u64(sub, bMask)] =
                slidingAttacks(sq, sub, DIRS_B);
            sub = (sub - bMask) & bMask;
        } while (sub);
    }
}

}  // namespace

bool pextAttacksEnabled = false;

__attribute__((target("bmi2")))
Bitboard pextRookAttacks(int sq, Bitboard occ) {
    return PEXT_ROOK_TAB[sq][_pext_u64(occ, ROOK_MASK[sq])];
}

__attribute__((target("bmi2")))
Bitboard pextBishopAttacks(int sq, Bitboard occ) {
    return PEXT_BISHOP_TAB[sq][_pext_u64(occ, BISHOP_MASK[sq])];
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

    // ---- Zobrist keys (deterministic) ----
    uint64_t rng = 0x5EEDBEEF1234C0DEULL;
    for (int p = 0; p < 12; p++)
        for (int sq = 0; sq < 64; sq++) Z_PIECE[p][sq] = splitmix64(rng);
    for (int i = 0; i < 16; i++) Z_CASTLE[i] = splitmix64(rng);
    for (int i = 0; i < 8; i++) Z_EP_FILE[i] = splitmix64(rng);
    Z_SIDE = splitmix64(rng);

    // ---- Magic bitboards (deterministic search) ----
    uint64_t magicRng = 0x1F2E3D4C5B6A7988ULL;
    for (int sq = 0; sq < 64; sq++) {
        if (!findMagic(sq, true, magicRng, ROOK_MASK[sq], ROOK_MAGIC_N[sq],
                       ROOK_SHIFT[sq], ROOK_ATT[sq])) {
            __builtin_trap();  // unreachable in practice
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        if (!findMagic(sq, false, magicRng, BISHOP_MASK[sq], BISHOP_MAGIC_N[sq],
                       BISHOP_SHIFT[sq], BISHOP_ATT[sq])) {
            __builtin_trap();
        }
    }

    // ---- BMI2 / PEXT dispatch (x86-64 only; decided once) ----
#if defined(TACTURN_PEXT_DISPATCH)
    if (__builtin_cpu_supports("bmi2")) {
        buildPextTables();
        pextAttacksEnabled = true;
    }
#endif
    });  // call_once
}

}  // namespace tacturn
