// TacTurn Chess Engine v6 — NNUE residual evaluation (HCE + neural residual).
//
// Design (v6 "res512-kb4-ob16" net — the v5.5 architecture grown 2x wider
// (L1 256->512) with 2x finer output taper (8->16 material buckets), which
// the v5.5 campaign measured and REJECTED only because the eager
// accumulator maintenance taxed it ~45% nps. The v6 campaign removed that
// tax (lazy op ring, below), retrained on a 13.5M-position SF 17.1 corpus,
// and made the wider net pay: hybrid vs strict-HCE moved from +15 @ 50 ms /
// +52 @ 150 ms / ~0 @ 400 ms (v5.5) to +81 / +75 / +80 (HISTORY.md 19).):
//   - Features: KING-BUCKETED piece-square, 3072 per perspective
//     (4 king quadrants x 768). The bucket is the perspective's OWN king
//     quadrant in perspective coordinates, so a king move usually dirties
//     nothing. The second perspective sees the board vertically mirrored.
//   - Feature transformer: L1 = 512 int16 accumulator per perspective
//     (v5.5: 256), maintained LAZILY (v6): makeMove records a compact
//     add/sub feature-op stream into StateInfo instead of updating the
//     accumulator; nnueEnsureFresh() materializes the accumulator only
//     when an eval actually needs it, walking a 128-slot Zobrist-keyed
//     ring of per-ply accumulators and replaying recorded ops downward.
//     unmake is FREE (the previous ply's ring slot is still valid); a
//     same-quadrant king move is an exact 4-row shift instead of the
//     v5.x unconditional 32-row refresh. Bit-exact vs the eager scheme
//     (verified: identical bench trees, verifyNnue 11728/0).
//   - Inference (per eval-cache miss): concat [clip(acc[stm]+b1),
//     clip(acc[opp]+b1)] -> L2=32 -> L3=32 -> 1 output, clipped-linear
//     activations and PER-MATERIAL-BUCKET weight stacks for L2/L3/OUT
//     (16 buckets by piece count). On AVX-512 BW+VL+VNNI hosts the L2/L3
//     dots run as batched dpwssd kernels with 4 interleaved accumulators
//     (the single-accumulator version was latency-bound at ~7 cycles per
//     dpwssd; interleaving took inference from 15 us to ~6 us). All int16
//     weights, int32 accumulation, fixed scale QA=4096, NO runtime
//     calibration. Portable path is bit-identical.
//   - The net is trained to predict the RESIDUAL (deep-search eval minus
//     the HCE static eval) in centipawns. A zero net is bit-identical to
//     the v4.0 engine: the shipped default starts from HCE strength and
//     adds only what the trained net proves on top.
//
// Quantization contract (mirrored exactly by the trainer/exporter):
//   acc_q      = sum of int16 FT rows            (|row| <= 880 enforced)
//   A          = clip(acc_q + FT_B, 0, 4096)     (int16, |FT_B| <= 4000)
//   y2[j]      = B2[j] + sum_i A[i]*W2[j][i]     (int32; |W2| <= 400)
//   act2[j]    = z2[j]                             (clipped linear)
//   y3[k]      = B3[k] + sum_j act2[j]*W3[k][j]  (int32; |W3| <= 12000)
//   act3[k]    = z3[k]
//   o          = B4 + sum_k act3[k]*W4[k]        (int32; |W4| <= 8192)
//   residual   = clamp((o * 25) >> (8 + S4), -600, 600)  // cp, stm POV
//
// int32 audit: worst L2 dot = 512 terms x 4096 x 400 = 8.4e8 < 2^31
// (trained L2 weights live at ~2-3 integer units, ~150 sigma away);
// y3 dot = 32 terms x 4096 x 12000 = 1.57e9 < 2^31. Biases are checked
// to |B| <= 3x10^7 by the exporter. The final (o*25) is computed in int64.
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include <stdint.h>

#include "types.h"

