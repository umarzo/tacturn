// TacTurn Chess Engine v5 — NNUE residual evaluation implementation.
//
// See nnue.h for the architecture and the quantization contract. This file
// holds everything that is NOT generated: the from-scratch accumulator
// refresh, the quantized inference (SCReLU + per-material output buckets),
// and the verification helpers used by the host harness (incremental-vs-
// refresh fuzz + double-precision reference of the integer inference).
//
// The layer-2/3/output dot products use explicit SIMD (AVX2/SSE2 pmaddwd
// on x86, vmull+vaddq on ARM NEON, plain scalar elsewhere). The
// pair-multiply-add idiom is exact for the int32 bounds audited in the
// header: worst dot = 32 terms x 4096 x 12000 = 1.57e9 < 2^31.
//
// The A-vector (clipped accumulator pair) is built with a SIMD min/max
// stream so the per-eval inference touches each int16 exactly once.

#include "nnue.h"

#include <cmath>
#include <cstring>
#include <string>

// SIMD headers at FILE scope (NEON intrinsics break when <arm_neon.h> is
// included inside a namespace on some NDK toolchains).
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "position.h"

namespace tacturn {
namespace nnue {

bool enabled = true;
// v8 shipped default: the residual is blended at 75% — the measured
// optimum of the blend curve on the v8-generation net (v8-38m-3ep-512,
// b*=81 theory minus search-noise haircut; 1040-game gate: 70 -> +14.0,
// 75 -> +24.8, 80 -> -8.0 vs the v7_1 baseline; see HISTORY.md §22).
// v9.5 (HISTORY.md §27): the v95 different-navigator net runs ~1.5% colder
// in effective scale than the v93 champion (measured implied-a 1.1807 vs
// 1.1980 on a champion-distribution sample — the v5.4 scale lesson in
// reverse), so its operating point sits slightly ABOVE the old blend:
// 77/72 measured +22.5 Elo vs the champion at SPRT(0,10) H1 (n=1883,
// W748 D509 L626), while 75/70 measured +2.3 +- 10.6 at n=1080 on the
// same net.
// v9.6 (HISTORY.md §28): shipped at 77/72 — the SPRT-accepted pairing.
// A harness bug (match.py parse_opts vs run_gate sentence format, §28.4)
// made every opts-carrying gate run at compiled defaults, so the only
// VALID measurements are compiled-default pairings: v96b@77/72 vs the
// v95 champion measured +14.1 +-6.0 SPRT(0,10) H1 accepted (n=3389),
// while v96b@74/73 (compiled separately) measured +0.5 +-12.7 (n=743)
// — the blend axis is real and steep (~4 Elo/unit) and 77/72 is the
// evidence-backed optimum. Ship 77/72.
int blendPct = 77;
// Main-search static-eval blend: tracks blendPct (v7 measured 70 optimal
// for both the search residual and the static blend; see nnue.h).
// v9.5 shipped 72; v9.6 keeps 72 (the accepted pairing ran 77/72).
int staticBlendPct = 72;
// v8 phase blend: endgame-bucket blend (0 = off; see nnue.h).
int blendPctEG = 0;

DataSinkFn dataSink = nullptr;
uint64_t dataSinkMask = 0;
void *dataSinkCtx = nullptr;

namespace {

inline int clipI(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int32_t clipI_(int y, int shift) {
    return clipI(y >> shift, 0, QA);
}

#if defined(__AVX2__)

#if defined(__AVX512BW__) && defined(__AVX512VL__)

/** v5.3: AVX-512 kernel — 32 int16 lanes per madd with TWO independent
 *  accumulators to break the serial add chain (the dependency stall, not
 *  the multiply throughput, was the bottleneck on wide hosts). Horizontal
 *  reduction folds 512 -> 2x256 -> 128 -> scalar exactly once per half.
 */
inline int32_t dotI16(const int16_t *a, const int16_t *w, int n) {
    __m512i acc0 = _mm512_setzero_si512();
    __m512i acc1 = _mm512_setzero_si512();
    __m256i tail = _mm256_setzero_si256();
    int i = 0;
    // Two 512-bit ops per iteration = 64 int16 elements consumed; the
    // stride MUST be 64 (a stride-32 version double-counts the overlap —
    // caught by the int-vs-double verification battery).
    for (; i + 64 <= n; i += 64) {
        acc0 = _mm512_add_epi32(acc0, _mm512_madd_epi16(
            _mm512_loadu_si512((const void *)(a + i)),
            _mm512_loadu_si512((const void *)(w + i))));
        acc1 = _mm512_add_epi32(acc1, _mm512_madd_epi16(
            _mm512_loadu_si512((const void *)(a + i + 32)),
            _mm512_loadu_si512((const void *)(w + i + 32))));
    }
    if (i + 32 <= n) {  // one 512-bit block left
        acc0 = _mm512_add_epi32(acc0, _mm512_madd_epi16(
            _mm512_loadu_si512((const void *)(a + i)),
            _mm512_loadu_si512((const void *)(w + i))));
        i += 32;
    }
    if (i + 16 <= n) {  // AVX2-width tail
        tail = _mm256_madd_epi16(
            _mm256_loadu_si256((const __m256i *)(const void *)(a + i)),
            _mm256_loadu_si256((const __m256i *)(const void *)(w + i)));
        i += 16;
    }
    __m512i acc = _mm512_add_epi32(acc0, acc1);
    // Horizontal sum: 16x32 -> 2x(8x32) -> 8x32 (tail folded in here) ->
    // 4x32 -> scalar. Each lane is added exactly once (the classic
    // double-count pitfall lives in re-adding the extracted halves of
    // BOTH acc0 and acc1).
    __m256i lo = _mm512_castsi512_si256(acc);
    __m256i hi = _mm512_extracti64x4_epi64(acc, 1);
    __m256i s = _mm256_add_epi32(_mm256_add_epi32(lo, hi), tail);
    __m128i l2 = _mm256_castsi256_si128(s);
    __m128i h2 = _mm256_extracti128_si256(s, 1);
    __m128i s2 = _mm_add_epi32(l2, h2);
    s2 = _mm_add_epi32(s2, _mm_shuffle_epi32(s2, _MM_SHUFFLE(1, 0, 3, 2)));
    s2 = _mm_add_epi32(s2, _mm_shuffle_epi32(s2, _MM_SHUFFLE(2, 3, 0, 1)));
    int32_t r = _mm_cvtsi128_si32(s2);
    for (; i < n; i++) r += (int32_t)a[i] * w[i];
    return r;
}

/** dst[i] = clip(src1[i] + src2[i], 0, QA), int16, 32 lanes per op. */
inline void clipBias16(int16_t *dst, const int16_t *src, const int16_t *bias, int n) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i q = _mm512_set1_epi16((int16_t)QA);
    int i = 0;
    for (; i + 32 <= n; i += 32) {
        __m512i va = _mm512_loadu_si512((const void *)(src + i));
        __m512i vb = _mm512_loadu_si512((const void *)(bias + i));
        __m512i t = _mm512_add_epi16(va, vb);
        t = _mm512_max_epi16(t, zero);
        t = _mm512_min_epi16(t, q);
        _mm512_storeu_si512((void *)(dst + i), t);
    }
    for (; i < n; i++) dst[i] = (int16_t)clipI((int)src[i] + bias[i], 0, QA);
}

/** v1.4.7: fused clip + nonzero-pair-index build (replaces the separate
 *  clipBias16 pass + 2 KB buildNzPairs reload). While the clamped vector
 *  is still in a register, the 16 activation PAIRS are tested (a pair is
 *  nonzero iff its int32 bit pattern is nonzero — the identical test
 *  buildNzPairs applied to the stored copy):
 *   - an all-zero chunk is NEVER STORED: the sparse L2 kernel only reads
 *     pairs listed in nzIdx, and an all-zero chunk contributes none —
 *     ~60-70% of chunks on typical middlegame activations;
 *   - a chunk with any nonzero pair is stored and its nonzero indices
 *     compressed into nzIdx in the same ascending order buildNzPairs
 *     produced (pair at int16 offset i = int32 lane i/2).
 *  Bit-exact: every pair the kernel later reads was stored; the index
 *  list is value- and order-identical to the two-pass form.
 *  pairBase is the global pair index of dst[0] (the second perspective's
 *  call continues the numbering). n must be a multiple of 32. */
inline int clipBiasNz(int16_t *dst, const int16_t *src, const int16_t *bias,
                      int n, int pairBase, int *nzIdx) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i q = _mm512_set1_epi16((int16_t)QA);
    const __m512i iota = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                           12, 13, 14, 15);
    int cnt = 0;
    for (int i = 0; i < n; i += 32) {
        __m512i va = _mm512_loadu_si512((const void *)(src + i));
        __m512i vb = _mm512_loadu_si512((const void *)(bias + i));
        __m512i t = _mm512_add_epi16(va, vb);
        t = _mm512_max_epi16(t, zero);
        t = _mm512_min_epi16(t, q);
        __mmask16 nz = _mm512_cmpneq_epi32_mask(t, zero);
        if (nz == 0) continue;  // all 16 pairs zero: never read back
        _mm512_storeu_si512((void *)(dst + i), t);
        __m512i idx = _mm512_add_epi32(_mm512_set1_epi32(pairBase + (i >> 1)), iota);
        _mm512_mask_compressstoreu_epi32((void *)(nzIdx + cnt), nz, idx);
        cnt += __builtin_popcount((unsigned)nz);
    }
    return cnt;
}

#else  // AVX2 only

/** v5.3: dual independent accumulators (ILP) on the AVX2 path too. */
inline int32_t dotI16(const int16_t *a, const int16_t *w, int n) {
    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    int i = 0;
    for (; i + 32 <= n; i += 32) {
        acc0 = _mm256_add_epi32(acc0, _mm256_madd_epi16(
            _mm256_loadu_si256((const __m256i *)(const void *)(a + i)),
            _mm256_loadu_si256((const __m256i *)(const void *)(w + i))));
        acc1 = _mm256_add_epi32(acc1, _mm256_madd_epi16(
            _mm256_loadu_si256((const __m256i *)(const void *)(a + i + 16)),
            _mm256_loadu_si256((const __m256i *)(const void *)(w + i + 16))));
    }
    if (i + 16 <= n) {
        acc0 = _mm256_add_epi32(acc0, _mm256_madd_epi16(
            _mm256_loadu_si256((const __m256i *)(const void *)(a + i)),
            _mm256_loadu_si256((const __m256i *)(const void *)(w + i))));
        i += 16;
    }
    __m256i acc = _mm256_add_epi32(acc0, acc1);
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i s = _mm_add_epi32(lo, hi);
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    int32_t r = _mm_cvtsi128_si32(s);
    for (; i < n; i++) r += (int32_t)a[i] * w[i];
    return r;
}

/** dst[i] = clip(src1[i] + src2[i], 0, QA), int16, n multiple of 16. */
inline void clipBias16(int16_t *dst, const int16_t *src, const int16_t *bias, int n) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i q = _mm256_set1_epi16((int16_t)QA);
    int i = 0;
    for (; i + 16 <= n; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(const void *)(src + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(const void *)(bias + i));
        __m256i t = _mm256_add_epi16(va, vb);
        t = _mm256_max_epi16(t, zero);
        t = _mm256_min_epi16(t, q);
        _mm256_storeu_si256((__m256i *)(void *)(dst + i), t);
    }
    for (; i < n; i++) dst[i] = (int16_t)clipI((int)src[i] + bias[i], 0, QA);
}

