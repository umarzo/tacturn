// TacTurn Chess Engine — evaluation tables (PST + tunable term parameters).
//
// PSTs are PeSTO-derived (Ronald de Man's published tables, standard
// technique), written visually (row 0 == rank 8) and flipped into a1==0
// layout at init. All tunable term parameters live in EV_MG / EV_EG
// (see evaltables.h); tools/tuner.cpp refits them on self-play data and
// emits eval_tuned.h, which overrides these defaults at build time.

#include "evaltables.h"

#include <mutex>

namespace tacturn {

int PST_MG[6][64];
int PST_EG[6][64];
int PST_MG_BLACK[6][64];
int PST_EG_BLACK[6][64];
int PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};
int PASSED_MG[8];  // kept in sync with EV_[MG|EG][F_PASSED_R*] at init
int PASSED_EG[8];
int KING_ATTACK_WEIGHT[6] = {0, 2, 2, 3, 5, 0};

int SAFETY[64] = {
    0, 0, 1, 2, 3, 5, 7, 9, 12, 15, 18, 22, 26, 30, 35, 39,
    44, 50, 56, 61, 68, 75, 83, 91, 100, 110, 120, 131, 142, 154, 167, 180,
    195, 211, 227, 244, 263, 282, 302, 324, 346, 370, 395, 421, 448, 476, 506, 537,
    569, 603, 639, 676, 715, 755, 797, 841, 887, 935, 985, 1037, 1091, 1147, 1205, 1265
};

int MOBILITY_BASE[6] = {0, 4, 6, 7, 13, 0};

// ---- Tunable evaluation-term parameters (midgame / endgame) ----
// Starting values: the hand-tuned v2.1 set where the term existed; new
// terms start from classical-engine reference values. The tuner refits.
int EV_MG[EV_NUM_FEATURES] = {
    // (v2.5 layout: per-file isolated, per-count mobility tables,
    //  new terms at the end — remapped from the v2.4 tuned values;
    //  the tuner refits on fresh data.)
    -12,-8,-8,-8,-8,-8,-8,-8,-8,-1,
    -15,15,15,0,0,5,10,55,90,13,
    -5,-5,0,10,-8,45,9,5,0,18,
    5,-28,-21,-14,-7,0,7,14,21,28,
    -36,-30,-24,-18,-12,-6,0,6,12,18,
    24,30,36,42,-42,-36,-30,-24,-18,-12,
    -6,0,6,12,18,24,30,36,42,-52,
    -48,-44,-40,-36,-32,-28,-24,-20,-16,-12,
    -8,-4,0,4,8,12,16,20,24,28,
    32,36,40,44,48,52,56,20,12,7,
    8,4,0,-5,-25,-8,22,10,5,25,
    40,2,25,3,0,-1,0,-6,-20,50,
    26,0,-5,-2,-5,-5,0,17,-10,0,
    0,-11,8,-15,0,-25,-25,-25,-30,-30,
    -30,45,5,20,0,-5,0,0,0,0,
    0,
    0,0,0,0,0,0,0,0,  // F_DCOR_B0..B7 (v2.6 additions)
    0,0,0,0,0,0,0,0,  // F_IMB_* (v3.1 material imbalance; 0 == v3.0.0 eval)
};

int EV_EG[EV_NUM_FEATURES] = {
    // (v2.5 layout: per-file isolated, per-count mobility tables,
    //  new terms at the end — remapped from the v2.4 tuned values;
    //  the tuner refits on fresh data.)
    -15,-19,-19,-19,-19,-19,-19,-19,-19,-13,
    0,0,2,30,35,50,70,77,78,0,
    10,8,-39,70,-4,5,20,45,35,25,
    20,-12,-9,-6,-3,0,3,6,9,12,
    -30,-25,-20,-15,-10,-5,0,5,10,15,
    20,25,30,35,-14,-12,-10,-8,-6,-4,
    -2,0,2,4,6,8,10,12,14,-13,
    -12,-11,-10,-9,-8,-7,-6,-5,-4,-3,
    -2,-1,0,1,2,3,4,5,6,7,
    8,9,10,11,12,13,14,0,0,0,
    0,0,0,0,0,0,22,26,22,30,
    38,0,12,8,8,8,5,0,-8,60,
    45,22,30,23,37,20,-16,0,50,22,
    15,-17,40,0,0,10,30,50,28,30,
    21,35,35,5,6,-6,4,0,0,0,
    0,
    0,0,0,0,0,0,0,0,  // F_DCOR_B0..B7 (v2.6 additions)
    0,0,0,0,0,0,0,0,  // F_IMB_* (v3.1 material imbalance; 0 == v3.0.0 eval)
};

#if defined(__has_include)
#if __has_include("eval_tuned.h")
#include "eval_tuned.h"
#endif
#endif

#ifdef TACTURN_TUNED_EVAL
// Overwrite defaults with the tuner's output (kept as const arrays so the
// build fails loudly if the feature enum and the generated file disagree).
static_assert(sizeof(TUNED_EV_MG) == sizeof(EV_MG), "tuned MG params stale");
static_assert(sizeof(TUNED_EV_EG) == sizeof(EV_EG), "tuned EG params stale");
#endif