// ---- v1.2 SIMD includes for the accumulator row-ops (below) ----
// Same ladder as the inference kernels: AVX-512BW -> AVX2 -> SSE2 -> NEON
// -> portable scalar. Android aarch64/armv7 takes NEON; the portable
// reference build takes SSE2.
#if defined(__AVX2__) || defined(__SSE2__) || defined(__AVX512BW__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace tacturn {
class Position;
struct StateInfo;  // position.h — carries the recorded NnueOp stream

namespace nnue {

// ---- architecture (must match the generated nnue_net.cpp) ----
// v8: stays 512. A 1024-wide experiment (net v8-38m-4ep-1024) measured
// -11% nps on real search trees vs 512 — the lazy ring + VNNI dots do NOT
// absorb the width on this host — and the ~-25 Elo speed tax ate the modest
// accuracy gain (see HISTORY.md v8). The width experiment is parked in the
// v8 campaign notes; 512 + the 37.7M corpus is the shipped configuration.
constexpr int L1 = 512;           // feature-transformer width, per perspective
constexpr int L2 = 32;
constexpr int L3 = 32;
constexpr int OUT_BUCKETS = 16;    // by piece count: clip((pc-1)>>1, 0, 15)
// (v6: 16 — finer material taper; the L2/L3/OUT slices are selected by
// bucket so inference cost is unchanged, only the tables grow)
constexpr int KING_BUCKETS = 4;    // king quadrant buckets (feature index)

// v8: the RAW network output (before blending) is clamped to +-RES_CLAMP.
// v5-v7 used 600. The v8 campaign tested 1500 (enabling true blend-100
// replacement) with a 1024-wide net: search time at 50ms did NOT favor it
// (see HISTORY.md v8) — the shipped v8 net is 512-wide trained on the
// clamp-600 target, so the proven v7 search envelope is preserved.
// The blended result is bounded by RES_CLAMP * blendPct / 100 exactly,
// which is the bound leafEvalBounded uses to skip far-boundary inferences.
constexpr int RES_CLAMP = 600;

// Fixed activation scale: float activations are clamped to [0,1] during
// training and mapped to [0, QA] here. int32 dot-product bounds are audited
// above.
constexpr int QA = 4096;

// Fixed per-layer shifts (power-of-two weight scales; identical for every
// output bucket — the buckets specialize the weights, not the scale).
constexpr int S2 = 7;   // log2 L2 weight scale
constexpr int S3 = 13;  // log2 L3 weight scale (v5.0 author value —
                         // lets the v5.0 L3 warm-start transfer within
                         // the int16 cap)
constexpr int S4 = 12;  // log2 output weight scale (v5.0 author value)

// Weight caps (enforced identically by trainer and exporter).
constexpr int CAP_W1 = 880;    // |FT_W| entry cap (32*880 + QA < 32767)
constexpr int CAP_B1 = 4000;   // |FT_B| cap
constexpr int CAP_W2 = 400;    // |L2_W| cap (512 terms x 4096 x 400 = 8.4e8 < 2^31)
constexpr int CAP_W3 = 12000;  // |L3_W| entry cap
constexpr int CAP_W4 = 8192;   // |OUT_W| entry cap

// Master switch (default on — a zero net is behavior-identical to v4.0).
// The host UCI harness toggles this for A/B gating; the shipped app always
// runs with the trained net enabled.
extern bool enabled;

// Residual blend in percent (100 = full residual). Host/testing only.
// v5.0 shipped default: 50 — the measured optimum of the blend curve
// (50ms: +28, 150ms: +77 vs NNUE-off; blend 100 measured NEGATIVE at both
// TCs; see HISTORY.md v5.0). v5.1/v5.2 keep 50 (re-measured best).
extern int blendPct;

// Main-search static-eval blend. v5.3 shipped default: 50 — measured
// +15 ± 19 (325 games, 50 ms, consistently positive across blocks) over
// the v5.2 default of 0: with the 33%-better v5.2 net, injecting the
// residual into the main-search static (RFP/NMP/razoring/improving
// signal) is now a win, keeping the pruning signal consistent with the
// qsearch stand-pat.
extern int staticBlendPct;

// v8: phase-dependent blend. Output buckets 0..EG_MAX_BUCKET (endgames,
// pieceCount <= 2*EG_MAX_BUCKET + 2) blend the residual at blendPctEG
// instead of blendPct. 0 = disabled (bit-identical v7 behavior) — the
// endgame menus in the datagen corpus leave the net relatively stronger
// there, so a higher endgame blend can pay where the correction is
// trusted, while keeping the middlegame blend at the measured optimum.
constexpr int EG_MAX_BUCKET = 6;  // buckets 0..6 = pieceCount <= 14
extern int blendPctEG;

/** Effective blend percent for an output bucket b. */
inline int blendForBucket(int b) {
    return (blendPctEG > 0 && b <= EG_MAX_BUCKET) ? blendPctEG : blendPct;
}

// ---- datagen sink (host tools only; null in the shipped app) ----
// When set, fires on eval-cache misses the search actually evaluates,
// sampled by dataSinkMask ((nodes & mask) == 0 keeps 1 in mask+1 misses).
typedef void (*DataSinkFn)(void *ctx, const Position &pos, int hceMover);
extern DataSinkFn dataSink;
extern uint64_t dataSinkMask;
extern void *dataSinkCtx;

// ---- incremental accumulator ----
struct Accumulator {
    int16_t v[2][L1];
};

/** v1.4.2 Finny table entry: per-(perspective, own-king-bucket)
 *  accumulator cache. featureIndex() depends on the placement ONLY
 *  through the perspective's OWN king bucket (the enemy king never
 *  enters the index), so an accumulator built for (persp, bucket) is
 *  valid for every placement reachable by adding/removing feature rows.
 *  The entry snapshots the placement (per color+type bitboards) it
 *  represents; a later refresh to the same bucket applies only the
 *  placement DELTA (typically 1-3 pieces) instead of re-summing every
 *  piece on the board. Bit-exact: int16 accumulator arithmetic wraps
 *  (mod 2^16, associative), so cached-plus-delta equals the from-scratch
 *  square-order sum. */
struct FinnyEntry {
    int16_t acc[L1];
    Bitboard pieces[2][6];  // placement the acc represents (color, type)
    bool valid = false;
};

/** King-bucket of the perspective's OWN king, in perspective coordinates
 *  (vertical mirror applied for the black perspective). The quadrant
 *  scheme keeps king moves WITHIN a quadrant dirty-free. */
inline int kingBucketOf(int kingSqPerspective) {
    return (fileOf(kingSqPerspective) >> 2) | ((rankOf(kingSqPerspective) >> 2) << 1);
}

/** Feature index of (color, pieceType, sq) from `perspective`'s view:
 *  king-quadrant-bucketed piece-square (3072 features per perspective).
 *  The second perspective sees the board vertically mirrored. */
inline int featureIndex(int color, int type, int sq, int perspective, int wKingSq,
                        int bKingSq) {
    int ksq = perspective == WHITE ? wKingSq : (bKingSq ^ 56);
    int bucket = kingBucketOf(ksq);
    int s = perspective == WHITE ? sq : (sq ^ 56);
    return bucket * 768 + (color * 6 + type) * 64 + s;
}

// Weight tables — defined in nnue_net.cpp (generated by the trainer).
// L2_W/L3_W are OUTPUT-major rows; each row is the input vector of one
// output unit, dotted with the activation vector (see nnue.cpp for the
// pmaddwd-style SIMD dot products). L2/L3/OUT tables are per output bucket
// (selected by piece count); the FT is shared.
extern const int16_t FT_W[KING_BUCKETS * 768][L1];  // row = feature, col = L1 unit
extern const int16_t FT_B[L1];
extern const int16_t L2_W[OUT_BUCKETS][L2][2 * L1];
extern const int32_t L2_B[OUT_BUCKETS][L2];
extern const int16_t L3_W[OUT_BUCKETS][L3][L2];
extern const int32_t L3_B[OUT_BUCKETS][L3];
extern const int16_t W4[OUT_BUCKETS][L3];
extern const int32_t B4[OUT_BUCKETS];
extern const char NET_ID[64];  // training metadata (printed by the harness)

// ---- hooks called from Position ----
//
// v1.2: the row-ops are vectorized. int16 wraparound add/sub and the
// fused sub+add shift are EXACT at every SIMD width (mod-2^16 arithmetic
// is associative, and wrapping vector add/sub matches the scalar
// (int16_t)cast semantics bit-for-bit), so no verification beyond the
// usual fingerprint equality is required — unlike the GEMV kernels,
// nothing is even reordered. These fire on every lazy-ring
// materialization (~38-42% of evals) and every from-scratch refresh;
// the v6/v1.1 campaigns vectorized the L2/L3 dots but left these as the
// last scalar hot loops in the eval path.

#if defined(__AVX512BW__)

inline void accAddRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 32) {
        __m512i d = _mm512_loadu_si512((const void *)(dst + i));
        __m512i r = _mm512_loadu_si512((const void *)(row + i));
        _mm512_storeu_si512((void *)(dst + i), _mm512_add_epi16(d, r));
    }
}

