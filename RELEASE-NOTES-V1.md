# TacTurn V1.2 — Release Notes (internal 1.4.20)

Author: **Umar Khan** · License: MIT · `id name TacTurn V1.2`.
Binaries are named `TacTurn-V1.2-x86-64[-v3|-v4][.exe]`. **Replace all
earlier V1 / V1.1 files with these.** The notes of the earlier releases are
kept below.

## What is new in V1.2

- **New NNUE net `v113-null19-ha-s025`** (internal 1.4.17 → 1.4.20). Only the
  embedded network changed; search, clock manager and UCI layer are
  unchanged from V1.1.
- **Lc0-derived training data (disclosure).** Three training rounds
  (1.4.18, 1.4.19, 1.4.20) used **Leela Chess Zero's search evaluations** as
  the teacher, sampled from the public Lc0 NNUE-training binpacks hosted by
  Linmiao Xu (linrock) at robotmoon.com/nnue-training-data. Previously the
  teacher was Stockfish 17.1. No Lc0 code or network file is included, and no
  data is redistributed. README "Credits, training data & transparency"
  gives full credits.
- **Measured self-play results vs the previous shipped net** (SPRT(0,10),
  30 ms / 60 ms controls): 1.4.17 +7.7 ± 5.2 pooled; 1.4.18 +93.2 ± 18.3 /
  +87.5 ± 17.6; 1.4.19 +172.0 ± 28.9 / +192.6 ± 31.9; 1.4.20 +31.8 ± 9.6 /
  +48.6 ± 12.3. These are very-fast-control internal results, not additive
  and not a CCRL rating. Note (HISTORY §57): the 1.4.20 gain came from
  re-anchoring the 1.4.19 net's output scale (s = 0.25); all freshly trained
  candidates in that round lost to it.
- **New bench identity: `bench 9` = 230,912 nodes** (identical on all three
  tiers and identical to the author's own 1.4.20 build).
- Default options unchanged: `Hash` 32 MB, `Move Overhead` 60 ms,
  `NNUEBlend` 77, `StaticBlend` 72.

## Verification

- `bench 9` = 230,912 on `x86-64`, `x86-64-v3`, `x86-64-v4` (Linux builds).
- `scripts/verify_uci.py`: 33 checks, all passing on all three Linux tiers.
- Windows `.exe` builds are cross-compiled with MinGW-w64 (static) from the
  same source; they were not executed in the build environment — confirm on
  a real Windows machine.

---

# TacTurn V1.1 — previous release notes (internal 1.4.16, kept for history)

Author: **Umar Khan** · License: MIT. The release stays **V1.1**
(public V1 = internal 1.4.9; V1.1 = internal 1.4.16, `id name TacTurn V1.1`).
**Replace all earlier V1 / V1.1 files with these.**

## What is new in this build (on top of everything listed below)

- **Stronger NNUE net: `v99-ftd10-ha-b400`** (internal 1.4.12 → 1.4.16;
  only the embedded network changed — search, clock manager and UCI layer
  are unchanged from the earlier V1.1 build):
  - 1.4.12 anchored-head retrain on a multi-navigator corpus: +81.5 ± 16.8
    Elo @ 60 ms vs 1.4.11 (SPRT(0,10) H1).
  - 1.4.13–1.4.14: corpus refreshes with Stockfish-guided exits and a
    home-anchored head (≈ +10 pooled @ 30/60 ms).
  - 1.4.15: measured exhaustion of the blend sweep (engine unchanged).
  - 1.4.16: feature transformer unfrozen + depth-10 teacher labels:
    +18.0 ± 6.6 @ 30 ms and +32.7 ± 9.7 @ 60 ms vs 1.4.15 (both SPRT(0,10)
    H1). Full ledger in `HISTORY.md` §49–53.
- **New bench identity: `bench 9` = 180,736 nodes** (the net changes node
  counts). Identical on all three tiers, and identical to the author's own
  1.4.16 build.
- Default options unchanged: `Hash` 32 MB, `Move Overhead` 60 ms,
  `NNUEBlend` 77, `StaticBlend` 72.

## Verification

- `bench 9` = 180,736 on `x86-64`, `x86-64-v3`, `x86-64-v4` (Linux builds).
- `scripts/verify_uci.py`: 33 checks, all passing on all three Linux tiers.
- Windows `.exe` builds are cross-compiled with MinGW-w64 (static) from the
  same source; they were not executed in the build environment — confirm on
  a real Windows machine.

---

# TacTurn V1.1 — earlier build notes (internal 1.4.11, kept for history)

Author: **Umar Khan** · License: MIT · Internal version 1.4.11
(public V1 = internal 1.4.9; V1.1 = internal 1.4.11).

**Replace all V1 files with V1.1.** Binaries are now named
`TacTurn-V1.1-x86-64[-v3|-v4][.exe]`. `id name` is `TacTurn V1.1`.
Bench identity changed to **`bench 9` = 171,520 nodes** (V1 was 188,416 —
the search changed in 1.4.10), identical on all three tiers.

## Engine changes (from 1.4.10 and 1.4.11)

- **1.4.10 — two search levers:** continuation-history correction and a
  single-reply extension (+8.6 ± 6.0 Elo @ 100 ms, n=1973).
- **1.4.11 — clock overhaul:** the hard time wall was dead code in clocked
  play and the old pacing lost games on time. Rebuilt flag-proof pacing:
  a `Move Overhead` reserve (default 60 ms), expected-overhead accounting,
  aged moves-to-go, shrinking depth floor in time trouble, and a pacing
  floor-vs-cap ordering fix. Self-play at 1+0: previous pacing flagged
  16/16 games, V1.1 flags 0/16; 2+0.03: 6/8 → 0/16. Head-to-head vs the
  previous build: +708 (1+0), +116 (2+0.03), +33 (10+0.1) Elo — mostly
  from no longer flagging. Full data in `HISTORY.md` §48.
- New UCI option **`Move Overhead`** (ms, default 60, 0–10000). Raise it if
  your GUI/hardware adds latency.

## Host / UCI fixes (carried over from the V1 → V1.1 audit)

1. **Exe closed ~1 s after launch (double-click / bare console start).**
   The old startup self-test used a stale expected value and exited. It is
   removed; a bare launch now stays open as a normal UCI engine.
2. **Dropped command right after `bestmove`** (the "searching" flag was
   cleared too late) — fixed.
3. **`stop`/`quit`/closed stdin right after `go` could be lost** and leave
   the search running forever — fixed.
4. **`Hash` used twice the requested memory** — now rounds down to a power
   of two (`Hash 256` = 256 MB).
5. **Negative/zero clock or a bare `go depth`** could run an unlimited
   search — fixed. Missing flushes and a stray `info depth 0` line fixed.

## Verification

- `bench 9` = 171,520 on `x86-64`, `x86-64-v3`, `x86-64-v4` (Linux builds).
- `scripts/verify_uci.py`: 33 checks, all passing on all three Linux tiers.
- Windows `.exe` builds are cross-compiled with MinGW-w64 (static; KERNEL32 +
  msvcrt only) from the same source. They were not executed in the build
  environment — confirm on a real Windows machine.

---

# TacTurn V1 — Release Notes (original)

**First public release** · Author: **Umar Khan** · License: MIT · Protocol: UCI

TacTurn V1 is the first public version of the engine. Internally it was
developed as **1.4.9** — the repository's HISTORY.md carries the complete
engineering ledger with every test verdict — and V1 is that exact engine
with a **UCI protocol compliance upgrade** for public testing. The chess
core (search, evaluation, NNUE, book, move generation) is **bit-identical**
to that development build: the bench fingerprint (188,416 nodes at depth 9)
is unchanged on every platform and ISA tier. All changes live in the host
UCI layer and in build portability.

Engine identity: `id name TacTurn V1` · `id author Umar Khan`

## Protocol fixes vs the original development harness

1. **Responsive `stop` / `quit` / `isready` during search** — the search
   now runs on a worker thread while the main thread keeps reading stdin.
   Previously `go infinite` + `stop` deadlocked the GUI, and `quit` was
   ignored until the time budget expired.
2. **`Hash` option declared in megabytes** (default 32, min 1, max 32768).
   The old declaration (`default 86016 min 12 max 30`) made GUIs clamp or
   reject standard values such as 256/512 MB. Same 16 B/entry mapping as
   before — the actual table allocation for a given MB value is unchanged.
3. **`go ... movestogo N` is parsed and used**: period-based repeating
   time controls (CCRL 40/15 = 40 moves / 15 min repeating) now get a
   moves-to-go-aware allocation (soft = t/(mtg+2) + 0.75·inc, hard cap
   bounded so one unstable move can never eat a period). The legacy
   formula is kept bit-identically for increment-only clocks.
4. **Clean engine identity**: `id name TacTurn V1` with
   `id author Umar Khan` (the development harness printed the doubled
   "TacTurn TacTurn 1.4.9 (host UCI)" and no author).
5. **`OwnBook` option (default false)** — documents and enforces that the
   UCI build never uses the engine's embedded book (an Android-app-only
   feature). Rating lists require own books off; the default is compliant
   and testable.