#endif  // __AVX512BW__ && __AVX512VL__ (inner AVX-512/AVX-2 selector)

// ------------------------------------------------------------------
// v1.4.2 AVX2 zero-skip GEMV for hosts WITHOUT AVX-512 VNNI (and for
// AVX-512-without-VNNI hosts, which land here too). The old fallback ran
// 32 separate dense dotI16 sweeps (one per output, each with its own
// horizontal reduction) over all 1024 L1 activations — ignoring that
// ~80% of activation PAIRS are zero. This is the same win the v1.3.2
// AVX-512 kernel took, ported to _mm256_madd_epi16:
//   - weights packed PAIR-major [bucket][pair][block][16]: each input
//     pair's four 8-output block-rows are 128 contiguous bytes (the
//     locality rule the v1.4.1 transposes enforce — the v1.3.2 8 KB
//     stride mistake is not repeated here);
//   - each nonzero pair is broadcast once ((a0,a1) in every int32 lane)
//     and fed to all 4 blocks with 4 independent madd accumulator
//     chains (ILP — the single-chain dependency stall is the lesson
//     from the v5.3 dotI16 rewrite);
//   - zero pairs are skipped exactly (a zero pair contributes 0 to every
//     accumulator; integer MACs are associative, so skipping and
//     reassociation are bit-identical);
//   - L3 stays DENSE (16 pairs — the nz scan would cost more than it
//     saves, the same call the AVX-512 path makes) but runs through the
//     same madd GEMV instead of 32 dotI16 sweeps.
// ------------------------------------------------------------------
struct Avx2PackedLayers {
    alignas(32) int16_t l2[OUT_BUCKETS][2 * L1 / 2][L2 / 8][16];
    alignas(32) int16_t l3[OUT_BUCKETS][L2 / 2][L3 / 8][16];
    bool ready = false;
};
static Avx2PackedLayers g_avx2Packed;

static void packAvx2Layers() {
    static_assert(L2 % 8 == 0 && L3 % 8 == 0, "8-output AVX2 blocks");
    for (int b = 0; b < OUT_BUCKETS; b++) {
        for (int q = 0; q < 2 * L1 / 2; q++) {
            for (int blk = 0; blk < L2 / 8; blk++) {
                int16_t *dst = g_avx2Packed.l2[b][q][blk];
                for (int j = 0; j < 8; j++) {
                    dst[2 * j] = L2_W[b][blk * 8 + j][2 * q];
                    dst[2 * j + 1] = L2_W[b][blk * 8 + j][2 * q + 1];
                }
            }
        }
        for (int q = 0; q < L2 / 2; q++) {
            for (int blk = 0; blk < L3 / 8; blk++) {
                int16_t *dst = g_avx2Packed.l3[b][q][blk];
                for (int j = 0; j < 8; j++) {
                    dst[2 * j] = L3_W[b][blk * 8 + j][2 * q];
                    dst[2 * j + 1] = L3_W[b][blk * 8 + j][2 * q + 1];
                }
            }
        }
    }
    g_avx2Packed.ready = true;
}
static const bool g_avx2PackedInit = (packAvx2Layers(), true);

/** Nonzero-pair index list, 8 pairs (one 256-bit vector) at a time.
 *  cmpeq_epi32 + movemask gives the zero pairs; the ctz walk expands the
 *  complement. AVX2 has no compress-store, so the indices land one at a
 *  time — still far cheaper than the madd work each skipped pair saves. */
inline int buildNzPairsAvx2(const int16_t *A, int npairs, int *nzIdx) {
    const __m256i zero = _mm256_setzero_si256();
    int cnt = 0;
    for (int i = 0; i < npairs; i += 8) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(const void *)(A + 2 * i));
        uint32_t eqmask = (uint32_t)_mm256_movemask_ps(
            _mm256_castsi256_ps(_mm256_cmpeq_epi32(v, zero)));
        uint32_t nzmask = ~eqmask & 0xFFu;
        while (nzmask) {
            nzIdx[cnt++] = i + __builtin_ctz(nzmask);
            nzmask &= nzmask - 1;
        }
    }
    return cnt;
}

/** v1.4.7: AVX2 fused clip + nonzero-pair-index build (the mirror of the
 *  AVX-512 clipBiasNz). All-zero 8-pair chunks skip their store; nonzero
 *  chunks store and append their indices via the same ctz walk
 *  buildNzPairsAvx2 used, in the same ascending order. */
inline int clipBiasNzAvx2(int16_t *dst, const int16_t *src, const int16_t *bias,
                          int n, int pairBase, int *nzIdx) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i q = _mm256_set1_epi16((int16_t)QA);
    int cnt = 0;
    for (int i = 0; i < n; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(const void *)(src + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(const void *)(bias + i));
        __m256i t = _mm256_add_epi16(va, vb);
        t = _mm256_max_epi16(t, zero);
        t = _mm256_min_epi16(t, q);
        uint32_t eqmask = (uint32_t)_mm256_movemask_ps(
            _mm256_castsi256_ps(_mm256_cmpeq_epi32(t, zero)));
        uint32_t nzmask = ~eqmask & 0xFFu;
        if (nzmask == 0) continue;  // all 8 pairs zero: never read back
        _mm256_storeu_si256((__m256i *)(void *)(dst + i), t);
        while (nzmask) {
            nzIdx[cnt++] = pairBase + (i >> 1) + __builtin_ctz(nzmask);
            nzmask &= nzmask - 1;
        }
    }
    return cnt;
}