inline void accSubRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 32) {
        __m512i d = _mm512_loadu_si512((const void *)(dst + i));
        __m512i r = _mm512_loadu_si512((const void *)(row + i));
        _mm512_storeu_si512((void *)(dst + i), _mm512_sub_epi16(d, r));
    }
}

inline void accShiftRows(int16_t *w, int16_t *b, const int16_t *wF,
                          const int16_t *wT, const int16_t *bF, const int16_t *bT) {
    for (int i = 0; i < L1; i += 32) {
        __m512i fw = _mm512_loadu_si512((const void *)(wF + i));
        __m512i tw = _mm512_loadu_si512((const void *)(wT + i));
        __m512i fb = _mm512_loadu_si512((const void *)(bF + i));
        __m512i tb = _mm512_loadu_si512((const void *)(bT + i));
        __m512i dw = _mm512_loadu_si512((const void *)(w + i));
        __m512i db = _mm512_loadu_si512((const void *)(b + i));
        _mm512_storeu_si512((void *)(w + i), _mm512_add_epi16(_mm512_sub_epi16(dw, fw), tw));
        _mm512_storeu_si512((void *)(b + i), _mm512_add_epi16(_mm512_sub_epi16(db, fb), tb));
    }
}

inline void accShiftPerspRows(int16_t *p, const int16_t *rF, const int16_t *rT) {
    for (int i = 0; i < L1; i += 32) {
        __m512i f = _mm512_loadu_si512((const void *)(rF + i));
        __m512i t = _mm512_loadu_si512((const void *)(rT + i));
        __m512i d = _mm512_loadu_si512((const void *)(p + i));
        _mm512_storeu_si512((void *)(p + i), _mm512_add_epi16(_mm512_sub_epi16(d, f), t));
    }
}

