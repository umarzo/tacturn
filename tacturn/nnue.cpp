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
 *  call continues the numbering). n must be a multiple of 32.
 *  v1.4.26 (B1): the 2 KB A[1024] scratch array is GONE. The nonzero pair
 *  VALUES are compressed straight into the parallel nzPairs array (one
 *  mask-compress per chunk — the values were already in the register),
 *  so the sparse L2 kernel reads its activation pairs as a SEQUENTIAL
 *  int32 stream instead of a data-dependent gather A + 2*nzIdx[k] (the
 *  5-cycle load-to-use stall that used to sit on every active pair).
 *  The A-array store traffic (~2 KB per inference, ~790k forward passes
 *  per bench 14) disappears with it. Bit-exact: identical values,
 *  identical ascending order, identical accumulation order. */
inline int clipBiasNz(const int16_t *src, const int16_t *bias,
                      int n, int pairBase, int *nzIdx, int32_t *nzPairs) {
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
        _mm512_mask_compressstoreu_epi32((void *)(nzPairs + cnt), nz, t);
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

/** v1.4.26 (B1): branchless 8-lane compress LUT for the AVX2 path. Row m
 *  holds the positions of the SET bits of m in ascending order (tail
 *  zero-filled) — _mm256_cvtepu8_epi32 of a row feeds
 *  _mm256_permutevar8x32_epi32 (vpermd) directly, compressing the 8 pair
 *  lanes of a chunk by its nonzero mask with zero branches and zero
 *  loop-exit mispredicts (the old ctz walk branched once per active pair,
 *  ~35 mispredicts per forward pass).
 *  v1.4.27 (N3): idx16 is the uint16 mirror of the same table — ONE
 *  16-byte aligned load replaces the unpacklo+cvtepu8 ladder on the index
 *  side, so both compress outputs (values + indices) come straight from
 *  LUT rows with no per-chunk integer plumbing. */
struct Avx2PermTable {
    alignas(32) uint8_t idx[256][8];
    alignas(32) uint16_t idx16[256][8];
    constexpr Avx2PermTable() : idx{}, idx16{} {
        for (int m = 0; m < 256; ++m) {
            int c = 0;
            for (int b = 0; b < 8; ++b) {
                if ((m >> b) & 1) {
                    idx[m][c] = (uint8_t)b;
                    idx16[m][c] = (uint16_t)b;
                    c++;
                }
            }
        }
    }
};
static constexpr Avx2PermTable kAvx2PermLut{};

/** v1.4.7: AVX2 fused clip + nonzero-pair-index build (the mirror of the
 *  AVX-512 clipBiasNz).
 *  v1.4.26 (B1): the branchy while (nzmask) ctz walk is GONE, and so is
 *  the 2 KB A[1024] scratch array. Per nonzero chunk: ONE vpermd (via the
 *  compress LUT) writes the compressed nonzero pair VALUES into nzPairs,
 *  and ONE unpacklo+add+store writes the compressed uint16 pair indices
 *  into nzIdx — both branchless, both in the same ascending order the ctz
 *  walk produced.
 *  v1.4.27 (N3): STLF-free form. The old per-nonzero-chunk
 *  `_mm256_storeu_si256(nzPairs + cnt, compPairs)` advanced the 32-byte
 *  store target by cnt += popcount(nzmask) — typically 4-12 BYTES — so
 *  every store overlapped its predecessor's tail; dotBlocks8Avx2's 4-byte
 *  scalar reads (nzPairs[k]) then hit guaranteed store-to-load-forwarding
 *  stalls (~12-15 cycles each, dozens per forward pass). The
 *  `if (_mm256_testz_si256(t, t)) continue;` branch mispredicted on the
 *  ~35% of nonzero-at-unpredictable-offsets chunks on top. Now EVERY
 *  chunk stores its clipped pairs to dstPairs + (i >> 1) — an ALIGNED,
 *  NON-OVERLAPPING 32-byte store (16 int16 inputs = 8 pairs = 32 bytes;
 *  dstPairs is pair-INDEXED and dense, zero pairs included) — and the
 *  index side comes from ONE aligned 16-byte idx16 LUT load. The kernel
 *  reads aPairs[q] for listed q only, so the stored zero pairs are never
 *  read: identical values, identical index list, identical order.
 *  n must be a multiple of 16; dstPairs must be 32-byte aligned with room
 *  for n/2 int32 lanes. */
inline int clipBiasNzAvx2(const int16_t *src, const int16_t *bias,
                          int n, int pairBase, uint16_t *nzIdx, int32_t *dstPairs) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i q = _mm256_set1_epi16((int16_t)QA);
    __m128i vbase = _mm_set1_epi16((int16_t)pairBase);
    const __m128i vstep = _mm_set1_epi16(8);
    int cnt = 0;
    for (int i = 0; i < n; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(const void *)(src + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(const void *)(bias + i));
        __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_add_epi16(va, vb), zero), q);
        _mm256_store_si256((__m256i *)(void *)(dstPairs + (i >> 1)), t);
        uint32_t nzmask = (uint32_t)_mm256_movemask_ps(
            _mm256_castsi256_ps(_mm256_cmpgt_epi32(t, zero)));
        __m128i loc16 =
            _mm_load_si128((const __m128i *)(const void *)kAvx2PermLut.idx16[nzmask]);
        __m128i compIdx = _mm_add_epi16(loc16, vbase);
        _mm_storeu_si128((__m128i *)(void *)(nzIdx + cnt), compIdx);
        cnt += __builtin_popcount(nzmask);
        vbase = _mm_add_epi16(vbase, vstep);
    }
    return cnt;
}

/** GEMV for NBLK blocks of 8 outputs over the packed [pair][NBLK][16]
 *  rows. v1.4.26 (B1): the activation pair broadcast reads the pair stream
 *  and the weight row keys off the uint16 nzIdx — the old data-dependent
 *  A + 2*q0 4-byte gather (with its load-to-use stall on q0 at every
 *  active pair) is gone.
 *  v1.4.27 (N3): aPairs is the DENSE, pair-indexed array written by
 *  clipBiasNzAvx2's aligned non-overlapping stores — every 4-byte
 *  aPairs[q] read is fully contained in one complete 32-byte store, so
 *  the store buffer forwards it without an STLF stall (the old
 *  overlapping-store layout stalled on nearly every read). The
 *  accumulators are PRE-BIASED (acc0[b] starts at bias[b*8..b*8+7]) —
 *  integer addition is associative, so (bias + sum) == (sum + bias) and
 *  the tail drops its per-block bias load/add. Same values, same order —
 *  bit-identical. */
