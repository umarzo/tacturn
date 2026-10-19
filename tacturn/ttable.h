// TacTurn Chess Engine v1 — lock-free transposition table.
//
// Port of TranspositionTable.kt with a race-hardened layout for Lazy SMP.
// v1.3.2: ONE-CACHE-LINE ENTRY. The v1.3.1 layout kept three parallel
// arrays (verification word, payload, static eval), so a single probe
// touched up to three distinct cache lines/TLB entries that were `size_`
// elements apart. Every entry now lives in one aligned 16-byte struct —
// four entries per 64-byte line — and a probe reads exactly one line:
//
//   entry[0].w0 entry[0].w1 | entry[1].w0 entry[1].w1 | ...
//
// Measured on the 1-core AVX-512 gate host: +20% nps at the default
// size, +74% at 100 MB (the win grows with the table: cache/TLB misses,
// not instruction count, were the bottleneck). Phones should feel it at
// least as much.
//
// v1.4.0: 4-WAY BUCKETS + AGE-AWARE REPLACEMENT. The 64-byte cache line
// that one entry used to occupy alone now holds a CLUSTER of four
// entries; the probe index selects a cluster and scans its four slots,
// so a probe still reads exactly ONE cache line (the one-cache-line
// property is preserved — only unused slots of the same line are
// touched). Collisions on the top bucket bits no longer evict each
// other: three same-cluster keys coexist.
//
// Replacement (age-aware, Stockfish-style): a store first looks for a
// slot holding the SAME key (in-place update, keeping move/eval when the
// new payload is shallower). Otherwise the victim is chosen by score:
//   empty slot          -> best victim (a fresh line is free real estate)
//   older generation    -> next best, shallowest first (stale entries are
//                          worth less than any fresh one, regardless of
//                          depth)
//   current generation,
//   depth <= incoming+2 -> last resort, shallowest first (the v1.3.2
//                          depth-preferred hybrid is preserved: a shallow
//                          store never evicts a much deeper fresh entry)
//   current generation,
//   depth >  incoming+2 -> protected (0x10000 + depth)
// The always-replace leak of the old direct-mapped scheme (a fresh shallow
// store evicting a deep entry of a different key) is gone at equal memory
// footprint — the classic direct-mapped vs bucket trade, with the bucket
// now spread over the same 16 bytes per slot.
//
// Measured (fixed 5k-node games, scaled hash, 240 games each): +59.9
// ± 19.9 with a saturated table, +8.7 ± 18.3 at moderate load, no nps
// cost (same cache lines per probe). Near-empty tables (short fixed
// gates at default hash) are neutral.
//
// v1.4.0: ttPv BIT. One of the two spare payload bits now records
// whether the position was stored from a PV window (beta - alpha > 1).
// Ex-PV positions are where the best line lived; LMR reduces their
// quiet moves one ply less (search.cpp).
//
//   w0 = key ^ w1        ("verification word")
//   w1 packs:  move(16) | score(16, +32768) | eval(16) | depth(6) |
//              flag(2) | gen(5) | evalValid(1) | pv(1) | spare(1)
//
// The move is stored as a 15-bit key (from | to<<6 | promo<<12) instead
// of the full 32-bit encoding: piece/captured/castle/EP/double-push bits
// are pure functions of the probing position, so ttDecodeMove()
// reconstructs the exact movegen encoding on the way out (a 16-byte
// entry needs the static eval inside w1 — the 32-bit move did not fit).
//
// The eval field carries the RAW static eval + static-blend residual
// (NO correction-history term — corrections are learned continuously
// during a search and would go stale inside a long-lived table; readers
// re-apply the live corrhist sum, see search.cpp v1.4.0).
//
// Writers fill w1 (payload + eval, one atomic 64-bit store) first and
// publish w0 last; readers load w1, then w0, and verify
// w0 ^ w1 == key. Any interleaving between concurrent writers therefore
// degrades to a clean miss — a torn entry can never be used. Writers
// only ever mutate the single victim slot in place, so concurrent stores
// to different keys of the same cluster cannot tear each other's entries.
//
// All original code — MIT licensed, TacTurn contributors.

#pragma once

#include "types.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

// [CCRL-KIT] Windows portability: posix_memalign does not exist on Win32;
// use _aligned_malloc/_aligned_free instead. Allocation semantics are
// identical (64-byte alignment, same best-effort fallback on failure).
#ifdef _WIN32
#include <malloc.h>
static inline void *tacturn_aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}
static inline void tacturn_aligned_free(void *p) { _aligned_free(p); }
#else
static inline void *tacturn_aligned_alloc(size_t alignment, size_t size) {
    void *m = nullptr;
    if (posix_memalign(&m, alignment, size) != 0) return nullptr;
    return m;
}
static inline void tacturn_aligned_free(void *p) { free(p); }
#endif