// v1.4.4: fused single-pass replay (dst = src - subs + adds). Reads src
// once and writes dst once — the memcpy plus per-op re-read/re-write of
// the 2 KB accumulator collapses into ONE pass. int16 SIMD arithmetic is
// modular (associative and commutative), so grouping all subtractions
// before the additions is element-wise identical to any op ordering.
inline void accFusedRows(int16_t *dst, const int16_t *src,
                         const int16_t *const *sub, int nSub,
                         const int16_t *const *add, int nAdd) {
    for (int i = 0; i < L1; i += 32) {
        __m512i v = _mm512_loadu_si512((const void *)(src + i));
        for (int k = 0; k < nSub; k++)
            v = _mm512_sub_epi16(v, _mm512_loadu_si512((const void *)(sub[k] + i)));
        for (int k = 0; k < nAdd; k++)
            v = _mm512_add_epi16(v, _mm512_loadu_si512((const void *)(add[k] + i)));
        _mm512_storeu_si512((void *)(dst + i), v);
    }
}

#elif defined(__AVX2__)

inline void accAddRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 16) {
        __m256i d = _mm256_loadu_si256((const __m256i *)(dst + i));
        __m256i r = _mm256_loadu_si256((const __m256i *)(row + i));
        _mm256_storeu_si256((__m256i *)(dst + i), _mm256_add_epi16(d, r));
    }
}