template <int NBLK>
inline void dotBlocks8Avx2(const int32_t *aPairs, const uint16_t *nzIdx, int cnt,
                           const int16_t *packed, const int32_t *bias,
                           int shift, int16_t *y) {
    // v1.4.25 (B3): 2x-unrolled dual accumulator banks — the single-bank
    // loop carries a serial vpmaddwd (5-cycle) + vpaddd dependency chain
    // through every block; two independent banks (8 YMM registers at
    // NBLK=4 of 16 available) double the instruction-level parallelism.
    // Integer addition is associative, so the regrouped partial sums are
    // bit-identical.
    __m256i acc0[NBLK], acc1[NBLK];
    for (int b = 0; b < NBLK; b++) {
        acc0[b] = _mm256_loadu_si256((const __m256i *)(const void *)(bias + b * 8));
        acc1[b] = _mm256_setzero_si256();
    }
    int k = 0;
    for (; k + 2 <= cnt; k += 2) {
        const int q0 = nzIdx[k];
        const int q1 = nzIdx[k + 1];
        const __m256i av0 = _mm256_set1_epi32(aPairs[q0]);      // (a0,a1) every lane
        const __m256i av1 = _mm256_set1_epi32(aPairs[q1]);      // (a0,a1) every lane
        const int16_t *row0 = packed + (size_t)q0 * (NBLK * 16);
        const int16_t *row1 = packed + (size_t)q1 * (NBLK * 16);
        for (int b = 0; b < NBLK; b++) {
            acc0[b] = _mm256_add_epi32(acc0[b], _mm256_madd_epi16(
                av0, _mm256_load_si256((const __m256i *)(const void *)(row0 + b * 16))));
            acc1[b] = _mm256_add_epi32(acc1[b], _mm256_madd_epi16(
                av1, _mm256_load_si256((const __m256i *)(const void *)(row1 + b * 16))));
        }
    }
    for (; k < cnt; k++) {
        const int q = nzIdx[k];
        const __m256i av = _mm256_set1_epi32(aPairs[q]);  // (a0,a1) every lane
        const int16_t *row = packed + (size_t)q * (NBLK * 16);
        for (int b = 0; b < NBLK; b++)
            acc0[b] = _mm256_add_epi32(acc0[b], _mm256_madd_epi16(
                av, _mm256_load_si256((const __m256i *)(const void *)(row + b * 16))));
    }
    for (int b = 0; b < NBLK; b++) acc0[b] = _mm256_add_epi32(acc0[b], acc1[b]);
    const __m256i z32 = _mm256_setzero_si256();
    const __m256i qa32 = _mm256_set1_epi32(QA);
    if ((NBLK & 1) == 0) {
        // v1.4.24 (L2): pack BLOCK PAIRS — two clipped 8-lane blocks become
        // ONE 256-bit int16 store instead of two 128-bit stores. Halves the
        // store count of the tail and, critically, hands the next layer a
        // single 32-byte store per 16 activations (its 4-byte pair loads
        // forward from one store instead of straddling two). permute4x64
        // [3,1,2,0] over packs_epi32(v0,v1) orders the four 64-bit halves
        // as [v0.lo|v0.hi|v1.lo|v1.hi] == y[b*8 .. b*8+15] in order — the
        // exact bytes the two old 128-bit stores wrote, so bit-identical.
        // (v1.4.27 N3: bias already in acc0 — no tail loads.)
        for (int b = 0; b < NBLK; b += 2) {
            __m256i v0 = _mm256_srai_epi32(acc0[b], shift);
            __m256i v1 = _mm256_srai_epi32(acc0[b + 1], shift);
            v0 = _mm256_min_epi32(_mm256_max_epi32(v0, z32), qa32);
            v1 = _mm256_min_epi32(_mm256_max_epi32(v1, z32), qa32);
            __m256i p = _mm256_permute4x64_epi64(_mm256_packs_epi32(v0, v1),
                                                 _MM_SHUFFLE(3, 1, 2, 0));
            _mm256_storeu_si256((__m256i *)(void *)(y + b * 8), p);
        }
        return;
    }
    for (int b = 0; b < NBLK; b++) {
        // v1.4.7: vectorized shift+clip+narrow tail (odd-NBLK fallback).
        // srai + min/max + packs(+permute) is bit-identical: the values are
        // already clamped to [0, QA] before the saturating narrow.
        // (v1.4.27 N3: bias already in acc0 — no tail load.)
        __m256i v = _mm256_srai_epi32(acc0[b], shift);
        v = _mm256_max_epi32(v, z32);
        v = _mm256_min_epi32(v, qa32);
        __m256i p = _mm256_permute4x64_epi64(_mm256_packs_epi32(v, v),
                                             _MM_SHUFFLE(3, 1, 2, 0));
        _mm_storeu_si128((__m128i *)(void *)(y + b * 8), _mm256_castsi256_si128(p));
    }
}

/** v1.4.24 (L2): fused L3 -> output dot for the AVX2 path. Computes the 32
 *  L3 activations across the four 8-output madd blocks exactly like
 *  dotBlocks8Avx2<4>, but keeps them in TWO packed 256-bit registers and
 *  reduces the W4 output dot immediately — act3 never touches memory. The
 *  old flow spilled act3 with four separate 128-bit stores and reloaded it
 *  with two 256-bit dotI16 loads that each SPANNED two of those stores —
 *  a guaranteed store-to-load-forwarding stall on every NNUE inference
 *  (the same defect v1.4.7 fixed on AVX-512 with dotBlock16Ret). The W4
 *  reduction mirrors the AVX-512 fused path: every product fits int32
 *  (QA * CAP_W4 = 2^25, 16-lane partial sums < 2^30) and integer addition
 *  is associative, so the result is bit-identical to the dotI16 reference.
 *  Dense pair walk (the L3 call is always dense — same call as AVX-512). */
