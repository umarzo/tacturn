// TacTurn Chess Engine — evaluation implementation.
//
// Tapered evaluation. Every term is a (feature amount x tuned parameter)
// product, MG and EG, so the whole evaluation is linear in the parameter
// vector given a position — tools/tuner.cpp fits that vector with
// logistic-regression (Texel) tuning on self-play data, using the exact
// same code path via the EvalTrace hook.
//
// v2.4 HCE overhaul (Ethereal-inspired, Texel-tuned on self-play):
//   - mobility area: enemy-pawn attacks, own king square and own blocked
//     pawns excluded; bishops x-ray through own B/Q, rooks through own R/Q
//   - pawn structure: doubled, isolated, backward, islands, phalanx /
//     supported, passed by rank, connected passers, king-proximity and
//     blocked passers, CANDIDATE passers, edge-file passers, defended
//     passers  -> pawn hash cached
//   - passers with a clean promotion path (EG)
//   - bishop pair, bad bishop, rammed pawns on the bishop's colour,
//     bishops on long diagonals hitting both centres
//   - knights / bishops behind pawns, knights far from both kings
//   - rooks: open / semi-open file, 7th rank, behind passed pawns
//   - knight / bishop outposts (pawn-protected, beyond enemy pawn reach)
//   - king shelter per-file table + PAWN STORM (enemy pawns approaching
//     the king, with a blocked variant) + king defenders + king-to-nearest-
//     pawn file proximity
//   - king-attack danger: weighted attacker units + king-zone hits
//     (squares cheaply recapturable by enemy pawns discounted) + SAFE
//     CHECKS + weak squares in the king zone, queenless attacks damped,
//     quadratic safety table (MG full, EG half)
//   - safe mobility for N/B/R/Q (see mobility area)
//   - threats: pawn attacks on minors/majors, safe minor attacks on majors,
//     rook attacked by lesser pieces, queen attacked, pawn-push threats,
//     weak pawns, hanging pieces
//   - space: restricted squares, uncontested centre
//   - closedness-adjusted knight/rook values
//   - tempo; phase interpolation engages endgame weights ~12% early
//   - endgame material scaling: opposite-colour-bishop fortresses,
//     pawnless leaders, WRONG-COLOUR bishop vs rook pawn, lone queen
//     against multiple pieces, and a complexity term that damps/amplifies
//     the endgame score by conversion potential
//
// All original code — MIT licensed, TacTurn contributors.

#include "evaluate.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace tacturn {

bool evalPawnHashDisabled = false;

// Conversion-potential ("complexity") adjustment strength, in percent of the
// reference values (tuned for ~3300-Elo engines; lower engines convert less
// reliably, so the default dilutes the drawish damping).
#ifndef TACTURN_COMPLEXITY_PCT
#define TACTURN_COMPLEXITY_PCT 50
#endif

namespace {

inline int chebyshev(int a, int b) {
    int dr = (a >> 3) - (b >> 3);
    int df = (a & 7) - (b & 7);
    if (dr < 0) dr = -dr;
    if (df < 0) df = -df;
    return dr > df ? dr : df;
}

inline int clampI2(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** Add a parameterised term (white-POV amount; black terms pass negative). */
#define TADD(feat, amt)                                                      \
    do {                                                                     \
        const int a_ = (amt);                                                \
        mgScore += EV_MG[feat] * a_;                                          \
        egScore += EV_EG[feat] * a_;                                          \
        if (tr) {                                                            \
            tr->mgAmt[feat] += a_;                                            \
            tr->egAmt[feat] += a_;                                            \
        }                                                                    \
    } while (0)

/** Cached pawn-structure evaluation. Amounts go to the trace when asked. */
void pawnStructure(Bitboard wp, Bitboard bp, PawnEntry &e, EvalTrace *tr) {
    int mgScore = 0, egScore = 0;

    // ---- per-file: doubled / isolated (per-file) / islands ----
    int wIslands = 0, bIslands = 0;
    bool lastW = false, lastB = false;
    for (int f = 0; f < 8; f++) {
        Bitboard fileBb = FILES_BB[f];
        Bitboard adjacent = ADJACENT_FILES[f];

        int wCnt = popcount(wp & fileBb);
        int bCnt = popcount(bp & fileBb);

        if (wCnt > 1) TADD(F_DOUBLED, wCnt - 1);
        if (bCnt > 1) TADD(F_DOUBLED, -(bCnt - 1));

        if (wCnt > 0 && (wp & adjacent) == 0) TADD(F_ISOLATED_F0 + f, 1);
        if (bCnt > 0 && (bp & adjacent) == 0) TADD(F_ISOLATED_F0 + f, -1);

        bool wHere = wCnt > 0, bHere = bCnt > 0;
        if (wHere && !lastW) wIslands++;
        if (bHere && !lastB) bIslands++;
        lastW = wHere;
        lastB = bHere;
    }
    if (wIslands > 1) TADD(F_ISLANDS, wIslands - 1);
    if (bIslands > 1) TADD(F_ISLANDS, -(bIslands - 1));

    // ---- phalanx / supported (connected pawns) ----
    // phalanx: side-by-side own pawns; supported: defended by an own pawn.
    {
        // A white pawn pair side by side shares an edge bit once via (wp << 1).
        Bitboard wPhal = (wp << 1) & wp & NOT_FILE_A_BB;
        int cnt = popcount(wPhal);
        if (cnt) TADD(F_PHALANX, cnt);
        Bitboard bPhalanx = (bp >> 1) & bp & NOT_FILE_H_BB;
        cnt = popcount(bPhalanx);
        if (cnt) TADD(F_PHALANX, -cnt);

        // Squares defended by own pawns = own pawn attacks.
        Bitboard wDef = ((wp << 7) & NOT_FILE_H_BB) | ((wp << 9) & NOT_FILE_A_BB);
        Bitboard bDef = ((bp >> 9) & NOT_FILE_H_BB) | ((bp >> 7) & NOT_FILE_A_BB);

        cnt = popcount(wp & wDef);  // white pawns defended by white pawns
        if (cnt) TADD(F_SUPPORTED, cnt);
        cnt = popcount(bp & bDef);
        if (cnt) TADD(F_SUPPORTED, -cnt);

        // Backward: not defensible by own pawns AND stop square controlled
        // by an enemy pawn (semi-backward, cheap approximation).
        Bitboard bb = wp;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            if (sq + 8 > 55) continue;  // promotion rank: no stop square
            bool defensible = (wp & PAWN_ATT[BLACK][sq]) != 0;
            bool stopControlled = (bp & PAWN_ATT[WHITE][sq + 8]) != 0;
            if (!defensible && stopControlled && (wp & ADJACENT_FILES[sq & 7]))
                TADD(F_BACKWARD, 1);
        }
        bb = bp;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            if (sq - 8 < 0) continue;
            bool defensible = (bp & PAWN_ATT[WHITE][sq]) != 0;
            bool stopControlled = (wp & PAWN_ATT[BLACK][sq - 8]) != 0;
            if (!defensible && stopControlled && (bp & ADJACENT_FILES[sq & 7]))
                TADD(F_BACKWARD, -1);
        }
    }

    // ---- passed pawns + candidate passers + edge/defended passers ----
    Bitboard allPawns = wp | bp;
    {
        Bitboard bb = wp;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            Bitboard stoppers = bp & PASSED_MASK[WHITE][sq];
            if (stoppers == 0) {
                int r = sq >> 3;
                if (r >= 1 && r <= 6) TADD(F_PASSED_R1 + r - 1, 1);
                e.wPass |= bitOf(sq);
                // Edge-file passers are harder to escort.
                if ((sq & 7) == 0 || (sq & 7) == 7) TADD(F_PASSED_EDGE, 1);
            } else {
                // Candidate passer: every stopper can be dealt with by a
                // lever or a push-attack, and the push is supported.
                Bitboard threats = bp & PAWN_ATT[WHITE][sq];
                Bitboard pushThreats = (sq + 8 <= 55) ? (bp & PAWN_ATT[WHITE][sq + 8])
                                                      : (Bitboard)0;
                Bitboard pushSupport = (sq + 8 <= 55) ? (wp & PAWN_ATT[BLACK][sq + 8])
                                                      : (Bitboard)0;
                // Stoppers that are neither capturable now, nor capturable
                // after a push, nor attackable by the pushed pawn.
                Bitboard leftovers = stoppers & ~(threats | pushThreats);
                if (leftovers == 0 && popcount(pushSupport) >= popcount(pushThreats)) {
                    int r = sq >> 3;
                    if (r >= 2 && r <= 5) TADD(F_PAWN_CAND_R3 + r - 2, 1);
                }
            }
        }
        bb = bp;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            Bitboard stoppers = wp & PASSED_MASK[BLACK][sq];
            if (stoppers == 0) {
                int r = 7 - (sq >> 3);
                if (r >= 1 && r <= 6) TADD(F_PASSED_R1 + r - 1, -1);
                e.bPass |= bitOf(sq);
                if ((sq & 7) == 0 || (sq & 7) == 7) TADD(F_PASSED_EDGE, -1);
            } else {
                Bitboard threats = wp & PAWN_ATT[BLACK][sq];
                Bitboard pushThreats = (sq - 8 >= 0) ? (wp & PAWN_ATT[BLACK][sq - 8])
                                                     : (Bitboard)0;
                Bitboard pushSupport = (sq - 8 >= 0) ? (bp & PAWN_ATT[WHITE][sq - 8])
                                                     : (Bitboard)0;
                Bitboard leftovers = stoppers & ~(threats | pushThreats);
                if (leftovers == 0 && popcount(pushSupport) >= popcount(pushThreats)) {
                    int r = 7 - (sq >> 3);
                    if (r >= 2 && r <= 5) TADD(F_PAWN_CAND_R3 + r - 2, -1);
                }
            }
        }

        // Defended passers (own pawn defends the passer's square).
        Bitboard wDef = ((wp << 7) & NOT_FILE_H_BB) | ((wp << 9) & NOT_FILE_A_BB);
        Bitboard bDef = ((bp >> 9) & NOT_FILE_H_BB) | ((bp >> 7) & NOT_FILE_A_BB);
        int cnt = popcount(e.wPass & wDef);
        if (cnt) TADD(F_PASSED_DEFENDED, cnt);
        cnt = popcount(e.bPass & bDef);
        if (cnt) TADD(F_PASSED_DEFENDED, -cnt);

        // Stacked passers: two (or more) own passers on one file — the rear
        // one is far weaker than a lone passer (its path is occupied).
        for (int f = 0; f < 8; f++) {
            int wc = popcount(e.wPass & FILES_BB[f]);
            if (wc > 1) TADD(F_PASSED_STACKED, wc - 1);
            int bc = popcount(e.bPass & FILES_BB[f]);
            if (bc > 1) TADD(F_PASSED_STACKED, -(bc - 1));
        }
        (void)allPawns;
    }

    e.mg = mgScore;
    e.eg = egScore;
}