inline void accSubRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 16) {
        __m256i d = _mm256_loadu_si256((const __m256i *)(dst + i));
        __m256i r = _mm256_loadu_si256((const __m256i *)(row + i));
        _mm256_storeu_si256((__m256i *)(dst + i), _mm256_sub_epi16(d, r));
    }
}

inline void accShiftRows(int16_t *w, int16_t *b, const int16_t *wF,
                          const int16_t *wT, const int16_t *bF, const int16_t *bT) {
    for (int i = 0; i < L1; i += 16) {
        __m256i fw = _mm256_loadu_si256((const __m256i *)(wF + i));
        __m256i tw = _mm256_loadu_si256((const __m256i *)(wT + i));
        __m256i fb = _mm256_loadu_si256((const __m256i *)(bF + i));
        __m256i tb = _mm256_loadu_si256((const __m256i *)(bT + i));
        __m256i dw = _mm256_loadu_si256((const __m256i *)(w + i));
        __m256i db = _mm256_loadu_si256((const __m256i *)(b + i));
        _mm256_storeu_si256((__m256i *)(w + i), _mm256_add_epi16(_mm256_sub_epi16(dw, fw), tw));
        _mm256_storeu_si256((__m256i *)(b + i), _mm256_add_epi16(_mm256_sub_epi16(db, fb), tb));
    }
}

inline void accShiftPerspRows(int16_t *p, const int16_t *rF, const int16_t *rT) {
    for (int i = 0; i < L1; i += 16) {
        __m256i f = _mm256_loadu_si256((const __m256i *)(rF + i));
        __m256i t = _mm256_loadu_si256((const __m256i *)(rT + i));
        __m256i d = _mm256_loadu_si256((const __m256i *)(p + i));
        _mm256_storeu_si256((__m256i *)(p + i), _mm256_add_epi16(_mm256_sub_epi16(d, f), t));
    }
}

inline void accFusedRows(int16_t *dst, const int16_t *src,
                         const int16_t *const *sub, int nSub,
                         const int16_t *const *add, int nAdd) {
    for (int i = 0; i < L1; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(src + i));
        for (int k = 0; k < nSub; k++)
            v = _mm256_sub_epi16(v, _mm256_loadu_si256((const __m256i *)(sub[k] + i)));
        for (int k = 0; k < nAdd; k++)
            v = _mm256_add_epi16(v, _mm256_loadu_si256((const __m256i *)(add[k] + i)));
        _mm256_storeu_si256((__m256i *)(dst + i), v);
    }
}

#elif defined(__SSE2__)

inline void accAddRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 8) {
        __m128i d = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i r = _mm_loadu_si128((const __m128i *)(row + i));
        _mm_storeu_si128((__m128i *)(dst + i), _mm_add_epi16(d, r));
    }
}

inline void accSubRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 8) {
        __m128i d = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i r = _mm_loadu_si128((const __m128i *)(row + i));
        _mm_storeu_si128((__m128i *)(dst + i), _mm_sub_epi16(d, r));
    }
}

inline void accShiftRows(int16_t *w, int16_t *b, const int16_t *wF,
                          const int16_t *wT, const int16_t *bF, const int16_t *bT) {
    for (int i = 0; i < L1; i += 8) {
        __m128i fw = _mm_loadu_si128((const __m128i *)(wF + i));
        __m128i tw = _mm_loadu_si128((const __m128i *)(wT + i));
        __m128i fb = _mm_loadu_si128((const __m128i *)(bF + i));
        __m128i tb = _mm_loadu_si128((const __m128i *)(bT + i));
        __m128i dw = _mm_loadu_si128((const __m128i *)(w + i));
        __m128i db = _mm_loadu_si128((const __m128i *)(b + i));
        _mm_storeu_si128((__m128i *)(w + i), _mm_add_epi16(_mm_sub_epi16(dw, fw), tw));
        _mm_storeu_si128((__m128i *)(b + i), _mm_add_epi16(_mm_sub_epi16(db, fb), tb));
    }
}