inline int32_t dotL3W4Avx2(const int16_t *A, const int16_t *packed,
                           const int32_t *bias, int shift,
                           const int16_t *w4, int32_t b4) {
    // v1.4.25 (B3): 2x-unrolled dual accumulator banks (see
    // dotBlocks8Avx2) — same associative-regrouping bit-exactness.
    // v1.4.27 (N3): accumulators PRE-BIASED (bias + sum == sum + bias);
    // the tail drops its four bias loads/adds.
    __m256i acc0[4], acc1[4];
    for (int b = 0; b < 4; b++) {
        acc0[b] = _mm256_loadu_si256((const __m256i *)(const void *)(bias + b * 8));
        acc1[b] = _mm256_setzero_si256();
    }
    int q = 0;
    for (; q + 2 <= L2 / 2; q += 2) {
        int32_t pair0, pair1;
        std::memcpy(&pair0, A + 2 * q, sizeof(pair0));      // aliasing-safe
        std::memcpy(&pair1, A + 2 * (q + 1), sizeof(pair1)); // aliasing-safe
        const __m256i av0 = _mm256_set1_epi32(pair0);
        const __m256i av1 = _mm256_set1_epi32(pair1);
        const int16_t *row0 = packed + (size_t)q * 64;       // NBLK*16 = 64
        const int16_t *row1 = packed + (size_t)(q + 1) * 64;
        for (int b = 0; b < 4; b++) {
            acc0[b] = _mm256_add_epi32(acc0[b], _mm256_madd_epi16(
                av0, _mm256_load_si256((const __m256i *)(const void *)(row0 + b * 16))));
            acc1[b] = _mm256_add_epi32(acc1[b], _mm256_madd_epi16(
                av1, _mm256_load_si256((const __m256i *)(const void *)(row1 + b * 16))));
        }
    }
    for (; q < L2 / 2; q++) {
        int32_t pair;
        std::memcpy(&pair, A + 2 * q, sizeof(pair));  // aliasing-safe
        const __m256i av = _mm256_set1_epi32(pair);   // (a0,a1) every lane
        const int16_t *row = packed + (size_t)q * 64;  // NBLK*16 = 64
        for (int b = 0; b < 4; b++)
            acc0[b] = _mm256_add_epi32(acc0[b], _mm256_madd_epi16(
                av, _mm256_load_si256((const __m256i *)(const void *)(row + b * 16))));
    }
    for (int b = 0; b < 4; b++) acc0[b] = _mm256_add_epi32(acc0[b], acc1[b]);
    const __m256i z32 = _mm256_setzero_si256();
    const __m256i qa32 = _mm256_set1_epi32(QA);
    __m256i v[4];
    for (int b = 0; b < 4; b++) {
        v[b] = _mm256_srai_epi32(acc0[b], shift);
        v[b] = _mm256_min_epi32(_mm256_max_epi32(v[b], z32), qa32);
    }
    // act3[0..15] and act3[16..31], packed in order (see the paired-store
    // note above for the permute ordering proof).
    __m256i a01 = _mm256_permute4x64_epi64(_mm256_packs_epi32(v[0], v[1]),
                                           _MM_SHUFFLE(3, 1, 2, 0));
    __m256i a23 = _mm256_permute4x64_epi64(_mm256_packs_epi32(v[2], v[3]),
                                           _MM_SHUFFLE(3, 1, 2, 0));
    __m256i mad = _mm256_add_epi32(
            _mm256_madd_epi16(a01, _mm256_loadu_si256((const __m256i *)(const void *)(w4 + 0))),
            _mm256_madd_epi16(a23, _mm256_loadu_si256((const __m256i *)(const void *)(w4 + 16))));
    __m128i m = _mm256_castsi256_si128(mad);
    __m128i h = _mm256_extracti128_si256(mad, 1);
    m = _mm_add_epi32(m, h);
    m = _mm_add_epi32(m, _mm_shuffle_epi32(m, _MM_SHUFFLE(1, 0, 3, 2)));
    m = _mm_add_epi32(m, _mm_shuffle_epi32(m, _MM_SHUFFLE(2, 3, 0, 1)));
    return b4 + _mm_cvtsi128_si32(m);
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

// v1.4.24 (L2-NEON) kernel block. Everything between the two NEON-KERNELS
// markers is extracted VERBATIM by scripts/host-tools/run_neon_simde_test.sh
// and compiled+executed on x86 through SIMDe (simde/arm/neon.h), then
// compared bit-for-bit against the portable per-output reference — the
// intrinsics themselves, not just a scalar emulation, are validated.
// ---- NEON-KERNELS BEGIN ----

// v1.4.27 (N4): the uint16 mirror of the AVX2 perm LUT — for each 8-pair
// nonzero mask, the ascending list of set-bit positions (zero-padded).
// One aligned 16-byte load replaces the branchy per-bit ctz walk (~35
// branch mispredictions per forward pass: the walk's trip count is the
// data-dependent popcount).
struct NeonIdxLutTable {
    uint16_t idx16[256][8];
    constexpr NeonIdxLutTable() : idx16{} {
        for (int m = 0; m < 256; ++m) {
            int c = 0;
            for (int b = 0; b < 8; ++b)
                if ((m >> b) & 1) idx16[m][c++] = (uint16_t)b;
        }
    }
};
static constexpr NeonIdxLutTable kNeonIdxLut16{};

/** v1.4.24 (L2-NEON): fused clip + nonzero-pair-index build — the NEON
 *  mirror of clipBiasNzAvx2. Two int16 lanes form one 32-bit pair word; a
 *  pair is nonzero iff its word is nonzero (vtstq_s32 gives an all-ones
 *  lane per nonzero word). All-zero 8-pair chunks skip their store (the
 *  sparse kernel only reads listed pairs); nonzero chunks store and append
 *  their indices via the same ascending ctz walk. Same values, same index
 *  order as the AVX2 builder.
 *  v1.4.27 (N4): the index append is BRANCHLESS — one LUT load, one
 *  vaddq_u16 of the chunk's pair base, one vst1q_u16 (the 8-lane store
 *  is bounded: chunk k's store covers [cnt, cnt+8) and cnt <= 8*(k+1),
 *  so it always fits the L1-entry buffer). nzIdx narrows to uint16_t
 *  (indices <= L1-1 = 511), matching the AVX2 N3 layout and halving the
 *  walk's memory traffic. */
inline int clipBiasNzNeon(int16_t *dst, const int16_t *src, const int16_t *bias,
                          int n, int pairBase, uint16_t *nzIdx) {
    // v1.4.25 (B3): the 8-lane serial vgetq_lane_u32 SIMD->GPR extraction
    // (8 cross-register-file UMOV transfers per chunk) is replaced by a
    // single narrowing + weighted horizontal reduction: vmovn_u32 takes
    // each nz word's low half (0xFFFF / 0), the AND with {1,2,4,...,128}
    // leaves exactly the bit for each nonzero pair, and the pairwise adds
    // sum the DISTINCT powers of two — the sum of distinct bits IS the
    // mask (no carries possible). One cross-file transfer instead of 8.
    // Verified bit-identical across all 256 mask states.
    static const uint16_t kBitWeights[8] = {1, 2, 4, 8, 16, 32, 64, 128};
    const uint16x8_t bitWeights = vld1q_u16(kBitWeights);
    const int16x8_t zero = vdupq_n_s16(0);
    const int16x8_t q = vdupq_n_s16((int16_t)QA);
    int cnt = 0;
    for (int i = 0; i < n; i += 16) {
        int16x8_t t0 = vminq_s16(vmaxq_s16(vaddq_s16(vld1q_s16(src + i),
                                                     vld1q_s16(bias + i)), zero), q);
        int16x8_t t1 = vminq_s16(vmaxq_s16(vaddq_s16(vld1q_s16(src + i + 8),
                                                     vld1q_s16(bias + i + 8)), zero), q);
        uint32x4_t nz0 = vtstq_s32(vreinterpretq_s32_s16(t0),
                                   vreinterpretq_s32_s16(t0));
        uint32x4_t nz1 = vtstq_s32(vreinterpretq_s32_s16(t1),
                                   vreinterpretq_s32_s16(t1));
        uint16x8_t bits = vandq_u16(
            vcombine_u16(vmovn_u32(nz0), vmovn_u32(nz1)), bitWeights);
        uint16x4_t s4 = vadd_u16(vget_low_u16(bits), vget_high_u16(bits));
        uint16x4_t s2 = vpadd_u16(s4, s4);
        uint16x4_t s1 = vpadd_u16(s2, s2);
        uint32_t mask = vget_lane_u16(s1, 0);
        if (mask == 0) continue;  // all 8 pairs zero: never read back
        vst1q_s16(dst + i, t0);
        vst1q_s16(dst + i + 8, t1);
        // v1.4.27 (N4): branchless index append (LUT + base add + store).
        uint16x8_t loc = vld1q_u16(kNeonIdxLut16.idx16[mask]);
        uint16x8_t comp =
            vaddq_u16(loc, vdupq_n_u16((uint16_t)(pairBase + (i >> 1))));
        vst1q_u16(nzIdx + cnt, comp);
        cnt += __builtin_popcount(mask);
    }
    return cnt;
}

/** v1.4.24 (L2-NEON): vectorized bias+shift+clip+narrow tail shared by the
 *  pair-major kernels. The old tail spilled every int32x4 accumulator to
 *  int32_t d[4] and ran 32 branchy clipI_ calls plus 32 two-byte stores —
 *  stores the next layer re-loaded with 4-byte pair reads spanning two of
 *  them (an STLF stall on Cortex-A7x/X). vshlq_s32 with a negative
 *  broadcast shift is the arithmetic shift right of the signed word (ARM
 *  VSHL semantics), values are pre-clamped to [0, QA] so the saturating
 *  vqmovn narrow is exact, and two blocks land in ONE 16-byte store —
 *  bit-identical bytes to the scalar tail. */
inline void clipStoreTailNeon(const int32x4_t *accs, int shift, int16_t *y) {
    // v1.4.27 (N4): the bias add moved to the callers' accumulator
    // INITIALIZATION (pre-biased banks) — every dot kernel now starts
    // acc = bias instead of paying the add in the tail. Integer addition
    // is associative, so the regrouping is bit-exact.
    const int32x4_t z32 = vdupq_n_s32(0);
    const int32x4_t qa32 = vdupq_n_s32(QA);
    const int32x4_t neg = vdupq_n_s32(-shift);
    for (int blk = 0; blk < 8; blk += 2) {
        int32x4_t v0 = accs[blk];
        int32x4_t v1 = accs[blk + 1];
        v0 = vminq_s32(vmaxq_s32(vshlq_s32(v0, neg), z32), qa32);
        v1 = vminq_s32(vmaxq_s32(vshlq_s32(v1, neg), z32), qa32);
        vst1q_s16(y + blk * 4, vcombine_s16(vqmovn_s32(v0), vqmovn_s32(v1)));
    }
}

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
    // v1.4.27 (N4): pre-biased accumulators (bias rides the banks from
    // the start; the tail no longer adds it).
    int32x4_t acc0 = vld1q_s32(bias + 0);
    int32x4_t acc1 = vld1q_s32(bias + 4);
    int32x4_t acc2 = vld1q_s32(bias + 8);
    int32x4_t acc3 = vld1q_s32(bias + 12);
    int32x4_t acc4 = vld1q_s32(bias + 16);
    int32x4_t acc5 = vld1q_s32(bias + 20);
    int32x4_t acc6 = vld1q_s32(bias + 24);
    int32x4_t acc7 = vld1q_s32(bias + 28);
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
    // v1.4.24 (L2-NEON): vectorized tail (was: spill + 32 branchy clipI_
    // + 32 two-byte stores).
    const int32x4_t accs[8] = {acc0, acc1, acc2, acc3, acc4, acc5, acc6, acc7};
    clipStoreTailNeon(accs, shift, y);
}