/** Union of squares attacked by at least two of the six attack sets. */
inline Bitboard attackedTwice(Bitboard a, Bitboard b, Bitboard c, Bitboard d, Bitboard e,
                              Bitboard f) {
    // v1.4.1 bit-exact speedup: the old 15-pair expansion (15 ANDs + 14
    // ORs) is replaced by the standard running-accumulator identity —
    // att1 holds the union of the sets processed so far, att2 collects
    // every square the NEXT set shares with that union. A square covered
    // by k >= 2 of the six sets enters att2 exactly when its second
    // covering set is folded in; a square covered by 0 or 1 sets never
    // does. Bit-identical result, ~10 ops instead of ~29.
    Bitboard att1 = a, att2 = 0;
    att2 |= att1 & b; att1 |= b;
    att2 |= att1 & c; att1 |= c;
    att2 |= att1 & d; att1 |= d;
    att2 |= att1 & e; att1 |= e;
    att2 |= att1 & f;
    return att2;
}


/** v1.4.3: the king-POSITION eval terms — everything that depends only on
 *  (wp, bp, wk, bk, shelterOn): per-file shelter distance tables, enemy
 *  pawn-storm distances, king-to-nearest-pawn file proximity, king on a
 *  fully open file. Extracted verbatim from evaluate() so the results can
 *  be cached in the pawn hash entry (F_KING_DEFENDERS and F_PAWN_BLOCKED
 *  need knight/bishop/occupancy data and stay live). Accumulates into the
 *  passed mg/eg references exactly as the inline TADDs did; when the tuner
 *  traces, the amounts emit from here on every call (the cache is bypassed
 *  whenever a trace is requested). */
void kingPositionTerms(Bitboard wp, Bitboard bp, int wk, int bk, bool shelterOn,
                       int &mgScore, int &egScore, EvalTrace *tr) {
    // ---- King shelter (midgame; per-file distance x pawn-rank table) ----
    // ---- + pawn storm ----
    if (shelterOn) {
        int wkFile = wk & 7, wkRank = wk >> 3;
        if (wkRank <= 2) {
            for (int ff = (wkFile > 0 ? wkFile - 1 : 0);
                 ff <= (wkFile < 7 ? wkFile + 1 : 7); ff++) {
                int d = ff == wkFile ? 0 : 1;
                Bitboard ahead = wp & FILES_BB[ff];
                // keep only pawns in front of the king on this file
                if (wkRank < 7) ahead &= ~RANKS_LE[wkRank];
                int ourDist = 7;
                if (ahead != 0) ourDist = (lsb(ahead) >> 3) - wkRank;
                if (ahead == 0) {
                    if (d == 0) TADD(F_SHELTER_MISS_D0, 1);
                    else TADD(F_SHELTER_MISS_D1, 1);
                } else {
                    int pr = lsb(ahead) >> 3;  // closest pawn ahead
                    int rd = pr - wkRank;
                    if (rd > 3) rd = 3;
                    if (d == 0)
                        TADD(F_SHELTER_D0_R1 + rd - 1, 1);
                    else
                        TADD(F_SHELTER_D1_R1 + rd - 1, 1);
                }

                // Enemy pawn storm on this file: nearest enemy pawn ahead.
                Bitboard theirs = bp & FILES_BB[ff];
                if (wkRank < 7) theirs &= ~RANKS_LE[wkRank];
                if (theirs != 0) {
                    int theirDist = (lsb(theirs) >> 3) - wkRank;
                    bool blocked = (ourDist != 7) && (ourDist == theirDist - 1);
                    int bucket = theirDist > 3 ? 3 : theirDist;  // 1..3
                    if (bucket >= 1) {
                        if (blocked) {
                            if (bucket == 1) TADD(F_STORM_B1, 1);
                            else if (bucket == 2) TADD(F_STORM_B2, 1);
                        } else {
                            if (bucket == 1) TADD(F_STORM_D1, 1);
                            else if (bucket == 2) TADD(F_STORM_D2, 1);
                            else TADD(F_STORM_D3, 1);
                        }
                    }
                }
            }
        } else {
            TADD(F_SHELTER_KING_FAR, 1);
        }

        int bkFile = bk & 7, bkRank = bk >> 3;
        if (bkRank >= 5) {
            for (int ff = (bkFile > 0 ? bkFile - 1 : 0);
                 ff <= (bkFile < 7 ? bkFile + 1 : 7); ff++) {
                int d = ff == bkFile ? 0 : 1;
                Bitboard ahead = bp & FILES_BB[ff];
                if (bkRank > 0) ahead &= ~RANKS_GE[bkRank];
                int ourDist = 7;
                if (ahead != 0) ourDist = bkRank - (msb(ahead) >> 3);
                if (ahead == 0) {
                    if (d == 0) TADD(F_SHELTER_MISS_D0, -1);
                    else TADD(F_SHELTER_MISS_D1, -1);
                } else {
                    int pr = msb(ahead) >> 3;  // closest pawn ahead (below king)
                    int rd = bkRank - pr;
                    if (rd > 3) rd = 3;
                    if (d == 0)
                        TADD(F_SHELTER_D0_R1 + rd - 1, -1);
                    else
                        TADD(F_SHELTER_D1_R1 + rd - 1, -1);
                }

                Bitboard theirs = wp & FILES_BB[ff];
                if (bkRank > 0) theirs &= ~RANKS_GE[bkRank];
                if (theirs != 0) {
                    int theirDist = bkRank - (msb(theirs) >> 3);
                    bool blocked = (ourDist != 7) && (ourDist == theirDist - 1);
                    int bucket = theirDist > 3 ? 3 : theirDist;
                    if (bucket >= 1) {
                        if (blocked) {
                            if (bucket == 1) TADD(F_STORM_B1, -1);
                            else if (bucket == 2) TADD(F_STORM_B2, -1);
                        } else {
                            if (bucket == 1) TADD(F_STORM_D1, -1);
                            else if (bucket == 2) TADD(F_STORM_D2, -1);
                            else TADD(F_STORM_D3, -1);
                        }
                    }
                }
            }
        } else {
            TADD(F_SHELTER_KING_FAR, -1);
        }
    }

    // King-to-nearest-pawn file proximity (king should stay near pawns).
    {
        // v1.4.1 bit-exact speedup: file occupancy via south-fill (bit f of
        // filesMask set iff file f has a pawn), then the nearest occupied
        // file by ascending distance — the identical minimum the old two
        // 8-iteration file loops computed, with early exit.
        Bitboard filesBb = wp | bp;
        filesBb |= filesBb >> 8;
        filesBb |= filesBb >> 16;
        filesBb |= filesBb >> 32;
        int filesMask = (int)(filesBb & 0xFF);
        int wkf = wk & 7, bkf = bk & 7;
        int wDist = 7, bDist = 7;
        for (int d = 0; d <= 7 && (wDist == 7 || bDist == 7); d++) {
            if (wDist == 7) {
                int lo = wkf - d, hi = wkf + d;
                if ((lo >= 0 && (filesMask & (1 << lo))) ||
                    (hi <= 7 && (filesMask & (1 << hi))))
                    wDist = d;
            }
            if (bDist == 7) {
                int lo = bkf - d, hi = bkf + d;
                if ((lo >= 0 && (filesMask & (1 << lo))) ||
                    (hi <= 7 && (filesMask & (1 << hi))))
                    bDist = d;
            }
        }
        if (wDist == 0) TADD(F_KING_PAWN_F0, 1);
        else if (wDist == 1) TADD(F_KING_PAWN_F1, 1);
        else if (wDist == 2) TADD(F_KING_PAWN_F2, 1);

        if (bDist == 0) TADD(F_KING_PAWN_F0, -1);
        else if (bDist == 1) TADD(F_KING_PAWN_F1, -1);
        else if (bDist == 2) TADD(F_KING_PAWN_F2, -1);
    }

    // ---- v2.5: king on a fully open file ----
    {
        // A king standing on a file with no pawns of either colour is a
        // target down the whole file (rook/queen invasion lines).
        if (((wp | bp) & FILES_BB[wk & 7]) == 0) TADD(F_KING_OPEN_FILE, 1);
        if (((wp | bp) & FILES_BB[bk & 7]) == 0) TADD(F_KING_OPEN_FILE, -1);
    }
}

}  // namespace