/** GEMV for NBLK blocks of 8 outputs over the packed [pair][NBLK][16]
 *  rows. nzIdx == nullptr -> dense walk (L3). */
template <int NBLK>
inline void dotBlocks8Avx2(const int16_t *A, const int *nzIdx, int cnt,
                           const int16_t *packed, const int32_t *bias,
                           int shift, int16_t *y) {
    __m256i acc[NBLK];
    for (int b = 0; b < NBLK; b++) acc[b] = _mm256_setzero_si256();
    for (int k = 0; k < cnt; k++) {
        const int q = nzIdx ? nzIdx[k] : k;
        int32_t pair;
        std::memcpy(&pair, A + 2 * q, sizeof(pair));  // aliasing-safe
        const __m256i av = _mm256_set1_epi32(pair);   // (a0,a1) every lane
        const int16_t *row = packed + (size_t)q * (NBLK * 16);
        for (int b = 0; b < NBLK; b++)
            acc[b] = _mm256_add_epi32(acc[b], _mm256_madd_epi16(
                av, _mm256_load_si256((const __m256i *)(const void *)(row + b * 16))));
    }
    for (int b = 0; b < NBLK; b++) {
        // v1.4.7: vectorized bias+shift+clip+narrow tail. The scalar loop
        // did 8 branchy clipI_ calls plus 8 narrow stores that the next
        // layer re-loaded with wide vectors (STLF stall). srai + min/max
        // + packs(+permute) is bit-identical: the values are already
        // clamped to [0, QA] before the saturating narrow.
        const __m256i z32 = _mm256_setzero_si256();
        const __m256i qa32 = _mm256_set1_epi32(QA);
        __m256i v = _mm256_add_epi32(acc[b],
                _mm256_loadu_si256((const __m256i *)(const void *)(bias + b * 8)));
        v = _mm256_srai_epi32(v, shift);
        v = _mm256_max_epi32(v, z32);
        v = _mm256_min_epi32(v, qa32);
        __m256i p = _mm256_permute4x64_epi64(_mm256_packs_epi32(v, v),
                                             _MM_SHUFFLE(3, 1, 2, 0));
        _mm_storeu_si128((__m128i *)(void *)(y + b * 8), _mm256_castsi256_si128(p));
    }
}

#elif defined(__SSE2__)

inline int32_t dotI16(const int16_t *a, const int16_t *w, int n) {
    __m128i acc = _mm_setzero_si128();
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        __m128i va = _mm_loadu_si128((const __m128i *)(const void *)(a + i));
        __m128i vw = _mm_loadu_si128((const __m128i *)(const void *)(w + i));
        acc = _mm_add_epi32(acc, _mm_madd_epi16(va, vw));
    }
    __m128i s = acc;
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    int32_t r = _mm_cvtsi128_si32(s);
    for (; i < n; i++) r += (int32_t)a[i] * w[i];
    return r;
}

inline void clipBias16(int16_t *dst, const int16_t *src, const int16_t *bias, int n) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i q = _mm_set1_epi16((int16_t)QA);
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        __m128i va = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i vb = _mm_loadu_si128((const __m128i *)(const void *)(bias + i));
        __m128i t = _mm_add_epi16(va, vb);
        t = _mm_max_epi16(t, zero);
        t = _mm_min_epi16(t, q);
        _mm_storeu_si128((__m128i *)(void *)(dst + i), t);
    }
    for (; i < n; i++) dst[i] = (int16_t)clipI((int)src[i] + bias[i], 0, QA);
}

#elif defined(__ARM_NEON) || defined(__aarch64__)

// ------------------------------------------------------------------
// v1.1 NEON kernels.
//
// The old dot used ONE int32x4 accumulator fed by
//   acc = vaddq_s32(acc, vaddq_s32(vmull lo, vmull hi))
// — a serial vaddq dependency chain (one madd result per ~2 dependent
// adds) that left the multiply ports idle. The rewrite:
//   - FOUR interleaved int32x4 accumulators (ILP across the chain)
//   - vmlal_s16 (fused multiply-accumulate-long) halves the op count:
//     one vmlal replaces vmull + vadd for 4 products
//   - 32 inputs consumed per unrolled iteration
// Integer multiply-accumulate is associative within the audited int32
// bounds (nnue.h), so the reassociation is BIT-IDENTICAL to the old
// kernel and to the double-precision reference.
// ------------------------------------------------------------------
inline int32_t dotI16(const int16_t *a, const int16_t *w, int n) {
    int32x4_t acc0 = vdupq_n_s32(0);
    int32x4_t acc1 = vdupq_n_s32(0);
    int32x4_t acc2 = vdupq_n_s32(0);
    int32x4_t acc3 = vdupq_n_s32(0);
    int i = 0;
    for (; i + 32 <= n; i += 32) {
        int16x8_t va0 = vld1q_s16(a + i);
        int16x8_t vw0 = vld1q_s16(w + i);
        int16x8_t va1 = vld1q_s16(a + i + 8);
        int16x8_t vw1 = vld1q_s16(w + i + 8);
        int16x8_t va2 = vld1q_s16(a + i + 16);
        int16x8_t vw2 = vld1q_s16(w + i + 16);
        int16x8_t va3 = vld1q_s16(a + i + 24);
        int16x8_t vw3 = vld1q_s16(w + i + 24);
        acc0 = vmlal_s16(acc0, vget_low_s16(va0), vget_low_s16(vw0));
        acc1 = vmlal_s16(acc1, vget_high_s16(va0), vget_high_s16(vw0));
        acc2 = vmlal_s16(acc2, vget_low_s16(va1), vget_low_s16(vw1));
        acc3 = vmlal_s16(acc3, vget_high_s16(va1), vget_high_s16(vw1));
        acc0 = vmlal_s16(acc0, vget_low_s16(va2), vget_low_s16(vw2));
        acc1 = vmlal_s16(acc1, vget_high_s16(va2), vget_high_s16(vw2));
        acc2 = vmlal_s16(acc2, vget_low_s16(va3), vget_low_s16(vw3));
        acc3 = vmlal_s16(acc3, vget_high_s16(va3), vget_high_s16(vw3));
    }
    for (; i + 8 <= n; i += 8) {
        int16x8_t va = vld1q_s16(a + i);
        int16x8_t vw = vld1q_s16(w + i);
        acc0 = vmlal_s16(acc0, vget_low_s16(va), vget_low_s16(vw));
        acc1 = vmlal_s16(acc1, vget_high_s16(va), vget_high_s16(vw));
    }
    int32x4_t acc01 = vaddq_s32(acc0, acc1);
    int32x4_t acc23 = vaddq_s32(acc2, acc3);
    int32x4_t acc = vaddq_s32(acc01, acc23);
    int32_t buf[4];
    vst1q_s32(buf, acc);
    int32_t r = buf[0] + buf[1] + buf[2] + buf[3];
    for (; i < n; i++) r += (int32_t)a[i] * w[i];
    return r;
}

inline void clipBias16(int16_t *dst, const int16_t *src, const int16_t *bias, int n) {
    const int16x8_t zero = vdupq_n_s16(0);
    const int16x8_t q = vdupq_n_s16((int16_t)QA);
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        int16x8_t t = vaddq_s16(vld1q_s16(src + i), vld1q_s16(bias + i));
        t = vmaxq_s16(t, zero);
        t = vminq_s16(t, q);
        vst1q_s16(dst + i, t);
    }
    for (; i < n; i++) dst[i] = (int16_t)clipI((int)src[i] + bias[i], 0, QA);
}