inline void accShiftPerspRows(int16_t *p, const int16_t *rF, const int16_t *rT) {
    for (int i = 0; i < L1; i += 8) {
        __m128i f = _mm_loadu_si128((const __m128i *)(rF + i));
        __m128i t = _mm_loadu_si128((const __m128i *)(rT + i));
        __m128i d = _mm_loadu_si128((const __m128i *)(p + i));
        _mm_storeu_si128((__m128i *)(p + i), _mm_add_epi16(_mm_sub_epi16(d, f), t));
    }
}

inline void accFusedRows(int16_t *dst, const int16_t *src,
                         const int16_t *const *sub, int nSub,
                         const int16_t *const *add, int nAdd) {
    for (int i = 0; i < L1; i += 8) {
        __m128i v = _mm_loadu_si128((const __m128i *)(src + i));
        for (int k = 0; k < nSub; k++)
            v = _mm_sub_epi16(v, _mm_loadu_si128((const __m128i *)(sub[k] + i)));
        for (int k = 0; k < nAdd; k++)
            v = _mm_add_epi16(v, _mm_loadu_si128((const __m128i *)(add[k] + i)));
        _mm_storeu_si128((__m128i *)(dst + i), v);
    }
}

#elif defined(__ARM_NEON) || defined(__aarch64__)

// Android (the deployment target): 512 int16 = 8 lanes of 16B per op.
// Wrapping vaddq/vsubq_s16 exactly match the scalar (int16_t) casts.
inline void accAddRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 8) {
        int16x8_t d = vld1q_s16(dst + i);
        int16x8_t r = vld1q_s16(row + i);
        vst1q_s16(dst + i, vaddq_s16(d, r));
    }
}

inline void accSubRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i += 8) {
        int16x8_t d = vld1q_s16(dst + i);
        int16x8_t r = vld1q_s16(row + i);
        vst1q_s16(dst + i, vsubq_s16(d, r));
    }
}

inline void accShiftRows(int16_t *w, int16_t *b, const int16_t *wF,
                          const int16_t *wT, const int16_t *bF, const int16_t *bT) {
    for (int i = 0; i < L1; i += 8) {
        int16x8_t fw = vld1q_s16(wF + i);
        int16x8_t tw = vld1q_s16(wT + i);
        int16x8_t fb = vld1q_s16(bF + i);
        int16x8_t tb = vld1q_s16(bT + i);
        int16x8_t dw = vld1q_s16(w + i);
        int16x8_t db = vld1q_s16(b + i);
        vst1q_s16(w + i, vaddq_s16(vsubq_s16(dw, fw), tw));
        vst1q_s16(b + i, vaddq_s16(vsubq_s16(db, fb), tb));
    }
}

inline void accShiftPerspRows(int16_t *p, const int16_t *rF, const int16_t *rT) {
    for (int i = 0; i < L1; i += 8) {
        int16x8_t f = vld1q_s16(rF + i);
        int16x8_t t = vld1q_s16(rT + i);
        int16x8_t d = vld1q_s16(p + i);
        vst1q_s16(p + i, vaddq_s16(vsubq_s16(d, f), t));
    }
}

inline void accFusedRows(int16_t *dst, const int16_t *src,
                         const int16_t *const *sub, int nSub,
                         const int16_t *const *add, int nAdd) {
    for (int i = 0; i < L1; i += 8) {
        int16x8_t v = vld1q_s16(src + i);
        for (int k = 0; k < nSub; k++) v = vsubq_s16(v, vld1q_s16(sub[k] + i));
        for (int k = 0; k < nAdd; k++) v = vaddq_s16(v, vld1q_s16(add[k] + i));
        vst1q_s16(dst + i, v);
    }
}

