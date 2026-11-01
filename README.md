# TacTurn V1.2

A UCI chess engine — **hybrid evaluation: hand-crafted eval (HCE) base with an
NNUE residual network on top** — engine code 100% original C++17, MIT
licensed. The embedded network was trained on Stockfish 17.1 evaluation
labels. (A series of Leela Chess Zero–data experiments was run and
**rejected**; none of it is in this release — see
[Credits, training data & transparency](#credits-training-data--transparency).)

> **This repository contains the engine core only.** The author's Android chess
> app that embeds TacTurn is a separate project and is not part of this
> repository.

| | |
|---|---|
| Engine | TacTurn **V1.2** |
| Author | Umar Khan |
| Protocol | UCI |
| License | MIT — full source included, no third-party chess code (training-data credits below) |
| NNUE network | Embedded in the executable — **no external files, nothing to configure** |
| Binaries | Windows x64 (3 ISA tiers) and Linux x64 (3 ISA tiers), statically linked |

---

## Downloads — pick the right binary

| File | Requires | Works on |
|---|---|---|
| `TacTurn-V1.2-x86-64.exe` / `TacTurn-V1.2-x86-64` | any x86-64 CPU (SSE2) | anything from ~2003 onwards |
| `TacTurn-V1.2-x86-64-v3.exe` / `TacTurn-V1.2-x86-64-v3` | AVX2 + BMI2 (Haswell, 2013+) | **recommended** — e.g. Core i7-4770k and anything newer |
| `TacTurn-V1.2-x86-64-v4.exe` / `TacTurn-V1.2-x86-64-v4` | AVX-512 (Ice Lake / Zen 4+) | modern CPUs only |

All Windows builds are static (`KERNEL32` + `msvcrt` only — no DLL hell, no
runtime installers). All three tiers are **bit-identical in play strength
behavior** — the deterministic bench suite returns exactly the same node
counts on every tier (see *Verification* below); only raw speed differs.

## How to run it

TacTurn is a **headless UCI engine** — it has no board or window of its own.
Load the `.exe` in a chess GUI (Cute Chess, Arena, Banksia, En Croissant, …)
via *Engines → Add engine*. Double-clicking it (or launching it from a
console) opens a command window that waits for UCI commands — type `uci`,
`isready`, `bench 9` or `quit`. **Since V1.1:** earlier V1 files ran a startup
self-test and closed right after launch; that is fixed.

If a file will not start, use the next tier down (v4 → v3 → plain
`x86-64`).

## What kind of engine is this?

TacTurn is a bitboard engine (magic bitboards generated at init, runtime
PEST dispatch on x86) with an iterative-deepening PVS / Lazy-SMP search and
one unusual evaluation design: **the shipped evaluation is a hybrid**.

- A **classical hand-crafted evaluation** (tapered, Texel-tuned: pawn
  structure, passers, king safety with a quadratic danger model,
  non-linear per-count mobility tables, outposts, threats, endgame
  material scaling, …) provides the base score.
- A **small quantized NNUE residual network** (3072→512×2→32→32→1, king
  buckets, 16 output buckets, int16 SIMD inference, incrementally
  maintained inside make/unmake) predicts a **residual correction** to that
  HCE score, injected both into the main-search static eval and the
  quiescence stand-pat, at a tuned blend.
- The shipped net is **`v96pre`** (embedded id `v96pre-1.4.11-deployed`;
  called `v96b-mix21m-3ep` in older READMEs): 21.47M engine-generated
  positions labeled with **Stockfish 17.1 "Final evaluation" static scores**.
  Its weights are **bit-identical to the net inside the original V1 and the
  first V1.1 build** (checked weight-for-weight). It was measured at
  **+162.0 ± 16.6 Elo** over the same binary with the net disabled
  (`UseNNUE=false`, n=540 @ 50 ms, pre-audit measurement).
- **What happened to the later nets (full disclosure).** Between internal
  versions 1.4.12 and 1.4.20 the author trained a series of replacement nets,
  the last three (1.4.18–1.4.20) against **Lc0-derived evaluations**. Their
  gate results were later found to have been reported with the **sign
  flipped** by the match harness (`HISTORY.md` §58): every one of those
  "gains" was actually a loss, and the 1.4.20 net had collapsed to a near
  constant (it contributed nothing). An independent audit (`HISTORY.md` §59,
  python-chess referee) found even the mid-series nets weaker than `v96pre`
  (v99 net vs v96pre: **−75 Elo** for v99 at 30 and 60 ms). The earlier
  public **V1.1 (1.4.16 net) and the first V1.2 (1.4.20 net) releases were
  therefore regressions** and are superseded by this release.
- Further Lc0-data experiments after the audit (1.4.23, `HISTORY.md` §60)
  were run to a clean rejection at natural scale at both time controls, and
  nothing from them shipped.

Search highlights: aspiration windows with a partial-iteration salvage
guard, singular extensions with multi-cut, adaptive null-move, reverse
futility with an improving flag, razoring, futility/late-move pruning,
unified SEE pruning, LMR with log formula + history adjustments,
evaluation-correction histories (pawn/non-pawn buckets), lock-free
race-hardened transposition table shared by up to 8 Lazy-SMP threads, and
a complexity-aware adaptive time manager with stability pacing and a
falling-eval extension.

## Strength

**There is no verified CCRL rating for this engine yet** — the list will
assign its own. Earlier versions of this README quoted "~3250 Elo" and
gain numbers (+87…+193 Elo from the Lc0 rounds, etc.); **those figures are
withdrawn.** The harness behind them was sign-inverted (`HISTORY.md` §58) and
the historical external ladder was measured under different controls and
books, so its rungs are not comparable (`HISTORY.md` §59).

What has been re-measured with the independent referee (python-chess
adjudication, paired openings, both colors; all vs the immediately previous
build, self-play, very fast fixed-time controls — **not additive, not a
rating**):

- 1.4.22 (v96pre restored) vs the v99-net build: **+74.5 ± 12.6 @ 30 ms,
  +74.8 ± 15.7 @ 60 ms**; external check vs Simbelmyne 1.10.0 at 5+0.05:
  68.8% (n=120).
- 1.4.23 (three suspect search levers reverted): **+13.9 ± 5.9 @ 30 ms,
  +18.7 ± 7.2 @ 60 ms** over 1.4.22 (in-family, SPRT H1).
- 1.4.26 (search bug fixes, NNUE speed pack, time management): **+73.5 ±
  19.0 @ 50 ms** over 1.4.25 (pooled 25+50 ms: +47.7 ± 15.0; at 15 ms the
  time-management lever read slightly negative, see release notes).
- 1.4.28 (this release, search salvage fixes + capture-history aging + speed
  pack): **+17.4 ± 9.4 pooled** (900 games, three regimes) over 1.4.27.

Treat all of this as the author's engineering log, not a strength claim.

## UCI options

| Option | Type | Default | Notes |
|---|---|---|---|
| `Hash` | spin (MB) | 32 | 1–32768 MB. CCRL uses 256 or 512 — supported. Table size is rounded down to a power of two (256 → 256 MB, 100 → 64 MB). |
| `Threads` | spin | 1 | 1–8 (Lazy SMP). Use 1 for single-CPU lists, 4 for 4-CPU. |
| `Move Overhead` | spin (ms) | 60 | 0–10000. Safety margin reserved from every clock so the engine never flags; raise it on slow machines/GUIs. |
| `Contempt` | spin | 12 | −200–200 cp. Keep default for rating play. |
| `UseNNUE` | check | true | `false` = pure HCE (the hybrid's ablation reference). |
| `NNUEBlend` | spin | 77 | Residual blend in the main eval. Keep default. |
| `StaticBlend` | spin | 72 | Residual blend at qsearch stand-pat. Keep default. |
| `OwnBook` | check | **false** | The engine's embedded 3,996-line book is an Android-app feature and is **never used over UCI**; this option makes that explicit and testable. Rating lists require own books off — the default is compliant. |
| `MultiPV` | spin | 1 | 1–8, for analysis. |
| `Clear Hash` | button | — | Clears TT + learned histories (same state as `ucinewgame`). |

Notes for testers:

- **Ponder**: not supported (not advertised) — CCRL tests with ponder off.
- **Syzygy/EGTB**: not supported in this version; the engine plays
  endgames without tablebases. (Planned for a future release.)
- `go` understands `depth`, `movetime`, `nodes`, `wtime/btime/winc/binc`
  and `movestogo` (period-based repeating time controls such as 40/15 are
  allocated per moves-to-go).
- `stop`, `isready` and `quit` are honored **during** a search.

## Verification

- **Perft**: standard 6-position suite bit-exact (startpos perft(5) =
  4,865,609; Kiwipete perft(4) = 4,085,603; promotion/EP torture included).
- **Deterministic bench fingerprint**: `<engine> bench 9` = **165,888
  total nodes** on every ISA tier and platform — identical node counts
  across builds prove identical search behavior.
- NNUE inference verified bit-exact against a double-precision reference
  on random playout batteries; SEE short-circuit verified against exact
  swap-off lists; gives-check predicate verified against post-make truth.
- A 33-check UCI protocol compliance suite (handshake, options, position
  handling, all `go` modes, mid-search stop/quit, MultiPV, mate reporting,
  malformed-input robustness, stop-right-after-go, closed-stdin shutdown,
  Hash memory sizing) passes 33/33 on all three Linux tiers — see
  `UCI-COMPLIANCE-REPORT.md`. Re-run it yourself:
  `python3 scripts/verify_uci.py ./TacTurn-V1.2-x86-64-v3`.

## Building from source

The engine core has no Android dependencies — plain C++17. The layout is
flat: `uci.cpp` (UCI harness) plus the `tacturn/` directory (engine core):

```bash
# Linux (or cross-compile with x86_64-w64-mingw32-g++-posix for Windows)
g++ -std=c++17 -O3 -flto -DNDEBUG -march=x86-64-v3 \
    -I. -Itacturn \
    uci.cpp tacturn/*.cpp \
    -o TacTurn-V1.2-x86-64-v3 -lpthread
```

Or simply run `./build.sh` — it builds all three tiers into `bin/` and
bench-checks each one (every build must print `Total nodes: 165888` for
`bench 9`).

Drop `-march=x86-64-v3` for the universal baseline build, or use
`-march=x86-64-v4` for AVX-512. For Windows add
`-static -static-libgcc -static-libstdc++`.

## Credits, training data & transparency

All engine code is original to this project (MIT). PeSTO piece-square
tables were used as the Texel-tuning starting point; chessdb.cn was queried
read-only to verify opening-book moves.

**Stockfish** (17.1) was used during development strictly as (1) an external
benchmark *opponent* (binary only — never distributed, never linked) and
(2) the training-label *teacher* for the NNUE net (static evaluations of
engine-generated positions). No Stockfish code, weights or network
architectures are copied.

**Leela Chess Zero (Lc0) data — experiments only, not in this release.**
During development (internal versions 1.4.18–1.4.20 and a re-test in 1.4.23)
the author trained candidate nets against **Lc0-derived evaluations**, taken
from the public NNUE-training binpacks hosted at
<https://robotmoon.com/nnue-training-data/> (converted from Lc0 "test80"
self-play data by **Linmiao Xu (linrock)**; the data is the work of the
**Leela Chess Zero project and its contributors**). Credit and thanks to
them. Those nets were **measured, found worse, and discarded**
(`HISTORY.md` §58–§60); the network embedded in this release was trained
**before** that work and contains no Lc0-derived training. TacTurn includes
no Lc0 code, no Lc0 network file, and redistributes none of that data. The
earlier public releases that did embed those nets (the first V1.2 build,
1.4.20) are superseded.

The full engineering history, including the incident report, the audit and
the negative results, is in `HISTORY.md`.