// ------------------------------------------------------------------
// v1.1 NEON block-output GEMV — the NEON mirror of the AVX-512 VNNI
// batched kernels. 16 outputs per dot became 8 batches of 4 outputs,
// each batch one int32x4 accumulator (one lane per output), consuming
// input PAIRS broadcast to [a0 x4 | a1 x4] and folded with two vmlal
// into the lane accumulators — ONE horizontal reduction per batch
// instead of one per output, and sequential weight streaming through a
// repacked [pair][8] table. Same integer arithmetic, bit-identical.
//
// Repacked layout for a block of 4 outputs (o0..o3) and input pair q:
//   lanes 0..3 = {W[o0][2q], W[o1][2q], W[o2][2q], W[o3][2q]}   (input 2q)
//   lanes 4..7 = {W[o0][2q+1], W[o1][2q+1], ...}                (input 2q+1)
// ------------------------------------------------------------------
struct NeonPackedLayers {
    // v1.4.1 fix (8 KB stride bug): the v1.3.2 layout was
    //   [bucket][L2/4 blocks][pairs][8]  ->  &l2[b][blk0][q] and
    //   &l2[b][blk1][q] were 512*16 = 8192 bytes apart, so every nonzero
    //   input pair touched 8 cache lines strided by 8 KB across a 64 KB
    //   buffer (the v1.3.2 comment claiming "8 sequential 16-byte
    //   streams" was wrong — the middle index is the block, not the
    //   pair). Transposed to [bucket][pair][block][8]: the 8 blocks'
    //   rows for pair q now occupy 8*16 = 128 CONTIGUOUS bytes (two
    //   64-byte lines), exactly what dotBlocks4PairMajor wants.
    alignas(64) int16_t l2[OUT_BUCKETS][2 * L1 / 2][L2 / 4][8];
    // L3: [bucket][L2/2 pairs][L3/4 blocks][8] — same transposition
    alignas(64) int16_t l3[OUT_BUCKETS][L2 / 2][L3 / 4][8];
    bool ready = false;
};

static NeonPackedLayers g_neonPacked;

static void packNeonLayers() {
    // v1.4.1: pack into the transposed [pair][block][8] layout — the SAME
    // values, merely reindexed (bit-identical arithmetic in the kernel).
    for (int b = 0; b < OUT_BUCKETS; b++) {
        for (int q = 0; q < 2 * L1 / 2; q++) {
            for (int blk = 0; blk < L2 / 4; blk++) {
                int16_t *dst = g_neonPacked.l2[b][q][blk];
                for (int k = 0; k < 4; k++) {
                    dst[k] = L2_W[b][blk * 4 + k][2 * q];
                    dst[4 + k] = L2_W[b][blk * 4 + k][2 * q + 1];
                }
            }
        }
        for (int q = 0; q < L2 / 2; q++) {
            for (int blk = 0; blk < L3 / 4; blk++) {
                int16_t *dst = g_neonPacked.l3[b][q][blk];
                for (int k = 0; k < 4; k++) {
                    dst[k] = L3_W[b][blk * 4 + k][2 * q];
                    dst[4 + k] = L3_W[b][blk * 4 + k][2 * q + 1];
                }
            }
        }
    }
    g_neonPacked.ready = true;
}

static const bool g_neonPackedInit = (packNeonLayers(), true);

// ------------------------------------------------------------------
// v1.3.2 pair-major zero-skipping GEMV for NEON (all 8 four-output
// blocks in ONE pass over the input pairs).
//
// The v1.1 dotBlock4 kernel re-broadcast EVERY input pair for EACH of
// the 8 output blocks (2 vdup + vcombine per block per pair — roughly
// 25k instructions per inference) and consumed all 512 pairs densely.
// This kernel:
//   - loads each pair ONCE and feeds all 8 blocks with vmlal_lane
//     (the pair rides lanes 0/1 of one 64-bit register; no broadcast)
//   - skips pairs whose two int16 activations are both zero (~80% of
//     pairs on the gate corpus): a zero pair contributes exactly 0 to
//     every accumulator, so the skip is exact
//   - v1.4.1 FIX: the packed layout is now genuinely pair-major
//     ([pair][block][8]) — the v1.3.2 comment claimed "the 8 blocks'
//     rows for pair q are 8 sequential 16-byte streams" while the
//     array was still [block][pair][8], putting those 8 rows 8 KB
//     apart. The layout is now transposed to match the comment's
//     intent (128 contiguous bytes per pair).
// Integer MACs are associative and zero terms are the identity, so the
// result is bit-identical to dotBlock4 and to the double reference
// (test_neon_pack.cpp emulates the exact loop shape on the host; the
// fuzznnue battery re-proves it on-device). Expected: several-fold
// fewer instructions in the L1->L2 matmul on big cores — an estimate
// until timed on a phone, per the campaign notes.
// ------------------------------------------------------------------
template <int NPAIRS>
inline void dotBlocks4PairMajor(const int16_t *A,
                                const int16_t (&packed)[NPAIRS][8][8],
                                const int32_t *bias, int shift, int16_t *y) {
    int32x4_t acc0 = vdupq_n_s32(0);
    int32x4_t acc1 = vdupq_n_s32(0);
    int32x4_t acc2 = vdupq_n_s32(0);
    int32x4_t acc3 = vdupq_n_s32(0);
    int32x4_t acc4 = vdupq_n_s32(0);
    int32x4_t acc5 = vdupq_n_s32(0);
    int32x4_t acc6 = vdupq_n_s32(0);
    int32x4_t acc7 = vdupq_n_s32(0);
    for (int q = 0; q < NPAIRS; q++) {
        int32_t p;
        std::memcpy(&p, A + 2 * q, sizeof(p));  // strict-aliasing-safe pair load
        if (p == 0) continue;  // both activations zero: contributes exactly 0
        int16x4_t av = vreinterpret_s16_s32(vdup_n_s32(p));  // lane0=a0, lane1=a1
        // v1.4.1: with the transposed [pair][block][8] layout the 8 blocks'
        // rows for pair q are 8 sequential 16-byte streams — 128 contiguous
        // bytes — instead of 8 lines strided 8 KB apart.
        int16x8_t w;
        w = vld1q_s16(&packed[q][0][0]);
        acc0 = vmlal_lane_s16(acc0, vget_low_s16(w), av, 0);
        acc0 = vmlal_lane_s16(acc0, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][1][0]);
        acc1 = vmlal_lane_s16(acc1, vget_low_s16(w), av, 0);
        acc1 = vmlal_lane_s16(acc1, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][2][0]);
        acc2 = vmlal_lane_s16(acc2, vget_low_s16(w), av, 0);
        acc2 = vmlal_lane_s16(acc2, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][3][0]);
        acc3 = vmlal_lane_s16(acc3, vget_low_s16(w), av, 0);
        acc3 = vmlal_lane_s16(acc3, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][4][0]);
        acc4 = vmlal_lane_s16(acc4, vget_low_s16(w), av, 0);
        acc4 = vmlal_lane_s16(acc4, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][5][0]);
        acc5 = vmlal_lane_s16(acc5, vget_low_s16(w), av, 0);
        acc5 = vmlal_lane_s16(acc5, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][6][0]);
        acc6 = vmlal_lane_s16(acc6, vget_low_s16(w), av, 0);
        acc6 = vmlal_lane_s16(acc6, vget_high_s16(w), av, 1);
        w = vld1q_s16(&packed[q][7][0]);
        acc7 = vmlal_lane_s16(acc7, vget_low_s16(w), av, 0);
        acc7 = vmlal_lane_s16(acc7, vget_high_s16(w), av, 1);
    }
    const int32x4_t accs[8] = {acc0, acc1, acc2, acc3, acc4, acc5, acc6, acc7};
    for (int blk = 0; blk < 8; blk++) {
        int32_t d[4];
        vst1q_s32(d, accs[blk]);
        for (int k = 0; k < 4; k++)
            y[blk * 4 + k] = (int16_t)clipI_(bias[blk * 4 + k] + d[k], shift);
    }
}

#else

inline int32_t dotI16(const int16_t *a, const int16_t *w, int n) {
    int32_t r = 0;
    for (int i = 0; i < n; i++) r += (int32_t)a[i] * w[i];
    return r;
}

inline void clipBias16(int16_t *dst, const int16_t *src, const int16_t *bias, int n) {
    for (int i = 0; i < n; i++) dst[i] = (int16_t)clipI((int)src[i] + bias[i], 0, QA);
}
#endif

}  // namespace

// ------------------------------------------------------------------
// Accumulator refresh (setFen path + verification reference)
// ------------------------------------------------------------------