#else
// Portable scalar reference (bit-identical baseline).
inline void accAddRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i++) dst[i] = (int16_t)(dst[i] + row[i]);
}

inline void accSubRow(int16_t *dst, const int16_t *row) {
    for (int i = 0; i < L1; i++) dst[i] = (int16_t)(dst[i] - row[i]);
}

inline void accShiftRows(int16_t *w, int16_t *b, const int16_t *wF,
                          const int16_t *wT, const int16_t *bF, const int16_t *bT) {
    for (int i = 0; i < L1; i++) {
        w[i] = (int16_t)(w[i] - wF[i] + wT[i]);
        b[i] = (int16_t)(b[i] - bF[i] + bT[i]);
    }
}

inline void accShiftPerspRows(int16_t *p, const int16_t *rF, const int16_t *rT) {
    for (int i = 0; i < L1; i++) p[i] = (int16_t)(p[i] - rF[i] + rT[i]);
}

inline void accFusedRows(int16_t *dst, const int16_t *src,
                         const int16_t *const *sub, int nSub,
                         const int16_t *const *add, int nAdd) {
    for (int i = 0; i < L1; i++) {
        int v = src[i];
        for (int k = 0; k < nSub; k++) v -= sub[k][i];
        for (int k = 0; k < nAdd; k++) v += add[k][i];
        dst[i] = (int16_t)v;
    }
}

#endif

inline int rowIndexOf(int color, int type, int sq, int perspective, int wKingSq,
                      int bKingSq) {
    return featureIndex(color, type, sq, perspective, wKingSq, bKingSq);
}

// ------------------------------------------------------------------
// v6 LAZY ACCUMULATOR RING — replay op stream
//
// v5.x maintained the accumulator EAGERLY inside addPiece/removePiece/
// shiftPiece (every make AND every unmake paid the 2xL1 row traffic).
// v6 records each move's feature-row operations (with the exact king
// squares the eager order used) into the StateInfo at make time — a few
// ALU ops, no memory traffic — and unmake pays NOTHING. The accumulator
// itself lives in a per-ply RING inside Position and is materialized
// on demand at eval time (see Position::nnueEnsureFresh): walk to the
// nearest valid ancestor slot, then copy+apply the recorded ops down.
// Slots are keyed by the position's Zobrist key, so a slot stays valid
// across sibling re-searches AND across transpositions; unmake+remake
// of a different move invalidates by key mismatch.
//
// Semantics are bit-identical to v5.x at every eval-visible point:
//   - cross-quadrant king moves mark the mover's OWN perspective dirty
//     (the op stream only updates the other perspective); the first
//     reader refreshes it from the placement;
//   - same-quadrant king moves now update BOTH perspectives
//     incrementally (valid: the own bucket did not change), replacing
//     v5.x's unconditional dirty+refresh — the from-scratch result is
//     identical, minus a 32-row sweep.
// ------------------------------------------------------------------

enum NnueOpKind : uint8_t {
    NNUE_OP_ADD = 0,     // add a piece's feature rows to both perspectives
    NNUE_OP_SUB = 1,    // remove a piece's feature rows from both
    NNUE_OP_SHIFT = 2,  // piece from->to, both perspectives (fused sub+add)
    NNUE_OP_SHIFT_P = 3 // king from->to, ONE perspective (other is dirty)
};

/** One recorded feature-row operation. Row indices are precomputed at
 *  makeMove time with the exact king squares the v5.x eager order used
 *  (captures: pre-move kings; mover/rook: post-move kings). */
struct NnueOp {
    uint8_t kind = NNUE_OP_ADD;
    uint8_t persp = 0;         // for SHIFT_P: the perspective updated
    uint16_t wF = 0, wT = 0;   // white-perspective rows (ADD/SUB use wF)
    uint16_t bF = 0, bT = 0;   // black-perspective rows (ADD/SUB use bF)
};

/** Copy src into dst and apply the recorded ops of StateInfo `si`.
 *  `dirty` is the propagated dirty-perspective mask IN/OUT. */
void applyMoveAcc(Accumulator &dst, const Accumulator &src, const StateInfo &si,
                  uint8_t &dirty);

