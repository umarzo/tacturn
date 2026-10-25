# TacTurn V1.1 — UCI Protocol Compliance Report

**Builds tested:** `TacTurn-V1.1-x86-64`, `TacTurn-V1.1-x86-64-v3`, `TacTurn-V1.1-x86-64-v4` (Linux)
**Author:** Umar Khan · **Harness:** `scripts/verify_uci.py` (33 checks, included in the repo)
**Result: 33/33 PASS on all three tiers**

Run it yourself:

    python3 scripts/verify_uci.py ./TacTurn-V1.1-x86-64-v3

Cross-checks:
- `bench 9` = **180,736 total nodes** on every tier.
- perft(5) startpos = 4,865,609.
- Windows builds: PE32+ console executables, statically linked (imports: KERNEL32.dll, msvcrt.dll only).
  Cross-compiled from the same source; not executed in the build environment — please confirm on Windows.

## What is checked

Handshake: `id name`, `id author`, `uciok`, `Hash` (1–32768 MB, default 32), `Threads`, `OwnBook` (default false),
`MultiPV`, `Move Overhead`, `Clear Hash`. `isready`/`readyok`; `setoption` (Hash/Threads) accepted while staying responsive.

Search modes: `go depth`, `go movetime` (time respected), `go nodes`, increment clock, `movestogo` clock,
zero clock, negative clock, `position fen ... moves`, `go infinite` + `stop`, `isready` during search,
MultiPV 3 (three lines), `OwnBook` false/true, mate reporting (`score mate 1`, correct move), `nps` in info lines.

Robustness / regressions fixed in V1.1: malformed input survived; `go infinite` immediately followed by `stop`
returns `bestmove`; `position`/`go` right after `bestmove` is not dropped; `quit` during search exits < 4 s;
closing stdin during `go infinite` exits cleanly; `Hash 256` uses about 256 MB (not 512 MB).

## Result

    RESULT: 33 passed, 0 failed  (TacTurn-V1.1-x86-64)
    RESULT: 33 passed, 0 failed  (TacTurn-V1.1-x86-64-v3)
    RESULT: 33 passed, 0 failed  (TacTurn-V1.1-x86-64-v4)

Note: V1.1 contains the 1.4.10 search changes, the 1.4.11 clock manager and the 1.4.16 net, so the bench value (180,736) differs from V1 (188,416).