void refreshPerspective(Accumulator &acc, const Position &pos, int perspective) {
    const int wk = pos.kingSquare(WHITE), bk = pos.kingSquare(BLACK);
    const int ksq = perspective == WHITE ? wk : (bk ^ 56);
    const int bucket = kingBucketOf(ksq);
    FinnyEntry &fe = pos.finny[perspective][bucket];

    if (!fe.valid) {
        // Cold fill: one pass over the occupied squares. lsb iteration
        // visits squares in ascending order — the same order the old
        // 64-square scan used — so the cached sum is identical.
        std::memset(fe.acc, 0, sizeof(fe.acc));
        Bitboard occ = pos.occupied;
        while (occ) {
            int sq = lsb(occ);
            occ &= occ - 1;
            int p = pos.pieceAt[sq];
            accAddRow(fe.acc, FT_W[rowIndexOf(p >> 3, p & 7, sq, perspective, wk, bk)]);
        }
        for (int c = 0; c < 2; c++)
            for (int pt = 0; pt < 6; pt++)
                fe.pieces[c][pt] = pos.colorBb[c] & pos.pieceBb[pt];
        fe.valid = true;
    } else {
        // Delta path: the entry holds a valid accumulator for its snapshot
        // placement; walk every (color, type) plane and apply only the
        // squares that differ. Typically 1-3 pieces after a king walked
        // back into a previously-seen quadrant, instead of a 20+ piece
        // re-sum. rowIndexOf() hashes through the SAME (perspective,
        // bucket) pair this entry is keyed on (the enemy king never
        // enters the index), so the delta rows are exactly the rows a
        // from-scratch refresh would add/remove.
        for (int c = 0; c < 2; c++) {
            for (int pt = 0; pt < 6; pt++) {
                Bitboard cur = pos.colorBb[c] & pos.pieceBb[pt];
                Bitboard old = fe.pieces[c][pt];
                Bitboard added = cur & ~old;
                while (added) {
                    int sq = lsb(added);
                    added &= added - 1;
                    accAddRow(fe.acc, FT_W[rowIndexOf(c, pt, sq, perspective, wk, bk)]);
                }
                Bitboard removed = old & ~cur;
                while (removed) {
                    int sq = lsb(removed);
                    removed &= removed - 1;
                    accSubRow(fe.acc, FT_W[rowIndexOf(c, pt, sq, perspective, wk, bk)]);
                }
                fe.pieces[c][pt] = cur;
            }
        }
    }
    std::memcpy(acc.v[perspective], fe.acc, sizeof(fe.acc));
}

void refresh(Accumulator &acc, const Position &pos) {
    refreshPerspective(acc, pos, WHITE);
    refreshPerspective(acc, pos, BLACK);
}

// ------------------------------------------------------------------
// v6 lazy-ring replay: copy src into dst, apply the recorded ops
// ------------------------------------------------------------------

void applyMoveAcc(Accumulator &dst, const Accumulator &src, const StateInfo &si,
                  uint8_t &dirty) {
    // v1.4.4: fused single-pass replay. The generic path first memcpy'd
    // the 2 KB accumulator and then re-read/re-wrote dst per op — a quiet
    // move (1 SHIFT) touched the accumulator twice, a capture (SUB+SHIFT)
    // three times. The fused path reads src ONCE, applies every recorded
    // row delta, and writes dst ONCE (see accFusedRows in nnue.h).
    // Bit-exact: int16 SIMD arithmetic is modular — subtraction is
    // addition of the negation and addition is associative/commutative
    // modulo 2^16 — so "all subs then all adds" is element-wise identical
    // to any recorded op ordering. SHIFT_P semantics are preserved
    // exactly: only op.persp is updated, the other perspective keeps the
    // plain copy and its dirty bit is set. The in-place and overflow
    // cases fall back to the original op-by-op loop.
    if (&dst == &src) {
        // In-place replay (never hit by the lazy-ring walk, kept for
        // safety): the original path.
        if (si.wasNull) return;
        for (int i = 0; i < si.nnueNops; i++) {
            const NnueOp &op = si.nnueOp[i];
            int16_t *w = dst.v[WHITE];
            int16_t *b = dst.v[BLACK];
            switch (op.kind) {
            case NNUE_OP_ADD:
                accAddRow(w, FT_W[op.wF]);
                accAddRow(b, FT_W[op.bF]);
                break;
            case NNUE_OP_SUB:
                accSubRow(w, FT_W[op.wF]);
                accSubRow(b, FT_W[op.bF]);
                break;
            case NNUE_OP_SHIFT:
                accShiftRows(dst.v[WHITE], dst.v[BLACK], FT_W[op.wF], FT_W[op.wT],
                             FT_W[op.bF], FT_W[op.bT]);
                break;
            case NNUE_OP_SHIFT_P:
                accShiftPerspRows(dst.v[op.persp],
                                  FT_W[op.persp == WHITE ? op.wF : op.bF],
                                  FT_W[op.persp == WHITE ? op.wT : op.bT]);
                dirty |= (uint8_t)(1u << (op.persp ^ 1));
                break;
            }
        }
        return;
    }
    if (si.wasNull) {  // placement unchanged: pure copy
        std::memcpy(&dst, &src, sizeof(Accumulator));
        return;
    }
    // Max recorded ops per move is 3 (promo-capture: SUB victim, SUB pawn,
    // ADD promo), each contributing at most 2 rows to one perspective
    // (castle: king SHIFT + rook SHIFT = 4 rows) — 5 slots is ample.
    const int16_t *subW[5], *addW[5], *subB[5], *addB[5];
    int nSubW = 0, nAddW = 0, nSubB = 0, nAddB = 0;
    bool fusedOk = true;
    for (int i = 0; i < si.nnueNops && fusedOk; i++) {
        const NnueOp &op = si.nnueOp[i];
        switch (op.kind) {
        case NNUE_OP_ADD:
            if (nAddW == 5 || nAddB == 5) { fusedOk = false; break; }
            addW[nAddW++] = FT_W[op.wF];
            addB[nAddB++] = FT_W[op.bF];
            break;
        case NNUE_OP_SUB:
            if (nSubW == 5 || nSubB == 5) { fusedOk = false; break; }
            subW[nSubW++] = FT_W[op.wF];
            subB[nSubB++] = FT_W[op.bF];
            break;
        case NNUE_OP_SHIFT:
            if (nSubW + nAddW + 2 > 5 || nSubB + nAddB + 2 > 5) { fusedOk = false; break; }
            subW[nSubW++] = FT_W[op.wF];
            addW[nAddW++] = FT_W[op.wT];
            subB[nSubB++] = FT_W[op.bF];
            addB[nAddB++] = FT_W[op.bT];
            break;
        case NNUE_OP_SHIFT_P:
            // Only op.persp is updated; the other perspective keeps the
            // plain copy and goes dirty (identical to the generic path).
            if (op.persp == WHITE) {
                if (nSubW + nAddW + 2 > 5) { fusedOk = false; break; }
                subW[nSubW++] = FT_W[op.wF];
                addW[nAddW++] = FT_W[op.wT];
            } else {
                if (nSubB + nAddB + 2 > 5) { fusedOk = false; break; }
                subB[nSubB++] = FT_W[op.bF];
                addB[nAddB++] = FT_W[op.bT];
            }
            dirty |= (uint8_t)(1u << (op.persp ^ 1));
            break;
        }
    }
    if (!fusedOk) {
        // Defensive fallback (unreachable with the current op recorder).
        std::memcpy(&dst, &src, sizeof(Accumulator));
        uint8_t dummyDirty = dirty;
        for (int i = 0; i < si.nnueNops; i++) {
            const NnueOp &op = si.nnueOp[i];
            switch (op.kind) {
            case NNUE_OP_ADD:
                accAddRow(dst.v[WHITE], FT_W[op.wF]);
                accAddRow(dst.v[BLACK], FT_W[op.bF]);
                break;
            case NNUE_OP_SUB:
                accSubRow(dst.v[WHITE], FT_W[op.wF]);
                accSubRow(dst.v[BLACK], FT_W[op.bF]);
                break;
            case NNUE_OP_SHIFT:
                accShiftRows(dst.v[WHITE], dst.v[BLACK], FT_W[op.wF], FT_W[op.wT],
                             FT_W[op.bF], FT_W[op.bT]);
                break;
            case NNUE_OP_SHIFT_P:
                accShiftPerspRows(dst.v[op.persp],
                                  FT_W[op.persp == WHITE ? op.wF : op.bF],
                                  FT_W[op.persp == WHITE ? op.wT : op.bT]);
                dirty |= (uint8_t)(1u << (op.persp ^ 1));
                break;
            }
        }
        (void)dummyDirty;
        return;
    }
    // v1.4.6: skip the fused-row pass for perspectives already marked
    // dirty — their rows are garbage-by-definition (a king-bucket crossing
    // or inherited staleness), and nnueEnsureFresh unconditionally
    // overwrites exactly those perspectives from the placement before any
    // reader sees the accumulator (the `if (dirty & ...) refreshPerspective`
    // blocks at both return sites). Bit-exact work skip: after a bucket
    // crossing at step k, steps k+1..cur-1 no longer burn 1 KB of SIMD
    // row updates on a perspective that is about to be rebuilt anyway.
    if (!(dirty & 1u))
        accFusedRows(dst.v[WHITE], src.v[WHITE], subW, nSubW, addW, nAddW);
    if (!(dirty & 2u))
        accFusedRows(dst.v[BLACK], src.v[BLACK], subB, nSubB, addB, nAddB);
}