namespace {

const int MG_VALUE[6] = {82, 337, 365, 477, 1025, 0};
const int EG_VALUE[6] = {94, 281, 297, 512, 936, 0};

// Tables written visually: row 0 == rank 8, columns a..h.
const char *PAWN_MG_ROWS[8] = {
    "  0   0   0   0   0   0   0   0",
    " 98 134  61  95  68 126  34 -11",
    " -6   7  26  31  65  56  25 -20",
    "-14  13   6  21  23  12  17 -23",
    "-27  -2  -5  12  17   6  10 -25",
    "-26  -4  -4 -10   3   3  33 -12",
    "-35  -1 -20 -23 -15  24  38 -22",
    "  0   0   0   0   0   0   0   0",
};
const char *PAWN_EG_ROWS[8] = {
    "  0   0   0   0   0   0   0   0",
    "178 173 158 134 147 132 165 187",
    " 94 100  85  67  56  53  82  84",
    " 32  24  13   5  -2   4  17  17",
    " 13   9  -3  -7  -7  -8   3  -1",
    "  4   7  -6   1   0  -5  -1  -8",
    " 13   8   8  10  13   0   2  -7",
    "  0   0   0   0   0   0   0   0",
};
const char *KNIGHT_MG_ROWS[8] = {
    "-167 -89 -34 -49  61 -97 -15 -107",
    " -73 -41  72  36  23  62   7  -17",
    " -47  60  37  65  84 129  73   44",
    "  -9  17  19  53  37  69  18   22",
    " -13   4  16  13  28  19  21   -8",
    " -23  -9  12  10  19  17  25  -16",
    " -29 -53 -12  -3  -1  18 -14  -19",
    "-105 -21 -58 -33 -17 -28 -19  -23",
};
const char *KNIGHT_EG_ROWS[8] = {
    "-58 -38 -13 -28 -31 -27 -63 -99",
    "-25  -8 -25  -2  -9 -25 -24 -52",
    "-24 -20  10   9  -1  -9 -19 -41",
    "-17   3  22  22  22  11   8 -18",
    "-18  -6  16  22  18  12  22  -5",
    " -2  22  15  11  15  16   4  -3",
    " -3  -1  -4   0  -3   4   1 -13",
    " -4 -21  -8 -14  -4  -8 -17 -34",
};
const char *BISHOP_MG_ROWS[8] = {
    "-29   4 -82 -37 -25 -42   7  -8",
    "-26  16 -18 -13  30  59  18 -47",
    "-16  37  43  40  35  50  37  -2",
    " -4   5  19  50  37  37   7  -2",
    " -6  13  13  26  34  12  10   1",
    "  0  15  15  15  14  27  18  10",
    "  4  15  16   0   7  21  33   1",
    "-33  -3 -14 -21 -13 -12 -39 -21",
};
const char *BISHOP_EG_ROWS[8] = {
    "-14 -21 -11  -8  -7  -9 -17 -24",
    " -8  -4   7 -12  -3 -13  -4 -14",
    "  2  -8   0  -1  -2   6   0   4",
    " -3   9  12   9  14  10   3   2",
    " -6   3  13  19   7  10  -3  -9",
    " -8  -3  -4  -2   3 -14  -6  -5",
    " -5 -11  -5  -3   1 -17 -11  -6",
    "-22  -9  -5 -13  -5 -14 -23 -30",
};
const char *ROOK_MG_ROWS[8] = {
    " 32  42  32  51  63   9  31  43",
    " 27  32  58  62  80  67  26  44",
    " -5  19  26  36  17  45  61  16",
    "-40 -16 -14  -4  -6   8  37  -9",
    "-40 -16 -14  -4  -6   8  37  -9",
    "-51 -11  -7   5   2  16  26  -5",
    "-54 -27 -22  -5   1  35  21  -1",
    "-55 -32 -20 -19  -8  13   4 -38",
};
const char *ROOK_EG_ROWS[8] = {
    " 13  11   7   4   3   2   3  -1",
    " -2  -1  -4  -7  -8  -9 -10  -8",
    "-10  -4  -3  -1   0   0  -1  -6",
    "-11  -3   0   4   5   4   3  -6",
    "-11  -4   0   6   9   7   5  -7",
    "-12  -5  -1   6  11   9   7 -10",
    "-15  -9  -4   2   6   4   3 -13",
    "-18 -13  -9  -4   0   3   4 -19",
};
const char *QUEEN_MG_ROWS[8] = {
    "-28   0  29  12  59  44  43  45",
    "-24 -39  -5   1 -16  57  28  54",
    "-13 -17   7   8  29  56  47  57",
    "-27 -27 -16 -16  -1  17  -2   1",
    " -9 -26  -9 -10  -2  -4   3  -3",
    "-14   2 -11  -2  -5   2  14   5",
    "-35  -8  11   2   8  15  -3   1",
    " -1 -18  -9  10 -15 -25 -31 -50",
};
const char *QUEEN_EG_ROWS[8] = {
    " -9  22  22  27  27  19  10  20",
    "-17  20  32  41  58  25  30   0",
    "-20   6   9  49  47  35  19   9",
    "  3  22  24  45  57  40  57  14",
    "-18  28  19  47  31  34  39  20",
    "-16 -27  15   6   9  17  10   5",
    "-22 -23 -30 -16 -16 -23 -36 -32",
    "-33 -28 -22 -43  -5 -32 -20 -41",
};
const char *KING_MG_ROWS[8] = {
    "-65  23  16 -15 -56 -34   2  13",
    " 29  -1 -20  -7  -8  -4 -38 -29",
    " -9  24   2 -16 -20   6  22 -22",
    "-17 -20 -12 -27 -30 -25 -14 -36",
    "-49  -1 -27 -39 -46 -44 -33 -51",
    "-14 -14 -22 -46 -44 -30 -15 -27",
    "  1   7  -8 -64 -43 -16   9   8",
    "-15  36  12 -54   8 -28  24  14",
};
const char *KING_EG_ROWS[8] = {
    "-74 -35 -18 -18 -11  15   4 -17",
    "-12  17  14  17  17  38  23  11",
    " 10  17  23  15  20  45  44  13",
    " -8  22  24  27  26  33  26   3",
    "-18  -4  21  24  27  23   9 -11",
    "-19  -3  11  21  23  16   7  -9",
    "-27 -11   4  13  14   4  -5 -17",
    "-53 -34 -21 -11 -28 -14 -24 -43",
};

void parseRow(const char *row, int out[8]) {
    int i = 0;
    const char *p = row;
    while (i < 8) {
        while (*p == ' ') p++;
        int sign = 1;
        if (*p == '-') { sign = -1; p++; }
        int v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        out[i++] = sign * v;
        while (*p == ' ') p++;
    }
}

void buildTable(const char *rows[8], int out[64]) {
    for (int r = 0; r < 8; r++) {
        int vals[8];
        parseRow(rows[7 - r], vals);  // visual row 0 == rank 8 -> flip
        for (int f = 0; f < 8; f++) out[r * 8 + f] = vals[f];
    }
}

}  // namespace