namespace tacturn {

class Position;

/** Rebuild the full 32-bit Move from a packed TT move key plus the
 *  probing position. from/to/promo identify a legal move uniquely; the
 *  piece, captured, castle, en-passant and double-push fields are exactly
 *  what movegen derives from the same placement, so the reconstruction
 *  is bit-identical to the stored encoding for every entry whose key
 *  matches the position. Stale/colliding entries (empty `from`) decode
 *  to MOVE_NONE — the same harmless "no ordering hint" the old code
 *  produced. Defined in game.cpp (needs the complete Position). */
Move ttDecodeMove(const Position &b, uint16_t moveKey);

class TranspositionTable {
public:
    static constexpr int FLAG_EXACT = 1;
    static constexpr int FLAG_LOWER = 2;  // fail-high: score is a lower bound
    static constexpr int FLAG_UPPER = 3;  // fail-low: score is an upper bound
    static constexpr int EVAL_NONE = -32768;

    explicit TranspositionTable(int bits) { resize(bits); }
    ~TranspositionTable() { tacturn_aligned_free(mem_); }

    /** `bits` keeps its historical meaning — the ENTRY-equivalent count:
     *  the table occupies 2^bits * 16 bytes of memory, exactly as the
     *  direct-mapped v1.3.2 did, so every UCI "Hash MB" mapping and gate
     *  script stays bit-compatible. Internally those entries live as
     *  2^(bits-2) four-slot clusters. */
    void resize(int bits) {
        if (bits < 12) bits = 12;
        if (bits > 30) bits = 30;
        // 32-bit processes (armeabi-v7a) have a cramped address space; a
        // large allocation can fail outright on low-RAM devices.
        if (sizeof(void *) < 8 && bits > 25) bits = 25;
        tacturn_aligned_free(mem_);
        mem_ = nullptr;
        clusters_ = nullptr;
        clusterCount_ = 0;
        bits_ = bits;
        // Try the requested size, then fall back to smaller tables rather
        // than ever leaving the table unusable (a null store would crash).
        while (bits_ >= 12) {
            clusterCount_ = 1ULL << (bits_ - 2);
            // 64-byte alignment keeps every cluster on exactly one cache
            // line (malloc's 8/16-byte guarantee does not).
            void *m = tacturn_aligned_alloc(64, clusterCount_ * sizeof(Cluster));
            if (!m) m = malloc(clusterCount_ * sizeof(Cluster));  // best-effort
            if (m) {
                mem_ = m;
                break;
            }
            bits_--;
        }
        clusters_ = reinterpret_cast<Cluster *>(mem_);
        clear();
    }

    void clear() {
        if (mem_) memset(mem_, 0, clusterCount_ * sizeof(Cluster));
        age_ = 0;
    }

    /** The generation field in the entry is 5 bits wide, so the age counter
     *  must wrap at 32. (v1.1.0 and earlier wrapped at 64, so for every
     *  search with age 32..63 the stored 5-bit gen could never equal age_
     *  and every store degenerated to always-replace — measured +38.4
     *  ± 5.9 Elo on the 1.1.0 stack when fixed, 100 games at 100 ms.) */
    inline void nextSearch() { age_ = (age_ + 1) & 31; }

    struct ProbeResult {
        bool hit = false;
        int score = 0;
        int depth = 0;
        int flag = 0;
        Move move = MOVE_NONE;
        int eval = EVAL_NONE;
        bool pv = false;  // v1.4.0: stored from a PV window
    };

    inline ProbeResult probe(const Position &b, Bitboard key) const {
        ProbeResult r;
        const Cluster &c = clusters_[clusterIndex(key)];
        for (int s = 0; s < 4; s++) {
            const Entry &e = c.slot[s];
            Word1 w1;
            w1.u = e.w1.load(std::memory_order_relaxed);
            Bitboard verif = e.w0.load(std::memory_order_relaxed);
            if ((verif ^ w1.u) != key) continue;  // miss (or torn write)
            r.hit = true;
            r.move = ttDecodeMove(b, (uint16_t)w1.f.move);
            r.score = w1.f.score - 32768;
            r.depth = w1.f.depth;
            r.flag = w1.f.flag;
            r.eval = w1.f.evalValid ? (int)(int16_t)w1.f.eval : EVAL_NONE;
            r.pv = w1.f.pv != 0;
            return r;
        }
        return r;
    }