int evaluate(const Position &b, PawnHashTable &pawnHash, EvalTrace *trace) {
    EvalTrace *tr = trace;
    if (tr) {
        memset(tr->mgAmt, 0, sizeof(tr->mgAmt));
        memset(tr->egAmt, 0, sizeof(tr->egAmt));
        tr->scaled = false;
    }

    // Fast path: bare kings / single minors — hopeless for either side.
    if ((b.pieceBb[PAWN] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) == 0) {
        int wMin = popcount(b.pieceBb[KNIGHT] & b.colorBb[WHITE]) +
                   popcount(b.pieceBb[BISHOP] & b.colorBb[WHITE]);
        int bMin = popcount(b.pieceBb[KNIGHT] & b.colorBb[BLACK]) +
                   popcount(b.pieceBb[BISHOP] & b.colorBb[BLACK]);
        if (wMin <= 1 && bMin <= 1) return 0;
        // v4.0: KNN vs K cannot force mate — treat as dead drawn so the
        // search never chases an unwinnable +2N material edge (it only
        // burns clock or shuffles into 50-move draws anyway).
        if (wMin == 2 && bMin == 0 &&
            popcount(b.pieceBb[KNIGHT] & b.colorBb[WHITE]) == 2)
            return 0;
        if (bMin == 2 && wMin == 0 &&
            popcount(b.pieceBb[KNIGHT] & b.colorBb[BLACK]) == 2)
            return 0;
    }

    int mgScore = b.mg[WHITE] - b.mg[BLACK];
    int egScore = b.eg[WHITE] - b.eg[BLACK];
    // Phase scale: the tuned model prefers endgame weights to engage ~12%
    // earlier than raw material count (learned from the PST-only fit, which
    // pushed every table uniformly MG-down / EG-up).
    int phase = b.phase < 24 ? b.phase : 24;
    phase = phase * 21 / 24;

    Bitboard white = b.colorBb[WHITE];
    Bitboard black = b.colorBb[BLACK];
    Bitboard occ = b.occupied;
    Bitboard wp = b.pieceBb[PAWN] & white;
    Bitboard bp = b.pieceBb[PAWN] & black;

    int wk = b.kingSquare(WHITE);
    int bk = b.kingSquare(BLACK);

    // ---- Pawn structure (via the pawn hash table) ----
    PawnEntry *pe = pawnHash.probe(b.pawnKey);
    // v1.4.3: kept for the king-position term cache below — the king terms
    // depend on the pawn placement, so their cache is only valid on an
    // entry that actually holds THIS position's pawn key.
    bool pawnHit = !evalPawnHashDisabled && pe->key == b.pawnKey;
    if (pawnHit) {
        mgScore += pe->mg;
        egScore += pe->eg;
    } else {
        PawnEntry fresh;
        fresh.mg = 0;
        fresh.eg = 0;
        fresh.wPass = 0;
        fresh.bPass = 0;
        EvalTrace *ptr = evalPawnHashDisabled ? tr : nullptr;
        pawnStructure(wp, bp, fresh, ptr);
        pe->mg = fresh.mg;
        pe->eg = fresh.eg;
        pe->wPass = fresh.wPass;
        pe->bPass = fresh.bPass;
        pe->key = b.pawnKey;
        mgScore += pe->mg;
        egScore += pe->eg;
    }
    Bitboard wPassers = pe->wPass;
    Bitboard bPassers = pe->bPass;

    // ---- Pawn-derived masks (attacks, double attacks, blocked / rammed) ----
    Bitboard wPawnAtt = ((wp << 7) & NOT_FILE_H_BB) | ((wp << 9) & NOT_FILE_A_BB);
    Bitboard bPawnAtt = ((bp >> 9) & NOT_FILE_H_BB) | ((bp >> 7) & NOT_FILE_A_BB);
    // Squares attacked by two (or more) own pawns — cheap recapture spots.
    Bitboard wPawnAtt2 = ((wp << 7) & NOT_FILE_H_BB) & ((wp << 9) & NOT_FILE_A_BB);
    Bitboard bPawnAtt2 = ((bp >> 9) & NOT_FILE_H_BB) & ((bp >> 7) & NOT_FILE_A_BB);
    Bitboard allPawns = wp | bp;
    // Pawns blocked by any pawn; pawns rammed by an ENEMY pawn.
    Bitboard wBlocked = ((wp << 8) & allPawns) >> 8;
    Bitboard bBlocked = ((bp >> 8) & allPawns) << 8;
    Bitboard wRammed = ((wp << 8) & bp) >> 8;
    Bitboard bRammed = ((bp >> 8) & wp) << 8;

    // Mobility areas: enemy pawn control, own king square and own blocked
    // pawns do not count as real mobility (Ethereal-style).
    Bitboard wMobArea = ~(bPawnAtt | bitOf(wk) | wBlocked);
    Bitboard bMobArea = ~(wPawnAtt | bitOf(bk) | bBlocked);

    // King zones: the king box plus one rank pushed toward the enemy.
    Bitboard wkZone = KING_ATT[wk] | (KING_ATT[wk] << 8);
    Bitboard bkZone = KING_ATT[bk] | (KING_ATT[bk] >> 8);

    // X-ray occupancy: bishops see through own B/Q, rooks through own R/Q.
    Bitboard occNoWB = occ ^ (white & (b.pieceBb[BISHOP] | b.pieceBb[QUEEN]));
    Bitboard occNoBB = occ ^ (black & (b.pieceBb[BISHOP] | b.pieceBb[QUEEN]));
    Bitboard occNoWR = occ ^ (white & (b.pieceBb[ROOK] | b.pieceBb[QUEEN]));
    Bitboard occNoBR = occ ^ (black & (b.pieceBb[ROOK] | b.pieceBb[QUEEN]));
    // The four centre squares (d4, e4, d5, e5).
    Bitboard CENTER_SQ = bitOf(27) | bitOf(28) | bitOf(35) | bitOf(36);

    // ---- Material imbalance (v3.1: piece-count interactions the linear
    // PST material model cannot express; SF-inspired). Defaults 0 keep
    // the eval bit-identical to v3.0.0. ----
    {
        int wNc = popcount(b.pieceBb[KNIGHT] & white);
        int bNc = popcount(b.pieceBb[KNIGHT] & black);
        int wBc = popcount(b.pieceBb[BISHOP] & white);
        int bBc = popcount(b.pieceBb[BISHOP] & black);
        int wRc = popcount(b.pieceBb[ROOK] & white);
        int bRc = popcount(b.pieceBb[ROOK] & black);
        int wQc = popcount(b.pieceBb[QUEEN] & white);
        int bQc = popcount(b.pieceBb[QUEEN] & black);
        int wPc = popcount(wp), bPc = popcount(bp);
        int avgP = (wPc + bPc + 1) / 2;
        TADD(F_IMB_QUEEN_PAWNS, (wQc - bQc) * avgP);
        TADD(F_IMB_ROOK_PAWNS, (wRc - bRc) * avgP);
        TADD(F_IMB_KNIGHT_PAWNS, (wNc - bNc) * avgP);
        TADD(F_IMB_BISHOP_PAWNS, (wBc - bBc) * avgP);
        if (wRc >= 2) TADD(F_IMB_ROOK_PAIR, 1);
        if (bRc >= 2) TADD(F_IMB_ROOK_PAIR, -1);
        if (wNc + wBc >= 2) TADD(F_IMB_MINOR_PAIR, 1);
        if (bNc + bBc >= 2) TADD(F_IMB_MINOR_PAIR, -1);
        // Rooks with enemy-pawn mass to hunt; knights likewise (weaker).
        TADD(F_IMB_ROOK_ENEMY_PAWNS,
             (wRc * (bPc - wPc) - bRc * (wPc - bPc)) / 2);
        TADD(F_IMB_KNIGHT_ENEMY_PAWNS,
             (wNc * (bPc - wPc) - bNc * (wPc - bPc)) / 2);
    }

    // ---- Bishop pair + bad bishop + rammed + behind pawn ----
    if (popcount(b.pieceBb[BISHOP] & white) >= 2) TADD(F_BISHOP_PAIR, 1);
    if (popcount(b.pieceBb[BISHOP] & black) >= 2) TADD(F_BISHOP_PAIR, -1);
    {
        Bitboard wB = b.pieceBb[BISHOP] & white;
        while (wB) {
            int sq = lsb(wB);
            wB &= wB - 1;
            int cnt = popcount(wp & colorSquaresOf(sq));
            if (cnt) TADD(F_BAD_BISHOP, cnt);
            cnt = popcount(wRammed & colorSquaresOf(sq));
            if (cnt) TADD(F_BISHOP_RAMMED, cnt);
            // Bishop behind (one square below) any pawn.
            if (sq >= 8 && (allPawns & bitOf(sq - 8))) TADD(F_BISHOP_BEHIND_PAWN, 1);
        }
        Bitboard bB = b.pieceBb[BISHOP] & black;
        while (bB) {
            int sq = lsb(bB);
            bB &= bB - 1;
            int cnt = popcount(bp & colorSquaresOf(sq));
            if (cnt) TADD(F_BAD_BISHOP, -cnt);
            cnt = popcount(bRammed & colorSquaresOf(sq));
            if (cnt) TADD(F_BISHOP_RAMMED, -cnt);
            if (sq <= 55 && (allPawns & bitOf(sq + 8))) TADD(F_BISHOP_BEHIND_PAWN, -1);
        }
    }

    // ---- Passed-pawn terms (king proximity, connected, blocked) ----
    {
        Bitboard bb = wPassers;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            int promo = (sq & 7) + 56;
            int wd = chebyshev(wk, promo);
            int bd = chebyshev(bk, promo);
            if (wd > 5) wd = 5;
            if (bd > 5) bd = 5;
            TADD(F_PASSED_KING_OWN, 5 - wd);
            TADD(F_PASSED_KING_ENEMY, -(5 - bd));
            // Clean promotion path is checked later, once the attack maps
            // are available (see the threats section).
        }
        bb = bPassers;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            int promo = (sq & 7);
            int bd = chebyshev(bk, promo);
            int wd = chebyshev(wk, promo);
            if (bd > 5) bd = 5;
            if (wd > 5) wd = 5;
            TADD(F_PASSED_KING_OWN, -(5 - bd));
            TADD(F_PASSED_KING_ENEMY, 5 - wd);
        }
        // Connected passers: passers defending / standing next to each other.
        if (wPassers) {
            Bitboard support = ((wPassers << 7) & NOT_FILE_H_BB) |
                               ((wPassers << 9) & NOT_FILE_A_BB) |
                               ((wPassers >> 7) & NOT_FILE_A_BB) |
                               ((wPassers >> 9) & NOT_FILE_H_BB);
            int cnt = popcount(support & wPassers);
            if (cnt > 0) TADD(F_PASSED_CONN, cnt);
        }
        if (bPassers) {
            Bitboard support = ((bPassers << 7) & NOT_FILE_H_BB) |
                               ((bPassers << 9) & NOT_FILE_A_BB) |
                               ((bPassers >> 7) & NOT_FILE_A_BB) |
                               ((bPassers >> 9) & NOT_FILE_H_BB);
            int cnt = popcount(support & bPassers);
            if (cnt > 0) TADD(F_PASSED_CONN, -cnt);
        }
        // Blocked passers.
        bb = wPassers;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            if ((sq >> 3) < 7 && (occ & bitOf(sq + 8))) TADD(F_PASSED_BLOCKED, 1);
        }
        bb = bPassers;
        while (bb) {
            int sq = lsb(bb);
            bb &= bb - 1;
            if ((sq >> 3) > 0 && (occ & bitOf(sq - 8))) TADD(F_PASSED_BLOCKED, -1);
        }
    }

    // ---- Rooks on open / semi-open files, the 7th, behind passers ----
    {
        Bitboard rooks = b.pieceBb[ROOK] & white;
        while (rooks) {
            int sq = lsb(rooks);
            rooks &= rooks - 1;
            Bitboard fileBb = FILES_BB[sq & 7];
            if ((wp & fileBb) == 0) {
                if ((bp & fileBb) == 0) TADD(F_ROOK_OPEN, 1);
                else TADD(F_ROOK_SEMI, 1);
            }
            if ((sq >> 3) == 6 && (bp & RANKS_BB[6]) != 0) TADD(F_ROOK_7TH, 1);

            if (wPassers & fileBb) {
                Bitboard passersOnFile = wPassers & fileBb;
                while (passersOnFile) {
                    int p = lsb(passersOnFile);
                    passersOnFile &= passersOnFile - 1;
                    if ((p >> 3) > (sq >> 3)) {
                        TADD(F_ROOK_BEHIND_PASSER, 1);
                        break;
                    }
                }
            }
        }
        rooks = b.pieceBb[ROOK] & black;
        while (rooks) {
            int sq = lsb(rooks);
            rooks &= rooks - 1;
            Bitboard fileBb = FILES_BB[sq & 7];
            if ((bp & fileBb) == 0) {
                if ((wp & fileBb) == 0) TADD(F_ROOK_OPEN, -1);
                else TADD(F_ROOK_SEMI, -1);
            }
            if ((sq >> 3) == 1 && (wp & RANKS_BB[1]) != 0) TADD(F_ROOK_7TH, -1);

            if (bPassers & fileBb) {
                Bitboard passersOnFile = bPassers & fileBb;
                while (passersOnFile) {
                    int p = lsb(passersOnFile);
                    passersOnFile &= passersOnFile - 1;
                    if ((p >> 3) < (sq >> 3)) {
                        TADD(F_ROOK_BEHIND_PASSER, -1);
                        break;
                    }
                }
            }
        }
    }

    // ---- King defenders (live: knight/bishop placement) + king-position
    // terms (cached in the pawn hash entry) ----
    // v1.4.3: F_SHELTER_* / F_STORM_* / F_KING_PAWN_F* / F_KING_OPEN_FILE
    // depend only on (wp, bp, wk, bk, phase>6) and are cached in the pawn
    // hash entry (see kingPositionTerms). F_KING_DEFENDERS needs the
    // knight/bishop placement and F_PAWN_BLOCKED needs the full occupancy,
    // so both stay live. The cache key includes the phase gate: a capture
    // can cross the phase-6 boundary without touching the pawn key.
    if (phase > 6) {
        {
            Bitboard wDefenders = (wp | b.pieceBb[KNIGHT] | b.pieceBb[BISHOP]) & white;
            int cnt = popcount(wDefenders & wkZone);
            cnt = clampI2(cnt - 3, -3, 5);
            TADD(F_KING_DEFENDERS, cnt);
        }
        {
            Bitboard bDefenders = (bp | b.pieceBb[KNIGHT] | b.pieceBb[BISHOP]) & black;
            int cnt = popcount(bDefenders & bkZone);
            cnt = clampI2(cnt - 3, -3, 5);
            TADD(F_KING_DEFENDERS, -cnt);
        }
    }
    {
        bool shelterOn = phase > 6;
        if (pawnHit && pe->kingValid && pe->wkSq == (uint8_t)wk &&
            pe->bkSq == (uint8_t)bk && pe->kingGate == (uint8_t)shelterOn) {
            mgScore += pe->kpMg;
            egScore += pe->kpEg;
        } else {
            int kmg = 0, keg = 0;
            // (A trace request always recomputes: the tuner must see every
            // feature amount, so the cache is bypassed exactly when the
            // pawn cache is.)
            kingPositionTerms(wp, bp, wk, bk, shelterOn, kmg, keg, tr);
            pe->wkSq = (uint8_t)wk;
            pe->bkSq = (uint8_t)bk;
            pe->kingGate = (uint8_t)shelterOn;
            pe->kpMg = (int16_t)kmg;
            pe->kpEg = (int16_t)keg;
            pe->kingValid = true;
            mgScore += kmg;
            egScore += keg;
        }
    }

    // ---- Blocked pawns (live: full occupancy) ----
    {
        // Blocked pawns: the push square is occupied by any piece.
        int cnt = popcount(wp & (occ >> 8));
        if (cnt) TADD(F_PAWN_BLOCKED, cnt);
        cnt = popcount(bp & (occ << 8));
        if (cnt) TADD(F_PAWN_BLOCKED, -cnt);
    }

    // ---- Mobility + king-attack danger (single pass over N/B/R/Q) ----
    // Full attack sets (own pieces included) are used for mobility-area
    // counting and king-danger; sliders x-ray through own B/Q and R/Q.
    Bitboard wKnightAtt = 0, bKnightAtt = 0;
    Bitboard wBishopAtt = 0, bBishopAtt = 0;
    Bitboard wRookAtt = 0, bRookAtt = 0;
    Bitboard wQueenAtt = 0, bQueenAtt = 0;
    int wDanger = 0, bDanger = 0;
    int wAttackers = 0, bAttackers = 0;

    Bitboard kn = b.pieceBb[KNIGHT] & white;
    while (kn) {
        int sq = lsb(kn);
        kn &= kn - 1;
        Bitboard att = KNIGHT_ATT[sq];
        wKnightAtt |= att;
        int safe = popcount(att & wMobArea);
        TADD(F_MOB_N0 + safe, 1);  // per-count mobility table
        int zoneHits = popcount(att & bkZone & ~bPawnAtt2);
        if (zoneHits > 0) {
            wAttackers++;
            wDanger += KING_ATTACK_WEIGHT[KNIGHT] + zoneHits / 2;
        }
        // Knight outposts: rank 4-6, pawn-protected, beyond enemy pawn reach.
        if ((sq >> 3) >= 3 && (sq >> 3) <= 5 && (wp & PAWN_ATT[BLACK][sq]) != 0 &&
            (bp & PAWN_ATT[WHITE][sq]) == 0) {
            TADD(F_OUTPOST_KNIGHT, 1);
        }
        // Knight behind a pawn (supporting its advance).
        if (sq >= 8 && (allPawns & bitOf(sq - 8))) TADD(F_KNIGHT_BEHIND_PAWN, 1);
        // Knight far from both kings (Siberia).
        int kd = chebyshev(sq, wk);
        int kd2 = chebyshev(sq, bk);
        if (kd2 < kd) kd = kd2;
        if (kd == 4 || kd == 5) TADD(F_KNIGHT_SIBERIA, 1);
        else if (kd >= 6) TADD(F_KNIGHT_SIBERIA2, 1);
    }
    kn = b.pieceBb[KNIGHT] & black;
    while (kn) {
        int sq = lsb(kn);
        kn &= kn - 1;
        Bitboard att = KNIGHT_ATT[sq];
        bKnightAtt |= att;
        int safe = popcount(att & bMobArea);
        TADD(F_MOB_N0 + safe, -1);
        int zoneHits = popcount(att & wkZone & ~wPawnAtt2);
        if (zoneHits > 0) {
            bAttackers++;
            bDanger += KING_ATTACK_WEIGHT[KNIGHT] + zoneHits / 2;
        }
        if ((7 - (sq >> 3)) >= 3 && (7 - (sq >> 3)) <= 5 &&
            (bp & PAWN_ATT[WHITE][sq]) != 0 && (wp & PAWN_ATT[BLACK][sq]) == 0) {
            TADD(F_OUTPOST_KNIGHT, -1);
        }
        if (sq <= 55 && (allPawns & bitOf(sq + 8))) TADD(F_KNIGHT_BEHIND_PAWN, -1);
        int kd = chebyshev(sq, wk);
        int kd2 = chebyshev(sq, bk);
        if (kd2 < kd) kd = kd2;
        if (kd == 4 || kd == 5) TADD(F_KNIGHT_SIBERIA, -1);
        else if (kd >= 6) TADD(F_KNIGHT_SIBERIA2, -1);
    }

    Bitboard bi = b.pieceBb[BISHOP] & white;
    while (bi) {
        int sq = lsb(bi);
        bi &= bi - 1;
        Bitboard att = bishopAttacks(sq, occNoWB);
        wBishopAtt |= att;
        int safe = popcount(att & wMobArea);
        TADD(F_MOB_B0 + safe, 1);
        int zoneHits = popcount(att & bkZone & ~bPawnAtt2);
        if (zoneHits > 0) {
            wAttackers++;
            wDanger += KING_ATTACK_WEIGHT[BISHOP] + zoneHits / 2;
        }
        if ((sq >> 3) >= 3 && (sq >> 3) <= 5 && (wp & PAWN_ATT[BLACK][sq]) != 0 &&
            (bp & PAWN_ATT[WHITE][sq]) == 0) {
            TADD(F_OUTPOST_BISHOP, 1);
        }
        // Bishop on a long diagonal (rim squares) hitting both centres
        // through the pawn wall.
        {
            int f = sq & 7, r = sq >> 3;
            bool onLong = (f == r || f + r == 7) && !(f >= 3 && f <= 4 && r >= 3 && r <= 4);
            if (onLong && popcount(bishopAttacks(sq, allPawns) & CENTER_SQ) >= 2)
                TADD(F_BISHOP_LONG_DIAG, 1);
        }
    }
    bi = b.pieceBb[BISHOP] & black;
    while (bi) {
        int sq = lsb(bi);
        bi &= bi - 1;
        Bitboard att = bishopAttacks(sq, occNoBB);
        bBishopAtt |= att;
        int safe = popcount(att & bMobArea);
        TADD(F_MOB_B0 + safe, -1);
        int zoneHits = popcount(att & wkZone & ~wPawnAtt2);
        if (zoneHits > 0) {
            bAttackers++;
            bDanger += KING_ATTACK_WEIGHT[BISHOP] + zoneHits / 2;
        }
        if ((7 - (sq >> 3)) >= 3 && (7 - (sq >> 3)) <= 5 &&
            (bp & PAWN_ATT[WHITE][sq]) != 0 && (wp & PAWN_ATT[BLACK][sq]) == 0) {
            TADD(F_OUTPOST_BISHOP, -1);
        }
        {
            int f = sq & 7, r = sq >> 3;
            bool onLong = (f == r || f + r == 7) && !(f >= 3 && f <= 4 && r >= 3 && r <= 4);
            if (onLong && popcount(bishopAttacks(sq, allPawns) & CENTER_SQ) >= 2)
                TADD(F_BISHOP_LONG_DIAG, -1);
        }
    }

    Bitboard ro = b.pieceBb[ROOK] & white;
    while (ro) {
        int sq = lsb(ro);
        ro &= ro - 1;
        Bitboard att = rookAttacks(sq, occNoWR);
        wRookAtt |= att;
        int safe = popcount(att & wMobArea);
        TADD(F_MOB_R0 + safe, 1);
        int zoneHits = popcount(att & bkZone & ~bPawnAtt2);
        if (zoneHits > 0) {
            wAttackers++;
            wDanger += KING_ATTACK_WEIGHT[ROOK] + zoneHits / 2;
        }
    }
    ro = b.pieceBb[ROOK] & black;
    while (ro) {
        int sq = lsb(ro);
        ro &= ro - 1;
        Bitboard att = rookAttacks(sq, occNoBR);
        bRookAtt |= att;
        int safe = popcount(att & bMobArea);
        TADD(F_MOB_R0 + safe, -1);
        int zoneHits = popcount(att & wkZone & ~wPawnAtt2);
        if (zoneHits > 0) {
            bAttackers++;
            bDanger += KING_ATTACK_WEIGHT[ROOK] + zoneHits / 2;
        }
    }

    Bitboard qu = b.pieceBb[QUEEN] & white;
    while (qu) {
        int sq = lsb(qu);
        qu &= qu - 1;
        Bitboard att = queenAttacks(sq, occ);
        wQueenAtt |= att;
        int safe = popcount(att & wMobArea);
        TADD(F_MOB_Q0 + safe, 1);
        int zoneHits = popcount(att & bkZone & ~bPawnAtt2);
        if (zoneHits > 0) {
            wAttackers++;
            wDanger += KING_ATTACK_WEIGHT[QUEEN] + zoneHits / 2;
        }
    }
    qu = b.pieceBb[QUEEN] & black;
    while (qu) {
        int sq = lsb(qu);
        qu &= qu - 1;
        Bitboard att = queenAttacks(sq, occ);
        bQueenAtt |= att;
        int safe = popcount(att & bMobArea);
        TADD(F_MOB_Q0 + safe, -1);
        int zoneHits = popcount(att & wkZone & ~wPawnAtt2);
        if (zoneHits > 0) {
            bAttackers++;
            bDanger += KING_ATTACK_WEIGHT[QUEEN] + zoneHits / 2;
        }
    }

    Bitboard wMinorAtt = wKnightAtt | wBishopAtt;
    Bitboard bMinorAtt = bKnightAtt | bBishopAtt;
    Bitboard wAllAtt = wPawnAtt | wMinorAtt | wRookAtt | wQueenAtt;
    Bitboard bAllAtt = bPawnAtt | bMinorAtt | bRookAtt | bQueenAtt;

    // ---- Threats + hanging pieces ----
    {
        Bitboard wMinors = (b.pieceBb[KNIGHT] | b.pieceBb[BISHOP]) & white;
        Bitboard wMajors = (b.pieceBb[ROOK] | b.pieceBb[QUEEN]) & white;
        Bitboard bMinors = (b.pieceBb[KNIGHT] | b.pieceBb[BISHOP]) & black;
        Bitboard bMajors = (b.pieceBb[ROOK] | b.pieceBb[QUEEN]) & black;

        // Pawn attacks on enemy minors/majors.
        int cnt = popcount(bMinors & wPawnAtt);
        if (cnt) TADD(F_THREAT_PAWN_MINOR, cnt);
        cnt = popcount(bMajors & wPawnAtt);
        if (cnt) TADD(F_THREAT_PAWN_MAJOR, cnt);
        cnt = popcount(wMinors & bPawnAtt);
        if (cnt) TADD(F_THREAT_PAWN_MINOR, -cnt);
        cnt = popcount(wMajors & bPawnAtt);
        if (cnt) TADD(F_THREAT_PAWN_MAJOR, -cnt);

        // Pawn-safe minor attacks on enemy majors.
        Bitboard wSafeMinorAtt = wMinorAtt & ~bPawnAtt;
        cnt = popcount(bMajors & wSafeMinorAtt);
        if (cnt) TADD(F_THREAT_MINOR_MAJOR, cnt);
        Bitboard bSafeMinorAtt = bMinorAtt & ~wPawnAtt;
        cnt = popcount(wMajors & bSafeMinorAtt);
        if (cnt) TADD(F_THREAT_MINOR_MAJOR, -cnt);

        // Enemy rooks attacked by our pawns or minors (rook must respond).
        cnt = popcount((b.pieceBb[ROOK] & black) & (wPawnAtt | wMinorAtt));
        if (cnt) TADD(F_THREAT_ROOK_LESSER, cnt);
        cnt = popcount((b.pieceBb[ROOK] & white) & (bPawnAtt | bMinorAtt));
        if (cnt) TADD(F_THREAT_ROOK_LESSER, -cnt);

        // Enemy queen attacked by our pawn / minor / rook.
        cnt = popcount((b.pieceBb[QUEEN] & black) & (wPawnAtt | wMinorAtt | wRookAtt));
        if (cnt) TADD(F_THREAT_QUEEN_ONE, cnt);
        cnt = popcount((b.pieceBb[QUEEN] & white) & (bPawnAtt | bMinorAtt | bRookAtt));
        if (cnt) TADD(F_THREAT_QUEEN_ONE, -cnt);

        // Pawn-push threats: a one-step push attacks an enemy minor/major.
        {
            Bitboard wPush = (wp << 8) & ~occ;
            Bitboard wPushAtt = ((wPush << 7) & NOT_FILE_H_BB) | ((wPush << 9) & NOT_FILE_A_BB);
            cnt = popcount(wPushAtt & (bMinors | bMajors));
            if (cnt) TADD(F_THREAT_BY_PUSH, cnt);

            Bitboard bPush = (bp >> 8) & ~occ;
            Bitboard bPushAtt = ((bPush >> 9) & NOT_FILE_H_BB) | ((bPush >> 7) & NOT_FILE_A_BB);
            cnt = popcount(bPushAtt & (wMinors | wMajors));
            if (cnt) TADD(F_THREAT_BY_PUSH, -cnt);
        }

        // Weak pawns: attacked by an enemy pawn, not defended by own pawns.
        cnt = popcount(wp & bPawnAtt & ~wPawnAtt);
        if (cnt) TADD(F_WEAK_PAWN, cnt);
        cnt = popcount(bp & wPawnAtt & ~bPawnAtt);
        if (cnt) TADD(F_WEAK_PAWN, -cnt);

        // Clean promotion paths: rank>=5 passers with empty, unattacked
        // squares all the way to promotion.
        {
            Bitboard bb2 = wPassers;
            while (bb2) {
                int sq = lsb(bb2);
                bb2 &= bb2 - 1;
                if ((sq >> 3) >= 5) {
                    Bitboard path = FILES_BB[sq & 7] & ~RANKS_LE[sq >> 3];
                    if ((path & (occ | bAllAtt)) == 0) TADD(F_PASSED_PATH, 1);
                }
            }
            bb2 = bPassers;
            while (bb2) {
                int sq = lsb(bb2);
                bb2 &= bb2 - 1;
                if ((sq >> 3) <= 2) {
                    Bitboard path = FILES_BB[sq & 7] & ~RANKS_GE[sq >> 3];
                    if ((path & (occ | wAllAtt)) == 0) TADD(F_PASSED_PATH, -1);
                }
            }
        }

        // Hanging pieces: attacked by us and not defended by the enemy.
        // v1.4.1 bit-exact guard: a square outside the defender union
        // (enemy all-attacks | enemy king attacks) has no enemy piece
        // attacking it under FULL occupancy either (every enemy attack
        // map in that union is computed at full occ or at a SUBSET occ,
        // which only extends slider reach) — so the expensive attackersTo
        // call is provably zero there and can be skipped. Squares inside
        // the union fall through to the exact check (the union may also
        // contain x-ray-only squares; those still get the exact test).
        Bitboard wKingAttGate = KING_ATT[wk];
        Bitboard bKingAttGate = KING_ATT[bk];
        Bitboard blackDefGate = bAllAtt | bKingAttGate;
        Bitboard whiteDefGate = wAllAtt | wKingAttGate;
        // v1.4.2 strict-defended sets: pawn / knight / queen / king attacks
        // are all computed at FULL occupancy (exact — no x-ray extension),
        // so a victim square inside this set has a REAL defender and needs
        // no attackersTo call at all. Only squares defended (possibly) via
        // the x-ray bishop/rook supersets keep the exact test. Combined
        // with the def-gate shortcut: provably-hanging -> O(1),
        // provably-defended -> O(1), only the residual pays attackersTo.
        Bitboard blackStrictDef = bPawnAtt | bKnightAtt | bQueenAtt | bKingAttGate;
        Bitboard whiteStrictDef = wPawnAtt | wKnightAtt | wQueenAtt | wKingAttGate;
        Bitboard victims = bMinors & wAllAtt;
        while (victims) {
            int sq = lsb(victims);
            victims &= victims - 1;
            Bitboard sqBb = bitOf(sq);
            if ((sqBb & blackDefGate) == 0 ||
                ((sqBb & blackStrictDef) == 0 &&
                 (b.attackersTo(sq, occ) & black & ~sqBb) == 0))
                TADD(F_HANGING_MINOR, 1);
        }
        victims = bMajors & wAllAtt;
        while (victims) {
            int sq = lsb(victims);
            victims &= victims - 1;
            Bitboard sqBb = bitOf(sq);
            if ((sqBb & blackDefGate) == 0 ||
                ((sqBb & blackStrictDef) == 0 &&
                 (b.attackersTo(sq, occ) & black & ~sqBb) == 0))
                TADD(F_HANGING_MAJOR, 1);
        }
        victims = wMinors & bAllAtt;
        while (victims) {
            int sq = lsb(victims);
            victims &= victims - 1;
            Bitboard sqBb = bitOf(sq);
            if ((sqBb & whiteDefGate) == 0 ||
                ((sqBb & whiteStrictDef) == 0 &&
                 (b.attackersTo(sq, occ) & white & ~sqBb) == 0))
                TADD(F_HANGING_MINOR, -1);
        }
        victims = wMajors & bAllAtt;
        while (victims) {
            int sq = lsb(victims);
            victims &= victims - 1;
            Bitboard sqBb = bitOf(sq);
            if ((sqBb & whiteDefGate) == 0 ||
                ((sqBb & whiteStrictDef) == 0 &&
                 (b.attackersTo(sq, occ) & white & ~sqBb) == 0))
                TADD(F_HANGING_MAJOR, -1);
        }
    }

    // ---- Shared twice-attack maps + king attacks (used by Space AND danger) ----
    Bitboard wKingAtt = KING_ATT[wk];
    Bitboard bKingAtt = KING_ATT[bk];
    Bitboard wAtt2 = attackedTwice(wPawnAtt, wKnightAtt, wBishopAtt, wRookAtt, wQueenAtt,
                                   wKingAtt);
    Bitboard bAtt2 = attackedTwice(bPawnAtt, bKnightAtt, bBishopAtt, bRookAtt, bQueenAtt,
                                   bKingAtt);

    // ---- Space: restricted squares + uncontested centre ----
    {
        Bitboard uncontrolledW = bAtt2 & wAllAtt & ~wAtt2 & ~wPawnAtt;
        Bitboard uncontrolledB = wAtt2 & bAllAtt & ~bAtt2 & ~bPawnAtt;
        int cnt = popcount(uncontrolledW & occ);
        if (cnt) TADD(F_SPACE_RESTRICT, cnt);
        cnt = popcount(uncontrolledB & occ);
        if (cnt) TADD(F_SPACE_RESTRICT, -cnt);

        // Uncontested central squares matter while much material remains.
        int bigMaterial = popcount(b.pieceBb[KNIGHT] | b.pieceBb[BISHOP]) +
                          2 * popcount(b.pieceBb[ROOK] | b.pieceBb[QUEEN]);
        if (bigMaterial > 12) {
            Bitboard centerBig = (FILES_BB[2] | FILES_BB[3] | FILES_BB[4] | FILES_BB[5]) &
                                 (RANKS_BB[2] | RANKS_BB[3] | RANKS_BB[4] | RANKS_BB[5]);
            Bitboard bAllAttAll = bAllAtt | bKingAtt;
            Bitboard wAllAttAll = wAllAtt | wKingAtt;
            cnt = popcount(~bAllAttAll & (wAllAttAll | white) & centerBig);
            if (cnt) TADD(F_SPACE_CENTER, cnt);
            cnt = popcount(~wAllAttAll & (bAllAttAll | black) & centerBig);
            if (cnt) TADD(F_SPACE_CENTER, -cnt);
        }
    }

    // ---- Closedness: knights like closed, rooks like open positions ----
    {
        int pawnCount = popcount(allPawns);
        int rammedCount = popcount(wRammed);
        // v1.4.1 bit-exact speedup: south-fill file occupancy replaces the
        // 8-iteration loop — bit f set iff file f has any pawn.
        Bitboard openFilesBb = allPawns;
        openFilesBb |= openFilesBb >> 8;
        openFilesBb |= openFilesBb >> 16;
        openFilesBb |= openFilesBb >> 32;
        int openFiles = 8 - popcount(openFilesBb & 0xFF);
        int closedness = (pawnCount + 3 * rammedCount - 4 * openFiles) / 3;
        closedness = clampI2(closedness, 0, 8);
        int knDiff = popcount(b.pieceBb[KNIGHT] & white) - popcount(b.pieceBb[KNIGHT] & black);
        int roDiff = popcount(b.pieceBb[ROOK] & white) - popcount(b.pieceBb[ROOK] & black);
        if (closedness <= 2) {
            TADD(F_CLOSED_KNIGHT_OPEN, knDiff);
            TADD(F_CLOSED_ROOK_OPEN, roDiff);
        } else if (closedness <= 5) {
            TADD(F_CLOSED_KNIGHT_MID, knDiff);
            TADD(F_CLOSED_ROOK_MID, roDiff);
        } else {
            TADD(F_CLOSED_KNIGHT_CLOSED, knDiff);
            TADD(F_CLOSED_ROOK_CLOSED, roDiff);
        }
    }

    // ---- King danger (safe checks + weak squares + quadratic table) ----
    {
        // Safe checks: the attacker can give a check that cannot be met by a
        // cheap capture on the checking square. Weak squares: in the
        // defender's king zone, attacked by the enemy and not doubly
        // defended. Both add danger units on top of the attacker units.
        // (wAtt2/bAtt2/wKingAtt/bKingAtt hoisted above the space block.)
        Bitboard bAllDef = bAllAtt | bKingAtt;
        Bitboard wAllDef = wAllAtt | wKingAtt;
        // White attacking the black king.
        {
            Bitboard weak = wAllAtt & ~bAtt2 & (~bAllDef | bQueenAtt | bKingAtt);
            Bitboard safe = ~white & (~bAllDef | (weak & wAtt2));
            Bitboard bishChk = bishopAttacks(bk, occ);
            Bitboard rookChk = rookAttacks(bk, occ);
            int nch = popcount(KNIGHT_ATT[bk] & safe & wKnightAtt);
            int bch = popcount(bishChk & safe & wBishopAtt);
            int rch = popcount(rookChk & safe & wRookAtt);
            int qch = popcount((bishChk | rookChk) & safe & wQueenAtt);
            wDanger += 3 * (nch > 2 ? 2 : nch) + 2 * (bch > 2 ? 2 : bch) +
                       3 * (rch > 2 ? 2 : rch) + 6 * (qch > 2 ? 2 : qch);
            int weakSq = popcount(weak & bkZone);
            if (weakSq > 4) weakSq = 4;
            wDanger += 2 * weakSq;
        }
        // Black attacking the white king.
        {
            Bitboard weak = bAllAtt & ~wAtt2 & (~wAllDef | wQueenAtt | wKingAtt);
            Bitboard safe = ~black & (~wAllDef | (weak & bAtt2));
            Bitboard bishChk = bishopAttacks(wk, occ);
            Bitboard rookChk = rookAttacks(wk, occ);
            int nch = popcount(KNIGHT_ATT[wk] & safe & bKnightAtt);
            int bch = popcount(bishChk & safe & bBishopAtt);
            int rch = popcount(rookChk & safe & bRookAtt);
            int qch = popcount((bishChk | rookChk) & safe & bQueenAtt);
            bDanger += 3 * (nch > 2 ? 2 : nch) + 2 * (bch > 2 ? 2 : bch) +
                       3 * (rch > 2 ? 2 : rch) + 6 * (qch > 2 ? 2 : qch);
            int weakSq = popcount(weak & wkZone);
            if (weakSq > 4) weakSq = 4;
            bDanger += 2 * weakSq;
        }

        // Queenless attacks rarely mate: cap the danger units.
        bool wQ = (b.pieceBb[QUEEN] & white) != 0;
        bool bQ = (b.pieceBb[QUEEN] & black) != 0;
        if (!wQ) wDanger = wDanger < 12 ? wDanger : 12;
        if (!bQ) bDanger = bDanger < 12 ? bDanger : 12;

        int wAmount = 0, bAmount = 0;
        int wBk = -1, bBk = -1;   // danger-bucket for the SAFETY-curve correction
        if (wAttackers >= 2 || (wAttackers >= 1 && wQ)) {
            wAmount = SAFETY[wDanger > 63 ? 63 : wDanger];
            wBk = (wDanger > 63 ? 63 : wDanger) >> 3;
        }
        if (bAttackers >= 2 || (bAttackers >= 1 && bQ)) {
            bAmount = SAFETY[bDanger > 63 ? 63 : bDanger];
            bBk = (bDanger > 63 ? 63 : bDanger) >> 3;
        }
        int d = wAmount - bAmount;
        if (d != 0) {
            mgScore += EV_MG[F_DANGER] * d;
            egScore += EV_EG[F_DANGER] * (d / 2);
            if (tr) {
                tr->mgAmt[F_DANGER] += d;
                tr->egAmt[F_DANGER] += d / 2;
            }
        }
        // v2.6: tunable per-bucket correction on top of the fixed SAFETY
        // quadratic — reshapes the danger curve without breaking the base
        // (defaults 0 == bit-exact v2.6.0 eval). Bucket k covers danger
        // 8k..8k+7, gated exactly like the wAmount/bAmount above.
        if (wBk >= 0 || bBk >= 0) {
            if (wBk >= 0) {
                mgScore += EV_MG[F_DCOR_B0 + wBk];
                egScore += EV_EG[F_DCOR_B0 + wBk];
            }
            if (bBk >= 0) {
                mgScore -= EV_MG[F_DCOR_B0 + bBk];
                egScore -= EV_EG[F_DCOR_B0 + bBk];
            }
            if (tr) {
                if (wBk >= 0) {
                    tr->mgAmt[F_DCOR_B0 + wBk] += 1;
                    tr->egAmt[F_DCOR_B0 + wBk] += 1;
                }
                if (bBk >= 0) {
                    tr->mgAmt[F_DCOR_B0 + bBk] -= 1;
                    tr->egAmt[F_DCOR_B0 + bBk] -= 1;
                }
            }
        }
    }

    // ---- v2.5: simplification — the side ahead in material prefers
    // piece trades (fewer non-pawn pieces left) in the endgame ----
    {
        int nonPawnTotal = popcount((b.pieceBb[KNIGHT] | b.pieceBb[BISHOP] |
                                     b.pieceBb[ROOK] | b.pieceBb[QUEEN]) &
                                    (white | black));
        if (egScore > 25) TADD(F_SIMPLIFY, -nonPawnTotal);
        else if (egScore < -25) TADD(F_SIMPLIFY, nonPawnTotal);
    }

    // ---- Tempo (linear, traced; moved around the scaling below) ----
    int tempoSgn = b.sideToMove == WHITE ? 1 : -1;
    mgScore += EV_MG[F_TEMPO] * tempoSgn;
    egScore += EV_EG[F_TEMPO] * tempoSgn;
    if (tr) {
        tr->mgAmt[F_TEMPO] += tempoSgn;
        tr->egAmt[F_TEMPO] += tempoSgn;
    }

    // Tuner reference: the fully linear score (pre-scaling), white POV.
    int linMg = mgScore, linEg = egScore;
    if (tr) tr->preScaleScore = (linMg * phase + linEg * (24 - phase)) / 24;

    // Remove the tempo for the scalings (Ethereal ordering: conversion
    // scaling must neither damp nor amplify the tempo), re-added below.
    mgScore -= EV_MG[F_TEMPO] * tempoSgn;
    egScore -= EV_EG[F_TEMPO] * tempoSgn;

    // ---- endgame material scaling (drawish structures) ----
    {
        Bitboard wB = b.pieceBb[BISHOP] & white;
        Bitboard bB = b.pieceBb[BISHOP] & black;
        bool onlyBishops = (b.pieceBb[KNIGHT] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) == 0;
        bool scaled = false;
        if (onlyBishops && popcount(wB) == 1 && popcount(bB) == 1) {
            int wsq = lsb(wB), bsq = lsb(bB);
            // Opposite square colors -> classic drawn fortress tendency.
            if ((((wsq >> 3) + (wsq & 7)) & 1) != (((bsq >> 3) + (bsq & 7)) & 1)) {
                // The more pawns remain, the more winning chances survive:
                // 0 pawns -> scale 4/12 (very drawish), 6+ pawns -> 10/12.
                int pawnCount = popcount(wp | bp);
                int scale = 4 + pawnCount;
                if (scale > 10) scale = 10;
                egScore = egScore * scale / 12;
                scaled = true;
            }
        }
        // A pawnless leader needs real material superiority to convert:
        // big edges (queen-level) keep 3/4, smaller edges scale to 1/2.
        if ((wp == 0 && egScore > 0) || (bp == 0 && egScore < 0)) {
            int adv = egScore > 0 ? egScore : -egScore;
            if (adv > 550) egScore = egScore * 3 / 4;
            else egScore /= 2;
            scaled = true;
        }
        // Wrong-colour bishop + rook pawn: the classic unconvertable ending.
        if (egScore != 0) {
            int strong = egScore > 0 ? WHITE : BLACK;
            Bitboard strongPawns = strong == WHITE ? wp : bp;
            Bitboard strongBishops = b.pieceBb[BISHOP] & b.colorBb[strong];
            bool strongOnlyMinor = popcount(strongBishops) == 1 &&
                                   (b.pieceBb[KNIGHT] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) == 0;
            if (strongOnlyMinor && strongPawns != 0) {
                bool allA = (strongPawns & ~FILE_A_BB) == 0;
                bool allH = (strongPawns & ~FILE_H_BB) == 0;
                if (allA || allH) {
                    int corner = strong == WHITE ? (allA ? 56 : 63) : (allA ? 0 : 7);
                    int bsq = lsb(strongBishops);
                    bool wrongColour =
                        ((((bsq >> 3) + (bsq & 7)) & 1) != (((corner >> 3) + (corner & 7)) & 1));
                    if (wrongColour) {
                        egScore /= 8;
                        scaled = true;
                    }
                }
            }
        }
        // A lone queen against several pieces struggles to convert.
        if (egScore != 0) {
            int strong = egScore > 0 ? WHITE : BLACK;
            int weak = strong ^ 1;
            Bitboard strongNonPawn =
                (b.pieceBb[KNIGHT] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) & b.colorBb[strong];
            Bitboard weakNonPawn =
                (b.pieceBb[KNIGHT] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) & b.colorBb[weak];
            if (popcount(strongNonPawn) == 1 && strongNonPawn == (b.pieceBb[QUEEN] & b.colorBb[strong]) &&
                popcount(weakNonPawn) >= 2 &&
                (b.pieceBb[QUEEN] & b.colorBb[weak]) == 0) {
                egScore = egScore * 3 / 4;
                scaled = true;
            }
        }
        // Conversion potential: damp simple (drawish) endgames toward zero,
        // keep tension in complex ones (Ethereal-style complexity).
        if (egScore != 0) {
            int pawnCount = popcount(wp | bp);
            Bitboard leftFlank = FILES_BB[0] | FILES_BB[1] | FILES_BB[2] | FILES_BB[3];
            Bitboard rightFlank = FILES_BB[4] | FILES_BB[5] | FILES_BB[6] | FILES_BB[7];
            bool pawnsOnBothFlanks = (allPawns & leftFlank) != 0 && (allPawns & rightFlank) != 0;
            bool pawnEndgame = (b.pieceBb[KNIGHT] | b.pieceBb[BISHOP] | b.pieceBb[ROOK] |
                                b.pieceBb[QUEEN]) == 0;
            int complexity = 8 * pawnCount + 82 * (pawnsOnBothFlanks ? 1 : 0) +
                             76 * (pawnEndgame ? 1 : 0) - 157;
            // Diluted for a sub-3000 engine: full-strength draw scaling
            // gives away conversion chances this engine could still use.
            complexity = complexity * TACTURN_COMPLEXITY_PCT / 100;
            int egAbs = egScore < 0 ? -egScore : egScore;
            // v = max(complexity, -|eg|): the endgame score is adjusted
            // toward (sign * complexity) but never past zero.
            int v = complexity > -egAbs ? complexity : -egAbs;
            egScore += (egScore > 0 ? v : -v);
            scaled = true;
        }
        if (tr) tr->scaled = tr->scaled || scaled;
    }

    // ---- v9.2 mop-up: convert KX vs K instead of shuffling into the 50-move rule ----
    // Against a BARE defender king (no pawns, no pieces) with real mating
    // material, the eval stack otherwise carries almost no progress signal:
    // the HCE's pawnless-leader damping halves the material edge and the
    // complexity scaler pulls it further toward zero, while the trained
    // residual is clamp-flat here (the SF training targets clamp at +-600
    // long before mate distances resolve). The search then coasts on a
    // ~+200 eval with no gradient, and 50-move/repetition draws ensue —
    // measured: 4 of 16 games that reach this material state fail to
    // convert at 50 ms/move.
    // Remedy (the classic mop-up terms): reward driving the defender's
    // king toward an edge and closing in with the own king. Applied to
    // egScore only, strictly gated to bare-defender + mating material, so
    // every other ending (and the whole middlegame) is bit-identical.
    // Drawn bare-king material (KBK/KNK/KNNK) is fast-pathed to 0 above
    // and never reaches this block.
    if (egScore != 0) {
        int mopupStrong = egScore > 0 ? WHITE : BLACK;
        int mopupWeak = mopupStrong ^ 1;
        Bitboard weakPawns2 = mopupWeak == WHITE ? wp : bp;
        Bitboard weakNonPawn2 =
            (b.pieceBb[KNIGHT] | b.pieceBb[BISHOP] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) &
            b.colorBb[mopupWeak];
        if (weakPawns2 == 0 && weakNonPawn2 == 0) {
            Bitboard strongNonPawn2 =
                (b.pieceBb[KNIGHT] | b.pieceBb[BISHOP] | b.pieceBb[ROOK] | b.pieceBb[QUEEN]) &
                b.colorBb[mopupStrong];
            bool matingMaterial =
                (strongNonPawn2 & (b.pieceBb[ROOK] | b.pieceBb[QUEEN])) != 0 ||
                popcount(strongNonPawn2) >= 2;
            if (matingMaterial) {
                Bitboard strongPawns2 = mopupStrong == WHITE ? wp : bp;
                int weakK = mopupStrong == WHITE ? bk : wk;
                int strongK = mopupStrong == WHITE ? wk : bk;
                int md = std::abs(fileOf(weakK) - fileOf(strongK)) +
                         std::abs(rankOf(weakK) - rankOf(strongK));
                int mopup;
                // v1.3: KBN vs K is the one mating ending where the corner
                // COLOUR matters — the generic edge drive herded the bare
                // king toward the nearest corner, and KBNK converted only
                // 5 of 8 games at 300 ms. Drive toward the bishop-coloured
                // corner instead (nearest of the two sharing the bishop's
                // square colour).
                bool kbnk = strongPawns2 == 0 && popcount(strongNonPawn2) == 2 &&
                            popcount(strongNonPawn2 & b.pieceBb[BISHOP]) == 1 &&
                            popcount(strongNonPawn2 & b.pieceBb[KNIGHT]) == 1;
                if (kbnk) {
                    int bsq = lsb(strongNonPawn2 & b.pieceBb[BISHOP]);
                    bool lightB = ((((bsq >> 3) + (bsq & 7)) & 1) == 1);
                    int c1 = lightB ? 7 : 0;    // h1 / a1
                    int c2 = lightB ? 56 : 63;  // a8 / h8
                    int d1 = std::abs(fileOf(weakK) - fileOf(c1)) +
                             std::abs(rankOf(weakK) - rankOf(c1));
                    int d2 = std::abs(fileOf(weakK) - fileOf(c2)) +
                             std::abs(rankOf(weakK) - rankOf(c2));
                    int mdCorner = d1 < d2 ? d1 : d2;
                    int nsq = lsb(strongNonPawn2 & b.pieceBb[KNIGHT]);
                    int nmd = std::abs(fileOf(nsq) - fileOf(weakK)) +
                              std::abs(rankOf(nsq) - rankOf(weakK));
                    int edgeDist = std::min(std::min(fileOf(weakK), 7 - fileOf(weakK)),
                                            std::min(rankOf(weakK), 7 - rankOf(weakK)));
                    int cornerDrive = 3 - edgeDist;
                    // Three signals, tuned so herding keeps the BASELINE's
                    // proven dynamics (edge drive + king proximity — halved,
                    // an adjacent king was stalemate-bait) while the corner
                    // COLOUR steers along the edge with the dominant 20-per-
                    // step gradient, and the knight is drawn toward the net
                    // (the mating piece must come; checks do the rest).
                    mopup = 4 * (10 * cornerDrive + 2 * (14 - md)) +
                            20 * (14 - mdCorner) + 4 * (14 - nmd);
                } else {
                    int edgeDist = std::min(std::min(fileOf(weakK), 7 - fileOf(weakK)),
                                            std::min(rankOf(weakK), 7 - rankOf(weakK)));
                    int cornerDrive = 3 - edgeDist;  // 3 = defender on the edge
                    mopup = 4 * (10 * cornerDrive + 4 * (14 - md));
                }
                egScore += (mopupStrong == WHITE ? 1 : -1) * mopup;
                // v1.3: 50-move urgency. Nothing else scaled the eval as the
                // draw rule approached: the engine coasted on a flat +200
                // while the counter climbed to 100 and the game died a
                // draw. From halfmove 64 on, decay the winning score
                // linearly to zero at the rule — waiting lines now score
                // below any line that makes progress (a pawn push resets
                // the clock and restores the full score). Only when the
                // strong side HAS pawns: in pure-piece bare-king endings
                // (KQK/KRK/KBBK/KBNK) nothing can ever reset the counter,
                // so the decay would only crush the mop-up gradient that
                // has to finish the mate before the rule bites.
                if (b.halfmoveClock > 64 && strongPawns2 != 0) {
                    int f = 100 - b.halfmoveClock;  // 35 .. 1
                    egScore = egScore * f / 36;
                }
            }
        }
    }

    // Re-add the tempo after scaling.
    mgScore += EV_MG[F_TEMPO] * tempoSgn;
    egScore += EV_EG[F_TEMPO] * tempoSgn;

    int score = (mgScore * phase + egScore * (24 - phase)) / 24;
    if (score > 29000) score = 29000;
    if (score < -29000) score = -29000;
    return (b.sideToMove == WHITE) ? score : -score;
}