void initEvalTables() {
    // Thread-safe one-shot init (every Position constructor lands here).
    static std::once_flag once;
    std::call_once(once, []() {
    int mg[6][64], eg[6][64];
    buildTable(PAWN_MG_ROWS, mg[PAWN]);
    buildTable(PAWN_EG_ROWS, eg[PAWN]);
    buildTable(KNIGHT_MG_ROWS, mg[KNIGHT]);
    buildTable(KNIGHT_EG_ROWS, eg[KNIGHT]);
    buildTable(BISHOP_MG_ROWS, mg[BISHOP]);
    buildTable(BISHOP_EG_ROWS, eg[BISHOP]);
    buildTable(ROOK_MG_ROWS, mg[ROOK]);
    buildTable(ROOK_EG_ROWS, eg[ROOK]);
    buildTable(QUEEN_MG_ROWS, mg[QUEEN]);
    buildTable(QUEEN_EG_ROWS, eg[QUEEN]);
    buildTable(KING_MG_ROWS, mg[KING]);
    buildTable(KING_EG_ROWS, eg[KING]);

    for (int p = 0; p < 6; p++) {
        for (int sq = 0; sq < 64; sq++) {
            PST_MG[p][sq] = mg[p][sq] + MG_VALUE[p];
            PST_EG[p][sq] = eg[p][sq] + EG_VALUE[p];
            PST_MG_BLACK[p][sq ^ 56] = PST_MG[p][sq];
            PST_EG_BLACK[p][sq ^ 56] = PST_EG[p][sq];
        }
    }

#ifdef TACTURN_TUNED_EVAL
    // Tuned PSTs (already include material values).
    for (int p = 0; p < 6; p++)
        for (int sq = 0; sq < 64; sq++) {
            PST_MG[p][sq] = TUNED_PST_MG[p][sq];
            PST_EG[p][sq] = TUNED_PST_EG[p][sq];
            PST_MG_BLACK[p][sq ^ 56] = PST_MG[p][sq];
            PST_EG_BLACK[p][sq ^ 56] = PST_EG[p][sq];
        }
    for (int i = 0; i < EV_NUM_FEATURES; i++) {
        EV_MG[i] = TUNED_EV_MG[i];
        EV_EG[i] = TUNED_EV_EG[i];
    }
#endif

    // Legacy passer arrays (indexed by rank 0..7) mirror the features.
    PASSED_MG[0] = 0; PASSED_EG[0] = 0;
    PASSED_MG[7] = 0; PASSED_EG[7] = 0;
    for (int r = 1; r <= 6; r++) {
        PASSED_MG[r] = EV_MG[F_PASSED_R1 + r - 1];
        PASSED_EG[r] = EV_EG[F_PASSED_R1 + r - 1];
    }
    });  // call_once
}

}  // namespace tacturn
