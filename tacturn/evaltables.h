// TacTurn Chess Engine — evaluation tables (PST + tunable term parameters).
//
// PSTs include material values; *_BLACK variants are pre-mirrored. Tables
// are written visually (row 0 == rank 8) and flipped into a1==0 layout at
// init. EV_MG / EV_EG hold the tunable evaluation-term parameters; the
// tuner (tools/tuner.cpp) overwrites them together with the PSTs.
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include "types.h"

namespace tacturn {

// ---- Tunable evaluation-term feature ids (MG/EG param each) ----
enum EvalFeature {
    F_DOUBLED = 0,
    F_ISOLATED_F0,    // isolated pawn per file (0..7) — centre files cost more
    F_ISOLATED_F1,
    F_ISOLATED_F2,
    F_ISOLATED_F3,
    F_ISOLATED_F4,
    F_ISOLATED_F5,
    F_ISOLATED_F6,
    F_ISOLATED_F7,
    F_BACKWARD,
    F_ISLANDS,
    F_PHALANX,
    F_SUPPORTED,
    F_PASSED_R1,  // passer by (white) rank 1..6 (0 and 7 unused)
    F_PASSED_R2,
    F_PASSED_R3,
    F_PASSED_R4,
    F_PASSED_R5,
    F_PASSED_R6,
    F_PASSED_CONN,
    F_PASSED_KING_OWN,
    F_PASSED_KING_ENEMY,
    F_PASSED_BLOCKED,
    F_BISHOP_PAIR,
    F_BAD_BISHOP,
    F_ROOK_OPEN,
    F_ROOK_SEMI,
    F_ROOK_7TH,
    F_ROOK_BEHIND_PASSER,
    F_OUTPOST_KNIGHT,
    F_OUTPOST_BISHOP,
    // ---- per-count mobility tables (Berserk-style non-linear weights) ----
    F_MOB_N0,  // knight with 0 safe squares ... 8 (9 cells)
    F_MOB_N1,
    F_MOB_N2,
    F_MOB_N3,
    F_MOB_N4,
    F_MOB_N5,
    F_MOB_N6,
    F_MOB_N7,
    F_MOB_N8,
    F_MOB_B0,  // bishop with 0 safe squares ... 13 (14 cells)
    F_MOB_B1,
    F_MOB_B2,
    F_MOB_B3,
    F_MOB_B4,
    F_MOB_B5,
    F_MOB_B6,
    F_MOB_B7,
    F_MOB_B8,
    F_MOB_B9,
    F_MOB_B10,
    F_MOB_B11,
    F_MOB_B12,
    F_MOB_B13,
    F_MOB_R0,  // rook with 0 safe squares ... 14 (15 cells)
    F_MOB_R1,
    F_MOB_R2,
    F_MOB_R3,
    F_MOB_R4,
    F_MOB_R5,
    F_MOB_R6,
    F_MOB_R7,
    F_MOB_R8,
    F_MOB_R9,
    F_MOB_R10,
    F_MOB_R11,
    F_MOB_R12,
    F_MOB_R13,
    F_MOB_R14,
    F_MOB_Q0,  // queen with 0 safe squares ... 27 (28 cells)
    F_MOB_Q1,
    F_MOB_Q2,
    F_MOB_Q3,
    F_MOB_Q4,
    F_MOB_Q5,
    F_MOB_Q6,
    F_MOB_Q7,
    F_MOB_Q8,
    F_MOB_Q9,
    F_MOB_Q10,
    F_MOB_Q11,
    F_MOB_Q12,
    F_MOB_Q13,
    F_MOB_Q14,
    F_MOB_Q15,
    F_MOB_Q16,
    F_MOB_Q17,
    F_MOB_Q18,
    F_MOB_Q19,
    F_MOB_Q20,
    F_MOB_Q21,
    F_MOB_Q22,
    F_MOB_Q23,
    F_MOB_Q24,
    F_MOB_Q25,
    F_MOB_Q26,
    F_MOB_Q27,
    F_SHELTER_D0_R1,
    F_SHELTER_D0_R2,
    F_SHELTER_D0_R3,
    F_SHELTER_D1_R1,
    F_SHELTER_D1_R2,
    F_SHELTER_D1_R3,
    F_SHELTER_MISS_D0,
    F_SHELTER_MISS_D1,
    F_SHELTER_KING_FAR,
    F_THREAT_PAWN_MINOR,
    F_THREAT_PAWN_MAJOR,
    F_THREAT_MINOR_MAJOR,
    F_HANGING_MINOR,
    F_HANGING_MAJOR,
    F_DANGER,
    F_TEMPO,
    // ---- v2.4 additions (Ethereal-inspired, Texel-tuned) ----
    F_KING_DEFENDERS,  // own pawns+minors in king area (amount = count-3)
    F_STORM_D1,        // enemy pawn 1 rank ahead of king, king file +/- 1
    F_STORM_D2,
    F_STORM_D3,
    F_STORM_B1,        // storm pawn blocked by an own pawn
    F_STORM_B2,
    F_KING_PAWN_F0,    // nearest pawn (either colour) on the king's file
    F_KING_PAWN_F1,    // ... on an adjacent file
    F_KING_PAWN_F2,    // ... two files away
    F_PAWN_CAND_R3,    // candidate passer by relative rank (pawn hash)
    F_PAWN_CAND_R4,
    F_PAWN_CAND_R5,
    F_PAWN_CAND_R6,
    F_PASSED_EDGE,     // passer on file A/H (pawn hash)
    F_PASSED_DEFENDED, // passer defended by an own pawn (pawn hash)
    F_PASSED_PATH,     // rank>=5 passer, clean path to promotion
    F_KNIGHT_BEHIND_PAWN,
    F_BISHOP_BEHIND_PAWN,
    F_BISHOP_RAMMED,   // own rammed pawns on the bishop's colour
    F_BISHOP_LONG_DIAG,
    F_KNIGHT_SIBERIA,  // knight min-distance-to-kings 4..5
    F_KNIGHT_SIBERIA2, // 6..7
    F_CLOSED_KNIGHT_OPEN,   // closedness bucket x knight count diff
    F_CLOSED_KNIGHT_MID,
    F_CLOSED_KNIGHT_CLOSED,
    F_CLOSED_ROOK_OPEN,
    F_CLOSED_ROOK_MID,
    F_CLOSED_ROOK_CLOSED,
    F_THREAT_ROOK_LESSER,  // enemy rook attacked by our pawn or minor
    F_THREAT_QUEEN_ONE,    // enemy queen attacked by pawn/minor/rook
    F_THREAT_BY_PUSH,      // our pawn push attacks enemy minor/major
    F_WEAK_PAWN,           // own pawn attacked by enemy pawn, not pawn-defended
    F_SPACE_RESTRICT,      // own piece on doubly-enemy-attacked weak square
    F_SPACE_CENTER,        // uncontested big-centre squares (opening)
    // ---- v2.5 additions (state-of-the-art HCE pack) ----
    F_KING_OPEN_FILE,      // king on a fully open file (no pawns at all)
    F_SIMPLIFY,            // EG: the side ahead in material prefers trades
    F_PAWN_BLOCKED,        // own pawn with the push square occupied
    F_PASSED_STACKED,      // own passers stacked on one file (pawn hash)
    // ---- v2.6 additions: king-danger curve correction buckets ----
    // The fixed SAFETY[] quadratic maps danger units -> cp; these 8
    // buckets (danger/8, only when the attacker gate passes) add a
    // tunable per-bucket correction on top, letting the fit reshape the
    // most safety-critical nonlinearity without breaking the base curve.
    // Defaults 0 == bit-exact v2.6 eval.
    F_DCOR_B0,
    F_DCOR_B1,
    F_DCOR_B2,
    F_DCOR_B3,
    F_DCOR_B4,
    F_DCOR_B5,
    F_DCOR_B6,
    F_DCOR_B7,
    // ---- v3.1 additions: material imbalance interactions ----
    // Piece-count products the linear PST material model cannot express
    // (SF-inspired). Defaults 0 == bit-exact v3.0.0 eval.
    F_IMB_QUEEN_PAWNS,       // (queen count diff) x avg total pawns
    F_IMB_ROOK_PAWNS,        // (rook count diff) x avg total pawns
    F_IMB_KNIGHT_PAWNS,      // (knight count diff) x avg total pawns
    F_IMB_BISHOP_PAWNS,      // (bishop count diff) x avg total pawns
    F_IMB_ROOK_PAIR,         // side with two or more rooks
    F_IMB_MINOR_PAIR,        // side with two or more minors
    F_IMB_ROOK_ENEMY_PAWNS,  // own rooks x enemy-pawn excess (targets)
    F_IMB_KNIGHT_ENEMY_PAWNS,
    EV_NUM_FEATURES
};

extern int EV_MG[EV_NUM_FEATURES];
extern int EV_EG[EV_NUM_FEATURES];

extern int PST_MG[6][64];        // [piece][square], White POV
extern int PST_EG[6][64];
extern int PST_MG_BLACK[6][64];  // indexed with white-oriented sq: PST_*_BLACK[p][sq^56]
extern int PST_EG_BLACK[6][64];
extern int PHASE_WEIGHT[6];      // N=1 B=1 R=2 Q=4, max 24
extern int PASSED_MG[8];         // (legacy accessors, kept in sync at init)
extern int PASSED_EG[8];
extern int KING_ATTACK_WEIGHT[6];
extern int SAFETY[64];           // danger units -> centipawns (index clamped to 63)
extern int MOBILITY_BASE[6];

void initEvalTables();

}  // namespace tacturn