6. **`MultiPV` (1–8)** via sequential root-exclusion searches, with full
   PV lines from the transposition table.
7. **Mate scores reported as `score mate N`** instead of raw ~32000 cp;
   `nps` added to info lines; the final info line before `bestmove`
   carries the full PV.
8. **`Clear Hash` button** — clears the TT and learned histories (the
   same state `ucinewgame` produces).
9. **Robust `setoption` parsing** — multi-word option names, missing
   values, and unknown options can never derail the parser.
10. **Windows portability** — `posix_memalign` → `_aligned_malloc` (Win32
    guard), `unistd.h` → `io.h` guard, and the pthread helper-thread
    launch links cleanly under MinGW-w64 posix-threads. Windows builds
    are fully static.

## Portability fix (critical for testers)

The original development binary was compiled with `-march=native` on an
AVX-512 machine and would crash with an illegal instruction on
Haswell-class hardware (e.g. the CCRL reference i7-4770k). This release
ships explicit ISA tiers instead:

| Tier | Needs | Note |
|---|---|---|
| `x86-64` | SSE2 (any 64-bit CPU) | maximum compatibility |
| `x86-64-v3` | AVX2/BMI2 (Haswell 2013+) | **recommended** |
| `x86-64-v4` | AVX-512 (Ice Lake / Zen 4+) | modern CPUs |

## Identity fingerprints

- Bench: `TacTurn-V1-* bench 9` → **Total nodes: 188,416** (all tiers,
  all platforms).
- Startpos depth-9 search (contempt 12, 1 thread): identical best move and
  score on every tier.
- perft 5 (startpos): 4,865,609.

## Source layout of this kit

- `engine/` — the C++17 engine core (unchanged from the 1.4.9 development line)
- `host/uci.cpp` — the UCI harness with the fixes above
- Windows `ttable.h` aligned-allocation guard
- Docs: `ENGINE-README.md`, this file, `UCI-COMPLIANCE-REPORT.md`
