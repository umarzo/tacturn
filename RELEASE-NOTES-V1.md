# TacTurn V1.1 — Release Notes (host/UCI fix release)

**Released after V1** · Author: **Umar Khan** · License: MIT

V1.1 changes **only the UCI host layer (`uci.cpp`)**. The chess core —
search, evaluation, NNUE, move generation — is untouched and bit-identical:
`bench 9` = **188,416 nodes** on every tier (all six binaries rebuilt and
re-checked). Engine ID is unchanged (`id name TacTurn V1`), so rating-list
entries and strength estimates for V1 carry over. Replace all V1 binaries
with the V1.1 ones.

## Fixes

1. **Exe closed ~1 s after launch (double-click / bare console start).**
   A bare launch ran a startup "fingerprint" self-test and exited. Its
   expected value was stale (33,280 nodes; the real, correct result is
   32,768), so it printed `FINGERPRINT MISMATCH` and quit. The self-test is
   removed; a bare launch now prints a short hint and behaves like any UCI
   engine, staying open for commands. The real identity check is
   `bench 9` → `Total nodes: 188416`.
2. **Dropped command right after `bestmove`.** The "search running" flag
   was cleared *after* `bestmove` was printed, so a GUI that sent its next
   `position`/`go` immediately (as fast GUIs do) could have it silently
   ignored. The flag now drops before `bestmove` is written.
3. **`stop` right after `go` was lost.** The core clears its stop flag when
   a search starts, so a `stop` (or `quit`, or GUI crash/EOF) arriving
   before the worker thread reached the search left it running forever. The
   host now keeps requesting the stop until the search has actually ended.
   Closing stdin during `go infinite` also now exits cleanly.
4. **`Hash` used twice the memory requested** (`Hash 256` allocated
   512 MB; default 32 → 64 MB). The MB→table-size mapping now rounds down.
5. **Negative/zero clocks** (`wtime -50`) used to run an unlimited search;
   they now move immediately. A clock sent for only one side no longer
   breaks the time budget.
6. **Malformed `go` parameters** (e.g. a bare `go depth`) no longer zero
   the limit and turn the search unlimited.
7. Missing output flushes on `invalid FEN` / `illegal move` messages; no
   junk `info depth 0` line when a search is stopped before it begins; a
   printf format warning removed.

## Verification

- `bench 9` = 188,416 on `x86-64`, `x86-64-v3`, `x86-64-v4` (Linux builds).
- `scripts/verify_uci.py`: **32/32** on all three Linux tiers.
- Windows `.exe` builds: cross-compiled with MinGW-w64 (static; imports only
  KERNEL32.dll + msvcrt.dll) from the same source. The Windows files were
  not executed in the build environment; the double-click fix should be
  confirmed on a real Windows machine.

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
