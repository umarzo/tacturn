# TacTurn V1.1 — UCI Protocol Compliance Report

**Builds tested:** `TacTurn-V1-x86-64`, `TacTurn-V1-x86-64-v3`, `TacTurn-V1-x86-64-v4` (Linux)
**Author:** Umar Khan · **Harness:** `scripts/verify_uci.py` (32 checks, included in the repo)
**Result: 32/32 PASS on all three tiers**

Run it yourself:

    python3 scripts/verify_uci.py ./TacTurn-V1-x86-64-v3

Cross-checks:
- `bench 9` = **188,416 total nodes** on every tier (engine core bit-identical to V1).
- perft(5) startpos = 4,865,609.
- Windows builds: PE32+ console executables, statically linked (imports: KERNEL32.dll, msvcrt.dll only).
  Cross-compiled from the same source; not executed in the build environment — please confirm on Windows.

## What is checked

Handshake: `id name`, `id author`, `uciok`, `Hash` (1–32768 MB, default 32), `Threads`, `OwnBook` (default false),
`MultiPV`, `Clear Hash`. `isready`/`readyok`; `setoption` (Hash/Threads) accepted while staying responsive.

Search modes: `go depth`, `go movetime` (time respected), `go nodes`, increment clock, `movestogo` clock,
zero clock, negative clock, `position fen ... moves`, `go infinite` + `stop`, `isready` during search,
MultiPV 3 (three lines), `OwnBook` false/true, mate reporting (`score mate 1`, correct move), `nps` in info lines.

Robustness / regressions fixed in V1.1: malformed input survived; `go infinite` immediately followed by `stop`
returns `bestmove`; `position`/`go` right after `bestmove` is not dropped; `quit` during search exits < 4 s;
closing stdin during `go infinite` exits cleanly; `Hash 256` uses about 256 MB (not 512 MB).

## Result

    RESULT: 32 passed, 0 failed  (TacTurn-V1-x86-64)
    RESULT: 32 passed, 0 failed  (TacTurn-V1-x86-64-v3)
    RESULT: 32 passed, 0 failed  (TacTurn-V1-x86-64-v4)

Note: the earlier V1 report claimed a strength-parity match of the rebuild against the 1.4.9 binary. V1.1 changes
no search/eval code (bench identical), so that result is unchanged and not repeated here.