// ------------------------------------------------------------------
// Quantized inference — residual in centipawns, side-to-move POV
// ------------------------------------------------------------------

// ---- v6 AVX-512 VNNI batched kernels ----
// The v5.x per-output dot (madd + one horizontal reduction per output)
// spent ~60% of its cycles in the serialized cross-lane reduction chains
// (~24 dependent cycles per output, 32 outputs on the L2 layer alone).
// The VNNI path restructures the GEMV: 16 outputs live as the 16 int32
// LANES of one accumulator; each input PAIR is broadcast and folded in
// with a single _mm512_dpwssd_epi32, so the whole 16-output block is one
// dependency-free madd stream with ONE reduction at the very end.
// Integer dot products are order-independent (bounds audited in nnue.h),
// so this is bit-identical to the madd path and to the double-precision
// reference — the verify battery's int-vs-double fuzz proves it.
//
// Repacked weight layout (built once at startup from the exporter's
// output-major L2_W/L3_W): for pass p (16 outputs) and input pair q:
//   lane 2k = W[p*16+k][2q], lane 2k+1 = W[p*16+k][2q+1]
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512VNNI__)

struct PackedLayers {
    // v1.4.1 fix (fused L2 pass): L2 is now PAIR-major with both 16-output
    // passes adjacent — [bucket][pair][pass][32]. Each nonzero input pair's
    // 64 int16 weights for outputs 0..31 occupy 128 CONTIGUOUS bytes (two
    // 64-byte cache lines), and the fused kernel below broadcasts each
    // nonzero pair ONCE instead of once per pass.
    // L3: [bucket][2 passes][L2 / 2 pairs] of zmm (unchanged, dense)
    alignas(64) int16_t l2[OUT_BUCKETS][2 * L1 / 2][L2 / 16][32];
    // L3: [bucket][2 passes][L2 / 2 pairs] of zmm
    alignas(64) int16_t l3[OUT_BUCKETS][L3 / 16][L2 / 2][32];
    bool ready = false;
};

static PackedLayers g_packed;

static void packLayers() {
    for (int b = 0; b < OUT_BUCKETS; b++) {
        // v1.4.1: transposed L2 packing — [pair][pass][32].
        for (int q = 0; q < 2 * L1 / 2; q++) {
            for (int p = 0; p < L2 / 16; p++) {
                int16_t *dst = g_packed.l2[b][q][p];
                for (int k = 0; k < 16; k++) {
                    dst[2 * k] = L2_W[b][p * 16 + k][2 * q];
                    dst[2 * k + 1] = L2_W[b][p * 16 + k][2 * q + 1];
                }
            }
        }
        for (int p = 0; p < L3 / 16; p++) {
            for (int q = 0; q < L2 / 2; q++) {
                int16_t *dst = g_packed.l3[b][p][q];
                for (int k = 0; k < 16; k++) {
                    dst[2 * k] = L3_W[b][p * 16 + k][2 * q];
                    dst[2 * k + 1] = L3_W[b][p * 16 + k][2 * q + 1];
                }
            }
        }
    }
    g_packed.ready = true;
}

// Packed once at static-init time (before any search thread exists).
static const bool g_packedInit = (packLayers(), true);

/** y[j] = clip((B[j] + dot(A, W[j])) >> shift, 0, QA) for 16 consecutive j's
 *  starting at base, using the packed [input-pair] layout.
 *  FOUR independent accumulators: a single dpwssd chain is LATENCY-bound
 *  (256 dependent ~7-cycle ops per L2 pass); round-robin over 4 chains
 *  keeps the ports saturated instead. Integer adds are associative, so
 *  the split changes nothing but speed. */
/** v1.3: strict-aliasing-safe broadcast of one int16 activation pair as
 *  an int32. The old `*(const int32_t *)(const void *)(A + 2*q)` read int16
 *  storage through an int32 lvalue — UB that actually bit at -O2 on the
 *  host (2.2x node-count drift, garbage varying run to run). memcpy is the
 *  guaranteed-correct load (and handles the 2-byte alignment too). */
inline __m512i loadBroadcastPair(const int16_t *p) {
    int32_t pair;
    std::memcpy(&pair, p, sizeof(pair));
    return _mm512_set1_epi32(pair);
}

inline __m256i dotBlock16Ret(const int16_t *A, const int16_t (*packed)[32], int npairs,
                             const int32_t *bias, int base, int shift) {
    __m512i acc0 = _mm512_setzero_si512();
    __m512i acc1 = _mm512_setzero_si512();
    __m512i acc2 = _mm512_setzero_si512();
    __m512i acc3 = _mm512_setzero_si512();
    int q = 0;
    for (; q + 4 <= npairs; q += 4) {
        acc0 = _mm512_dpwssd_epi32(acc0,
                loadBroadcastPair(A + 2 * (q + 0)),
                _mm512_load_si512((const void *)packed[q + 0]));
        acc1 = _mm512_dpwssd_epi32(acc1,
                loadBroadcastPair(A + 2 * (q + 1)),
                _mm512_load_si512((const void *)packed[q + 1]));
        acc2 = _mm512_dpwssd_epi32(acc2,
                loadBroadcastPair(A + 2 * (q + 2)),
                _mm512_load_si512((const void *)packed[q + 2]));
        acc3 = _mm512_dpwssd_epi32(acc3,
                loadBroadcastPair(A + 2 * (q + 3)),
                _mm512_load_si512((const void *)packed[q + 3]));
    }
    __m512i acc = _mm512_add_epi32(_mm512_add_epi32(acc0, acc1),
                                   _mm512_add_epi32(acc2, acc3));
    for (; q < npairs; q++)
        acc = _mm512_dpwssd_epi32(acc,
                loadBroadcastPair(A + 2 * q),
                _mm512_load_si512((const void *)packed[q]));
    // v1.4.7: vectorized bias+shift+clip+narrow tail, and the 16 clamped
    // int16 outputs are RETURNED in a register (never spilled to a stack
    // array the next layer re-loads — the STLF stall the scalar loop paid
    // 16 times per call). srai_epi32 matches the scalar arithmetic shift
    // exactly; the min/max clamps to [0, QA] run BEFORE the saturating
    // narrow, so the narrow never saturates — bit-identical to clipI_.
    const __m512i z32 = _mm512_setzero_si512();
    const __m512i qa32 = _mm512_set1_epi32(QA);
    __m512i v = _mm512_add_epi32(acc,
            _mm512_loadu_si512((const void *)(bias + base)));
    v = _mm512_srai_epi32(v, shift);
    v = _mm512_max_epi32(v, z32);
    v = _mm512_min_epi32(v, qa32);
    return _mm512_cvtsepi32_epi16(v);
}

// ------------------------------------------------------------------
// v1.3.2 zero-skipping L1->L2 matmul.
//
// On the gate corpus ~89% of the clipped activations are zero and ~80%
// of the input PAIRS are entirely zero; a dpwssd against a zero pair
// adds exactly 0 to every accumulator lane, so those pairs (and their
// 64-byte weight loads) can be dropped outright. The nonzero pairs are
// compressed into a compact index list once per inference and shared by
// BOTH 16-output L2 passes. Integer dot products are associative and
// zero terms are the identity, so the surviving sum is unchanged in any
// accumulation order — bit-exact on every inference (fuzznnue battery:
// int kernel vs double reference, plus the baseline A/B evalpos diff).
// Measured on the 1-core AVX-512 gate host: the L2 matmul drops from
// 1,703 to 490 cycles per inference (~+20% nps on top of the v1.3.2 TT
// packing; together 600k -> 884k nps).
// ------------------------------------------------------------------