/** v1.4.24 (L2-NEON): sparse pair-major walk over the nonzero-pair index
 *  list built by clipBiasNzNeon — the NEON mirror of the AVX2 path's
 *  dotBlocks8Avx2(nzIdx, cnt) mode. Same vmlal_lane block structure and
 *  the same vectorized tail; only the pair enumeration changed (index
 *  list instead of a branchy `if (p == 0) continue` over all NPAIRS).
 *  Zero pairs contribute exactly 0, so the result is bit-identical.
 *  v1.4.27 (N4): 2x-unrolled DUAL accumulator banks (acc0..7 + acc0b..7b)
 *  with PRE-LOADED biases — pair k feeds bank A, pair k+1 feeds bank B,
 *  folded before the tail. The v1.4.25-B3 x86 dual-bank ILP trick, ported
 *  to where it belongs: ARM64's 32 NEON registers hide the 4-cycle
 *  vmlal_lane pipeline latency on Cortex-A7x big cores. int32 MACs are
 *  associative and zero terms are the identity, so the regrouping is
 *  bit-exact. */
template <int NPAIRS>
inline void dotBlocks4PairMajorNz(const int16_t *A, const uint16_t *nzIdx, int cnt,
                                  const int16_t (&packed)[NPAIRS][8][8],
                                  const int32_t *bias, int shift, int16_t *y) {
    int32x4_t acc0 = vld1q_s32(bias + 0);
    int32x4_t acc1 = vld1q_s32(bias + 4);
    int32x4_t acc2 = vld1q_s32(bias + 8);
    int32x4_t acc3 = vld1q_s32(bias + 12);
    int32x4_t acc4 = vld1q_s32(bias + 16);
    int32x4_t acc5 = vld1q_s32(bias + 20);
    int32x4_t acc6 = vld1q_s32(bias + 24);
    int32x4_t acc7 = vld1q_s32(bias + 28);
    int32x4_t acc0b = vdupq_n_s32(0);
    int32x4_t acc1b = vdupq_n_s32(0);
    int32x4_t acc2b = vdupq_n_s32(0);
    int32x4_t acc3b = vdupq_n_s32(0);
    int32x4_t acc4b = vdupq_n_s32(0);
    int32x4_t acc5b = vdupq_n_s32(0);
    int32x4_t acc6b = vdupq_n_s32(0);
    int32x4_t acc7b = vdupq_n_s32(0);
    int k = 0;
    for (; k + 2 <= cnt; k += 2) {
        const int q0 = nzIdx[k];
        const int q1 = nzIdx[k + 1];
        int32_t p0, p1;
        std::memcpy(&p0, A + 2 * q0, sizeof(p0));
        std::memcpy(&p1, A + 2 * q1, sizeof(p1));
        int16x4_t av0 = vreinterpret_s16_s32(vdup_n_s32(p0));
        int16x4_t av1 = vreinterpret_s16_s32(vdup_n_s32(p1));
        int16x8_t w;
        w = vld1q_s16(&packed[q0][0][0]);
        acc0 = vmlal_lane_s16(acc0, vget_low_s16(w), av0, 0);
        acc0 = vmlal_lane_s16(acc0, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][1][0]);
        acc1 = vmlal_lane_s16(acc1, vget_low_s16(w), av0, 0);
        acc1 = vmlal_lane_s16(acc1, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][2][0]);
        acc2 = vmlal_lane_s16(acc2, vget_low_s16(w), av0, 0);
        acc2 = vmlal_lane_s16(acc2, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][3][0]);
        acc3 = vmlal_lane_s16(acc3, vget_low_s16(w), av0, 0);
        acc3 = vmlal_lane_s16(acc3, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][4][0]);
        acc4 = vmlal_lane_s16(acc4, vget_low_s16(w), av0, 0);
        acc4 = vmlal_lane_s16(acc4, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][5][0]);
        acc5 = vmlal_lane_s16(acc5, vget_low_s16(w), av0, 0);
        acc5 = vmlal_lane_s16(acc5, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][6][0]);
        acc6 = vmlal_lane_s16(acc6, vget_low_s16(w), av0, 0);
        acc6 = vmlal_lane_s16(acc6, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q0][7][0]);
        acc7 = vmlal_lane_s16(acc7, vget_low_s16(w), av0, 0);
        acc7 = vmlal_lane_s16(acc7, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q1][0][0]);
        acc0b = vmlal_lane_s16(acc0b, vget_low_s16(w), av1, 0);
        acc0b = vmlal_lane_s16(acc0b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][1][0]);
        acc1b = vmlal_lane_s16(acc1b, vget_low_s16(w), av1, 0);
        acc1b = vmlal_lane_s16(acc1b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][2][0]);
        acc2b = vmlal_lane_s16(acc2b, vget_low_s16(w), av1, 0);
        acc2b = vmlal_lane_s16(acc2b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][3][0]);
        acc3b = vmlal_lane_s16(acc3b, vget_low_s16(w), av1, 0);
        acc3b = vmlal_lane_s16(acc3b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][4][0]);
        acc4b = vmlal_lane_s16(acc4b, vget_low_s16(w), av1, 0);
        acc4b = vmlal_lane_s16(acc4b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][5][0]);
        acc5b = vmlal_lane_s16(acc5b, vget_low_s16(w), av1, 0);
        acc5b = vmlal_lane_s16(acc5b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][6][0]);
        acc6b = vmlal_lane_s16(acc6b, vget_low_s16(w), av1, 0);
        acc6b = vmlal_lane_s16(acc6b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q1][7][0]);
        acc7b = vmlal_lane_s16(acc7b, vget_low_s16(w), av1, 0);
        acc7b = vmlal_lane_s16(acc7b, vget_high_s16(w), av1, 1);
    }
    for (; k < cnt; k++) {
        const int q = nzIdx[k];
        int32_t p;
        std::memcpy(&p, A + 2 * q, sizeof(p));  // strict-aliasing-safe pair load
        int16x4_t av = vreinterpret_s16_s32(vdup_n_s32(p));  // lane0=a0, lane1=a1
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
    acc0 = vaddq_s32(acc0, acc0b);
    acc1 = vaddq_s32(acc1, acc1b);
    acc2 = vaddq_s32(acc2, acc2b);
    acc3 = vaddq_s32(acc3, acc3b);
    acc4 = vaddq_s32(acc4, acc4b);
    acc5 = vaddq_s32(acc5, acc5b);
    acc6 = vaddq_s32(acc6, acc6b);
    acc7 = vaddq_s32(acc7, acc7b);
    const int32x4_t accs[8] = {acc0, acc1, acc2, acc3, acc4, acc5, acc6, acc7};
    clipStoreTailNeon(accs, shift, y);
}

