# TacTurn V1 — Release Notes

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