/** Compress the nonzero pair indices of A[0..2*npairs) into nzIdx.
 *  A pair q is nonzero iff its two int16 activations are both zero,
 *  i.e. the int32 bit pattern (A[2q], A[2q+1]) is nonzero. */
inline int buildNzPairs(const int16_t *A, int npairs, int *nzIdx) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i iota = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                           14, 15);
    int cnt = 0;
    for (int i = 0; i < npairs; i += 16) {
        __m512i v = _mm512_loadu_si512((const void *)(A + 2 * i));
        __mmask16 nz = _mm512_cmpneq_epi32_mask(v, zero);
        if (nz == 0) continue;  // all 16 pairs zero: nothing to record
        __m512i idx = _mm512_add_epi32(_mm512_set1_epi32(i), iota);
        _mm512_mask_compressstoreu_epi32((void *)(nzIdx + cnt), nz, idx);
        cnt += __builtin_popcount((unsigned)nz);
    }
    return cnt;
}

/** v1.4.1 fix (fused L2 pass): BOTH 16-output L2 passes in ONE walk of
 *  the nonzero-pair list. The v1.3.2 form ran dotBlock16NZ twice — each
 *  pass re-broadcast every nonzero pair (loadBroadcastPair + nzIdx load)
 *  and streamed a separate 32 KB weight region. With the transposed
 *  [pair][pass][32] layout each nonzero pair is broadcast ONCE and its 64
 *  weights for outputs 0..31 stream 128 contiguous bytes. Eight
 *  independent accumulators (4 per output half) keep the dpwssd ports
 *  saturated. Integer MACs are associative within the audited int32
 *  bounds, so the reassociation is bit-identical (fuzznnue re-proves it:
 *  int kernel vs double reference).
 */
inline void dotBlock32NZ(const int16_t *A, const int *nzIdx, int cnt,
                         const int16_t (*packed)[2][32], const int32_t *bias,
                         int shift, int16_t *y) {
    __m512i acc0 = _mm512_setzero_si512();
    __m512i acc1 = _mm512_setzero_si512();
    __m512i acc2 = _mm512_setzero_si512();
    __m512i acc3 = _mm512_setzero_si512();
    __m512i acc4 = _mm512_setzero_si512();
    __m512i acc5 = _mm512_setzero_si512();
    __m512i acc6 = _mm512_setzero_si512();
    __m512i acc7 = _mm512_setzero_si512();
    int k = 0;
    for (; k + 4 <= cnt; k += 4) {
        const int16_t (*w0)[32] = packed[nzIdx[k + 0]];
        const int16_t (*w1)[32] = packed[nzIdx[k + 1]];
        const int16_t (*w2)[32] = packed[nzIdx[k + 2]];
        const int16_t (*w3)[32] = packed[nzIdx[k + 3]];
        __m512i p0 = loadBroadcastPair(A + 2 * nzIdx[k + 0]);
        __m512i p1 = loadBroadcastPair(A + 2 * nzIdx[k + 1]);
        __m512i p2 = loadBroadcastPair(A + 2 * nzIdx[k + 2]);
        __m512i p3 = loadBroadcastPair(A + 2 * nzIdx[k + 3]);
        acc0 = _mm512_dpwssd_epi32(acc0, p0, _mm512_load_si512((const void *)w0[0]));
        acc1 = _mm512_dpwssd_epi32(acc1, p0, _mm512_load_si512((const void *)w0[1]));
        acc2 = _mm512_dpwssd_epi32(acc2, p1, _mm512_load_si512((const void *)w1[0]));
        acc3 = _mm512_dpwssd_epi32(acc3, p1, _mm512_load_si512((const void *)w1[1]));
        acc4 = _mm512_dpwssd_epi32(acc4, p2, _mm512_load_si512((const void *)w2[0]));
        acc5 = _mm512_dpwssd_epi32(acc5, p2, _mm512_load_si512((const void *)w2[1]));
        acc6 = _mm512_dpwssd_epi32(acc6, p3, _mm512_load_si512((const void *)w3[0]));
        acc7 = _mm512_dpwssd_epi32(acc7, p3, _mm512_load_si512((const void *)w3[1]));
    }
    // Pass-0 accumulators are acc0/acc2/acc4/acc6 (pairs k+0..k+3, w[0]);
    // pass-1 accumulators are acc1/acc3/acc5/acc7 (pairs k+0..k+3, w[1]).
    __m512i accA = _mm512_add_epi32(_mm512_add_epi32(acc0, acc2),
                                    _mm512_add_epi32(acc4, acc6));
    __m512i accB = _mm512_add_epi32(_mm512_add_epi32(acc1, acc3),
                                    _mm512_add_epi32(acc5, acc7));
    for (; k < cnt; k++) {
        const int16_t (*w)[32] = packed[nzIdx[k]];
        __m512i p = loadBroadcastPair(A + 2 * nzIdx[k]);
        accA = _mm512_dpwssd_epi32(accA, p, _mm512_load_si512((const void *)w[0]));
        accB = _mm512_dpwssd_epi32(accB, p, _mm512_load_si512((const void *)w[1]));
    }
    // v1.4.7: vectorized bias+shift+clip+narrow tail for both 16-output
    // halves (the scalar form did 32 clipI_ calls + 32 narrow stores,
    // then the L3 layer re-loaded them with wide vectors — a 12-15 cycle
    // store-to-load-forwarding stall per load). Bit-identical: srai is
    // the scalar arithmetic shift; clamps run pre-narrow so the
    // saturating cvtsepi32 never saturates.
    const __m512i z32 = _mm512_setzero_si512();
    const __m512i qa32 = _mm512_set1_epi32(QA);
    __m512i vA = _mm512_add_epi32(accA, _mm512_loadu_si512((const void *)(bias + 0)));
    vA = _mm512_srai_epi32(vA, shift);
    vA = _mm512_min_epi32(_mm512_max_epi32(vA, z32), qa32);
    __m512i vB = _mm512_add_epi32(accB, _mm512_loadu_si512((const void *)(bias + 16)));
    vB = _mm512_srai_epi32(vB, shift);
    vB = _mm512_min_epi32(_mm512_max_epi32(vB, z32), qa32);
    _mm256_storeu_si256((__m256i *)(void *)(y + 0), _mm512_cvtsepi32_epi16(vA));
    _mm256_storeu_si256((__m256i *)(void *)(y + 16), _mm512_cvtsepi32_epi16(vB));
}

#endif  // AVX-512 BW+VL+VNNI