/** A piece appeared on sq: add its feature row to BOTH perspectives. */
inline void accAdd(Accumulator &acc, int color, int type, int sq, int wKingSq,
                   int bKingSq) {
    accAddRow(acc.v[WHITE], FT_W[rowIndexOf(color, type, sq, WHITE, wKingSq, bKingSq)]);
    accAddRow(acc.v[BLACK], FT_W[rowIndexOf(color, type, sq, BLACK, wKingSq, bKingSq)]);
}

/** A piece left sq: subtract its feature row from BOTH perspectives. */
inline void accSub(Accumulator &acc, int color, int type, int sq, int wKingSq,
                   int bKingSq) {
    accSubRow(acc.v[WHITE], FT_W[rowIndexOf(color, type, sq, WHITE, wKingSq, bKingSq)]);
    accSubRow(acc.v[BLACK], FT_W[rowIndexOf(color, type, sq, BLACK, wKingSq, bKingSq)]);
}

/** A piece moved from->to: exact combination of sub+add per perspective
 *  (the black perspective addresses the vertically mirrored squares). */
inline void accShift(Accumulator &acc, int color, int type, int from, int to,
                     int wKingSq, int bKingSq) {
    const int16_t *wF = FT_W[rowIndexOf(color, type, from, WHITE, wKingSq, bKingSq)];
    const int16_t *wT = FT_W[rowIndexOf(color, type, to, WHITE, wKingSq, bKingSq)];
    const int16_t *bF = FT_W[rowIndexOf(color, type, from, BLACK, wKingSq, bKingSq)];
    const int16_t *bT = FT_W[rowIndexOf(color, type, to, BLACK, wKingSq, bKingSq)];
    accShiftRows(acc.v[WHITE], acc.v[BLACK], wF, wT, bF, bT);
}

/** A piece moved from->to, updating ONLY one perspective (used for the
 *  non-moving perspective during a king move; that perspective's own king
 *  bucket is unchanged, so its rows for the piece are well-defined). */
inline void accShiftPersp(Accumulator &acc, int color, int type, int from, int to,
                          int perspective, int wKingSq, int bKingSq) {
    const int16_t *rF = FT_W[rowIndexOf(color, type, from, perspective, wKingSq, bKingSq)];
    const int16_t *rT = FT_W[rowIndexOf(color, type, to, perspective, wKingSq, bKingSq)];
    accShiftPerspRows(acc.v[perspective], rF, rT);
}

/** Recompute ONE perspective from the current placement (used when that
 *  perspective's own king moved across a bucket boundary). */
void refreshPerspective(Accumulator &acc, const Position &pos, int perspective);

// (The op-RECORDING helpers recAdd/recSub/recShift/recShiftP live in
//  position.cpp — they need the complete StateInfo definition.)

// ---- non-inline API (nnue.cpp) ----

/** Full accumulator recomputation from the placement (setFen path). */
void refresh(Accumulator &acc, const Position &pos);

/** Output bucket from piece count (incl. kings). */
inline int bucketOf(int pieceCount) {
    int b = (pieceCount - 1) >> 1;
    return b < 0 ? 0 : (b >= OUT_BUCKETS ? OUT_BUCKETS - 1 : b);
}

/** Residual in centipawns from the SIDE TO MOVE's POV (quantized; the
 *  blendPct scaling is applied inside and the result clamped to +-600).
 *  v5.3: non-const — the call materializes any lazy king-bucket refresh
 *  first (this is the accumulator's only hot reader). */
int residual(Position &pos);

/** True when the incremental accumulator equals a full refresh.
 *  v5.3: non-const — materializes lazy refreshes first. */
bool verifyIncremental(Position &pos);

/** Double-precision mirror of the integer inference (overflow audit).
 *  v5.3: non-const — materializes lazy refreshes first. */
double referenceResidual(Position &pos);

/** Network metadata string (embedded by the exporter). */
const char *netId();

}  // namespace nnue
}  // namespace tacturn