/** v1.4.24 (L2-NEON): fused L3 -> output dot — the NEON mirror of
 *  dotL3W4Avx2. The 32 L3 activations accumulate in eight int32x4 block
 *  registers, clip + saturating-narrow into int16x4 registers, and the W4
 *  output dot reduces immediately — act3 never touches memory (the old
 *  path spilled it with 32 two-byte stores that dotI16 re-loaded). Every
 *  product fits int32 (QA * CAP_W4 = 2^25, 8 products per lane = 2^28)
 *  and integer addition is associative, so the result is bit-identical to
 *  B4 + dotI16(act3, W4, L3). Dense pair walk (L3 is always dense). */
inline int32_t dotL3W4Neon(const int16_t *A,
                           const int16_t (&packed)[L2 / 2][8][8],
                           const int32_t *bias, int shift,
                           const int16_t *w4, int32_t b4) {
    // v1.4.27 (N4): pre-biased DUAL banks, 2x unrolled — the same ILP
    // port as dotBlocks4PairMajorNz (bias rides bank A; bank B collects
    // the odd pairs; one fold before the clip). int32 MACs associative:
    // bit-exact regrouping.
    int32x4_t acc0 = vld1q_s32(bias + 0);
    int32x4_t acc1 = vld1q_s32(bias + 4);
    int32x4_t acc2 = vld1q_s32(bias + 8);
    int32x4_t acc3 = vld1q_s32(bias + 12);
    int32x4_t acc4 = vld1q_s32(bias + 16);
    int32x4_t acc5 = vld1q_s32(bias + 20);
    int32x4_t acc6 = vld1q_s32(bias + 24);
    int32x4_t acc7 = vld1q_s32(bias + 28);
    int32x4_t acc0b = vdupq_n_s32(0);
    int32x4_t acc1b = vdupq_n_s32(0);
    int32x4_t acc2b = vdupq_n_s32(0);
    int32x4_t acc3b = vdupq_n_s32(0);
    int32x4_t acc4b = vdupq_n_s32(0);
    int32x4_t acc5b = vdupq_n_s32(0);
    int32x4_t acc6b = vdupq_n_s32(0);
    int32x4_t acc7b = vdupq_n_s32(0);
    int q = 0;
    for (; q + 2 <= L2 / 2; q += 2) {
        int32_t p0, p1;
        std::memcpy(&p0, A + 2 * q, sizeof(p0));
        std::memcpy(&p1, A + 2 * q + 2, sizeof(p1));
        int16x4_t av0 = vreinterpret_s16_s32(vdup_n_s32(p0));
        int16x4_t av1 = vreinterpret_s16_s32(vdup_n_s32(p1));
        int16x8_t w;
        w = vld1q_s16(&packed[q][0][0]);
        acc0 = vmlal_lane_s16(acc0, vget_low_s16(w), av0, 0);
        acc0 = vmlal_lane_s16(acc0, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][1][0]);
        acc1 = vmlal_lane_s16(acc1, vget_low_s16(w), av0, 0);
        acc1 = vmlal_lane_s16(acc1, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][2][0]);
        acc2 = vmlal_lane_s16(acc2, vget_low_s16(w), av0, 0);
        acc2 = vmlal_lane_s16(acc2, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][3][0]);
        acc3 = vmlal_lane_s16(acc3, vget_low_s16(w), av0, 0);
        acc3 = vmlal_lane_s16(acc3, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][4][0]);
        acc4 = vmlal_lane_s16(acc4, vget_low_s16(w), av0, 0);
        acc4 = vmlal_lane_s16(acc4, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][5][0]);
        acc5 = vmlal_lane_s16(acc5, vget_low_s16(w), av0, 0);
        acc5 = vmlal_lane_s16(acc5, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][6][0]);
        acc6 = vmlal_lane_s16(acc6, vget_low_s16(w), av0, 0);
        acc6 = vmlal_lane_s16(acc6, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q][7][0]);
        acc7 = vmlal_lane_s16(acc7, vget_low_s16(w), av0, 0);
        acc7 = vmlal_lane_s16(acc7, vget_high_s16(w), av0, 1);
        w = vld1q_s16(&packed[q + 1][0][0]);
        acc0b = vmlal_lane_s16(acc0b, vget_low_s16(w), av1, 0);
        acc0b = vmlal_lane_s16(acc0b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][1][0]);
        acc1b = vmlal_lane_s16(acc1b, vget_low_s16(w), av1, 0);
        acc1b = vmlal_lane_s16(acc1b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][2][0]);
        acc2b = vmlal_lane_s16(acc2b, vget_low_s16(w), av1, 0);
        acc2b = vmlal_lane_s16(acc2b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][3][0]);
        acc3b = vmlal_lane_s16(acc3b, vget_low_s16(w), av1, 0);
        acc3b = vmlal_lane_s16(acc3b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][4][0]);
        acc4b = vmlal_lane_s16(acc4b, vget_low_s16(w), av1, 0);
        acc4b = vmlal_lane_s16(acc4b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][5][0]);
        acc5b = vmlal_lane_s16(acc5b, vget_low_s16(w), av1, 0);
        acc5b = vmlal_lane_s16(acc5b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][6][0]);
        acc6b = vmlal_lane_s16(acc6b, vget_low_s16(w), av1, 0);
        acc6b = vmlal_lane_s16(acc6b, vget_high_s16(w), av1, 1);
        w = vld1q_s16(&packed[q + 1][7][0]);
        acc7b = vmlal_lane_s16(acc7b, vget_low_s16(w), av1, 0);
        acc7b = vmlal_lane_s16(acc7b, vget_high_s16(w), av1, 1);
    }
    for (; q < L2 / 2; q++) {
        int32_t p;
        std::memcpy(&p, A + 2 * q, sizeof(p));
        int16x4_t av = vreinterpret_s16_s32(vdup_n_s32(p));
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
    acc0 = vaddq_s32(acc0, acc0b);
    acc1 = vaddq_s32(acc1, acc1b);
    acc2 = vaddq_s32(acc2, acc2b);
    acc3 = vaddq_s32(acc3, acc3b);
    acc4 = vaddq_s32(acc4, acc4b);
    acc5 = vaddq_s32(acc5, acc5b);
    acc6 = vaddq_s32(acc6, acc6b);
    acc7 = vaddq_s32(acc7, acc7b);
    // clip + narrow each block (values already in [0, QA]: exact; the
    // bias rode the accumulators from the start — v1.4.27 N4).
    const int32x4_t accs[8] = {acc0, acc1, acc2, acc3, acc4, acc5, acc6, acc7};
    const int32x4_t z32 = vdupq_n_s32(0);
    const int32x4_t qa32 = vdupq_n_s32(QA);
    const int32x4_t neg = vdupq_n_s32(-shift);
    int16x4_t n[8];
    for (int blk = 0; blk < 8; blk++) {
        int32x4_t v = accs[blk];
        v = vminq_s32(vmaxq_s32(vshlq_s32(v, neg), z32), qa32);
        n[blk] = vqmovn_s32(v);
    }
    // Fused W4 reduce: 32 products, 8 per lane, then one horizontal add.
    int32x4_t s = vdupq_n_s32(0);
    int16x8_t a01 = vcombine_s16(n[0], n[1]);
    int16x8_t w01 = vld1q_s16(w4 + 0);
    s = vmlal_s16(s, vget_low_s16(a01), vget_low_s16(w01));
    s = vmlal_s16(s, vget_high_s16(a01), vget_high_s16(w01));
    int16x8_t a23 = vcombine_s16(n[2], n[3]);
    int16x8_t w23 = vld1q_s16(w4 + 8);
    s = vmlal_s16(s, vget_low_s16(a23), vget_low_s16(w23));
    s = vmlal_s16(s, vget_high_s16(a23), vget_high_s16(w23));
    int16x8_t a45 = vcombine_s16(n[4], n[5]);
    int16x8_t w45 = vld1q_s16(w4 + 16);
    s = vmlal_s16(s, vget_low_s16(a45), vget_low_s16(w45));
    s = vmlal_s16(s, vget_high_s16(a45), vget_high_s16(w45));
    int16x8_t a67 = vcombine_s16(n[6], n[7]);
    int16x8_t w67 = vld1q_s16(w4 + 24);
    s = vmlal_s16(s, vget_low_s16(a67), vget_low_s16(w67));
    s = vmlal_s16(s, vget_high_s16(a67), vget_high_s16(w67));
    int32_t sbuf[4];
    vst1q_s32(sbuf, s);
    return b4 + sbuf[0] + sbuf[1] + sbuf[2] + sbuf[3];
}
// ---- NEON-KERNELS END ----

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
        std::memcpy(acc.v[perspective], fe.acc, sizeof(fe.acc));
        return;
    } else {
        // Delta path: the entry holds a valid accumulator for its snapshot
        // placement; walk every (color, type) plane and apply only the
        // squares that differ. Typically 1-3 pieces after a king walked
        // back into a previously-seen quadrant, instead of a 20+ piece
        // re-sum. rowIndexOf() hashes through the SAME (perspective,
        // bucket) pair this entry is keyed on (the enemy king never
        // enters the index), so the delta rows are exactly the rows a
        // from-scratch refresh would add/remove.
        // v1.4.25 (B3): the row-at-a-time updates (each re-reading and
        // re-writing the whole 1 KB of fe.acc) are collected into sub/add
        // row lists and applied by ONE in-place accFusedRows pass —
        // element-wise modular int16 arithmetic, "all subs then all adds"
        // is identical to any interleaving. Max possible delta rows is 32
        // (every piece of both colors); 40 slots is ample.
        const int16_t *sub[40], *add[40];
        int nSub = 0, nAdd = 0;
        for (int c = 0; c < 2; c++) {
            for (int pt = 0; pt < 6; pt++) {
                Bitboard cur = pos.colorBb[c] & pos.pieceBb[pt];
                Bitboard old = fe.pieces[c][pt];
                Bitboard added = cur & ~old;
                while (added) {
                    int sq = lsb(added);
                    added &= added - 1;
                    add[nAdd++] = FT_W[rowIndexOf(c, pt, sq, perspective, wk, bk)];
                }
                Bitboard removed = old & ~cur;
                while (removed) {
                    int sq = lsb(removed);
                    removed &= removed - 1;
                    sub[nSub++] = FT_W[rowIndexOf(c, pt, sq, perspective, wk, bk)];
                }
                fe.pieces[c][pt] = cur;
            }
        }
        if (nSub || nAdd) {
            // v1.4.27 (N6): single-pass Finny refresh — the fused pass
            // updates the Finny cache entry AND writes the destination
            // perspective in the same loop (ONE read of fe.acc instead of
            // an in-place pass plus a trailing 1 KB memcpy pass). The
            // second store writes the identical computed vector, so the
            // value is bit-exact.
            accFusedRows(fe.acc, fe.acc, sub, nSub, add, nAdd,
                         acc.v[perspective]);
        } else {
            std::memcpy(acc.v[perspective], fe.acc, sizeof(fe.acc));
        }
    }
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
    // v1.4.25 (B3): direct fast paths for the two op shapes that cover
    // ~97% of moves — a single SHIFT (every non-king quiet and every
    // promotion) and SUB+SHIFT (plain captures and EP). Both bypass the
    // generic staging loop. v1.4.28 (B3): they now call the REGISTER-
    // PASSED accFused1x1 / accFused2x1 kernels directly — no sub[]/add[]
    // stack arrays, no nSub/nAdd shape branches on the hottest replay
    // path. Same modular int16 arithmetic, bit-identical.
    if (si.nnueNops == 1 && si.nnueOp[0].kind == NNUE_OP_SHIFT) {
        const NnueOp &op = si.nnueOp[0];
        if (!(dirty & 1u))
            accFused1x1(dst.v[WHITE], src.v[WHITE], FT_W[op.wF], FT_W[op.wT]);
        if (!(dirty & 2u))
            accFused1x1(dst.v[BLACK], src.v[BLACK], FT_W[op.bF], FT_W[op.bT]);
        return;
    }
    if (si.nnueNops == 2 && si.nnueOp[0].kind == NNUE_OP_SUB &&
        si.nnueOp[1].kind == NNUE_OP_SHIFT) {
        const NnueOp &subOp = si.nnueOp[0];
        const NnueOp &shOp = si.nnueOp[1];
        if (!(dirty & 1u))
            accFused2x1(dst.v[WHITE], src.v[WHITE], FT_W[subOp.wF], FT_W[shOp.wF],
                        FT_W[shOp.wT]);
        if (!(dirty & 2u))
            accFused2x1(dst.v[BLACK], src.v[BLACK], FT_W[subOp.bF], FT_W[shOp.bF],
                        FT_W[shOp.bT]);
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

// v1.4.26 (B3): back-heal reverse op. See nnue.h for the contract. The
// forward pass for perspective p (when it is actually applied — applyMoveAcc
// skips it only when p is already dirty, in which case the rows are garbage
// that the heal overwrites anyway) is:
//   ADD        : v += FT_W[p==WHITE ? wF : bF]
//   SUB        : v -= FT_W[p==WHITE ? wF : bF]
//   SHIFT      : v += FT_W[to] - FT_W[from]        (p's from/to rows)
//   SHIFT_P p  : v += FT_W[to] - FT_W[from]        (only op.persp's rows)
//   SHIFT_P !p : nothing (plain copy; p went dirty)
// The exact inverse swaps every add and sub. Because the heal starts from
// an authoritative (refreshed, from-scratch-exact) value at the CHILD and
// modular int16 addition is a group, the reconstructed parent rows equal
// the true placement sums bit-for-bit — regardless of whether the forward
// pass had skipped or applied them.
void healMoveAcc(Accumulator &dst, const Accumulator &src, const StateInfo &si,
                 int perspective) {
    if (&dst == &src) return;
    if (si.wasNull) {  // placement unchanged: the inverse is a plain copy
        std::memcpy(dst.v[perspective], src.v[perspective],
                    sizeof(dst.v[perspective]));
        return;
    }
    // Max recorded ops per move is 3; each op contributes at most one sub
    // and one add row to ONE perspective — 3 slots per list is ample.
    const int16_t *sub[3], *add[3];
    int nSub = 0, nAdd = 0;
    for (int i = 0; i < si.nnueNops; i++) {
        const NnueOp &op = si.nnueOp[i];
        switch (op.kind) {
        case NNUE_OP_ADD:      // forward add -> reverse sub
            sub[nSub++] = FT_W[perspective == WHITE ? op.wF : op.bF];
            break;
        case NNUE_OP_SUB:      // forward sub -> reverse add
            add[nAdd++] = FT_W[perspective == WHITE ? op.wF : op.bF];
            break;
        case NNUE_OP_SHIFT:    // forward (sub from, add to) -> swap
            sub[nSub++] = FT_W[perspective == WHITE ? op.wT : op.bT];
            add[nAdd++] = FT_W[perspective == WHITE ? op.wF : op.bF];
            break;
        case NNUE_OP_SHIFT_P:
            if ((int)op.persp == perspective) {
                sub[nSub++] = FT_W[perspective == WHITE ? op.wT : op.bT];
                add[nAdd++] = FT_W[perspective == WHITE ? op.wF : op.bF];
            }
            // other perspective: plain copy in the forward pass — the
            // inverse contributes nothing.
            break;
        }
    }
    if (nSub == 0 && nAdd == 0) {
        if (dst.v[perspective] != src.v[perspective])
            std::memcpy(dst.v[perspective], src.v[perspective],
                        sizeof(dst.v[perspective]));
        return;
    }
    accFusedRows(dst.v[perspective], src.v[perspective], sub, nSub, add, nAdd);
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
 *  v1.4.26 (B1): the activation pair broadcast reads the SEQUENTIAL
 *  nzPairs stream (compressed by clipBiasNz) — no more data-dependent
 *  A + 2*nzIdx[k] gather. The weight row address still keys off
 *  nzIdx[k], but the two loads no longer chain: the broadcast value is
 *  ready independently. Same values, same order — bit-identical.
 */
inline void dotBlock32NZ(const int32_t *nzPairs, const int *nzIdx, int cnt,
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
        __m512i p0 = _mm512_set1_epi32(nzPairs[k + 0]);
        __m512i p1 = _mm512_set1_epi32(nzPairs[k + 1]);
        __m512i p2 = _mm512_set1_epi32(nzPairs[k + 2]);
        __m512i p3 = _mm512_set1_epi32(nzPairs[k + 3]);
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
        __m512i p = _mm512_set1_epi32(nzPairs[k]);
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

int residualRaw(Position &pos) {
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
    // v1.4.26 (B1): on the AVX-512-VNNI and AVX2 paths the dense input
    // vector is never materialized at all — the nonzero pair VALUES are
    // compressed straight into nzPairs. Only the NEON and portable paths
    // keep the 2 KB array.
#if !defined(__AVX2__)
    alignas(64) int16_t A[2 * L1];
#endif
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
    // v1.4.26 (B1): clipBiasNz compresses the nonzero pair VALUES into
    // nzPairs alongside the indices — the 2 KB A scratch array is gone
    // and the L2 kernel's activation broadcast is a sequential load.
    int nzIdx[L1];
    alignas(64) int32_t nzPairs[L1];
    int nzCnt = clipBiasNz(acc.v[us], FT_B, L1, 0, nzIdx, nzPairs);
    nzCnt += clipBiasNz(acc.v[them], FT_B, L1, L1 / 2, nzIdx + nzCnt,
                        nzPairs + nzCnt);
    (void)act3;  // fused into registers on this path
    dotBlock32NZ(nzPairs, nzIdx, nzCnt, g_packed.l2[b], L2_B[b], S2, act2);
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
    // pair once (vmlal_lane).
    // v1.4.24 (L2-NEON): the clip pass IS the nz-pair scan now
    // (clipBiasNzNeon — the 512-iteration branchy `if (p == 0) continue`
    // walk is gone), L2 runs over the compact nonzero-pair list, and L3 +
    // the output layer are FUSED (dotL3W4Neon — act3 never touches
    // memory). All bit-exact (integer MACs associative; zero pairs are
    // the identity).
    static_assert(L2 / 4 == 8 && L3 / 4 == 8, "pair-major kernel expects 8 blocks");
    // v1.4.27 (N4): uint16 index list (the AVX2 N3 layout) — halves the
    // walk traffic; the LUT store is bounded by 8*(chunk+1) <= L1.
    uint16_t nzIdx[L1];
    int nzCnt = clipBiasNzNeon(A, acc.v[us], FT_B, L1, 0, nzIdx);
    nzCnt += clipBiasNzNeon(A + L1, acc.v[them], FT_B, L1, L1 / 2, nzIdx + nzCnt);
    dotBlocks4PairMajorNz<2 * L1 / 2>(A, nzIdx, nzCnt, g_neonPacked.l2[b],
                                      L2_B[b], S2, act2);
    (void)act3;  // fused into registers (dotL3W4Neon)
    o = dotL3W4Neon(act2, g_neonPacked.l3[b], L3_B[b], S3, W4[b], B4[b]);
#elif defined(__AVX2__)
    // v1.4.2: madd-based zero-skip GEMV (see the AVX2 section above) —
    // covers AVX2-only hosts AND AVX-512-without-VNNI hosts, which used
    // to fall through to the 32-sweep scalar path. L2 walks the nonzero
    // pair list (built by the fused clipBiasNzAvx2 pass); L3 stays dense.
    // v1.4.24 (L2): L3 and the output layer are FUSED via dotL3W4Avx2 —
    // the act3 spill (four 128-bit stores immediately reloaded by two
    // 256-bit dotI16 loads, a guaranteed STLF stall per inference) is
    // gone; act2 is now written with two paired 256-bit stores.
    // v1.4.26 (B1): nzPairs compression — the 2 KB A array is gone and
    // the L2 kernel broadcasts pairs from the sequential stream.
    static_assert(L2 / 8 == 4 && L3 / 8 == 4, "AVX2 GEMV block count");
    uint16_t nzIdx[L1];
    alignas(32) int32_t nzPairs[L1];
    // v1.4.27 (N3): dstPairs is DENSE and pair-indexed now — the first
    // perspective's 256 pairs occupy nzPairs[0..256), the second's
    // nzPairs[256..512) (a fixed 32-byte-aligned region, not the old
    // data-dependent nzPairs + cnt stream offset). The nzIdx list stays
    // sequential/compressed exactly as before.
    int nzCnt2 = clipBiasNzAvx2(acc.v[us], FT_B, L1, 0, nzIdx, nzPairs);
    nzCnt2 += clipBiasNzAvx2(acc.v[them], FT_B, L1, L1 / 2, nzIdx + nzCnt2,
                             nzPairs + L1 / 2);
    dotBlocks8Avx2<4>(nzPairs, nzIdx, nzCnt2, &g_avx2Packed.l2[b][0][0][0],
                      L2_B[b], S2, act2);
    (void)act3;  // fused into registers (dotL3W4Avx2)
    o = dotL3W4Avx2(act2, &g_avx2Packed.l3[b][0][0][0], L3_B[b], S3,
                    W4[b], B4[b]);
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
    return r;
}

// v1.4.27 (N2): residual() is now the bucket-blended view of residualRaw().
// The blend arithmetic is bit-identical to the historical inline tail
// (round half away from zero), so residual() keeps its value contract —
// the fuzznnue battery and referenceResidual() both compare against it.
int residual(Position &pos) {
    if (!enabled) return 0;
    int r = residualRaw(pos);
    int bp = blendForBucket(bucketOf(popcount(pos.occupied)));  // v8 phase blend
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