int residual(Position &pos) {
    // v6: the accumulator ring is not maintained while the subsystem is
    // disabled (nnueEnsureFresh returns null), so short-circuit to the
    // no-residual contract instead of dereferencing it.
    if (!enabled) return 0;
    const Accumulator *accp = pos.nnueEnsureFresh();
    const Accumulator &acc = *accp;
    const int us = pos.sideToMove;
    const int them = us ^ 1;

    // Output bucket by piece count (shared by L2/L3/OUT).
    int pc = popcount(pos.occupied);
    int b = bucketOf(pc);

    // Input vector: [clip(acc[us] + b1) ; clip(acc[them] + b1)]
    // v1.4.6: 64-byte alignment — the clip/store and the nz-pair scan walk
    // these 2 KB aligned to cache lines instead of straddling them.
    // v1.4.7 (AVX-512/VNNI + AVX2 paths): the clip pass IS the nz-pair scan
    // now (clipBiasNz / clipBiasNzAvx2) — the 2 KB reload the standalone
    // buildNzPairs paid is gone, and all-zero chunks are never stored at
    // all (the sparse L2 kernel only reads listed pairs).
    alignas(64) int16_t A[2 * L1];
    alignas(64) int16_t act2[L2];
    int16_t act3[L3];
    int32_t o;

#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512VNNI__)
    // v6 fast path: batched VNNI kernels (bit-identical, single reduction).
    // v1.3.2: the L1->L2 matmul runs on the shared nonzero-pair list
    // (~80% of pairs skipped); L3 stays dense (16 pairs — the scan would
    // cost more than it saves).
    // v1.4.1: the two 16-output L2 passes are FUSED into one
    // dotBlock32NZ walk over the nonzero pairs (see the kernel above).
    // v1.4.7: L3 and the output layer are FUSED — dotBlock16Ret keeps
    // each 16-lane act3 half in an xmm register, one 256-bit madd against
    // W4 and a horizontal sum compute o without act3 ever touching
    // memory. Every product fits int32 (QA*12000 < 2^27) and integer
    // addition is associative, so the summation order is free — the
    // result is bit-identical to the dotI16 reference.
    int nzIdx[L1];
    int nzCnt = clipBiasNz(A, acc.v[us], FT_B, L1, 0, nzIdx);
    nzCnt += clipBiasNz(A + L1, acc.v[them], FT_B, L1, L1 / 2, nzIdx + nzCnt);
    (void)act3;  // fused into registers on this path
    dotBlock32NZ(A, nzIdx, nzCnt, g_packed.l2[b], L2_B[b], S2, act2);
    __m256i a3lo = dotBlock16Ret(act2, g_packed.l3[b][0], L2 / 2, L3_B[b], 0, S3);
    __m256i a3hi = dotBlock16Ret(act2, g_packed.l3[b][1], L2 / 2, L3_B[b], 16, S3);
    __m256i mad = _mm256_add_epi32(
            _mm256_madd_epi16(a3lo, _mm256_loadu_si256((const __m256i *)(const void *)(W4[b] + 0))),
            _mm256_madd_epi16(a3hi, _mm256_loadu_si256((const __m256i *)(const void *)(W4[b] + 16))));
    __m128i m = _mm256_castsi256_si128(mad);
    __m128i h = _mm256_extracti128_si256(mad, 1);
    m = _mm_add_epi32(m, h);
    m = _mm_add_epi32(m, _mm_shuffle_epi32(m, _MM_SHUFFLE(1, 0, 3, 2)));
    m = _mm_add_epi32(m, _mm_shuffle_epi32(m, _MM_SHUFFLE(2, 3, 0, 1)));
    o = B4[b] + _mm_cvtsi128_si32(m);
#elif defined(__ARM_NEON) || defined(__aarch64__)
    // v1.1 fast path: batched NEON block kernels (bit-identical, one
    // reduction per 4 outputs, vmlal ILP — see the NEON section above).
    // v1.3.2: pair-major zero-skipping form — all 8 blocks consume each
    // pair once (vmlal_lane) and zero pairs are skipped entirely.
    static_assert(L2 / 4 == 8 && L3 / 4 == 8, "pair-major kernel expects 8 blocks");
    clipBias16(A, acc.v[us], FT_B, L1);
    clipBias16(A + L1, acc.v[them], FT_B, L1);
    dotBlocks4PairMajor<2 * L1 / 2>(A, g_neonPacked.l2[b], L2_B[b], S2, act2);
    dotBlocks4PairMajor<L2 / 2>(act2, g_neonPacked.l3[b], L3_B[b], S3, act3);
    o = B4[b] + dotI16(act3, W4[b], L3);
#elif defined(__AVX2__)
    // v1.4.2: madd-based zero-skip GEMV (see the AVX2 section above) —
    // covers AVX2-only hosts AND AVX-512-without-VNNI hosts, which used
    // to fall through to the 32-sweep scalar path. L2 walks the nonzero
    // pair list (built by the fused clipBiasNzAvx2 pass); L3 runs dense
    // through the same madd blocks.
    static_assert(L2 / 8 == 4 && L3 / 8 == 4, "AVX2 GEMV block count");
    int nzIdx[L1];
    int nzCnt2 = clipBiasNzAvx2(A, acc.v[us], FT_B, L1, 0, nzIdx);
    nzCnt2 += clipBiasNzAvx2(A + L1, acc.v[them], FT_B, L1, L1 / 2, nzIdx + nzCnt2);
    dotBlocks8Avx2<4>(A, nzIdx, nzCnt2, &g_avx2Packed.l2[b][0][0][0],
                      L2_B[b], S2, act2);
    dotBlocks8Avx2<4>(act2, nullptr, L2 / 2, &g_avx2Packed.l3[b][0][0][0],
                      L3_B[b], S3, act3);
    o = B4[b] + dotI16(act3, W4[b], L3);
#else
    // Portable path (v5.x kernels): one dot + one horizontal reduction per
    // output. Biases carry the full pre-shift scale; the arithmetic shift
    // is exact floor division (GCC semantics), matching the exporter.
    clipBias16(A, acc.v[us], FT_B, L1);
    clipBias16(A + L1, acc.v[them], FT_B, L1);
    for (int j = 0; j < L2; j++)
        act2[j] = (int16_t)clipI_(L2_B[b][j] + dotI16(A, L2_W[b][j], 2 * L1), S2);
    for (int k = 0; k < L3; k++)
        act3[k] = (int16_t)clipI_(L3_B[b][k] + dotI16(act2, L3_W[b][k], L2), S3);
    o = B4[b] + dotI16(act3, W4[b], L3);
#endif

    // o is in QA units of the normalized output scaled by 2^S4;
    // 1.0 normalized maps to 400 cp.
    // v8: clamp the RAW output to +-RES_CLAMP BEFORE the blend (the header
    // contract; v5-v7 clamped after, an inconsistency that was invisible
    // only because trained outputs rarely exceeded 600). The blended result
    // is then bounded by RES_CLAMP * blendPct / 100 exactly.
    int r = (int)(((int64_t)o * 25) >> (8 + S4));
    if (r < -RES_CLAMP) r = -RES_CLAMP;
    else if (r > RES_CLAMP) r = RES_CLAMP;
    int bp = blendForBucket(b);  // v8 phase blend (blendPct when off)
    if (bp != 100) {
        int t = r * bp;
        r = (t >= 0 ? (t + 50) : (t - 50)) / 100;  // round half away from zero
    }
    return r;
}

// ------------------------------------------------------------------
// Verification helpers (host harness only, but cheap enough to ship)
// ------------------------------------------------------------------

/** True when the incrementally-maintained accumulator exactly equals a
 *  from-scratch recomputation of the current placement. */
bool verifyIncremental(Position &pos) {
    // v6: with the subsystem disabled the ring carries no maintained
    // accumulator; report "not verified" instead of dereferencing null.
    if (!enabled) return false;
    const Accumulator *accp = pos.nnueEnsureFresh();  // fresh-vs-fresh
    Accumulator scratch;
    refresh(scratch, pos);
    return std::memcmp(scratch.v, accp->v, sizeof(scratch.v)) == 0;
}

/** Double-precision recomputation of the integer inference, used by the
 *  harness to prove the int16/int32 arithmetic never saturates. */
double referenceResidual(Position &pos) {
    if (!enabled) return 0.0;
    const Accumulator *accp = pos.nnueEnsureFresh();
    const Accumulator &acc = *accp;
    const int us = pos.sideToMove;
    const int them = us ^ 1;
    int b = bucketOf(popcount(pos.occupied));

    double A[2 * L1];
    for (int i = 0; i < L1; i++) {
        double a = (double)acc.v[us][i] + FT_B[i];
        double bb = (double)acc.v[them][i] + FT_B[i];
        A[i] = a < 0 ? 0 : (a > QA ? QA : a);
        A[L1 + i] = bb < 0 ? 0 : (bb > QA ? QA : bb);
    }
    double act2[L2];
    for (int j = 0; j < L2; j++) {
        double s = L2_B[b][j];
        for (int i = 0; i < 2 * L1; i++) s += A[i] * L2_W[b][j][i];
        double z = std::floor(s / (double)(1 << S2));
        act2[j] = z < 0 ? 0 : (z > QA ? QA : z);
    }
    double act3[L3];
    for (int k = 0; k < L3; k++) {
        double s = L3_B[b][k];
        for (int j = 0; j < L2; j++) s += act2[j] * L3_W[b][k][j];
        double z = std::floor(s / (double)(1 << S3));
        act3[k] = z < 0 ? 0 : (z > QA ? QA : z);
    }
    double o = B4[b];
    for (int k = 0; k < L3; k++) o += act3[k] * W4[b][k];
    double r = (double)((int32_t)((int64_t)llround(o) * 25 >> (8 + S4)));
    if (r < -RES_CLAMP) r = -RES_CLAMP;
    else if (r > RES_CLAMP) r = RES_CLAMP;
    int bp = blendForBucket(b);  // v8 phase blend (blendPct when off)
    if (bp != 100) r = (double)llround(r * bp / 100.0);
    return r;
}

const char *netId() { return NET_ID; }

}  // namespace nnue
}  // namespace tacturn