std::string mirrorFen(const std::string &fen) {
    // Split into fields.
    std::string fields[6];
    int nf = 0;
    size_t start = 0;
    for (size_t i = 0; i <= fen.size() && nf < 6; i++) {
        if (i == fen.size() || fen[i] == ' ') {
            if (i > start) fields[nf++] = fen.substr(start, i - start);
            start = i + 1;
        }
    }
    while (nf < 6) {
        fields[nf] = nf == 5 ? "1" : "0";
        nf++;
    }

    // Split placement into ranks, reverse, swap case.
    std::string rows[8];
    int nr = 0;
    start = 0;
    for (size_t i = 0; i <= fields[0].size() && nr < 8; i++) {
        if (i == fields[0].size() || fields[0][i] == '/') {
            rows[nr++] = fields[0].substr(start, i - start);
            start = i + 1;
        }
    }
    std::string out;
    for (int r = 7; r >= 0; r--) {
        for (char c : rows[r]) {
            if (c >= '0' && c <= '9') out += c;
            else out += char(std::isupper((unsigned char)c) ? std::tolower((unsigned char)c)
                                                            : std::toupper((unsigned char)c));
        }
        if (r > 0) out += '/';
    }
    out += ' ';
    out += (fields[1] == "w") ? 'b' : 'w';
    out += ' ' + fields[2] + ' ' + fields[3] + ' ' + fields[4] + ' ' + fields[5];
    return out;
}

}  // namespace tacturn