    void store(Bitboard key, Move move, int score, int depth, int flag,
               int eval = EVAL_NONE, bool pv = false) {
        Cluster &c = clusters_[clusterIndex(key)];
        int sameSlot = -1;
        int victim = 0;
        // v1.4.0 age-aware victim score: LOWER = more replaceable.
        //   empty          : -2 << 20   (free slot, always best victim)
        //   older search   : -1 << 20 + depth (any stale entry beats out
        //                                      any fresh one; shallow
        //                                      stale first)
        //   this search    :
        //     depth <= incoming+2 : depth (the v1.3.2 depth-preferred
        //                              hybrid — a shallow store may evict a
        //                              comparable current-gen entry, never
        //                              a much deeper one)
        //     depth >  incoming+2 : 0x10000 + depth (protected: much
        //                              deeper than the incoming store, and
        //                              from the current search)
        int victimScore = 0x3FFFFFFF;
        for (int s = 0; s < 4; s++) {
            Entry &e = c.slot[s];
            Word1 old;
            old.u = e.w1.load(std::memory_order_relaxed);
            Bitboard oldVerif = e.w0.load(std::memory_order_relaxed);
            if ((oldVerif ^ old.u) == key) {
                sameSlot = s;  // in-place update; no eviction decision
                break;
            }
            int sc;
            if (old.u == 0 && oldVerif == 0)
                sc = -(2 << 20);
            else if (old.f.gen != age_)
                sc = -(1 << 20) + old.f.depth;
            else if (old.f.depth > depth + 2)
                sc = 0x10000 + old.f.depth;  // protected deep current-gen
            else
                sc = old.f.depth;
            if (sc < victimScore) {
                victimScore = sc;
                victim = s;
            }
        }

        if (sameSlot >= 0) {
            // Same key: depth-preferred hybrid. Never let a shallower
            // store of the current search evict a deeper one; freshness
            // (generation) and exact bounds still win.
            Entry &e = c.slot[sameSlot];
            Word1 old;
            old.u = e.w1.load(std::memory_order_relaxed);
            int oldDepth = old.f.depth;
            int oldGen = old.f.gen;
            bool replace =
                oldGen != age_ || depth >= oldDepth || flag == FLAG_EXACT;
            if (!replace) {
                // Keep the entry, but remember a better move if we have one.
                if (move != MOVE_NONE && old.f.move == 0) {
                    Word1 w1 = old;
                    w1.f.move = packMove(move);
                    e.w1.store(w1.u, std::memory_order_relaxed);
                    e.w0.store(key ^ w1.u, std::memory_order_relaxed);
                }
                if (!old.f.evalValid && eval != EVAL_NONE) {
                    Word1 w1 = old;
                    w1.f.eval = (uint16_t)(int16_t)eval;
                    w1.f.evalValid = 1;
                    e.w1.store(w1.u, std::memory_order_relaxed);
                    e.w0.store(key ^ w1.u, std::memory_order_relaxed);
                }
                return;
            }
            publish(e, key, move, score, depth, flag, eval, pv, old, true);
            return;
        }

        // The v1.3.2 depth-preferred hybrid, generalized to the bucket: if
        // the best available victim is still a CURRENT-search entry more
        // than 2 plies deeper than the incoming store, the store is
        // dropped (the direct-mapped table refused exactly this eviction).
        // With an empty or stale slot available the store always lands.
        // (v1.4.2 tested a never-drop-exact/PV exception + victim bonus
        // for exact/PV entries: tree-neutral on the fresh-TT bench but
        // -45 Elo over full games — the aged/saturated regime this rule
        // was SPRT-gated for is exactly where the exception hurts; the
        // v1.3.2 policy stands. See HISTORY.md §39.)
        if (victimScore >= 0x10000) return;

        publish(c.slot[victim], key, move, score, depth, flag, eval, pv,
                Word1(), false);
    }

    /** Fill in a missing static eval for an existing entry (same-key only). */
    void storeEval(Bitboard key, int eval) {
        Cluster &c = clusters_[clusterIndex(key)];
        for (int s = 0; s < 4; s++) {
            Entry &e = c.slot[s];
            Word1 w1;
            w1.u = e.w1.load(std::memory_order_relaxed);
            Bitboard verif = e.w0.load(std::memory_order_relaxed);
            if ((verif ^ w1.u) != key) continue;
            if (w1.f.evalValid) return;
            w1.f.eval = (uint16_t)(int16_t)eval;
            w1.f.evalValid = 1;
            e.w1.store(w1.u, std::memory_order_relaxed);
            e.w0.store(key ^ w1.u, std::memory_order_relaxed);
            return;
        }
    }

    int usagePerMille() const {
        int used = 0;
        size_t step = clusterCount_ / 256;  // sample ~256 clusters
        if (step == 0) step = 1;
        size_t total = 0;
        for (size_t i = 0; i < clusterCount_; i += step) {
            const Cluster &c = clusters_[i];
            for (int s = 0; s < 4; s++) {
                Word1 w1;
                w1.u = c.slot[s].w1.load(std::memory_order_relaxed);
                if (w1.f.flag != 0) used++;
                total++;
            }
        }
        return total ? (int)(used * 1000 / total) : 0;
    }

    int bits() const { return bits_; }

private:
    union Word1 {
        uint64_t u;
        struct {
            uint64_t move : 16;     // from | to<<6 | promo<<12 (0 = none)
            uint64_t score : 16;    // +32768 offset
            uint64_t eval : 16;     // raw int16 static eval (gated by evalValid)
            uint64_t depth : 6;     // capped 63
            uint64_t flag : 2;
            uint64_t gen : 5;
            uint64_t evalValid : 1;
            uint64_t pv : 1;        // v1.4.0: stored from a PV window
            uint64_t spare : 1;
        } f;
    };

    /** One 16-byte entry: both words always share a cache line. */
    struct alignas(16) Entry {
        std::atomic<uint64_t> w0;  // verification word
        std::atomic<uint64_t> w1;  // payload
    };
    static_assert(sizeof(Entry) == 16, "TT entry must be exactly one 16-byte slot");

    /** Four entries — exactly one 64-byte cache line. */
    struct alignas(64) Cluster {
        Entry slot[4];
    };
    static_assert(sizeof(Cluster) == 64, "TT cluster must be exactly one cache line");

    static inline uint16_t packMove(Move m) {
        // from | to (low 12 bits) + promo (3 bits at 12..14). The 32-bit
        // encoding's piece/captured/flag fields are re-derived on probe.
        return (uint16_t)((m & 0xFFFu) | (((m >> 18) & 7u) << 12));
    }

    /** Build and publish a payload word into `e`. `old` carries the
     *  previous same-key payload (sameKey=true) whose move/eval survive
     *  when the new store has none; for a fresh victim it is zero. */
    inline void publish(Entry &e, Bitboard key, Move move, int score, int depth,
                        int flag, int eval, bool pv, const Word1 &old,
                        bool sameKey) {
        Word1 w1;
        w1.u = 0;
        w1.f.move = packMove(move);
        int sc = score + 32768;
        if (sc < 0) sc = 0;
        if (sc > 65535) sc = 65535;
        w1.f.score = (uint64_t)sc;
        w1.f.depth = depth > 63 ? 63 : (depth < 0 ? 0 : depth);
        w1.f.flag = flag & 3;
        w1.f.gen = age_;
        w1.f.pv = pv ? 1 : 0;

        // Preserve a useful move when we have none.
        if (move == MOVE_NONE && sameKey) w1.f.move = old.f.move;

        // Keep / set the static eval.
        if (eval != EVAL_NONE) {
            w1.f.eval = (uint16_t)(int16_t)eval;
            w1.f.evalValid = 1;
        } else if (sameKey && old.f.evalValid) {
            w1.f.eval = old.f.eval;
            w1.f.evalValid = 1;  // eval bits copied from the old payload
        }

        // Publish payload first, verification word last.
        e.w1.store(w1.u, std::memory_order_relaxed);
        e.w0.store(key ^ w1.u, std::memory_order_relaxed);
    }

public:
    /** v1.4.3 (REJECTED, kept for the record): prefetch the 64-byte cluster
     *  a probe of `key` would touch. Tree-neutral by construction and
     *  faster on an uncontended host — but measured -106 Elo at fixed
     *  movetime under CPU oversubscription (2-core host, 2 concurrent
     *  games): one prefetch per makeMove across 4 engine processes
     *  thrashes the shared L2/memory subsystem and the wall-clock budget
     *  then buys a shallower tree. Removed from all call sites; see
     *  HISTORY.md §40. */
    inline void prefetch(Bitboard key) const {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&clusters_[clusterIndex(key)], 0, 1);
#else
        (void)key;
#endif
    }

private:
    inline size_t clusterIndex(Bitboard key) const {
        return (size_t)(key >> (64 - (bits_ - 2)));
    }

    int bits_ = 20;  // entry-equivalent bits (memory = 2^bits * 16 B)
    size_t clusterCount_ = 0;
    void *mem_ = nullptr;
    Cluster *clusters_ = nullptr;
    int age_ = 0;
};

}  // namespace tacturn
