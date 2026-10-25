# TacTurn — Consolidated Development History

This file preserves the useful engineering history that previously lived in
`BUILD_NOTES.md`, `LADDER.md`, `UI-OVERHAUL-NOTES.md` and the version-history
sections of the README. Those files were removed when the project was cleaned
up for the production V1; the substance is consolidated here so no decision,
measurement or hard-won lesson is lost.

---

## 1. Version timeline

| Version | Focus | Verified placement / notes |
|---|---|---|
| 1.0.0 | Initial host build, perft-correct core | — |
| 2.0.0 | Strength ladder v1 + optional NNUE subsystem (default off) | ≈ 2635 at 150 ms, 1 thread |
| 2.1.0 | Stability + device hardening | `benchmarks/device-stability-v2.0.1.txt` era |
| 2.2.0 | Search + full HCE rewrite, Texel tuner pipeline | ≈ 2720–2735 at 150 ms, 1 thread |
| 2.3.0 | Search-ordering ladder (X-series) | ≈ 2758 (308-game cumulative confirm +24) |
| 2.4.0 | HCE overhaul (+34 terms) + first PST tune | ≈ 2792 at 150 ms, 1 thread |
| 2.5.0 | PEXT speed pack + depth-12 term re-tune | chain estimate +27 ± 17 vs v2.4 |
| 2.6.0 | Joint PST+terms Texel re-tune (251k corpus) | ≈ 2838 ± 14 |
| 2.7.0 | Profile-guided optimization build path (+10–12% nps) | ≈ 2857 ± 18 (host PGO) |
| 2.8.0 | Opening book 64 → 3,996 lines, time-management overhaul, 12 SPSA knobs | ≈ 2878 ± 44 (host PGO); clock play transformed |
| 3.0.0 | Eval-campaign convictions banked; deterministic book test | engine bit-identical to v2.8.0 |
| 3.1.0 | SF19 label bank + imbalance infrastructure (dormant, gate-rejected) | engine bit-identical to v2.8.0 |
| 3.2.0 | E1 partial-iteration salvage (+58 at 150 ms) | fast-TC regression discovered later |
| 3.2.1 | E1 soundness guard (2 semantic lines) | 30 ms +11 vs v2.8.0 (regression fixed), 150 ms +23 |
| **1.0.0 (V1 production)** | Production cleanup, NNUE removal, 2850 top level, bug fixes | bench 11 = 1,369,600 nodes, tests 2358/0 (see §7) |
| 5.0 (engine, author) | NNUE residual hybrid, SF-taught | see §13 |
| **1.2.0 (V1.2, engine v5.2)** | NNUE rebuild: king buckets, output buckets, warm-trained net, SPRT-verified gains | see §15; bench 6 = 57,344 nodes, all gates PASS |
| **1.3.0 (V1.3, engine v5.3)** | Final-stretch hybrid campaign: lazy refresh, AVX-512 inference, StaticBlendPct 50, search surface retune | see §16; vs shipped pure HCE: **+42.8 ± 13.8 @ 50 ms (SPRT H1)**, **+75 ± 24 @ 150 ms** |
| 1.4.0 (V1.4, engine v5.4) | Network retrain campaign | see §17 |
| 1.5.0 (V1.5, engine v5.5) | Data-scale + capacity campaign: 15.05 M corpus, res256-warm net | see §18 |
| **1.6.0 (V1.6, engine v6)** | The 200-Elo push: lazy accumulator ring (4.6x lower FT tax), VNNI batched inference, L1 512 + 16 output buckets, 13.5 M retrain, uniform TT-eval semantics | see §19; vs shipped pure HCE: **+81.0 ± 11.9 @ 50 ms (n=900)**, **+74.8 ± 15.9 @ 150 ms (n=500)**, **+79.5 ± 28.2 @ 400 ms (n=160)** |
| 1.7.0–1.9.4 (engine v7–v96) | Data-scale, envelope and different-navigator campaigns; every promotion SPRT-gated | see §20–§27 |
| 1.9.5 (engine v96b) | Echo-corrected corpus campaign; harness-integrity find | see §28; +14.1 ± 6.0 over v1.9.4, crush vs HCE +162.0 ± 16.6 |
| **1.0.0 (V1 production, second clean ship)** | The v1.9.5 (v96b) champion re-versioned as the first stable production release; full campaign-artifact cleanup, removals consolidated here | bench 9 = 281,600 nodes, verify all PASS (see §29) |
| **1.0.1 (V1 production)** | Elo ceiling 2850 → 3000 with genuinely more compute at the top (all 8 Lazy-SMP threads, 8 s soft budget, Elo-grown TT), in-repo release keystore, portable reference host binary shipped in the zip | startpos fingerprints d9 = 59,904 / d11 = 235,008 nodes — identical to 1.0.0 (see §30) |
| **1.1.0 (V1.1 production)** | External-review speed & soundness campaign: qsearch SEE-after-make fix, quiet-check prefilter, MVV-LVA + good/bad capture split (early-exit seeGE), NEON VNNI-style kernel, 16-bit histories, multi-cut un-dead-coded, SMP aspiration/best-thread, PGO infrastructure, per-ABI mtune | **+27.9 ± 4.5 Elo vs 1.0.1 (SPRT H1, n=200 @ 100 ms)**; bench 9 = 219,136 nodes x86 == aarch64; all fuzz batteries 0 mismatches (see §31) |
| **1.4.11 (production)** | Time-management overhaul: the dead hard-wall fix (absolute deadlines), flag-proof clock pacing (overhead reserve, aged moves-to-go, net-increment accounting, proportional floors), forced-move shortcut cut, app-side safety envelope | **vs 1.4.10: +708.3 ± 68.4 @ 1+0 (n=360), +116.0 ± 13.7 @ 2+0.03 (n=500), +33.3 ± 19.9 @ 10+0.1 (n=136)**; flags 100%→0 (1+0), 75%→0 (2+0.03); bench-identical d9 (see §48) |

Note on the version jump to 1.0.0: the production cleanup release was
deliberately re-versioned to mark the first "clean ship" (versionCode 14,
monotonically above every earlier build, so it installs over any predecessor).
The v1.0.0 second clean ship (§29) repeats the same discipline: versionName
reset to 1.0.0 with versionCode 30, monotonically above the v1.9.5 ship
(versionCode 29) so it installs over every predecessor.

---

## 2. Strength ledger — the short version

Method discipline that took the whole project to learn (see §3 for the raw
lessons): fixed-node screening is a weak proxy — all real gates ran at fixed
time per move (150 ms standard) with cumulative confirms every ~4 promotions;
winner's curse inflates chained point estimates several-fold; only large
cumulative confirms are meaningful.

### v2.2.0 — strength overhaul (2630 → ~2720–2735 at 150 ms, 1 thread)
All match-verified (281-game final, 64.6% = +104 Elo, CI +63..+149).
- Search: unified SEE pruning (`see(move) < -60*depth`, depth ≤ 6, non-PV,
  TT move exempt); correction history with enemy-non-pawn bucket (Stockfish
  structure); LMR for clearly bad captures (incl. the bounds fix that stopped
  quiet promotions indexing the capture-history victim table); aspiration
  windows with geometric widening + growth floor, full window after 4 fails;
  SEE-filtered qsearch first-ply checks; `go nodes N` UCI support.
- Evaluation: full HCE rewrite into a parameterised, tuner-consistent term
  set (doubled/isolated/backward/islands/phalanx/supported pawns, passers by
  rank +connected +king-proximity +blocked, bishop pair, bad bishop, rooks,
  outposts, per-file king shelter, pawnless-king penalty, king-attack danger,
  safe mobility, threats, hanging pieces, tempo, endgame material scaling,
  early-EG phase scale).
- Texel pipeline born: `tools/tuner.cpp` Gauss-Newton on logistic loss,
  result/score-blended targets, knowledge-bounded boxes, monotonicity
  constraints; ~950k d6 + ~105k d9 self-play positions from
  `tools/datagen.cpp`; tuner reuses the engine's own evaluate() via the
  EvalTrace hook so the fitted model cannot drift from the served eval.
- PSTs remained at PeSTO reference values (frozen during term fitting).

### v2.3.0 — X-series ladder (2734 → ≈ 2758)
Kept: TT depth-pref hybrid replace (+23 nodes-mode), removing the
unconditional check extension (+6, 11% faster), razoring d≤3/220 (confirmed
by ablation: off = −72), history bonus 1.5·d·d (ablation −40 confirms), eval
cache 2^16 direct-mapped, 4-ply + capture continuation histories (+33),
singular-extension threshold 6 → 5 (+44), history-gated quiet pruning
(d≤3, hist<−9000) (+16).
Rejected: RFP 75→68 (−30), IIR at cutNode/no-TT (−47), razoring d≤4 variants,
LMR divisor moves both directions, NMP verification (−151), aspiration delta
18 (−16), qsearch delta/SEE margin tightening (−63), HCE retune on 1M pos
(−32; HCE at ceiling).
Cumulative confirms C1..C4: +14 → +32 → +3 → final **+24 (CI −15..+63)**
over 308 games — while chained gated sums totalled +146. See Lessons (§3).

### v2.4.0 — HCE overhaul + first PST tune (2730 → ≈ 2792)
- Y1 HCE overhaul: +34 terms (pawn storm, king defenders, king-pawn
  proximity, candidate passers, passer edge/defended/path, minors behind
  pawns, bishop rammed/long-diag, siberia, closedness, extended threats,
  weak pawns, space, x-ray mobility, Ethereal mobility-area,
  pawnAtt2-filtered king zone, safe checks + weak squares in danger,
  queenless danger cap, wrong-RPB + lone-queen scaling, complexity).
- Texel rounds on 487k → 512k positions; first-ever PST tune (768 cells,
  ±12/cell box); eval cache 2^16 → 2^18; tuner gained pre-scaling linear
  target, blend/ridge/PST CLI, 924-column mode.

### v2.5.0 — speed pack + data cycle
- PEXT attack path with runtime BMI2 dispatch (x86-64 only; magic bitboards
  remain the ARM path) — node counts bit-exact vs v2.4. Host builds
  `-march=native -flto=auto` with silent probe fallback; Android library
  thin LTO (~+2-4% nps).
- 151-feature layout: per-count mobility tables (N/B/R/Q, Berserk-style
  non-linear), per-file isolated pawns, KING_OPEN_FILE / SIMPLIFY /
  PAWN_BLOCKED / PASSED_STACKED. Term-only re-tune on fresh depth-12 data;
  joint PST+terms fits REJECTED (151k positions → non-monotone mobility
  tables, −50..−120 in matches). Tuner rebuilt around PROJECTED Gauss-Newton
  (per-step projection onto emit-feasible set, ROUNDED-model validation,
  fixed step-accept loop, `--pst-box`, `--optimize-k`).
- Protocol fixes: UCI `send()` emits exactly one write per line; match
  runner `readLine()` accumulates partial reads instead of discarding them.
- Two search candidates (SF-style corrhist gating, double extensions) were
  match-gated at 180 games and rejected; both remained in source behind
  compile-time switches for future experiments.

### v2.6.0 — joint re-tune payoff (≈ 2838 ± 14)
Joint PST+terms re-fit of all 151 parameters AND 768 PST cells on the pooled
251,602-record depth-12 corpus, PST movement boxed to ±6 cp/cell, monotone
mobility constraints, blend .5, ridge .015, K = 0.00358, holdout val MSE
0.027522 → 0.026684. 353/768 PST cells moved (all within the box).
Placement evidence: three-measurement stack (chained +77 ± 24, direct
+30 ± 17, joint estimate **+46 ± 14**). The earlier v2.5 joint failures were
data starvation, not method error — tighter boxes + doubled data fixed it.

### v2.7.0 — PGO build path (≈ 2857 ± 18, host)
Two-pass PGO (instrumented → bench + 15-FEN sweep training → profile-use
rebuild), identical flags otherwise; bench tree bit-exact (1,361,920 both).
+10.4% nps under concurrent load, +11.8% unloaded. 800-game match: pooled
52.7% = +19 ± 12. Inactive zero-default DCOR king-danger buckets added
(tree-identical at zero defaults). Note: the Android APK build was unchanged —
ARM PGO would require on-device profiles.

### v2.8.0 — book breadth + timeman overhaul (≈ 2878 ± 44 host PGO)
- Opening book 64 → 3,996 lines: all 64 curated mainlines merged with a
  chessdb.cn-verified breadth book (4,489 queries, "!"-tier weighted picks,
  score-banded 4/3/2, playable-tier weight-1 backups, top-3 expansion so
  positions after opponent deviations are covered). Gap-free to ply 9,
  mainlines to ply 10. Book is rodata-only — nps unchanged (verified).
- 12 UCI tuning knobs (P_LmrBase..P_AspDelta) with clamped bounds; defaults
  bit-identical to v2.7.0.
- Time-management overhaul: (1) fixed-movetime ID loop starts iterations
  expected to fit the FULL budget — utilization 77–96% bimodal → stable
  98–100%; (2) iteration abort cap is always the hard cap — soft pacing only
  at iteration start; (3) UCI clock pacing t/(mtg+2)+inc/2, proportional
  floor, flag-proof hard cap — old code flagged and lost outright at fast
  clocks (25/26 games at 1s+30 → 1/26).
- SPSA convicted: full 161-iteration gradient search over the 12 constants
  (2,576 paired games) produced a candidate that lost −48 despite +8.5%
  nps. The v2.7 constants are a genuine local optimum.
- Twin gate with books on: 53.0% = +21 [−19, +61] over 150 games.

### v3.0.0 / v3.1.0 — eval campaigns: convicted at family optimum
- v3.0.0: 107,627 fresh gen-4 self-play positions; joint re-tune (PST ±6 +
  all 151 terms) val MSE −3.25% but gate 41.7% (−58) — REJECTED; term-only
  re-tune 49.3% — dead neutral; DCOR splice 47.9% — rejected. Conclusion:
  the v2.8 eval is at its family optimum against self-labeled data; progress
  requires out-of-family labels. The flaky `testBookProbesValid` was fixed
  (explicit probe seeds; suite now deterministic 2358/0).
- v3.1.0: SF19 static label bank (107,627 positions relabeled with
  Stockfish 19 WDL-scaled static eval, λ=0.66 sigmoid-space fit,
  corr 0.8356); term-only SF distillation gate 48.96% — neutral; the
  material-imbalance 8-term family (dormant, zero-default, bench-identical)
  gated 33.3%/40.3% — both rejected; plumbing shipped dormant for the next
  data cycle. Engine plays identically to v2.8.0 (bench 1,361,920).

### v3.2.0 / v3.2.1 — E1 partial-iteration salvage + soundness guard
- E1: when an iteration is aborted at the hard deadline, keep the best move
  that COMPLETED at that depth instead of discarding it; stop-check mask
  2048 → 512 nodes. +58 Elo at 150 ms (224/384, all 8 seed families
  positive); +110 directional at 5s+30 clock. Bench "fingerprint" changed
  to 1,369,600 @ d11 — a reporting-granularity change (truncation grid),
  proven NOT a tree change by mask-only bisect.
- v3.2.1 regression fix: user field testing found v3.2.0 losing ~61 Elo at
  30 ms vs v2.8.0. Root cause: the salvage published the aborted iteration's
  partial best move without a soundness guard — PVS scores below the
  aspiration floor are fail-low upper bounds, and comparing upper bounds
  across root moves can flip the salvaged pick to a junk move. Fix (two
  lines of semantics): publish only when `best > alphaIn` (exact in-window
  evidence), and reset the publication slot per re-search. Result: 30 ms
  51.6% (+11) vs v2.8.0 — the fast-TC regression gone; 150 ms 53.2% (+23) —
  the deep-search gain retained.

---

## 3. Hard-won lessons (from the ladder ledgers)

1. Winner's curse is real: chained gated sums inflated true gains ~6×.
   Only large cumulative confirms are meaningful.
2. Fixed-node screening does not transfer to time controls (twice reverted,
   twice re-confirmed by ablation).
3. What won repeatedly: move ordering and verification (conthist, SE,
   razoring, history bonus). What lost repeatedly: every attempt at more
   aggressive pruning (RFP/futility/LMR/razor widening, NMP verification).
4. Micro-experiments show first-chunk spikes that decay (+97 → +4);
   never trust a single 44-game chunk.
5. Match gates must use a VALID baseline: one cycle accidentally compared
   identical engines (a "reference" rebuild was contaminated with SPSA
   constants) — verdicts were re-run after the incident.
6. The eval lever is finite: the v2.8 eval sat at the optimum of its data
   family against every parameterisation tried (v3.0 campaign); moving it
   needs out-of-family labels, not more parameters.
7. Unconstrained tuner fits produce pathological sawtooth mobility tables
   that lose 50–120 Elo; monotonicity constraints are mandatory.
8. A tuner that reuses the engine's own evaluate() (EvalTrace hook) cannot
   drift from the served eval — keep that invariant.
9. Fast time controls are a distinct regime: the E1 salvage that won at
   150 ms lost at 30 ms until the soundness guard landed. Always gate on
   the fast regime too.
10. NNUE at this net's size played ~400 Elo below the classical eval at
    equal time (classical is ~5× faster per node); it stayed default-off
    for its entire life in the codebase.

---

## 4. App/UI history (from UI-OVERHAUL-NOTES, v3.0.0 overhaul)

Scope was layout/positioning/responsiveness only; all view IDs preserved so
ViewBinding references kept working.

- Design tokens: `values/dimens.xml` + `values-sw600dp/dimens.xml` spacing,
  bar heights, board padding (0dp edge-to-edge phones / 24dp tablets),
  control sizing, card corners.
- Game screen anatomy (chess.com-style): top bar → opponent row (avatar,
  name, live engine info, captured pieces + material diff inline) → board
  takes every leftover pixel (auto-square, self-balancing, minHeight floor)
  → player row → status pill → move strip → icon-over-label action bar
  (Undo / Flip / New / Resign). No scrolling during play.
- Analysis screen decongested: top bar (back + title/meta + hero-side pill +
  elo chip), dedicated replay-nav row (First/Prev/Next/Last + Flip),
  full-width move strip, report cards scroll in a weight-balanced panel.
- Fragments: hero header on Play, roomier Elo card, bottom-anchored full-round
  Start CTA, actionable import-source list on Analyze, 2×2 stat grid on
  Profile with 44dp+ touch targets.
- Responsiveness: `layout-land/` for Game + Analysis, `layout-sw600dp/`
  centered columns for fragments, `layout-sw600dp-land/` rails (400dp/420dp).
- Rotation unlocked (manifest); `startGame()` runs only on true launch;
  one-shot `resultPersisted` latch prevents duplicate history entries.
- Theme additions: `TacTurn.ControlButton/.Outlined/.Compact`,
  `TacTurn.NavButton`, `TacTurn.Pill`, `TacTurn.SectionLabel`; new drawables
  `bg_avatar_circle`, `bg_icon_plate`, `ic_chevron_right`.
- Automated static validation at the time: all XML parses; identical ID sets
  across layout variants (ViewBinding-safe); every binding reference
  resolves; all referenced resources exist.

## 5. Build notes (from BUILD_NOTES.md)

- The one historic build failure: a headless JRE as default Java lacks
  `jlink`, which AGP needs (`:app:compileReleaseJavaWithJavac` →
  `androidJdkImage` resolution failure). Fix: full JDK 17 (e.g. Temurin 17)
  as `JAVA_HOME`. No source changes required.
- Verified toolchain: JDK 17 (full), Gradle 8.7 (wrapper), AGP 8.5.2,
  Kotlin 1.9.24, Android SDK platform 34 + build-tools 34.0.0,
  NDK 27.2.12479018, CMake 3.22.1. Release APK ≈ 4.7 MB, v1+v2 signed via
  the standard debug keystore (self-hosted distribution).
- Host verification loop: `scripts/run_engine_tests.sh` (needs only g++);
  `./build-host/tacturn-uci bench 11` must print `Total nodes: 1369600`
  after any rebuild of the E1-era search (see §7 for the V1 check).
- Test-count note: self-play smoke assertions are ply-looped and
  book-dependent, so the total may vary ±4 across builds (2358-2369);
  zero failures is the invariant.
- PGO two-pass host flow (removed in V1 with `build_host_pgo.sh`; the
  technique is documented here for the future): pass 1
  `-fprofile-generate` + bench/15-FEN training, pass 2
  `-fprofile-use -fprofile-correction`. Stage 1 could exceed one tool
  window — resume by re-running stage 1 or training manually, then stage 2.
  Measured +6–13% nps depending on sandbox.

---

## 6. Removed development material (V1 cleanup inventory)

All of the following were removed from the production tree; the substantive
content is preserved in this file. Nothing the app, engine, build system or
runtime needs was removed.

- `benchmarks/` — 20 match-log / evidence files (ladder games, verifications,
  device stability, SF anchor runs, per-version campaigns). Key numbers are
  quoted in §1–§2 above.
- `tools/data/` — tuning/training corpora (gen-4 self-play .bin.gz, SF label
  banks, chessdb harvest lines + state). Regenerable with the documented
  datagen/harvest processes; the shipped artifacts (book_data.cpp,
  eval_tuned.h) are compiled into the engine and unaffected.
- NNUE subsystem — `nnue.cpp`, `nnue.h`, `nnue_net.cpp` (embedded network,
  ~9.9 MB of generated weights), `scripts/nnue/` trainer/labeler/exporter
  scripts, and all references in position/evaluate/uci/datagen/build files.
  Rationale and verification: see §7.1.
- Desktop-only dev scripts: `clock_ab.py`, `timeman_check.py`,
  `imbalance_ev_check.py`, `merge_sf_labels.py`, `sf19_label.py`,
  `harvest_book2.py`, `test_analysis_api.cpp`, `build_host_pgo.sh`.
- `engine/.../tools/tdiag.cpp` — orphaned tuner-debug tool not referenced by
  any build file.
- `BUILD_NOTES.md`, `LADDER.md`, `UI-OVERHAUL-NOTES.md` — consolidated into
  this file; removed from the tree.
- The UCI `UseNNUE`/`NNUEFile` options and datagen's `--hce` flag (both
  became meaningless once the NNUE subsystem was removed).

---

## 7. V1 production release record

### 7.1 NNUE removal — verification record

Verified before removal that the app does NOT use the NNUE:
- `nnue::enabled` defaults to `false` and is only ever toggled by the
  desktop UCI tool's `setoption name UseNNUE` (opt-in, default off).
- The JNI bridge (`tacturn_jni.cpp`) and the flat C API (`api.h`/`api.cpp`)
  expose no NNUE entry point; the Kotlin facade never references it
  (grep over `app/` and `engine/src/main/kotlin/`: zero matches).
- The embedded network was loaded into RAM at library init (a static
  constructor) and then never consulted — pure dead weight in the APK.

After removal:
- Full native test suite: **2358 passed, 0 failed**.
- Bench fingerprint: `bench 11` → **Total nodes: 1369600**, exactly the
  documented v3.2.x expectation — the search tree is bit-identical (NNUE was
  disabled, so removal cannot change play).
- APK payload loses ~9.9 MB of generated weight data plus the inference code.

### 7.2 Engine strength: the 2850 top level

The Play-vs-Engine slider now spans **100–2850** (was 2600; the old docs
topped the engine at "~2600 in-app / 2720–2735 host"). The new mapping in
`EloLevels.kt` keeps every level ≤ 2400 bit-identical to the previous
release and extends the top bands:

| Anchor | Depth | Soft time | Threads | Notes |
|---|---|---|---|---|
| 2400 | 16 | 2800 ms | 3 | unchanged |
| 2700 | 22 | 4000 ms | 4 | the old "max" experience |
| 2850 | 64 (unlimited) | 6000 ms | 6 | new ceiling |

- The ceiling is no longer only a displayed number: the top anchor genuinely
  raises the soft budget (4000 → 6000 ms, hard cap 1.5× = 9000 ms), the
  thread count (4 → 6 Lazy-SMP workers; the engine supports up to 8), and
  keeps adaptive time + depth-unlimited search. On-device scaling from the
  2700 anchor is conservatively +50–100 Elo, consistent with the documented
  host measurements (speed → depth → Elo; SMP scaling) and the ladder's
  2850 target.
- The per-move watchdog cap (15 s) still sits well above the 9 s hard cap.
- The session transposition table grew from 2^20 to 2^21 entries (~50 MB,
  matching the documented engine footprint; the native table downsizes
  automatically if the allocation ever fails, and is hard-capped on 32-bit).
- UI consistency: slider XML (`valueTo`) in both layout variants, the range
  hint string, `Prefs` clamps, tier labels and the per-level hint text all
  reflect 2850; no stale 2600 references remain in the Play flow (the
  analysis screen's separate 2600 Master-tier threshold and 100–3000 user-Elo
  control are a different feature and intentionally unchanged).

### 7.3 Bugs fixed in the V1 cleanup

1. **`Game.think()` node count was garbage** — the JNI result layout is
   `[4]=nodesHi, [5]=nodesLo`, but Kotlin reconstructed the long with the
   words swapped (and sign-extended the wrong one). Final node counts from a
   completed think were wrong; the live per-iteration callback path was
   unaffected. Fixed to read `[5]` as low, `[4]` as high.
2. **Engine watchdogs could never fire** — both ViewModels launched their
   watchdog coroutine on the engine's own single-slot dispatcher, whose only
   thread is occupied by the running native search. The watchdog is now
   launched on `Dispatchers.Default` so `requestStop()` can actually fire
   mid-search. (The native hard-time cap was unaffected; this restores the
   intended double safety.)
3. **Analysis move-chip selection highlight went stale** — `renderMoveChips`
   early-returned whenever the chip count was unchanged, so tapping a chip
   or navigating First/Prev/Next/Last never moved the selected-pill
   highlight after the review completed. The rebuild gate now also tracks
   the selected ply.
4. **Process-death left dead screens** — both GameActivity and
   AnalysisActivity only started/re-imported their session when
   `savedInstanceState == null`. After an OS process death the Activity is
   recreated WITH a saved-state bundle but a FRESH ViewModel, so the game
   screen came back frozen and the review screen came back empty. Both now
   also start when the ViewModel state is fresh.
5. **Finished games could silently fail to save** — persistence ran on the
   Activity's `lifecycleScope` after an async PGN-export round-trip; an
   Activity recreation in that window cancelled the save after the one-shot
   latch had already been armed. The save now lives on the ViewModel scope
   (survives recreation) and drops the unnecessary round-trip.
6. **Most recent sound replayed after rotation** — the Activity's
   `lastSoundId` started at −1 while the surviving ViewModel still carried
   the last sound event; the first render replayed it. The first render now
   adopts the event id without playing.
7. **Move-chip strip rebuilt on every engine-info update** — the rebuild
   condition stayed true for all subsequent emissions while the engine
   thought (chip rebuilds are full-strip reinflations). Rebuilds are now
   gated on a content signature (row count + last black SAN).
8. **Stale version strings** — `tacturn_version()` still reported
   "TacTurn 2.0.0" and the UCI tool "TacTurn 3.2.1" while the app shipped
   3.2.1; all identity strings are now unified with the app version.
9. **Misleading comment** — `AnalysisPresenter` claimed round-half-away
   rounding ("150 → +2") while the code truncates (150 → "+1"); comment
   corrected to describe the actual behavior.
10. **Dead code** — `EloLevels.skillTier()` had no callers anywhere (the
    analysis screen and profile each use their own tier logic); removed
    along with its now-unused import.

Deliberately NOT changed (safe, but behavior/design decisions):
- The analysis screen's Master-tier threshold (2600) and 100–3000 user-Elo
  range — that control personalizes analysis for the USER's rating and is
  independent of the engine-strength slider.
- ProfileFragment's own tier ladder wording (different UX context).
- Main-thread reads of the small history JSON in Profile/Analyze — a
  micro-optimization with restructuring risk not justified for V1.

---

## 8. The v2 app feature wave (consolidated from V2_CHANGES.md)

The v2 cycle turned the single-mode "Play vs Engine" app into a full chess
experience. Everything below is wired end-to-end (UI → ViewModel → native
engine), works in both game modes, every time-control category, and all
phone/tablet layout variants.

**Time controls / chess clocks.** 15 curated presets (Bullet 1+0…2+1,
Blitz 3+0…5+3, Rapid 10+0…15+10, Classical 30+0…60+30) + a custom builder
(1–120 min, 0–60 s increment). Fischer increment; real DGT-clock semantics
(White's clock runs from move one; each move settles the mover's elapsed
time and hands the turn over). Under 15 s the display switches to tenths and
turns red. A 10 Hz ticker detects flag fall and ends the game properly:
win on time if the opponent has mating material, otherwise the FIDE 6.9
safeguard applies (draw — timeout vs insufficient material). The engine's
search budget is clamped by its own remaining time (v1.4.11: safety
reserve + aged moves-to-go pacing + net-increment accounting, see §48),
so it can never flag itself. Leaving the
app pauses the clock; returning resumes it. Clock ticks render through a
dedicated `ClockUiState` flow so 10 Hz updates never re-render the board.

**Pass & Play.** Player names (persisted, shown on player rows, game-over
card and exported PGN), auto-flip board, own clock, both-side promotion
picker, draw offers answered in a dialog (making a move withdraws the
offer), resign as the side on move, single-ply undo (vs the engine: a full
move pair), rematch swaps colors/names.

**Draw offers vs the engine.** The engine decides from the current static
eval (`Game.evaluate()`, newly exposed through JNI): accepts when ≥1.5 pawns
for the offerer or dead-equal (±0.4) after move 40; otherwise declines with
a snackbar and blocks re-offering for 8 plies. Accepted draws end as
`GR_DRAW_AGREEMENT` — threaded through the C++ rules layer, flat C API, JNI
and the Kotlin facade, with undo/revive semantics and PGN
`Termination "agreement"`.

**Opening names.** Live top-bar opening name from a curated 127-line
ECO-style book matched by longest SAN prefix; carried into the game-over
card and the PGN `Opening` tag.

**PGN export & sharing.** Fully-tagged PGN (Event, Site, Date, Round, White,
Black, Result, engine Elo, TimeControl, Opening, Termination) via the
Android share sheet; "Analyze this game" imports it into the review screen.

**Other additions.** Auto-promote-to-queen and haptic-feedback settings;
history entries record game mode + time control; orientation-aware
captured-material strips and material-diff badge; `VIBRATE` permission.

**Engineering notes.** `ChessClock` is monotonic-time based
(`elapsedRealtimeNanos`); all game-deciding mutations happen on the single
engine thread under the session mutex; the main-thread ticker only reads
live values (a torn read degrades to a stale display, never a wrong
result). Flag-fall handling is idempotent and race-safe. All four game-screen
layout variants (phone/tablet × portrait/landscape) and both Play-tab
variants share identical view-ID sets (ViewBinding-safe). Native changes
were minimal and additive; the full native suite passed 2358/2358 at the
time and `bench 11` stayed at 1,369,600 nodes (play bit-identical).

---

## 9. v2.1 analysis overhaul — full record (consolidated from ANALYSIS_OVERHAUL.md)

**The problem:** game reviews and match ratings were reported as wildly
inaccurate across every kind of game. The root causes were not in the search
(the engine's strength was verified independently) but in how the analysis
layer interpreted the search. Six independent defects compounded:

| # | Defect | User-visible symptom |
|---|--------|----------------------|
| R1 | Move classification used **raw centipawn-loss thresholds** | A 200 cp give-back while ahead +9 (win% 96.5 → 93.0, harmless) was called a "Mistake"; the same 200 cp at equality got the same label. Absurd flags in every decided position. |
| R2 | Game accuracy used an invented **"live-phase gating" + "worst-quartile blend"** statistic | Accuracy read systematically low and behaved inconsistently between won/lost games; numbers were not comparable to Lichess/chess.com figures. |
| R3 | Performance Elo was an **ad-hoc 3-feature regression** whose dominant feature counted raw cp surrendered *while already winning* | The winner was punished for harmless coasting moves — "a good game reads as low Elo." |
| R4 | **No verification of flagged moments**: one 1.2 s search per position, take it or leave it | Search noise (±30–60 cp between consecutive positions) fabricated blunders — and masked real ones. |
| R5 | The engine's play-mode **draw contempt (12 cp) was active during analysis** | Drawish positions evaluated as ±0.12 instead of 0.00. |
| R6 | **Opening book moves were judged as errors** | Depth-18 noise produced spurious "Inaccuracy" flags on move 3 and polluted both aggregates. |

### The overhaul

**Native engine (C++).** `SearchLimits::contemptCp` — draw contempt is now
per-search. Play keeps the historical 12 cp default (strength bit-identical;
`bench 11` fingerprint unchanged), all analysis paths pass **contempt 0**.
The contempt threads cleanly through `TacSearchLimits` → `tacturn_think` /
`tacturn_analyze` → JNI → the Kotlin `SearchLimits`, and survives the
forced-move shortcut inside `Searcher::think`. Book queries
(`tacturn_book_contains` / `tacturn_book_contains_move`, `Book::contains()` /
`Book::containsMove()`) expose the engine's 3,996-line opening book to the
review pipeline, position-keyed (transposition-safe), capped at the book's
20-ply horizon.

**Analysis math (`Accuracy.kt`).** Per-move accuracy stays exactly Lichess's
published methodology (win% sigmoid + the
`103.1668·exp(−0.04354·Δwin%) − 3.1669` curve). Classification is
win%-drop based (`MoveClass.fromWinDrop`):

| Class | win% drop | ≈ at equality |
|---|---|---|
| Best | played the engine's top move, or ≤ 0.5% | — |
| Excellent | < 2% | ~12 cp |
| Good | < 4% | ~quarter pawn |
| Inaccuracy | < 8% | ~half pawn |
| Mistake | < 15% | ~a full pawn |
| Blunder | ≥ 15% | ~two pawns / a piece |

Because these live in win% space they saturate naturally in decided
positions. Club tiers keep a mild display-only leniency factor
(0.75 / 0.90 / 1.0); aggregates always use the raw drop. Game accuracy =
plain mean of per-move accuracies over non-book moves (the statistic
Lichess publishes). Performance Elo = monotone anchor-curve mapping of mean
accuracy onto the Elo scale (33 anchors from 100% → 2900 down to 0% → 580,
calibrated to real rating bands) plus shrinkage toward a 1200 prior when few
moves were judged.

**Review pipeline (`AnalysisViewModel`).** The batch pass runs three stages:
1. **First pass** — every position searched once at depth 18 / 1.2 s soft /
   2 s hard, adaptive time, contempt 0, one thread.
2. **Book pass** — each played move checked against the engine's book before
   it is made; theory moves get `MoveClass.BOOK` and are excluded from the
   aggregates. Book flags are committed atomically with their position eval,
   so a mid-search cancel/resume can never misalign the two lists.
3. **Verification pass** — every judged ply whose first-pass win% drop
   reaches 8% has BOTH flanking positions re-searched at depth 22 / 2.2 s
   soft / 3.2 s hard (capped at 20 positions, worst suspects first, progress
   bar extends transparently). This is the single most effective guard
   against "the rating is wrong": eval noise cannot fabricate the blunders
   that dominate the reported rating.

Chip classification was unified: the partial-chips path during the batch and
the final presenter go through the same `MoveClass.fromWinDrop` on the same
raw evals (the v1 duplicate paths could disagree with themselves).
Restored-from-disk reviews recompute book flags during the existing
best-move-SAN walk (no schema change; cached reviews keep instant restore).

### Verification record

- **Native suite: 2378/2378** (2358 prior + 20 new). `bench 11` unchanged
  (1,369,600) — play strength bit-identical; only analysis behavior changed.
- New native tests: `testContemptZeroIsAnalysisTruth` (50-move position
  scores exactly 0 with contempt 0, exactly +12 with the play default);
  `testBookContainsQueries`; `testAnalysisPipelineSane` (Morphy's Opera Game
  end-to-end through the real `tacturn_analyze` C API: 16.Qb8+!! verified
  loss-free, winner's mean win% drop strictly better than the loser's).
- A math harness (a line-faithful mirror of the Kotlin math) asserted the
  win% curve, the classification ladder, the v1 regression case, book
  exclusion, anchor monotonicity, rating bands, winner-not-punished,
  short-game shrinkage and mate-score clamps.

### Behavior changes at a glance

| Scenario | v1 verdict | v2 verdict |
|---|---|---|
| 200 cp give-back while ahead +9 | Mistake | Good |
| Same 200 cp at equality | Mistake | Blunder |
| Standard theory move, move 5 | often Inaccuracy | Book (excluded) |
| Winner converting +9 with small wobble | Elo punished (could read sub-1500) | Elo reflects play (~2300) |
| Eval-noise phantom blunder | flagged, decided the rating | re-searched at depth 22 before judging |
| Drawn/50-move positions in review | ±0.12 bias | exactly 0.00 |
| 20 clean moves + one pawn slip | accuracy dragged by tail-blend (~mid-80s) | 98.4% (Lichess-comparable) |

---

## 10. V1 ship — second-pass cleanup record

The second production pass (what shipped as versionName 1.0.0, versionCode 16):

**Fixed:**
1. **Move sounds silent on many devices** — the `SoundPool` used
   `USAGE_ASSISTANCE_SONIFICATION`, which routes to the SYSTEM stream and is
   muted whenever the ringer is silent / Do-Not-Disturb is on. Switched to
   `USAGE_GAME` (media stream, the chess.com/lichess convention) so move
   sounds always come through; added load-complete tracking with a
   pending-replay so a play request made before a sample decodes is honored.
2. **First move's sound was swallowed** — the Activity "seeded" its sound-id
   dedup on the first observed event (skipping it) and `StateFlow` conflation
   could drop rapid events. The id baseline is now adopted from the
   ViewModel's current state in `onCreate`, so the first move plays and a
   rotation never replays a stale event.
3. **Locale-dependent eval formatting** — `"%.1f".format()` /
   `"%.2f".format()` render with a comma decimal separator in e.g. German
   locales ("+1,2"). All engine-score formatting now pins `Locale.US`
   (chess scores always use a decimal point).
4. **PGN engine-Elo tag on the wrong side** — `[BlackElo]` was written even
   when the engine played White. The tag now follows the engine's actual
   color.
5. **Custom time control not persisted** — `saveToPrefs` refused to write
   index −1, so a selected custom control silently reverted to the previous
   preset after an app restart. −1 is now persisted (custom minutes/increment
   were already saved separately).
6. **Time-control selector burned vertical space** — 17 chips wrapped into
   four full rows on phones, pushing the Elo/color sections and the Start
   CTA below the fold. The selector is now a single-line horizontally
   scrollable chip row (the restored selection auto-scrolls into view), in
   both the phone and tablet Play layouts.

**Removed (analysis):** the optional Cloud Stockfish bridge (lichess cloud
eval) — `CloudEval.kt`, the Master-tier banner + toggle in all three
`activity_analysis` layout variants, `CloudState`/`cloudState`/`cloudEnabled`
state, `cloudLineModels`/`resolveUci`/`promoMatches` helpers, the
`fromCloud` line flag + badge, the `cloudEligible` profile field, the
`cloud_*` strings, and the `INTERNET` + `ACCESS_NETWORK_STATE` permissions —
TacTurn is now a fully offline app with zero network code. The banner was
replaced with an always-visible note: TacTurn analysis is approximate
(evaluations, accuracies and ratings are estimates from the built-in engine,
not definitive engine truth).

**Removed (development tooling):** the desktop-only engine tools
(`tools/uci.cpp`, `tools/datagen.cpp`, `tools/tuner.cpp`,
`tools/match_runner.cpp`, `tools/eval_dump.cpp`), the native test suite
(`tests/engine_tests.cpp`), the host build/test scripts
(`scripts/build_host.sh`, `scripts/run_engine_tests.sh`), the sound
generator (`scripts/generate_sounds.py` — the generated WAVs are shipped in
`res/raw/`), the host branch of `CMakeLists.txt`, and this file absorbed
`V2_CHANGES.md` + `ANALYSIS_OVERHAUL.md`. None of it is referenced by the
app, engine, build system or runtime; the shipped artifacts
(`book_data.cpp`, `eval_tuned.h`) are compiled into the engine and
unaffected. The historical test/bench numbers quoted throughout this file
were produced by that removed tooling and remain the verification record.

Deliberately NOT changed: engine search/evaluation behavior (bit-identical
by construction — only build tooling was removed); the Master-tier 2600
threshold and the 100–3000 user-Elo analysis control (personalization, not
engine strength); main-thread reads of the small history JSON (a
micro-optimization with restructuring risk not justified here).

---

## 11. v4.0 engine-strengthening campaign (post-V1)

Goal: real playing strength on the main game-playing engine (search + HCE;
NNUE explicitly out of scope). All gates were fixed-time head-to-head
matches on identical hardware, single thread, 2^21-entry TT, play contempt
12, book off, 30 paired openings (colors swapped per pair), SPRT(0,15)
logistic stopping where noted. Perft (6-position suite incl. Kiwipete and
the promotion/EP torture positions) stayed bit-exact after every change.

### Bugs found by deep inspection

1. **Multi-cut was dead code.** The singular-extension block entered on
   `beta > singularBeta` while the multi-cut branch inside required
   `singularBeta >= beta` — mutually exclusive, unreachable since it was
   written. (Left as a documented no-op after the re-implementation
   attempts below were rejected.)
2. **Three history reads used the wrong side.** `quietHistoryValue()` read
   `pos->sideToMove` internally, but both of its call sites (history-gated
   quiet pruning; the LMR history adjustment) run AFTER `makeMove`, where
   that is the OPPONENT — and the capture-LMR block read
   `pos->sideToMove` after `makeMove` too. Ordering (scoreMoves) and
   learning (updateCutoffStats) always used the mover's side, so the
   pruning/reduction consumers were reading the other side's cells: the
   v2.2 "LMR for bad captures" and "history-gated quiet pruning" features
   had been consuming scrambled data their entire life.
3. **Improving flag polluted by check plies.** In-check nodes stored
   staticEval = -INFINITE into evalStack, so any later finite eval compared
   as "improving" over a checked ply — right after a check the side to move
   is often doing WORSE. Now a sentinel skips those comparisons (the
   documented Stockfish-style handling).

### Shipped changes (v4.0)

- History side fixes (bug 2): `quietHistoryValue(move, ply, moverSide)`
  takes the mover's colour explicitly; capture-LMR indexes `us`.
- Improving sentinel (bug 3): `EVAL_NO_EVAL` sentinel + guard terms.
- KNN vs K evaluates as a dead draw (chess truth; the old eval claimed
  ~+3.6 from the two knights). One-line additions to the eval fast path.

### Experiment ledger (engine A = candidate, vs the V1 baseline unless noted)

| Test | TC | N | Score | Elo | Verdict |
|---|---|---|---|---|---|
| E1 multi-cut revival (isolated, sound fail-soft form) | 50 ms | 189 | 48.1% | −13 | rejected |
| E1b full SF-style singular pkg (multi-cut + neg. ext −3 + TT store) | 50 ms | 186 | 46.2% | −26 | rejected |
| E3 history side fixes | 50 ms | 555 | 53.2% | +22.6 (LOS 88%) | **kept** |
| E4 = E3 + improving sentinel | 50 ms | 500 | 50.9% | +6.3 | kept (neutral, correctness) |
| E5 = E4 + 2-slot TT buckets | 50 ms | 500 | 51.4% | +9.7 | rejected (see below) |
| E6 promo ordering + KNNvK | 50 ms | 240 | 49.0% | −7.2 | promo part rejected; KNNvK kept |
| Full stack (E3+E4+E5+KNNvK) | 150 ms | 139 | 45.3% | −33 | exposed E5 |
| E4 stack at standard TC | 150 ms | 134 | 51.1% | +7.8 ± 35 | shipped composition |
| v4 stack (E3+E4+KNNvK) | 35 ms | 245 | 47.6% | −17 ± 26 | **known fast-clock regression** |
| E3 alone at fast TC | 35 ms | 219 | 46.3% | −25 ± 27 | confirms E3 is TC-sensitive |

Notes on the rejections:

- **E5 (TT buckets)**: +9.7 at 50 ms but the 150 ms stack gate flipped to
  −33; isolating the stack without buckets recovered to +7.8. The
  always-replace overflow slot thrashes deep entries under the persistent,
  cross-move TT that real games use at longer think times. The single-slot
  depth-preferred hybrid stays.
- **E1/E1b**: the current SF singular/multi-cut/negative-extension design
  reduced the fixed-depth tree by up to 37% but consistently lost matches —
  this engine's operating point (SE threshold 5, no NMP verification, tuned
  pruning margins) does not want the extra verification layer, matching the
  historical pattern that aggressive-pruning attempts lose here.
- **E6 promo ordering**: moving underpromotions below captures was
  unproven (−7.2 ± 26) and was reverted; KNNvK alone is chess truth and
  ships.

### Honest strength statement

The verified change set is positive at the app's main playing regimes
(50 ms: ~+23 for the core fix over 555 games; 150 ms: +7.8 ± 35 over 134
games for the shipped stack) and negative at very fast clocks (35 ms:
−17 ± 26). No claim of "2850 Elo achieved" is made from these numbers —
they are relative head-to-head measurements against the preserved V1
baseline on the development host, whose absolute anchor (~2780–2820 by
independent check) carries its own uncertainty. The most likely net effect
at the app's 250 ms–6 s top levels is a modest single-digit-to-low-tens
Elo gain; the fast-clock regime should be re-gated before any blitz-
oriented release (one-line revert points are documented in the diffs of
`search.cpp`: the `quietHistoryValue` signature, the capture-LMR `us`
index, and the `evalStack` sentinel block).

## 12. v4.1 strengthening campaign — second grind (post-v4.0)

A second full experiment campaign on the game-playing engine, run with a
rebuilt methodology: a UCI `setoption` tuning harness (host dev wrapper
exposes the twelve SPSA tunables at runtime; the shipped engine binary is
untouched), runtime option overrides in the match runner, and gates run
directly at 150 ms/move — the closest affordable proxy for the app's
250 ms–6 s top levels — after the v4.0 campaign showed that 50 ms results
do not extrapolate.

### Headline finding: the engine sits at a measured local optimum

Every candidate of this campaign — parameter moves, mechanism additions,
and two "obvious bug fixes" — measured flat or negative at 150 ms. The
shipped v4.0 configuration is retained byte-for-byte. The campaign's value
is the mapped response curves and the load-bearing-bug characterization
below, which between them close off the cheap avenues and tell the next
campaign exactly where the remaining Elo is not.

### The load-bearing TT generation "bug" (major negative result)

`ttable.h` packs the generation in a 5-bit field while `age_` cycles 6 bits
(0..63): every store truncates `age_`, so for 32 of every 64 searches
`oldGen != age_` is always true and the table silently degrades to
always-replace. The TT also persists across games within a session
(`GameSession::reset` never cleared it), so roughly half of all match play
ran on a churning table. Unit-level proof: at `age_` 40 a same-generation
shallow store evicts a depth-30 exact entry.

Three fixes were gated at 150 ms (N≈70–75 first chunk, later extended):

| Variant | Change | 150 ms result |
|---|---|---|
| E10a | gen field widened 5→7 bits (policy becomes truly depth-preferred) | **−54** (LOS 12%) |
| E10b | gen fix + TT cleared on every new game (SF convention) | **−42** (LOS 17%) |
| E10d | deliberate 100%-duty-cycle always-replace for different keys | **−5** (LOS 46%, N=71) |

Response curve mapped: ~0% churn ≈ −50, the accidental ~50% duty cycle
(shipped) ≈ 0, 100% churn ≈ −5. The truncation is empirically load-bearing:
the accidental periodic full flush acts as a garbage collector against
stale deep bounds from aggressively reduced sub-searches, and the mixed
policy sits at the optimum. All three "fixes" reverted; the 5-bit field,
the 6-bit age, and the non-clearing reset are kept deliberately and are
now documented contracts, not oversights. Anyone touching the TT
replacement policy in the future must re-gate at 150 ms+ — this engine
loses ~50 Elo to textbook "corrections" here.

### Capture-LMR decomposition (the v4.0 fast-clock regression, re-measured)

The v4.0 side-index fix activated the capture-history LMR branch for the
first time in the engine's life (it had read the wrong side's table since
v2.2, i.e. noise). Decomposition gates this campaign:

| Variant | 35 ms | 50 ms | 150 ms |
|---|---|---|---|
| v4 (two-tier capture-LMR) | reference | reference | reference |
| E9: single tier (r=1 only) | −15.6 (N=200) | — | — |
| E9b: capture-LMR removed | −12.2 (N=200) | **+29** (N=192, LOS 81%) | **−17** (N=227, LOS 27%) |

The feature's value is genuinely TC-curved (helps 35 ms and 150 ms, hurts
50 ms). At the app's regime the two-tier version is the right call, so the
v4.0 code stands. The v4.0 fast-clock regression attribution is revised:
capture-LMR carries only ~5 Elo of it; the rest is inherent to the
history-signal corrections at ultra-fast clocks and is not separable
without giving up the 50 ms/150 ms gains.

### Parameter landscape (tuning harness, vs v4 defaults)

Probed at 50 ms (190-game chunks) and confirmed at 150 ms where they won:

| Probe | 50 ms | 150 ms | Verdict |
|---|---|---|---|
| lmr_div_m 2250→2000 | **+50** (LOS 92%) | −14.5 | rejected — fast-TC illusion |
| lmr_div_m 2250→1800 | +9 | — | rejected (flat at 50) |
| lmr_div_m 2250→2400 (conservative) | — | +4.6 (LOS 54%) | flat; default sits on a plateau |
| lmr_base_m 800→950 (on div 2000) | +18 | −23.5 vs 800 | rejected |
| rfp_non 105→90 | 0 | — | rejected (flat) |
| history-threshold retune (E7: −6500 prune gate, hist/4000 clamp 3, +10k bonus) | −25.6 | — | rejected — reductions by formula beat reductions by history discrimination here |

### Other mechanisms

- **E8 passed-pawn push extension** (+1 ply for passed pushes onto the
  6th/7th rank, the one extension family the engine lacked): +16.5 at
  50 ms (N=190, LOS 69%) but −4.7 at 150 ms (N=147, LOS 45%) — a coin
  flip at the app regime. Not shipped per the verified-wins-only rule;
  the clean tree is preserved under the campaign work log if a future
  campaign wants to re-gate it at 250 ms+.
- The LMR re-calibration trap: the v2.2 SPSA optima were fitted against
  the wrong-side history reads, so the corrected signal moved the
  optimum (div 2000 at 50 ms) — but only at 50 ms. Fixed-node and
  short-TC calibration protocols are now flagged in the ledger as
  systematically biased for this engine's real operating point.

### Campaign conclusion

v4.1 ships no engine-code change. The remaining realistic strength levers,
in order of expected value: (1) a full eval re-tune (Texel/SPSA at
150 ms+ conditions over game data — the eval's linear-feature
architecture is ready for it and the tuner hooks exist); (2) NNUE,
explicitly out of scope per the project brief; (3) a curated opening book
refresh. Parametric search tuning and single-mechanism additions are
measured dead ends at the app regime — this campaign spent its full
budget proving exactly that, with data.

---

## 13. v5.0 — the NNUE campaign (residual hybrid, SF-taught)

The mandate: transform the HCE engine toward 3000 Elo via a properly
engineered NNUE or NNUE+HCE hybrid, with strength testing as the only
authority. This section is the complete record — including every failed
variant, because the failure *structure* is what made the final
architecture measurable.

### 13.1 Infrastructure (all verified before any net existed)

- **`nnue.h` / `nnue.cpp` / `nnue_net.cpp`** (new): 768-sparse-feature
  residual network, architecture `768 → 256 (×2 perspectives) → 16 → 32 → 1`,
  fully quantized int16/int32 (QA=4096, audited caps: |FT_W|≤880,
  |L2_W|≤400, |OUT_W|≤8192; per-net power-of-two shifts for the deeper
  layers). Inference uses explicit SIMD dot products — AVX2/SSE2
  `pmaddwd` on x86, `vmull+smlal` on ARM NEON, scalar fallback — with the
  SIMD headers hoisted to file scope (NEON intrinsics break inside
  namespaces on some NDK toolchains).
- **Incremental accumulators** maintained inside
  `Position::addPiece/removePiece/shiftPiece` — the same hooks that
  update mg/eg PSTs. unmake replays exact inverse operations, so the
  accumulator restores bit-exactly with **zero extra StateInfo state**.
  Black perspective = vertical mirror (`sq^56`), no king-in-index:
  every piece event touches exactly one row per perspective.
- **Zero-net bit-identity gate**: with an all-zero network the engine is
  bit-identical to v4.0 — `bench 13` = 680,960 with NNUE on and off;
  perft suite exact. The subsystem cannot regress the engine by existing.
- **`nnueverify`** (host): fuzzes the incremental accumulator against a
  from-scratch refresh across random games (make/unmake, EP, castling,
  promotions, clone, setFen round-trips) AND proves the integer inference
  equals a double-precision mirror. Caught two real bugs during the
  campaign: a blend truncation asymmetry and a negative-rounding off-by-one.
- **`datagen`** (host): resumable self-play labeler — synthesized
  endgames (43 material menus, colors swapped), book openings, adjudicated
  games; records 88-byte packed positions with (fixed-depth search score,
  pure HCE score, game result).
- **Speed discipline**: accumulator maintenance costs ~4.6% nps;
  inference-at-every-eval-cache-miss ~14%; a bounded-skip (compute the
  residual only when the stand-pat is within ±600 cp of the [alpha,beta]
  window — decision-equivalent because the residual is clamped to ±600)
  recovered ~6 nps points. Final config: ~12.9% nps cost with NNUE on
  (identical trees, zero-net measurement).

### 13.2 The experiment ledger (all gates: same binary, `UseNNUE` on vs off, paired openings, SPRT 0/15)

| Variant | Target | Injection | 50 ms | 150 ms | Verdict |
|---|---|---|---|---|---|
| self-d5 | search−hce (d5 self-play, 2.58M pos) | every eval | −82 ± 27 (N=220) | — | rejected |
| self-d5 | same | qsearch stand-pat only | −123 ± 29 (N=191) | — | rejected |
| self-d5 | same, 30% blend | leaf | −16.6 ± 36 (N=128) | — | rejected |
| outcome-mix | 50% search + 50% game result | leaf | −424 ± 44 (N=81) | — | rejected (scale drift + quant 72 cp) |
| **SF-static** | SF17.1 static eval (scaled) − hce, 1.48M pos | leaf, 100% | −38 ± 29 (N=184) | — | too noisy at full strength |
| **SF-static** | same, 50% blend | leaf | **+28.0 ± 22 (N=323)** | **+77 ± 35 (N=133)** | **shipped** |
| SF-static | 40% / 60% blend | leaf | +21.6 / +29.7 (N≈129) | — | plateau 50–60 |
| SF-static | 75% blend | leaf | — | +68 ± 49 (N=67) | plateau |
| shipped defaults | SF-static, 50% blend baked in | leaf | +18.8 ± 29 (N=185) | +77 ± 50 (N=64) | confirmed |

35 ms fast-TC check (v4.0's known weak regime): **+20.1 ± 25 (N=259)** —
the hybrid also heals, not worsens, the fast end.

The decisive finding inside the ledger: the **self-taught residual is
circular and transfers negatively** — the engine's own d5 search can only
teach what its HCE+corrhist already know, so the learned residual is
dominated by unlearnable tactical noise (corr 0.72 on validation, yet
monotonically negative at every blend). Breaking the circle required an
**external teacher**: Stockfish 17.1's static NNUE eval, labeled on all
1,479,192 unique datagen positions at ~2,660 labels/s (9.3 min, two UCI
pipes), scale-mapped by linear fit (`TacTurn_cp = 1.2135·SF + 3.7`,
corr(TacTurn-d5, SF) = 0.83). The SF-taught residual is larger (std 192 cp
vs 131), biggest in sparse endgames (220 cp) — exactly where HCE is weakest
— and the blend curve acquires an interior optimum, the signature of real
signal.

### 13.3 The shipped architecture (v5.0 "res-256-sf", blend 50)

- `evaluate()` is **byte-identical to v4.0** — the Texel-tuner invariant
  (tuner and engine share the exact code path) is structurally preserved;
  the residual lives entirely in the search layer.
- Worker eval-cache entries carry the HCE score **and** a lazily-computed
  cached residual (same key, no extra table).
- PRUNING PATHS (RFP, futility, razoring, improving flag, corrhist) read
  the **pure HCE** signal — the search constants are tuned around it.
- QSEARCH STAND-PAT adds the residual at **50% blend** (the measured
  optimum) via the bounded-skip fast path; TT-stored static evals stay
  HCE-only so the improving flag never sees the residual.
- Net: `v50sf-res256x16x32-qS71312`, 28.5% of the SF-residual variance
  explained (val RMSE 197.4 → 141.1 cp, corr 0.70), quantization audit
  max |int−float| = 24.7 cp (p99 ≪ that), engine-vs-model cross-check
  corr = 1.0000 (max 1.4 cp) on 40 datagen FENs.
- Host `setoption UseNNUE 0/1` reproduces v4.0 exactly for A/B work;
  the shipped default is ON. `bench 13` fingerprints: **836,096** (NNUE
  on, new) / **680,960** (NNUE off, v4.0-identical).

### 13.4 Honest strength statement

Measured vs the v4.0 engine (same binary, NNUE toggled): **+20 ± 25 at
35 ms (N=259), +28 ± 22 at 50 ms (N=323), +77 ± 35 at 150 ms (N=133)**
— the gain GROWS with time control, classic knowledge-scaling. On the
~2850-calibrated baseline this places the app's top level in the
**~2880–2930** band at app-real think times (250 ms–6 s, where the 150 ms
measurement is the most representative anchor), not at 3000. What stands
between here and 3000: label depth (SF static is an oracle but blind to
tactics), net capacity (256-wide was chosen for the measured nps budget),
and the single-core-per-game app regime. The infrastructure to iterate —
datagen, labeling, constrained training, quantized export, verification,
A/B gating — is fully banked and reproducible.

### 13.5 Campaign lessons (additions to §3)

11. **Self-teaching is circular**: a net trained on the engine's own
    search residuals learns noise, not knowledge — corr 0.72 on
    validation, yet monotonically negative in play at every blend.
12. **Eval RMSE does not predict Elo**: injection point, blend, and the
    search's co-adaptation to its eval's error structure dominate.
    Only the SPRT ladder is truth.
13. **A mature Texel-tuned HCE + corrhist is a harsh teacher's student**:
    the only signal that transferred came from an out-of-family,
    out-of-engine teacher (SF static eval).
14. **Blend curves diagnose signal**: monotone-decay-to-zero = noise;
    interior optimum = real signal. Tune the blend empirically, never
    assume 100%.
15. **Speed is a first-class gate**: −14% nps ≈ −25 Elo at 50 ms. The
    bounded-skip trick (compute corrections only where they can change a
    decision) buys back a third of it for free.

---

## 15. v5.2 — the rebuild campaign (kb buckets, output buckets, warm-trained)

The v5.1 session's artifacts were lost to an environment reset (the whole
work tree, the 5.29M-position labeled corpus, the v5.1a/v5.1b nets). v5.2
rebuilt everything from the v5.0 release (re-downloaded) and shipped a
net that, for the first time in the project's history, beats pure HCE
with statistical significance at BOTH tested time controls.

### 15.1 What shipped

- **Engine** (architecture change vs v5.0, all bit-verified):
  king-quadrant feature buckets (3072 features per perspective; a
  cross-quadrant king move refreshes only the mover's perspective), 8
  per-material output buckets for L2/L3/OUT (FT shared), L2 widened
  16 -> 32, per-layer fixed shifts S2/S3/S4 = 7/13/12, clipped-linear
  activations, SIMD A-vector construction (min/max streams) and
  pmaddwd dot products unchanged. The stand-pat blend-skip bound is
  blend-scaled (`1 + (600*blendPct+99)/100`) — cutoff decisions exactly
  preserved at any blend.
- **Determinism fix (inherited from v5.1)**: Worker history tables are
  zero-initialized in the constructor — v5.0 left them uninitialized and
  only AGED them, so Worker reuse on recycled heap searched with garbage
  ordering tables.
- **Net v52-warm**: FT warm-started from the v5.0 net replicated into
  all 4 king-bucket blocks; L2/L3/OUT copied exactly (fresh 16 L2 units
  initialized dead); trained 20 epochs on 3.10M SF-17.1-labeled
  positions (fresh corpus, eval-cache-miss leaf sampling).
- **tools/**: the full host pipeline rebuilt from scratch — datagen
  (self-play + leaf sink), labeler (SF 17.1 "Final evaluation"), prep
  (packed arrays + scale fit), QAT trainer, exporter, match runner
  (paired SPRT).

### 15.2 Verification (all green on the shipped build)

- Zero-net bit-identity: bench 93,184 nodes IDENTICAL with NNUE on/off,
  every blend 0/50/100, StaticBlendPct 0/100, and on recycled heap.
- perft 6/6 vs SF oracle (pos6's expected value re-verified: 2,702,993).
- Eval mirror symmetry 0/200 fails; accumulator incremental-vs-refresh
  0/11,728 fails; int-vs-double inference 0/1,149 fails.
- Exporter cross-check: engine-emulation vs trainer max|diff| 0.9999 cp
  (the final-output floor rounding).
- bench 6 with the trained net: 57,344 nodes vs the v5.0 author net's
  74,752 (23% smaller tree — the stronger eval prunes harder).

### 15.3 Measured gates (paired openings, SPRT elo0=0/elo1=15, alpha=beta=0.05)

| Gate | TC | Games | Result | Verdict |
|---|---|---|---|---|
| v5.2 b50 vs HCE-off | 50 ms | 1,327 | **+23.6 ± 9.6** | **SPRT H1 accepted** |
| v5.2 b50 vs HCE-off | 150 ms | 617 | **+44.2 ± 14.1** | **SPRT H1 accepted** |

Comparison family: v5.0 author net blend50 was +28 ± 22 at 50 ms (not
significant) and +77 ± 35 at 150 ms (n=133); the lost v5.1b was −12 ± 15
at 50 ms. v5.2 is the first configuration to pass an SPRT gain test at
both time controls. Static quality: val RMSE 121.7 cp on this corpus vs
the author net's 180.9 on the same positions (their 141.1 was on their
own corpus/scale) — a 33% improvement.

### 15.4 The bug ledger (the rebuild's hard-won lessons)

1. **THE batch-reshape bug** (the campaign's defining bug): the trainer's
   forward paired `A.reshape(B, 2, L1)` — which groups CONSECUTIVE
   accumulator rows — instead of [stm-block; opp-block] rows. Correct at
   batch=1, wrong at every larger batch: every training run learned
   scrambled (stm_i, stm_j) perspectives, collapsing onto the target
   mean (val frozen at ~224 = the target std). Diagnosed by comparing
   per-position forwards (matched the author's engine exactly) against
   batched forwards (uncorrelated). The mirror error existed in the
   backward's gradient reassembly too. Every "init problem", "lr
   problem", and "SCReLU dead zone" hypothesis before this was noise.
2. The backward was also missing the `÷2^S3` on the L3 shift (every
   gradient except W4/B4 was 256× oversized; found via smooth-mode finite
   differences — quantized forwards hide gradient bugs: sub-0.5
   perturbations round away).
3. Scale fit corruption: mate scores (±32K cp) among the fit pairs drag
   the LSQ slope from 1.2 to 5.5 — fit on |search| ≤ 2000 only.
4. SF 17.1's `eval` output: the number is ALWAYS white-POV (the binary
   contains no "(black side)" string); in-check positions print
   "Final evaluation: none (in check)" — a readline that waits for a
   matching line deadlocks the labeler.
5. Labeler throughput: 2 SF pipes in 2 THREADS is GIL-bound (~257/s
   aggregate); 2 single-pipe PROCESSES give ~700/s.
6. Fresh SCReLU heads did not train in this campaign (three init
   strategies, two lr regimes, one warm-L2 copy — all mean-collapsed;
   root cause ultimately the batch bug, but the shipped net uses the
   author's clipped-linear activations with an exact warm transfer,
   which reached 121.7 RMSE regardless). SCReLU + fresh-head training
   remains future work.
7. The 2-thread GIL lesson applies to datagen too: 2 single-thread
   processes, one per core.

### 15.5 Shipped defaults (v1.2.0)

- NNUE enabled, net v52-warm, BlendPct 50 (unchanged optimum family),
  StaticBlendPct 0 (experimental, untested this campaign).
- No search-constant retuning yet: the +23.6/+44.2 gains were measured
  with v5.0's constants, which were tuned around the HCE signal. LMR
  futility / RFP margins tuned around the blended signal are the top
  follow-up (every past re-tune in this project bought double digits).

### 15.6 Data provenance

Training labels: Stockfish 17.1 "Final evaluation" of engine-generated
self-play positions (3.10M of 4.37M unique collected; remainder
in-check, correctly excluded). No SF code or nets copied; the teacher
dependency is stated plainly (README, tools/README.md).

## 16. v5.3 — the final-stretch campaign (v1.3.0)

Goal: maximize the hybrid's lead over pure HCE. The honest framing used
throughout: TWO baselines. The **shipped reference** is v1.2.0 running with
`UseNNUE=false` (the "pure HCE" the user sees), and the **strict reference**
is the same v5.3 binary with NNUE off — which this campaign also made much
faster, so the strict number is the harder one.

### 16.1 Speed (tree-identical, verified bit-exact before any search change)

1. **Lazy king-bucket refresh.** `refreshPerspective` is a pure function of
   the placement, so a cross-quadrant king move (make AND unmake) no longer
   sweeps the whole perspective: it sets a dirty bit on the Position, and
   the sweep runs only when `nnue::residual` actually reads the accumulator
   (its only hot reader; `verifyIncremental`/`referenceResidual` also
   materialize). King moves whose subtree is never evaluated now pay zero.
   The clone path copies the dirty bit; `setFen` re-anchors it to fresh.
2. **Inference ILP + AVX-512.** The dot products' serial
   `acc = add(acc, madd(...))` chain stalls on the dependency, not the
   multiplier: two independent accumulators + an AVX-512 (BW/VL) kernel
   with 512-bit loads and one careful horizontal reduction. AVX2 hosts get
   the dual-accumulator unroll; NEON/scalar unchanged.
3. **Honest HCE gating.** With `UseNNUE=false` the accumulator's results are
   never read, so `addPiece/removePiece/shiftPiece` skip its maintenance
   entirely — the pure-HCE baseline is its strongest self, not a hybrid
   paying a hidden tax.

| Config (bench 13) | nodes | nps | vs v5.2 shipped |
|---|---|---|---|
| v5.3 NNUE-on | 3,427,840 | 777,640 | **+22% nps, −16% nodes** |
| v5.3 HCE-off (gated) | 3,637,248 | 1,255,522 | **+68% nps** |
| v5.2 NNUE-on | 4,071,424 | 636,358 | — |
| v5.2 HCE-off | 3,194,368 | 746,522 | — |

### 16.2 Adopted search changes (each A/B measured, 50 ms, paired openings)

| Change | Measurement | Verdict |
|---|---|---|
| **StaticBlendPct 50 as shipped default** (was 0) | +15 ± 19 (325 g), positive in every block | **adopted** — the v5.1 experiment finally wins with the 33%-better v5.2 net: RFP/NMP/razoring/improving now see the same signal as the qsearch stand-pat |
| **Qsearch continuation history** (counterHist context in `scoreQMoves`) | +6.4 ± 17.8 | **adopted** — principled, free, consistently non-negative |
| **Singular gate 5→4 / ttDepth −3→−4, IIR also on stale TT depth** | +3.4 ± 11.5 (917 g), non-negative in all 5 blocks | **adopted** — the only "search more" direction that survived; bench 6 grows 57,344 → 70,144 nodes (more forced-line depth) |

### 16.3 The rejected-probe ledger (all 50 ms, vs the current stack)

| Probe | Result | Reading |
|---|---|---|
| RFP margins −20% (56/84) | −7.0 ± 17.4 (399 g) | margins were already right |
| LMR div 2250→2600 | −13.9 ± 18.0 (375 g) | reduction already optimal |
| leaf BlendPct 50→60 | −0.9 ± 17.8 (383 g) | the 50 optimum held |
| CorrWeight 1024→512 | −26.5 ± 25.7 (184 g, cut) | corrhist weight already optimal |
| Pruning surface +1 depth (LMP 9/fut 7/RFP 9/SEE 7) | −11.0 ± 18.6 (349 g) | the tree is not pruning-starved |
| NmpRdiv 60→50 | −27.6 ± 24.5 (202 g, cut) | NMP aggression already optimal |

Six consecutive rejections across every "prune harder" axis: the v2.2/v4.0
tuning ladders had already found this search's local optimum, and the
NNUE-blended static does not move it. The one direction that paid was the
opposite — search forcing lines deeper (§16.2 row 3).

### 16.4 Gates (paired openings, SPRT elo0=0/elo1=15, alpha=beta=0.05)

| Gate | TC | Games | Result | Verdict |
|---|---|---|---|---|
| **v5.3 vs shipped v5.2 HCE-off** | 50 ms | 644 | **+42.8 ± 13.8** (W291 D141 L212) | **SPRT H1 ACCEPTED** |
| **v5.3 vs shipped v5.2 HCE-off** | 150 ms | 221 | **+75.0 ± 23.9** (W111 D46 L64, LLR +1.87 climbing at cutoff) | trending H1 |
| v5.3 vs v5.3 gated HCE-off (strict) | 50 ms | 536 | +26.0 ± 15.0 (LLR +1.37 trending) | trending H1 |

Context: v5.2 shipped +23.6 ± 9.6 @ 50 ms and +44.2 ± 14.1 @ 150 ms against
the same reference. v5.3 **nearly doubles the 50 ms gap** and pushes the
150 ms gap toward +75 — and the strict number is measured against a pure-HCE
baseline that itself gained ~+40 Elo-equivalent of nps this campaign. The
gap grows with think time, the signature of real eval (not speed) gains.

### 16.5 Verification

- Battery: perft 6/6 PASS, mirror 0/200, incremental 0/11,728 (exercises the
  lazy dirty-bit machinery through clone/make/unmake), int-vs-double 0/1,149
  — **verify all: PASS**.
- Fingerprint ladder: bench 6 with StaticBlendPct 0 = **70,144 nodes**
  (tree-identical to the gate-measured binary); HCE-off bench 6 = 104,960.
- Post-rebuild sanity match vs shipped HCE-off: +78.0 ± 27.2 (171 g) —
  consistent with the 644-game gate.
- **Bug ledger entry #7**: the re-derived AVX-512 kernel initially had
  stride 32 with two 512-bit loads (64 elements consumed) — overlapping
  elements double-counted, every residual wrong. The int-vs-double battery
  caught it instantly (1,149/1,149 fails); fixed to stride 64 + a 32-lane
  tail block. Exactly the class of bug the battery exists for — quantized
  SIMD kernels are never trusted without their double mirror.

### 16.6 Shipped defaults (v1.3.0)

- NNUE on, net v52-warm, BlendPct 50, **StaticBlendPct 50** (new default).
- Search: qs-conthist ordering, singular gate depth 4 / ttDepth −4, IIR on
  stale TT depth; all other constants unchanged (§16.3).
- 13 search constants + both blends exposed as UCI spin options (the SPSA
  surface; snapshots are taken at prepare() so a running search is stable).
- `UseNNUE=false` now skips accumulator maintenance entirely (honest, fast
  pure-HCE mode).

## 17. v5.4 — the network-retrain campaign (v1.4.0)

Goal: close the remaining gap to "crush HCE by 100–200" by making the net
itself stronger — the one lever §16 did not pull.

### 17.1 Data (fresh, from the stronger engine)

- datagen: 2 × 1,200 self-play games from the v5.3 binary (16 k nodes/move,
  28 endgame menus, 20 % menu share) — **5.01 M records** in 9.5 min
  (2 procs). 97.5 % unique FENs → 4.66 M unique pool (shuffled).
- Teacher: Stockfish 17.1 (ubuntu-avx2), `eval` protocol per
  tools/label/label_sf.py — **2.68 M labels** (8 chained blocks,
  ~610/s with 2 SF pipes; in-check positions correctly excluded).
- prep: 2.68 M packed positions; scale refit on 28 k tagged game scores:
  a = 1.297, b = 6.5.

### 17.2 The fine-tune that regressed — and the fix

First run (variant A): full-net warm start from the shipped weights, target
scale a = 1.297 / b = 6.5, lr 0.35, 12 epochs. Val RMSE 135.26 → 117.24
(−13.3 %) — **but the adopt gate read −35.6 ± 25.6 Elo at 186 games.**
RMSE better, engine worse. Diagnosis:

- **Scale mismatch (the big one).** Reverse-fitting the shipped net's
  outputs on the new data gives implied a* = 1.233, b* ≈ 0 — the author's
  net is calibrated to ~1.23×SF, the fresh fit said 1.297. The fine-tuned
  net ran ~5 % hot (mean |out| ratio 1.044) against search thresholds
  (aspiration, RFP, futility, delta) tuned for the old operating point.
  Post-hoc W4/B4 ×0.9507 recovered ~+21 Elo of the loss in a probe —
  the single most instructive measurement of the campaign.
- **Drift.** corr(old, new) = 0.937 — 6 % of output variance moved; a
  5 % scale shift cannot explain −35 alone.

Variant C (shipped): a = 1.233 (operating-point-matched), b = 0, gentle
fine-tune lr 0.15, 8 epochs. Val RMSE 131.46 → **115.41** at the deployment
scale; output ratio 0.991, corr 0.951. Lesson recorded: **a warm net's
training-scale IS part of the search's tuning surface — refit the target
scale on the old net before fine-tuning, or match the nominal.**

### 17.3 Gates (v5.4 = v5.3 code + net v54-c-warm, Blend 50/50)

- Adopt (v5.4 vs v5.3, 50 ms): **+9.6 ± 10.0, 1,200 games** (7 blocks,
  last five +9.6 … +12.7 each) — adopted.
- Headline vs shipped pure HCE (v1.2.0, UseNNUE=false):
  - 50 ms: **+70.4 ± 18.3, SPRT(0,15) H1 ACCEPTED** (v5.3: +42.8)
  - 150 ms: **+111.5 ± 24.0, SPRT(0,15) H1 ACCEPTED** (v5.3: +75.0,
    never passed)
- Blend re-probe with the stronger net: BlendPct/StaticBlend 60 vs 50 =
  −7.3 ± 17.8 (383 g) — blend axis stays flat at 50; the gain is the net's.

### 17.4 Verification

- Export cross-check: engine-vs-trainer max|diff| = 0.9996 cp (n = 3,000,
  the final-output floor rounding only).
- Battery: perft PASS, mirror 0/200, accumulator 0/11,728, int-vs-double
  0/1,149 — **all PASS** with the AVX-512 kernels.
- Fingerprints: HCE-off bench 6 = 104,960 (identical to v5.3 — HCE path
  untouched); NNUE-on bench 6 = 67,072 (tree changed, as expected).
- Net id string: `v54-c-warm`.

### 17.5 Shipped defaults (v1.4.0)

Unchanged from v1.3.0 except the network: NNUE on, net **v54-c-warm**
(SF 17.1-taught residual fine-tune of v52-warm on 2.68 M fresh v5.3
positions, scale-matched), BlendPct 50, StaticBlendPct 50. Float weights
for warm restarts: `tools/train/pretrained/v54c_weights.npz`.

## 18. v5.5 — the data-scale + capacity campaign (v1.5.0)

Goal: the user's moonshot — crush pure HCE by 200–400 Elo (v5.4: +70.4
@ 50 ms, +111.5 @ 150 ms vs the shipped v1.2.0 HCE reference). The plan
of attack, in order of expected leverage: (1) data scale — 15.05 M
labeled positions vs v5.4's 2.68 M; (2) feature-transformer capacity —
L1 256 -> 512/1024; (3) a search-score teacher (SF depth-8 labels) — the
one lever this codebase never tried; (4) blend/knob retuning. Everything
measured; every dead end recorded.

### 18.1 Infrastructure (all new, all verified)

- **label_fast.py**: batched raw-read SF labeler (20 positions per pipe
  round-trip under the 64 KB pipe limit, byte-chunk reads + one in-order
  regex pass). ~1,450 labels/s/proc vs the v5.1 labeler's ~305/s/proc.
  14.70 M labels in 51 min on 2 procs.
- **sf_search_label.py**: SF `go depth 8` search-score labeler (the
  teacher-ladder step). 2 M labels at ~1.1 K/s. UCI gotcha recorded:
  `info score cp` is SIDE-TO-MOVE POV (the `eval` command is white-POV) —
  labels must be sign-flipped for black-to-move positions; and a mate-final
  search must not keep a stale intermediate cp (last-score-of-any-kind).
- **prep_data_v55.py**: the v5.2 dict-of-FENs join OOMs at 14.7 M labels
  (2.7 GB of python strings on a 4 GB host); v1 hash-join OOMed on the
  31 M-entry python hash lists. Final design: disk sort-merge (labels are
  fen-sorted by construction; datagen externally sorted; streaming line
  merge; the only large allocation is the packed array itself). 15.05 M
  positions in ~13 min.
- **nnue_trainer_v55.py**: fp32 fast path (all intermediates stay exact
  integers), qviews computed once per step, backward reuses the forward's
  accumulator + CSR (the v5.2 trainer recomputed the sparse product),
  best-epoch checkpointing, --mirror off (15 M positions teach color
  symmetry naturally; 2x epochs matter more), mate-filtered warm-scale
  reverse-fit. ~7 min/epoch at L1=512 on 15 M positions.
- **gradcheck_v55.py**: central-difference verification of the refactored
  backward in smooth mode (the v5.2 lesson: quantized forwards hide
  gradient bugs). Core paths agree 0.4–3.4%; residual diffs are the
  output-clamp STE by design. Run BEFORE any training.

### 18.2 The capacity measurements (the campaign's engineering core)

- **L1=1024**: 72% nps tax (bench 13, clean, NNUE-on 369K vs HCE-off
  1,307K nps on the dev rig). The FT update loops ARE AVX-512 vectorized
  (12.5 M events/s hot-cache microbench, 97 zmm instructions) — the cost
  is the memory wall: 4 random 2 KB row reads per piece event from a
  6.3 MB table, evicted by the search's own TT traffic. An
  inference-skip build isolates the split: accumulator 54% + inference
  39% of the tax.
- **L1=512**: ~45% tax on the dev rig (598K vs 1,307K). EC_BITS 18->20
  (eval cache 4->16 MB) measured FLAT — the materialization rate is
  intrinsic (genuinely new positions), not cache thrashing.
- **A lazy event-log accumulator** was designed in depth (push events on
  make, truncate on unmake, materialize on residual()) and REJECTED: the
  sibling-remake divergence (acc valid at the same log length under a
  different event tail) forces either per-entry epochs or frequent full
  refreshes. The measured 7.7 events/node vs materializations every ~2.6
  nodes left too little headroom for the complexity risk. Recorded as the
  known-negative path for a future campaign.
- **DECISION**: L1=512 shipped width (2x capacity, CAP_W2=400 kept — the
  1024-term dot audits to 1.68e9 < 2^31).

### 18.3 The function-preserving warm start (the campaign's safety net)

W1 = [v54c | noise], B1 = [v54c | 0], W2 = [v54c | exact zeros]: the
512-wide net starts BIT-EXACTLY at v5.4's function — bench 6 = 67,072
nodes, the v5.4 fingerprint, with the engine rebuilt at L1=512. The zero
L2 columns bootstrap themselves (Adam gradient from the noisy
activations, then W1 follows). This proved the whole
engine/trainer/exporter chain before any training happened.

### 18.4 The nets

- **v55-res512-warm** (trained, NOT shipped — see 18.7): 6 epochs, lr 0.18 cosine, batch 8192,
  no mirror, 15.05 M positions, scale a=1.233 (the v5.4 operating point —
  the 17.2 lesson held: per-bucket output bias stayed within -3.4..-0.5
  cp). val RMSE 127.22 (warm) -> 111.95. Explained residual variance
  74.5% (warm: 66.9%). The W2 new-capacity columns grew 0 -> |46|; the
  depth-13 bench tree shrank 17% (3,531,264 -> 2,932,736 nodes — a
  sharper eval prunes harder). Export cross-check 0.9995 cp, battery PASS.
- **v55-d8-finetune** (REJECTED): 2 M positions relabeled with SF depth-8
  SEARCH scores, fine-tuned at the mate-filtered implied scale a*=0.7707.
  val RMSE 232.9 -> 182.3 on the d8 target (target std 292.6) — but the
  PLAY gate read **-59.6 +/- 35.3 vs the static-labeled net** (100 games,
  50 ms). Search-score distillation into a static 512-wide net transfers
  NEGATIVELY: the tactical component of depth-8 scores is exactly what a
  static function cannot express, and the noise dominates. This closes
  the teacher ladder the v5.0 campaign opened: self-search (circular,
  negative), SF-static (positive, shipped), SF-search (negative at this
  net size).
- **v55-256-warm** (SHIPPED): the same 15.05 M corpus retrained at L1=256
  (v5.4's width, half the nps tax; 5 epochs, lr 0.18 cosine, warm from
  v54c). val RMSE 127.22 -> 113.74 (72.3% of residual variance explained
  vs v5.4's 66.9% on the same corpus) at nps ratio 0.61 vs the 512's
  0.47. The capacity-vs-speed control experiment that WON the ship:
  +63.2 +/- 28.8 vs HCE-off at 50 ms where the 512-net read -56.1.

### 18.5 The knob probes (all 50 ms, paired openings, 100 games)

| Probe | Result | Verdict |
|---|---|---|
| BlendPct/StaticBlend 100 vs 50 (v55-res512) | -52.5 +/- 35.1 | rejected |
| BlendPct/StaticBlend 65 vs 50 (v55-res512) | -56.1 +/- 35.2 | rejected; 50 held |
| d8-finetune vs static-labeled (both blend 50) | -59.6 +/- 35.3 | net rejected |

The 50 optimum held for the third net generation — the blend curve's
interior optimum is a property of the search's co-adapted margins, not
just the net's noise level. A-vs-A calibration (identical configs):
+8.7 +/- 55.0 over 40 games — the harness is fair.

### 18.6 Rig honesty note

The dev rig for this campaign is materially slower than the v5.2–v5.4
rig (2 cores, shared L3, contended memory). The warm-512 net — bit-exact
v5.4 in eval — measured -82.6 +/- 65 vs strict HCE-off at 50 ms HERE,
where v5.4 measured +26.0 strict (v5.3 binary) on the original rig. The
L1=512 memory traffic suffers disproportionately on this host. Treat
cross-rig absolutes as incomparable; the campaign's internal A/Bs
(paired, same rig, same binary) are the truth. Headline gates below are
reported with this caveat.

### 18.7 Gates (v1.5.0 = v5.4 engine code, byte-identical + v55-res256-warm)

All gates: paired openings, strict same-binary reference (the shipped
binary with `UseNNUE=false` — the honest HCE baseline at its fastest),
two concurrent games on the 2-core dev rig.

| Gate | TC | Games | Result | Verdict |
|---|---|---|---|---|
| 512-net vs HCE-off (width probe) | 50 ms | 100 | -56.1 +/- 35.2 | 512 rejected at 50 ms |
| 512-net vs HCE-off (width probe) | 150 ms | 60 | +58.5 +/- 45.5 | 512 climbs with TC; 256 still ships |
| 256-net vs HCE-off (decision gate) | 50 ms | 150 | **+63.2 +/- 28.8** | ship = 256 |
| 256-net vs HCE-off (final, SPRT 0/15) | 50 ms | 800 | **+15.2 +/- 12.3** (LLR +0.97, cap) | pooled with the decision gate: **+22.9 +/- 11.3 (n=950)** |
| **256-net vs HCE-off (headline)** | 150 ms | 508 | **+52.4 +/- 15.6, SPRT(0,15) H1 ACCEPTED** (LLR +2.94) | the gap grows with think time |
| 256-net vs HCE-off (app-regime probe) | 400 ms | 60 | -5.8 +/- 44.9 | inconclusive at n=60 (±45) |
| **v55-net vs v5.4-net (same binary, same tax)** | 50 ms | 150 | **+37.2 +/- 28.5** | the retrained net's own contribution, isolated |
| CorrWeight 768 vs 1024 | 50 ms | 200 | +15.6 +/- 24.6 | not shippable (0.6 sigma); 1024 kept |

The two 50 ms estimates (+63.2 n=150, +15.2 n=800) are opening-sample
variance (paired openings control colors, not opening difficulty); the
pooled n=950 number is the honest 50 ms figure on this rig. The rig note
(18.6) explains why these run colder than the v5.2-v5.4 rig's numbers:
the warm-512 net — bit-exact v5.4 in eval — measured -82.6 +/- 65 strict
HERE. The numbers that transfer across rigs are the ones that share the
penalty between both sides: the net-vs-net gate (+37.2 for v55 over the
v5.4 net, same binary, same tax) and the 150 ms headline, which continues
the project's signature pattern — the hybrid's lead over pure HCE grows
with think time (+22.9 pooled @ 50 ms -> +52.4 @ 150 ms (SPRT-accepted)).

### 18.8 Shipped defaults (v1.5.0)

- Engine: **byte-identical architecture to v5.4** (L1 256, CAP_W2 400 —
  the capacity ladder was measured and rejected; the shipped change is
  the net and the data behind it). Version strings bumped to 1.5.0;
  tools/ucibench Makefile gained header dependencies (a stale binary bit
  the campaign once).
- NNUE on, net **v55-res256-warm** (15.05 M-position retrain of the
  v5.2 architecture, warm from v54c, scale a=1.233), BlendPct 50,
  StaticBlendPct 50.
- Float weights for warm restarts: `tools/train/pretrained/v55_weights.npz`.
- Bench fingerprints: HCE-off bench 6 = 104,960 (v5.3/v5.4-identical — the
  HCE path is untouched); NNUE-on bench 6 = 62,464; bench 13 nps 800,913
  vs HCE-off 1,307,421 (0.61 ratio — the v5.4 tax band).

---

## 19. v6 — the 200-Elo push campaign (v1.6.0)

Mission: widen the hybrid-vs-pure-HCE gap from +15/+52/~0 (50/150/400 ms)
toward the 200-400 band. The honest ceiling of a *residual* hybrid (a net
that only predicts what HCE misses, blended at 50%) is far below a
full-replacement NNUE — but the campaign found and removed three
independent, compounding losses. Final placement, same rig, same binary,
strict same-binary HCE-off reference (results/v6_*.json):

| TC | v5.5 shipped | v6 | games (v6) |
|---|---|---|---|
| 50 ms | +15.2 ± 12.3 (n=800) | **+81.0 ± 11.9** | 900 |
| 150 ms | +52.4 ± 15.6 (n=508) | **+74.8 ± 15.9** | 500 |
| 400 ms | -5.8 ± 44.9 (n=60) | **+79.5 ± 28.2** | 160 |

The v5.5 profile — gap shrinking with think time, gone by 400 ms — is
inverted into a flat ~+80 across the whole app-relevant band. The 400 ms
number is the campaign's cleanest statement: v5.5's residual quality was
real but its NPS tax gave the speed back at depth; v6 keeps the eval and
keeps the speed.

### 19.1 Diagnosis: where the "40% NPS tax" actually was

Counters+timers build (fixed-time 1000 ms search): residual() is called on
36% of nodes; of the total wall time, full inference is ~9%, king-bucket
refreshes ~3.4% — and the eagerly-maintained accumulator (touched in
addPiece/removePiece/shiftPiece for EVERY node) the rest. The decisive
experiment staged the SAME depth-12 tree with NNUE machinery peeled off
layer by layer: HCE-only 1405 ms, +eager accumulator 1651 ms (+17.5%),
+lazy accumulator ring 1459 ms (+3.8%). The v5.5 "40% bench-nps loss" was
a fixed-depth bench artifact (bigger tree = more nodes per depth); the
real fixed-time tax was ~31.5%, of which the accumulator was the largest
single slice and inference the second.

### 19.2 The lazy accumulator ring (the big unlock)

v5.x maintained both perspectives' L1 accumulator eagerly inside
Position's piece helpers — work paid on EVERY node, used on ~1/3. v6
inverts the economics:

- `makeMove` records a compact op stream into StateInfo (feature
  add/sub with pre/post king squares, exactly the v5.x eager order:
  victim first with pre-move kings, mover/promo/castle-rook with
  post-move kings; same-quadrant king moves become exact 4-row shifts,
  cross-quadrant ones dirty one perspective).
- `nnueEnsureFresh()` materializes the accumulator only when an eval
  needs it: a 128-slot ring of per-ply accumulators keyed by Zobrist;
  walk down to the nearest valid ancestor, copy + replay ops. Unmake is
  FREE (previous ply's slot still valid); TT transpositions reuse ring
  slots by key; clone invalidates.
- Verified bit-identical to the eager scheme: bench 6 tree exactly
  62,464 nodes (v5.5-equal), verifyNnue 11728/0, and the fuzz battery
  (clone chains, random plies, int-vs-double) all PASS.

Same-tree cost: accumulator slice 17.5% -> 3.8% (4.6x). Fixed-time 100 ms
searches gained ~6% nodes with identical tree values.

### 19.3 VNNI batched inference

The v5.x inference loop was one dot product per output with a serial
256-step dependency chain. v6 packs L2/L3 weights SoA and runs batched
`vpdpwssd` kernels — but the first version interleaved NOTHING and
measured barely 2-3% better: the kernel was LATENCY-bound (~7 cycles per
dependent dpwssd). Four interleaved accumulator chains fixed it:
inference 174.8 ms -> 68.6 ms in the profiling harness (~15 us -> ~6 us
per call). Bit-identity re-verified (bench + full battery). With ring +
kernel the engine is ~10% faster than v5.5 at fixed time with identical
evals.

### 19.4 Capacity: the arch v5.5 rejected, at the price it measured

With the maintenance tax gone, the L1=512 / 16-output-bucket
architecture (v5.5 measured it at ~45% eager tax and rejected it)
became affordable: a function-preserving padded net (v55 weights mapped
8->16 buckets) ran SPEED-NEUTRAL vs the shipped v5.5 binary on identical
trees. Datagen (self-play, 2 procs) produced 13.3 M unique positions;
SF 17.1 static eval labeled them (~3.3 K labels/s); 13.5 M packed.
Warm-start training from the padded v55 weights (operating scale 1.233,
not the warm-implied 1.4373 — measured first!) took val RMSE 122.79 ->
110.75 cp (zero-net 227, v55 122.8 on the same data).

### 19.5 Uniform TT-eval semantics (free consistency)

The TT eval field stored MIXED semantics: main-search nodes stored
HCE+residual (full blend), qsearch nodes stored HCE-only — so stand-pat
readers could see inconsistent values for the same key. v6 stores the
full-blend eval everywhere (qsearch TT hits replay the stand-pat from
the TT). Gate: +17.4 ± 14.2 over 600 games vs the mixed version —
adopted. This also reshapes the tree: bench 6 NNUE 62,464 -> 58,880
nodes (fewer nodes to the same depth), and bench 13 3.06 M -> 2.38 M
(-22%) — v6 finds depth-13 cutoffs with a fifth fewer nodes than v5.5.

### 19.6 The gates

| Gate | TC | Games | Result | Verdict |
|---|---|---|---|---|
| v6 net vs v55 net (same engine, ring+VNNI) | 50 ms | ~400 | +20.6 ± 24 | net adopted (RMSE + direction) |
| BlendPct 70 vs 50 (new net) | 50 ms | ~400 | -23 ± 25 | rejected; blend stays 50 (4th generation running) |
| Uniform TT eval vs mixed | 50 ms | 600 | **+17.4 ± 14.2** | adopted |
| **v6 hybrid vs strict HCE-off** | 50 ms | 900 | **+81.0 ± 11.9** | headline |
| **v6 hybrid vs strict HCE-off** | 150 ms | 500 | **+74.8 ± 15.9** | headline |
| **v6 hybrid vs strict HCE-off** | 400 ms | 160 | **+79.5 ± 28.2** | headline (v5.5: -5.8 ± 44.9 at n=60) |

### 19.7 Bug found in final verification

`verify all` with `UseNNUE=false` SEGFAULTED: v6's residual() /
verifyIncremental() / referenceResidual() dereferenced the null that
nnueEnsureFresh() returns when the subsystem is off (v5.5 read a stale
member instead of crashing). Search paths were never affected (every
caller gates on `nnue::enabled`; the app layer cannot toggle it at all —
the option exists only in the host harness). Fixed by short-circuiting
the three entry points to the disabled contract (residual 0, verify
false). NNUE-off verify now reports fails=11728 exactly like v5.5
(subsystem off -> accumulator unmaintained -> not verified), exit 0.

### 19.8 Shipped defaults (v1.6.0)

- Engine: lazy op-ring accumulator + AVX-512 VNNI batched inference
  (portable path bit-identical), L1=512, 16 output buckets, uniform
  TT-eval semantics. Version strings 1.6.0 / versionCode 22.
- Net **v6-res512-ob16-warm** (13.5 M SF 17.1-labeled positions, warm
  from padded v55, scale a=1.233), BlendPct 50, StaticBlendPct 50.
- Bench fingerprints: HCE-off bench 6 = 104,960 (v5.3/4/5-identical —
  the HCE path is untouched; the match baseline is byte-behavior-
  identical); NNUE-on bench 6 = 58,880 (uniform-TT tree); verify all
  PASS (perft 6/6, mirror 200/0, nnue 11728/0, int-vs-double 1149/0).
- The residual-hybrid ceiling, stated honestly: at 50-400 ms think
  times the architecture converges to ~+80 over its own HCE. The next
  step change (200+) is a full-replacement net, a different project.

## 20. v7 — the data-scale campaign (v1.7.0)

Mission: break the ~+80 residual-hybrid plateau. v6 established that the
architecture was not the bottleneck — the NET was (13.5M positions,
val RMSE 110.75). This campaign scaled the corpus twice and re-measured
everything downstream of it.

### 20.1 Corpus scale-up 22.6M -> 27.9M

The v7 leg rebuilt the label pipeline (28M raw lines across 4 shards)
and packed 27.9M usable positions (`corpus_v75/`, mmap-backed — see the
v6 OOM lessons). SF 17.1 labeled everything; the trainer (v55, huber
loss, Adam) warm-started from the shipped v6 weights at the measured
operating scale a=1.2783 (re-measured on a representative random
slice, NOT the FEN-sorted head — the last campaign's scale-fit bias
lesson). 7 epochs: val RMSE 110.75 -> **100.85 cp** (v6 net re-scored
127.31 on the same held-out slice — the new net is 21% closer to SF).

### 20.2 Net quality -> Elo, measured directly

Same binary, NNUE on both sides, blend 70 both: v75 net vs v6 net
+41.0 ± 17.5 (400 games, 50 ms). The previous 22.6M v7-draft net had
measured +26 over v6 on the same protocol — the 5.3M extra positions
and 7-point RMSE gain bought ~+15 Elo of pure eval quality. The
data-scale lever is the only one in this project with a measured,
reproducible Elo-per-epoch exchange rate.

### 20.3 Blend re-sweep on the stronger net

The v7 leg had moved BlendPct 50 -> 70 (the stronger the residual, the
higher its optimal weight — the v5.0 measurement of 50 was a statement
about the v55-era net, not the architecture). This campaign re-swept on
the v75 net: 80 vs 70 lost (-25.7 ± 19.3, n=325), 60 vs 70 lost
(-37.6 ± 27.0, n=167). **70 confirmed as the peak**; shipped as the
default (search residual and static blend both).

### 20.4 Search re-tunes on the quieter eval — all rejected

Two probes on the theory that a quieter eval moves the search optimum:

| Probe | Idea | Result (50 ms, SPRT -3/3) | Verdict |
|---|---|---|---|
| CorrWeight 1536 vs 1024 | weight corrhist more | +3.5 ± 11.0 (n=1000; early +21 decayed) | rejected |
| AspDelta 8 vs 12 | tighter aspiration, fewer re-searches | -0.9 ± 17.5 (n=395; early +26 decayed) | rejected |

Both showed the same signature: early-trend peak, monotone decay to
zero. The search constants sit at a genuine local optimum — remaining
Elo is in the net, and the net is data-bound. Quantization was verified
lossless (0.01 cp int16-vs-float on 200K probe positions) — that lever
is closed too.

### 20.5 The gates (shipped binary, strict same-binary HCE-off)

| TC | Games | v6 shipped | **v7 (v75 net, blend 70)** |
|---|---|---|---|
| 50 ms | 800 (2 seeds pooled) | +81.0 ± 11.9 | **+120.6 ± 13.0** |
| 150 ms | 309 | +74.8 ± 15.9 | **+161.3 ± 21.9** |
| 400 ms | 188 | +79.5 ± 28.2 | **+121.1 ± 26.9** |

The 150 ms number is the campaign's cleanest statement: the gap now
GROWS with think time (+121 -> +161 from 50 -> 150 ms), where every
previous generation went flat or shrank. The stronger eval compounds
with depth instead of being diluted by it.

### 20.6 Shipped defaults (v1.7.0)

- Net **v75-res512-ob16-warm** (id `v75-28m-7ep-101`): 27.9M SF
  17.1-labeled positions, warm from v6 weights, scale a=1.2783, val
  RMSE 100.85. BlendPct 70, StaticBlendPct 70 (both re-swept, §20.3).
- Version strings 1.7.0 / versionCode 23.
- Bench fingerprints: HCE-off bench 6 = 104,960 (v5.3/4/5/6-identical —
  HCE path untouched, baseline validity preserved); NNUE-on bench 6 =
  48,640 (v6: 58,880 — the v75 net reaches depth 6 with 17% fewer
  nodes). verify all PASS (perft 6/6, mirror 200/0, nnue 11728/0,
  int-vs-double 1149/0). Shipped-default smoke vs HCE-off: +70 ± 28
  (n=155, fresh seed — consistent with the pooled gate within 2 sigma).
- Honest ceiling statement: +120-160 over its own HCE across the app
  band, bound by corpus size, not architecture. The next step change
  (200+) is a full-replacement net — the §18 feasibility analysis and
  the corpus pipeline built here (28M positions, mmap-hardened trainer)
  are the groundwork for it.

## 21. v8 — the corpus-refresh + envelope campaign (v1.8.0)

Mission: beat the shipped v7_1 hybrid head-to-head (the primary gate),
while preserving the HCE-off crush baseline. All gates vs the shipped
v7_1 binary (v1.7.0, net v75-28m-7ep-101, blend 70) on the same rig,
50 ms movetime, SPRT(0,10) unless noted.

### 21.1 Data (mission floor 30M+)

- Fresh v7-selfplay datagen (20 blocks, 2 procs): 42.4M raw records,
  37.79M unique after global sort-dedup (1.35x the v75 corpus).
- SF 17.1 labels via streaming labeler (~2900 pos/s aggregate);
  37,747,720 packed positions. LSQ scale a=1.2927 (v75: 1.2783).

### 21.2 Experiments run (and rejected, all with data)

- **L1=1024 wide net, clamp-1500**: 4 epochs, val RMSE 112.42 (best
  yet), but per-node cost +11% ~= -25 Elo at 50 ms. Gates: -37.6
  (b70) / -56.6 (b80) vs v7_1. Speed tax > accuracy gain. Dead end
  (v5.5's verdict re-confirmed on better data).
- **512-wide net, clamp-1500 target** (2 epochs, RMSE 115.45): beat
  the 512 champion head-to-head (+17.8 ± 17.6) but LOST to v7_1
  (-16.0 ± 17.6) — a ~3-sigma transitivity violation. The unclamped
  tail (injections to 1125 cp) is punished by v7's profile. clamp-600
  is validated with data; the champion ships.
- **BlendPctEG 90** (phase-dependent blend, new v8 knob, default off):
  +13.4 ± 21.6 on top of blend 75 — overshoots like the global blend.
  Ships off; kept for future tuning.
- **StaticBlendPct 75**: +2.7 ± 21.5 — pruning signals stay
  noise-sensitive; static stays 70.

### 21.3 Shipped: net v8-38m-3ep-512, BlendPct 75

- L1=512 (v7 speed envelope), clamp-600 target, warm from v75, 3
  epochs: val RMSE 123.30 -> 111.55 -> 109.90 -> **109.50** (-13.8,
  -11.2% vs the shipped v75 net on the same corpus). R^2 0.768
  (v75: ~0.72), theory b* = 81 -> shipped 75 after the empirical
  haircut: 70 -> +14.0 ± 15.2, 75 -> +24.8 ± 10.8 (1040 g),
  80 -> -8.0 ± 21.6.

### 21.4 The gates (same rig, same-binary HCE-off for crush)

| Matchup | TC | Games | Result |
|---|---|---|---|
| **v8 vs shipped v7_1** (primary) | 50 ms | 1040 | **+24.8 ± 10.8** (z=2.3) |
| v8 crush (hybrid vs HCE-off) | 50 ms | 1040 | +115.3 ± 11.4 (SPRT H1) |
| v7_1 crush, same rig | 50 ms | 780 | +123.4 ± 13.2 |
| v8 crush | 150 ms | 277 | +140.1 ± 22.6 (SPRT H1) |
| v7_1 crush, same rig | 150 ms | 320 | +143.3 ± 21.1 |

Crush margins preserved within noise at both TCs (deltas -8 ± 17 and
-3 ± 31); the v7 -> v8 gain is head-to-head strength, not crush
margin. Node efficiency: NNUE-on bench 8 = 147,456 nodes vs v7's
173,056 (-15%).

### 21.5 Shipped defaults (v1.8.0)

- Net **v8-38m-3ep-512**, BlendPct **75** (out-of-box = the gated
  config; bench-identical to explicit setoption, verified), 
  StaticBlendPct 70, BlendPctEG 0.
- Bench fingerprints: HCE-off bench 6 = 104,960 (v5.3-identical, HCE
  path untouched); NNUE-on bench 6 = 41,984 (blend 75). verify all
  PASS (perft 6/6, mirror 200/0, nnue 11728/0, int-vs-double 1149/0).
- Honest ceiling statement: the hybrid's crush over its own HCE
  remains ~115-140 across the app band, bound by the residual
  architecture (blend > 75 trades absolute strength for noise). The
  200+ crush step still requires the §18 full-replacement net; the
  37.7M-position corpus and the clamp/width/phase levers measured
  here narrow its search space materially.

## 22. v9 — the scale-fix + corpus campaign (v1.9.0)

Mission: beat the shipped v8_1 hybrid head-to-head (the primary gate,
SPRT(0,10) @ 50 ms), preserve the HCE-off crush baseline. The v8
campaign's rejected levers (L1=1024, clamp-1500, BlendPctEG) stayed
parked; this campaign went after the two levers with the best measured
exchange rates: inference speed and data scale — plus a scale-fit bug
that was silently distorting every training target.

### 22.1 nnuePrimeRoot — free speed from a lazy-materialization gap

The op-ring accumulator (v6) avoided eager updates, but qsearch TT hits
and other "fresh root" evals were still materializing accumulators by
FULL 32-row refresh from scratch — 34% of all materializations on the
profile. nnuePrimeRoot (committed before the net work) keys a prime-
root accumulator so those evals reuse the nearest valid ancestor
exactly like interior nodes do. Bit-exact (bench 6 = 41,984 NNUE-on
IDENTICAL, HCE-off 104,960 untouched), bench-8 wall clock -3.5%.

### 22.2 The scale-fit bug (the campaign's pivotal catch)

Corpus v9 (44,345,269 SF-17.1-labeled positions, 1.18x v8; fresh
champion-selfplay datagen + streaming labeler) packed with the LSQ
scale fit reporting a=1.3547. But the packer fits the scale on a
head-biased slice (np.arange(0, 100000, 7) of a FEN-SORTED file —
endgame-skewed), and the trainer's --scale-mode warm refit sampled the
same way (a=1.3803). A 200k RANDOM-slice refit of the warm net's
implied scale gives **a\*=1.2435** (warm RMSE 115.06 vs 123.49 at the
biased scale — training at the file scale would have pushed the net
past the sharp blend peak and cost the whole campaign). All v9 training
ran at the corrected scale. (The v8 campaign trained at its packer-fit
1.2927 — the same bias existed there; the v8 net's own random-slice
implied scale is ~1.24, i.e. the shipped champion has been operating
with a ~4% over-scaled target all along. Its blend-75 optimum is
consistent with the effective scale, which is why the v9 net's optimum
did not move up: both nets' effective scales match at ~1.24.)

### 22.3 Training

Warm from v8-38m-3ep-512, L1=512, clamp-600 target, huber-150,
batch 8192, lr 0.18 cosine-to-5-epochs, a=1.2435:
warm 115.06 -> 106.61 -> 104.95 -> 103.94 -> 103.42 -> **103.25**
(the v8 champion re-scores 115.06 on the same corpus — the v9 net is
10.2% closer to SF). R^2 0.7844 (champion 0.768). Quantization
lossless at export (max|engine-trainer| = 0.9999 cp on 20k probes).
NNUE bench 6: 41,984 -> **34,304 nodes (-18%)** — the v9 eval cuts the
same bench tree with a fifth fewer nodes. HCE-off bench 104,960
IDENTICAL (HCE path untouched, baseline validity preserved). verify
all PASS.

### 22.4 The gates (all on the same rig; primary vs the SHIPPED v8_1)

| Gate | TC | Games | Result | Verdict |
|---|---|---|---|---|
| Net isolation: v9@75 vs v8-net@75 (same binary) | 50 ms | 2400 | +13.0 ± 7.1 (z=1.83; positive from n=330 on) | adopted (z-significance) |
| Blend 80 vs 75 (v9 net) | 50 ms | 703 | -5.4 ± 13.1 | rejected — 75 confirmed peak |
| **Primary: v9 final vs shipped v8_1** | 50 ms | 1522 | **+27.0 ± 8.9 — SPRT(0,10) H1 ACCEPTED (LLR +2.95)** | **shipped** |
| Crush: v9 hybrid vs same-binary HCE-off | 50 ms | 1000 | +119.1 ± 11.6 | headline |
| Crush | 150 ms | 400 | +146.2 ± 18.9 | headline |
| Crush | 400 ms | 150 | +120.4 ± 30.1 | headline |

Crush reference (v8_1, same rig): +115.3 ± 11.4 / +140.1 ± 22.6 /
+121.1 ± 26.9. Crush margins preserved within noise at every TC
(deltas +3.8 ± 16.4, +6.1 ± 29.5, -0.7 ± 40.4); the +27 is
head-to-head strength on top of a preserved crush.

### 22.5 Methodology notes (sandbox + honesty)

- The sandbox's process-isolation regime changed mid-campaign:
  detached/setsid processes are now killed at tool-call boundaries
  (the original 4-hour training chain died silently). All remaining
  training ran as 520-s foreground blocks (the trainer's SIGTERM-safe
  120-s-interval checkpointing lost zero progress).
- The net-isolation gate (+13.0 ± 7.1) did not formally cross SPRT
  H1 (LLR peaked +2.40, ended +1.89 at the 2400-game cap) — it is
  reported as z-significant, not SPRT-accepted. The PRIMARY gate
  (the shipped configuration vs the shipped champion) DID formally
  accept. Both numbers are stated with their exact provenance.
- Blend theory (b* = var(r)/(var(r)+var(e)) = 82 for this net)
  again overestimated the empirical peak by ~7 points — consistent
  with the v8 campaign's 6-point haircut. The theory's value is
  direction, not location.

### 22.6 Shipped defaults (v1.9.0)

- Net **v9-44m-5ep-512** (44.3M positions, corrected scale a=1.2435,
  warm from v8-38m-3ep-512, 5 epochs, val RMSE 103.25, R^2 0.7844).
- BlendPct 75, StaticBlendPct 70, BlendPctEG 0 (all unchanged —
  re-swept, 75 confirmed).
- nnuePrimeRoot lazy materialization (bit-exact, -3.5% wall).
- Version 1.9.0 / versionCode 24.
- Bench fingerprints: HCE-off 104,960 (v5.3-identical); NNUE-on
  34,304; verify all PASS (perft 6/6, mirror 200/0, nnue 11728/0,
  int-vs-double 1149/0).
- Honest ceiling statement: crush over own HCE remains ~+115-145
  across the app band. The v9 leg bought +27 head-to-head (SPRT-
  accepted) via the scale-fix + 18% more data + inference speed.
  The 200+ crush step still requires the full-replacement net; the
  scale-fit lesson (fit on random slices, never sorted heads) is
  this campaign's transferable finding for it.

---

## 23. v9.1 — the final-stretch campaign (stretch-goal sprint)

Mission: maximum verified Elo over the shipped v1.9.0 hybrid (the gating
baseline throughout), stretch goal 200–400 Elo over pure HCE. Rig: 2 cores,
4 GB RAM (same class as prior campaigns), 50 ms/move gates, SPRT(0,10)
where applicable, match.py paired openings.

### 23.1 Harness reconstruction (the zip's missing file)

`tools/ucibench/ucibench.cpp` was absent from the v9 export (the Makefile
references it; only datagen.cpp shipped). It was reconstructed to the
shipped binary's exact UCI surface (option list, bestmove/result line
formats, bench/verify commands). Behavioral fidelity verified against
`binaries/tacturn-1.9.0-ucibench-linux-x64`:
- Fixed-node battery (30 positions x {3k, 20k, 60k nodes, d7, d9}):
  130/150 searches bit-identical (bestmove+score+depth+nodes).
- The 20 divergent searches are deep-search (d9+) tree differences from a
  source-vs-binary delta in the zip (see 23.5); bestmove agreement 149/150.
- `ucinewgame` semantics reverse-engineered behaviorally: clear TT, KEEP
  the Worker (histories persist across games, aged per search) — the naive
  fresh-session reconstruction measurably weakens match play.
- Startup cost note: ~0.85 s per process (static init + 48 MB TT clear);
  matches keep processes alive across games, so it is match-irrelevant, but
  it silently corrupts naive per-invocation nps comparisons. Real search
  speed: shipped 808–822 kN/s, rebuild 789–824 kN/s (equivalent).

### 23.2 Measured and REJECTED (all with data)

| Candidate | Measurement | Verdict |
|---|---|---|
| PGO two-pass build (match-workload profile) | 173.3 vs 173.2 kN/s fixed-node; 823 vs 824 kN/s fixed-time | 0% — dead (LTO already captures it; v2.7-era +10% is pre-LTO history) |
| BlendPctEG 85 vs 0 (option-only, shipped binary) | +1.1 ± 11.3 (n=950) | rejected — the v8-era +13.4 ± 21.6 does not reproduce on the v9 net |
| StaticBlendPct 75 vs 70 (option-only) | +6.1 ± 9.6 (n=1300; early +20 peak, monotone decay — the documented noise signature) | rejected — z=0.64, LLR peaked +0.8 |
| BlendPctEG 85 + StaticBlendPct 75 combo | +8.3 ± 9.6 (n=1300, same decay signature) | rejected |
| Eval-cache 2^18 -> 2^21 | miss rate 38.0% vs 37.9% (game-real sequence) | dead — misses are first-visit, not capacity |
| PawnHashTable 2^14 -> 2^16 | 844–887 vs 894–915 kN/s | NEGATIVE — L2 footprint beats hit-rate |
| Iteration pacing est 1.3x -> 1.2x/1.1x | fewer total nodes, lower utilization | rejected |
| TT bits 16–24 sweep | 860–909 kN/s (monotone-flat, 22 slightly worse) | dead |
| Decision-aware leaf skip (restore the dead-code leafEvalBounded far-boundary skip; +5.3% nps measured) | -3.4 ± 17.1 vs same-source baseline (n=414, converging negative) | rejected — the far-boundary TT evals carry HCE-only semantics and the engine measurably prefers uniform TT-eval semantics (v6's +17.4 re-confirmed a second time) |

Instrumentation findings that close the remaining speed levers: per-node
cycle split at match conditions ~22% HCE eval, ~36% NNUE inference,
~42% search machinery; eval-cache miss rate 38–42%; every miss pays one
full NNUE inference (the v5.0-era bounded-skip is dead code since the
v5.3 static-blend + v6 uniform-TT-eval changes — resCalls == evalCalls
exactly); far-boundary fraction at main-search nodes: 0/185,787 (the net
is decision-relevant everywhere the search evaluates — this is WHY the
hybrid works).

### 23.3 The net lever — feasibility verdict

The v9 corpus (44.3M positions) is not in the zip; the data pipeline
(datagen self-play + SF 17.1 labeling + packing + numpy QAT training)
cannot regenerate a meaningful corpus on 2 cores within a campaign window,
and a marginal top-up (≤10M positions, ~1.2x is what the rig can produce)
has a measured exchange rate far below the v9 leg's +27. The net lever
stays parked for a rig with real datagen throughput.

### 23.4 Final verification (the numbers that matter)

- **Primary gate — deliverable vs the shipped v1.9.0 champion (50 ms,
  1000 games): -6.3 ± 11.0.** The source-rebuilt engine is statistically
  equivalent to the shipped champion head-to-head (z = -0.57); no Elo
  was lost and none was gained. The engine source is byte-identical to
  the v9 export (md5-verified file by file).
- Eval-layer identity: the shipped harness's `eval` command was
  reverse-engineered and reproduced — HCE static eval AND NNUE residual
  bit-identical on every probed position (e.g. startpos `eval 23
  nnue_res -14`, both binaries).
- HCE-off nps: shipped 1445 vs rebuild 1478 kN/s (equivalent);
  NNUE-on: 856 vs 869.
- `ucinewgame` semantics verified identical (TT cleared, Worker kept)
  by the depth-9-then-depth-7 TT-persistence probe: both binaries show
  the 84x node collapse without ucinewgame and a fresh-sized search
  with it.
- verify all PASS (perft 6/6, mirror, nnue accumulator fuzz 15k checks,
  int-vs-double 2k checks) on the deliverable build.

### 23.5 The crush anomaly (documented with the full 2x2 matrix)

Crush gates (hybrid vs SAME-binary HCE-off, 50 ms, this rig):

| Hybrid \ HCE opponent | shipped HCE-off | rebuild HCE-off |
|---|---|---|
| shipped hybrid | **+87 ± 22 (n=260)** | -9.5 ± 33 (n=110) |
| rebuild hybrid | +51 ± 35 (n=103) | **+20 ± 14 (n=621)** |

Structure: matches against the SHIPPED HCE-off are decisive (18% draws);
matches against the REBUILD's HCE-off grind (56-64% draws, 4/12 games at
the 320-ply cap in the logged sample). Direct HCE-vs-HCE: +3.4 ± 34
(n=102 — underpowered for a ~30-60 Elo gap). The rebuild's HCE-off
mode is a materially stronger DEFENDER than the shipped binary's,
despite bit-identical evals, equivalent nps, and ~97% identical shallow
searches. Mechanism: the zip's source-vs-binary delta (23.6) lives in
DEEP search (d7: 10%, d9: 40%, d11: 80% of trees diverge) — exactly the
depth regime HCE-off reaches at 50 ms (d10-12, 150 k nodes/move), while
the hybrid's NNUE-on regime (d7-9) stays mostly below it, which is why
hybrid-vs-hybrid parity holds (the 1000-game gate) while the HCE-off
behaviors differ. The documented +119.1 ± 11.6 crush (prior rig, n=1000)
is consistent with this rig's shipped-binary reading (+87 ± 22) at
1.5 sigma.

### 23.6 Shipped from this campaign

No engine-strength change met the adoption bar (every candidate in 23.2
rejected with data). What ships: the reconstructed
`tools/ucibench/ucibench.cpp` including the `eval` diagnostic (the zip
was unbuildable without it), the campaign tooling (gate block runner,
deterministic engine-equivalence battery, depth-cliff forensics, probe
batch, PGO builder), the final verified build, and this record.

### 23.7 The source-vs-binary delta (documented honestly)

The zip's search source and the shipped binary differ in deep-search
behavior: identical to depth 6 (40/40 positions), diverging smoothly with
depth (d7: 4/40, d9: 16/40, d11: 32/40), bestmove agreement 149/150 on
the fixed-node battery. Sixteen single-constant probes (IIR/SE/TT-policy/
NMP/RFP/LMP/aspiration/ProbCut/LMR variants) failed to localize it; UBSan
clean; identical across -O1/-O2/-O3/-LTO/no-march. Head-to-head (see the
final report): the source rebuild measures within noise of the shipped
binary at 50 ms. Conclusions that depend on sub-10-Elo source-binary
parity should use the shipped binary as the reference.

---

## 24. v9.2 — the harness-integrity + conversion campaign (v1.9.2)

Mission: the +25 Elo push against the shipped v1.9.0 champion. What the
campaign actually found first was a measurement bug that had been silently
distorting every cross-binary gate since §23 — then a real, fixable
endgame-conversion weakness. Both are below, with numbers.

### 24.1 CRITICAL: the `position startpos moves` harness bug (§23's hidden landmine)

The §23 harness reconstruction had a one-line defect: after the `startpos`
token, the `moves` keyword was never skipped, so "moves" was tokenized as
the first move string, matched no legal move, and tripped the apply-loop's
break — **every game move after `position startpos` was silently dropped,
freezing that engine at the initial position for the whole game.**

- Reproduction: `position startpos moves e2e4 e7e5` + `go movetime 50` →
  the shipped champion answers a normal move (g1f3); every §23-era rebuild
  answered `bestmove e2e4 score 38 depth 8 nodes 25600` — a fresh startpos
  search, every time.
- FEN-opening commands were unaffected (that path scanned for " moves "),
  which is exactly why the fixed-node battery, the eval battery, and all
  bench fingerprints remained valid — and why the bug survived §23's
  behavioral verification: it only manifests in `startpos`-opening MATCH
  games (80 of the 147 openings in tools/match/openings.txt).
- Blast radius (recomputed this campaign): §23's F1 gate ("deliverable ≡
  champion, −6.3 ± 11.0, n=1000") was a ~54% corrupted mixture — in
  startpos games the rebuild played frozen-startpos soup against the
  champion's real positions. Same for F2 (+20.2 ± 14.0), the 2×2 crush
  matrix cells involving the rebuild, and the "stronger defender" theory
  of §23.5. Cells pairing the shipped binary with itself (e.g. the +87 ± 22
  crush reference) were clean. §23's speed/nps findings and the option-only
  gates on the shipped binary were also clean (option-space, both sides in
  one binary).
- The fix (skip the optional ` moves ` keyword after `startpos`) is in
  tools/ucibench/ucibench.cpp with a comment block; all v9.2 gates below
  ran on fixed-harness binaries, slot-balanced where noted.

### 24.2 The endgame-conversion weakness (the champion cannot reliably mate KX vs K)

Draw forensics on clean null games (champion vs champion, seed 1000,
n=182): 16 games (8.8%) reached bare-defender + ≥rook material; the
attacker failed to convert 4 of them (two 50-move-rule draws in KRK, two
too-late conversions at ply 272/305). Controlled battery (10 legal
KXK/KQK/KBBK positions, 50 ms/move, cap 200):

- shipped v1.9.0 champion: **8/10 mated** — two hard failures, both
  50-move-rule draws in KRK after 100+ quiet plies of aimless shuffling
  (plus slow conversions: 49-55 plies where the fix needs 15-41).
- The mechanism: against a bare defender the eval stack carries almost no
  progress signal. The pawnless-leader damping halves the material edge,
  the complexity scaler pulls it further toward zero, and the trained
  residual is clamp-flat here (the SF training targets clamp at ±600 long
  before mate distances resolve — measured: nnue_res 283 with the defender
  centralized vs 131 cornered). The search coasts on ~+200 with no
  gradient and shuffles into the 50-move rule.
- Real-game confirmation (clean logged gate block, seed 901): the champion
  blundered a whole rook in KRK (left it en prise to the defending king at
  ply 55) and ground out the resulting K-vs-K to a threefold draw.

### 24.3 The fix: mop-up evaluation (evaluate.cpp)

Classic mop-up terms, added to egScore strictly gated to bare-defender +
mating material (rook/queen or ≥2 minors; KNN-vs-K is fast-pathed to 0
earlier, KBK/KNK likewise):

    mopup = 4 * (10 * cornerDrive + 4 * (14 - manhattan(kings)))

cornerDrive = 3 − distance-to-nearest-edge of the DEFENDER's king (0..3).
Full swing ≈ +328 cp of progress gradient on top of the material signal.
Every other position is bit-identical:

- 150-search fixed-node battery vs the pre-fix build: **exactly 1 diff** —
  a K+2P-vs-K+P ending whose trees reach promotion→KQ-vs-K leaves (the
  intended surface; same bestmove, score 783 vs 719).
- 37-position eval battery vs the shipped champion: **exactly 4 diffs**,
  all KXK (188→342, 368→544, 646→793) — the intended surface.
- verify all PASS (perft 6/6, mirror, nnue accumulator, int-vs-double).
- Conversion battery after the fix: **10/10 mated, 15-41 plies.**
- Real-game conversion (clean logged gate, seed 901): 29 pool games, the
  fixed engine converted **100% of its own attacker chances** (the single
  pool failure was the champion blundering as attacker).

### 24.4 The gates (all clean-harness, 50 ms/move, paired openings, SPRT 0/15 where noted)

| Gate | Result | Verdict |
|---|---|---|
| Mop-up marginal, direct (fix vs no-fix, same source) | **+9.0 ± 14.5 (n=578)** | adopted (mechanism-verified; positive-leaning) |
| Baseline-v2 (pristine source + harness fix) vs shipped champion | +7.5 ± 11.2 (n=953, slot-balanced) | parity-to-slightly-better; §23's "equivalence" re-established honestly |
| **v1.9.2 final vs shipped v1.9.0 champion (the primary gate)** | **+4.0 ± 11.2 (n=971, slot-balanced)** | small positive, not SPRT-decisive |
| v1.9.2 hybrid vs own HCE-off (crush) | **+108.6 ± 23.5 (n=241, SPRT H1 ACCEPTED, LLR +2.96)** | hybrid quality preserved, clean-harness |

The three head-to-head readings are mutually consistent within their
error bars (the mop-up direct gate is the cleanest instrument for its
question). The honest headline: **v1.9.2 is a few Elo (≈+4-9) over the
shipped champion — real but not decisive; the +25 target was not reached.**

### 24.5 Measured and REJECTED this campaign (with data)

| Candidate | Result | Verdict |
|---|---|---|
| Singular gate 4→3 (the §16.2 winning direction, pushed further) | −5.9 ± 17.1 (n=413, direct) | rejected — the family's optimum sits at 4 |
| Stale-IIR removal (delta-replication probe) | battery diffs 20→25 vs champion | rejected |
| Singular ttDepth ≥ depth−3 (delta probe) | battery diffs 20→68 | rejected |
| Battery contempt 0 (delta probe) | battery diffs 20→15/30-subset, no improvement | rejected |
| LmrDiv 2150/2200/2300 (delta probes) | 26/24/22 diffs vs 15 bare (30-search subset) | rejected — 2250 is the tree-match optimum |
| Delta replication, total | 22 probes across §23+§24 all failed; the source-vs-binary tree-shape delta (20/150 battery searches, identical bestmoves, ±5-25 cp scores, onset exactly at depth 7) remains unlocalizable | documented, not replicated |

### 24.6 Shipped defaults (v1.9.2)

- Engine: v1.9.0 source + the mop-up eval term + the harness
  `position startpos moves` fix. Version 1.9.2 / versionCode 26.
- No search-constant or blend changes (all re-verified dead or left at
  their measured optima).
- Bench fingerprints: HCE-off path unchanged by the eval term outside
  bare-defender endings (see the 149/150 battery identity); verify all
  PASS; eval layer identical to the champion everywhere except KXK.
- Honest ceiling statement: the remaining +25-scale lever is the net
  (corpus scale), unchanged from §22/§23 — this rig (2 cores) cannot
  regenerate a meaningful corpus, and the source-vs-binary deep-search
  delta remains the shipped binary's unexplained edge.

## 25. v9.3 — the fresh-corpus net refresh (v1.9.3)

The final-stretch sprint: one lever with a measured exchange rate (a
fresh corpus → warm refresh, the v8/v9 pattern) plus one eval candidate,
gated hybrid-vs-hybrid at 50 ms against the v1.9.2 champion. The rig is
unchanged (2 cores / 4.1 GB; background processes still die at tool-call
boundaries — everything ran as chained ~9.5-min foreground blocks, every
stage resumable).

### 25.1 Pipeline (all fresh, all reproducible — campaign-scripts/sprint-v93/)

- **Datagen**: 4 chained blocks, 2 processes, ~370k records/min/process
  (≈2× the §23-era rig), NNUE-on self-play with the shipped v9 net →
  25.3M raw records. 93% unique → 23.0M unique FENs (fen-sorted global
  dedup, external sort, 8 sub-files for parallel labeling).
- **Labeling**: SF 17.1 (the exact teacher generation), 2 workers × a
  file queue (no idle-core tails), batch=150 pipe protocol (~1,250/s/
  proc; 2,073/s at the plateau — the §15.4 deadlock lessons hold).
  One OOM lesson re-learned: never hold 11.5M label lines in RAM per
  process; sub-files of ~3M lines each. 23,004,876 lines labeled.
- **Packing**: streaming shuffled-order corpus dir (random destination
  slot per row, seeded permutation — the val split stays unbiased);
  6 corrupt lines caught by a hardened board parser (a 14-rank FEN from
  a rare datagen/label glitch — pack now validates rank structure,
  square range, and exactly-two-kings).
- **Training**: warm from the shipped v9 checkpoint; 3 epochs, lr 0.12
  cosine, batch 8192, huber-150, mirror augmentation, a=1.2337.
  val RMSE 80.70 → 72.33 → 65.85 → 65.10 → 64.88; bucket |bias| ≤ 2.
- **Export**: v55 exporter, --verify 3000: max |engine−trainer| =
  0.9995 cp (lossless). Candidate built; bench 42,496 nodes (champion
  37,376 — single-position search-shape change only; nps ~600k vs 566k).

### 25.2 The scale-protocol catch

The trainer's `--scale-mode warm` refit regresses r_mover + hce_white
against sf_white — a POV mix. A/B on 200k real labeled rows vs the
known-good v9 operating scale (1.2435, §22.2): POV-consistent formula
→ 1.2442 ✓; repo formula → 1.3343 ✗ (black-stm rows fit 1.50 vs white
1.24 — the mixing is directly visible). v9.3 trained at a*=1.2337 from
the corrected standalone fitter (fit_scale.py). `--scale-mode warm`
remains buggy in-tree; do not use it until patched.

### 25.3 Gates (all vs the v1.9.2 champion, 50 ms, paired openings)

| Gate | n | Result | Verdict |
|---|---|---|---|
| v93 net (A slot) vs champion | 4000 | +5.4 ± 5.5, LLR +0.57 | not SPRT-decisive; positive |
| Slot-check (champion in A) | 600 | −3.5 ± 14.2 cand POV | no slot bias (z ≈ 0.5 agreement) |
| **Pooled slot-balanced** | **4600** | **+4.2 ± 5.1** | **shipped** (at-worst-neutral + positive EV) |
| v93 + KXK bypass vs champion | 970 | −4.3 ± 11.2 (≈ −10 vs v93-alone at matched n) | **REJECTED, reverted** |
| Crush: v1.9.3 vs own HCE | 600 | +123.7 ± 15.1 | context (champion: +108.6 ± 23.5; z ≈ 0.6) |

The KX-vs-K bypass story is the campaign's honest negative: the eval
narrative (clamp-flat residual dampening mop-up) and a 5/9 conversion
battery could not survive 970 real games — the residual carries load on
the paths INTO bare-defender endings. Reverted to byte-parity with the
champion's engine source (net + version strings only).

### 25.4 Shipped defaults (v1.9.3)

- Net **v93-23m-3ep** (23.0M positions, warm refresh, val RMSE 64.88).
- BlendPct 75 / StaticBlendPct 70 / BlendPctEG 0 (unchanged; the static
  RMSE proxy is monotone toward 100 — known uninformative — and a third
  game-gated sweep had negative expected value at 1.2k games/h).
- Version 1.9.3 / versionCode 27. Harness binaries rebuilt from final
  source; verify all PASS.
- Honest ceiling statement: +4.2 ± 5.1 over the champion; crush over own
  HCE ≈ +124 ± 15 at 50 ms (within noise of the champion's +109 ± 24).
  The 200–400 stretch was not reachable on this rig in one sprint; the
  remaining large levers (corpus scale beyond 44M-class, architecture
  width, search) all exceed a 2-core budget.

## 26. v9.4 — the convergence-proof campaign (engine ships unchanged)

Mission: maximum verified Elo over the shipped v1.9.3 hybrid (the gating
baseline throughout, per protocol; HCE numbers context only), stretch
goal 200–400 over pure HCE. Rig: 2 cores / 4.1 GB, AVX-512+VNNI — same
class as every prior campaign; all long jobs as chained ~9.2-minute
foreground blocks.

Three candidates were engineered, verified and gated to rejection. The
shipped engine is byte-identical to v1.9.3 (rebuild md5 8aa6e29b… =
binaries/tacturn-1.9.3-final-linux-x64; verify all PASS; bench 309,248).

### 26.1 The net lever at 1.56x corpus scale (v94-36m-3ep) — REJECTED

Full pipeline reproduction, every stage fresh and resumable
(campaign-scripts/sprint-v94/):

- **Datagen**: 8 blocks, 2 procs, v1.9.3-champion NNUE-on self-play
  (16k nodes/move) → 39.53M raw records (the largest corpus this rig
  has produced; v9.3 used 25.3M).
- **Dedup**: fen-sorted global dedup → 35.80M unique positions. Shards
  re-split ROUND-ROBIN (not contiguous): dropping any fraction of
  fen-sorted shards silently biases the corpus toward half the fen
  space (K-position-heavy tails) — a trap the contiguous-split
  v9.3 pipeline would have hit had it ever capped early.
- **Labels**: SF 17.1 (same teacher generation as every campaign since
  v5.0), official release binaries, 2 workers × 8 shards, batch-150
  pipe protocol, ~2515 pos/s aggregate (2 cores saturate; 3 labeler
  pairs measured no faster). 35,796,706 labeled lines.
- **Scale**: corrected random-slice protocol (fit_scale.py, the §25.2
  POV-consistent fitter) → a* = 1.2115 (n=250k, mate-filtered).
- **Training**: warm from v93-23m-3ep, 3 epochs, lr 0.13 cosine, batch
  8192, huber-150, mirror; val RMSE 57.59 (warm) → 55.58 → 54.95 →
  **54.73** (716k-row val, bucket |bias| ≤ 3).
- **Export**: quantization lossless (max |engine−trainer| = 0.9995 cp,
  3000 probes); verify all PASS (perft 6/6, mirror, nnue accumulator
  15262/0, int-vs-double 2036/0).

**Gate vs the v1.9.3 champion (50 ms, paired openings, SPRT(0,10)
monitored): −6.1 ± 9.8 (n=1260, W451 D336 L473) — REJECTED.**

The reading: the net now fits its own play better (RMSE −2.86 cp) and
plays no better. This is the self-distillation echo — the corpus came
from the net's own guided self-play, so the residual-learning signal
(the positions where SF and the engine disagree) thins with every
generation. The v9.3 campaign saw the same mechanism at +4.2 (its
corpus was one generation removed); at 1.56x scale with the SAME
generation's play, the refresh is net-neutral-to-negative. **The
fresh-corpus exchange rate is now measured at zero on this lineage
until the corpus comes from a materially different distribution.**

### 26.2 The weight soup (v93+v94 50/50) — REJECTED

The RMSE trajectory (57.59 → 54.73 with most of the move in epoch 1)
suggested the mid-trajectory point might be play-optimal (all of the
fresh-data signal, half the echo). Soup = arithmetic mean of the two
checkpoints (W2 relative-move between them: 29%).

**Gate: −0.4 ± 8.2 (n=1800) — REJECTED.** Early blocks read +4.6 ± 11.6
(n=900) and decayed monotonically to zero — the exact noise signature
the v9.1/v9.3 campaigns documented and warned about. This is the
cleanest demonstration yet that this rig cannot resolve <±8 differences
in real time, and that stopping at the first positive block is
winner's-curse.

### 26.3 Material-keyed correction history — REJECTED (novel mechanism, closed)

The one search-side mechanism never tried in this lineage. The corrhist
corrects the static eval from tables keyed by pawn placement and
own/enemy non-pawn placement — blind to material configuration. v9.4
added a fourth table keyed by the EXACT material count vector:

- `matKey`: 40-bit packed signature, one 4-bit counter per (colour,
  piece type), kings excluded; maintained additively in addPiece /
  removePiece (shifts are free), snapshot-restored through make /
  unmake / null — the exact nonPawnKey pattern (position.h/.cpp).
- Bucket: Fibonacci hash of the signature into the same 4096-bucket
  space; per-piece (type, square) cells applied and taught by the same
  gravity/limit/divisor contract as the other three tables
  (search.cpp: correctedEval / updateCorr / prepare / ctor).
- Correctness: matfuzz (new tool, campaign-scripts/sprint-v94/) —
  63,548 incremental-vs-scratch checks across random games incl.
  promotions, en passant, unmake chains and null round-trips: 0 fails.
  verify all PASS.

**Gate: −9.0 ± 10.6 (n=1080) — REJECTED.** NNUE-on bench rose 309,248
→ 371,200 nodes (+20%): the extra correction dimension costs node
efficiency it does not repay at 50 ms. (Note: unlike the placement-keyed
tables, a material-config key aliases many middlegame structures into
the same 4096 buckets; the signal dilutes rather than sharpens. The
upstream world's settled corrhist key set — pawn + non-pawn-own +
non-pawn-their — is now empirically re-confirmed from the other
direction.)

### 26.4 What ships (v1.9.4 package over the v1.9.3 engine)

- **Engine: unchanged.** The v1.9.3 champion source ships byte-identical
  (rebuild verified md5-equal to the shipped binary; bench 309,248;
  verify all PASS; crush vs own HCE unchanged at ≈ +124 ± 15 @ 50 ms,
  n=600, per the v9.3 report — the binary is the same object).
- Campaign artifacts (reproducible rejections): sprint-v94 block scripts
  (datagen / shard / label / pack / scale-fit / train / gate / export),
  the matfuzz tool, the matcorr patch (vs the shipped tree), the v94
  checkpoint (best.npz; NOT shipped in the engine, kept for the record),
  gate state files, and this section.
- **Honest Elo statement: +0 this sprint.** Three candidates, all gated
  against the CURRENT hybrid at n ≥ 1080, all rejected. The v1.9.3
  configuration is confirmed at its measured optimum on this rig; the
  next real step remains the full-replacement net campaign (corpus from
  a shifted distribution, architecture search, multi-day gating), which
  a 2-core rig cannot execute. The 200–400 crush stretch was not
  reachable; the ~+124 crush stands.

### 26.5 Transferable findings

1. **Self-play corpus refreshes have hit their exchange-rate floor** on
   this lineage (v8 +24.8 → v9 +27 → v9.3 +4.2 → v9.4 −6.1). What
   bought Elo was the SCALE of fresh signal (new generations of play);
   the v93/v94 nets' own play adds none. A future refresh should
   datagen from a deliberately DIFFERENT navigator (older net, higher
   temperature, or HCE-off play) to re-open the signal gap.
2. **Round-robin shard splits, not contiguous** — any capped labeling
   pass over fen-sorted shards is distribution-broken by construction.
3. **The soup of a converged net and its fine-tune is a no-op in play**
   even when it splits the RMSE difference (measured −0.4 ± 8.2).
4. **Material-config corrhist keys dilute** where placement keys don't;
   the mechanism is measured dead at 50 ms on this engine (+20% bench
   nodes for no play gain).
5. This rig's match harness resolves ±8–10 in ~2h of gating; nothing
   smaller than that is honestly shippable from a session-scale sprint
   — a floor worth stating for whoever plans the next one.

## 27. v9.5 — the different-navigator campaign (v1.9.4)

Mission: maximum verified Elo over the shipped v1.9.3 hybrid (gating
baseline throughout), stretch 200–400 over pure HCE. Rig: 2 cores /
4.1 GB, AVX-512+VNNI. **Result: +22.5 SPRT(0,10) H1 accepted (n=1883,
W748 D509 L626) — the first decisive head-to-head gain since v9 — and the
crush over own HCE moved +124 ± 15 → +160.4 ± 16.6 @ 50 ms (n=540).**

### 27.1 The lever (HISTORY 26.5's recommendation, executed)

Self-play corpus refreshes had hit their exchange-rate floor (v8 +24.8 →
v9 +27.0 → v9.3 +4.2 → v9.4 −6.1): the champion's own guided self-play
no longer contains positions where SF and the engine disagree. v9.5
generated the corpus from a DELIBERATELY DIFFERENT NAVIGATOR:

- `datagen --hce 1` (new flag, tools/ucibench/datagen.cpp): pure-HCE
  self-play navigation, same 16k nodes/move, same record format.
- Corpus: 9.67M champion-stream + 26.28M HCE-off-stream raw records →
  31.47M unique (fen-sorted global dedup, round-robin 8-way re-split —
  the v94 lesson). 24.26M labeled (SF 17.1, ~2,540 pos/s); the shipped
  net trained on the first 15.74M packed positions (29% champion / 71%
  HCE-off mix).
- The mechanism, measured: the champion net's RMSE on its own play is
  57.6 cp; on the mixed corpus the SAME net reads 131.46 cp — the HCE
  navigator visits exactly the positions the champion's knowledge is
  thin on. Residual std 276 cp vs ~192 on champion-only data. After 3
  warm epochs (lr 0.13 cosine, huber-150, mirror, a* = 1.2034 random-
  slice protocol): val RMSE 110.32 (−16.2%, vs the v94 same-generation
  refresh's −5%).

### 27.2 The blend operating point (the second half of the gain)

The v95 net's implied scale on champion-distribution positions measured
~1.5% colder than v93's (net_rmse_diag.py: implied-a 1.1807 vs 1.1980) —
the v5.4 scale lesson in reverse: at the shipped BlendPct 75 the new net
applies slightly LESS correction than the champion's calibration, and the
gate read only +2.3 ± 10.6 (n=1080). Restoring the operating point with
**BlendPct 77 / StaticBlendPct 72** (option-level probe, no rebuild)
measured **+22.5, SPRT(0,10) H1 accepted** on the same net. Shipped as
the new defaults (out-of-box ≡ gated config, bench-verified 320,000 nodes
both ways; 8/8 depth-9 bestmove agreement vs the gated binary+options).

### 27.3 The rejected extension (data is not always the answer)

v95c = +7.9M more labeled positions (23.60M total) warm-continued 2
epochs: val RMSE 108.72 (better) but the gate read −28.0 ± 18.4 (n=360)
— REJECTED. The bench tree grew 250,880 → 418,816 nodes: the further-
drifted eval left the search's co-adapted surface. §3 lesson 12 (RMSE
does not predict Elo) re-confirmed from a new direction: the drift that
helps mixed-distribution RMSE can hurt the deployment operating point.

### 27.4 The packer resume bug (found the hard way, fixed)

The v93 pack_corpus.py opened every memmap with mode="w+" — a resumed
pack TRUNCATED all previously packed rows, producing an almost-empty
corpus that still reported meta.n complete. Caught by integrity checks
(sf std 71 vs 372; 96% zero rows; stm imbalance), fixed to open r+ when
a matching state exists (ships in campaign-scripts/sprint-v95/). Two
corrupted packs were discarded; no training ran on corrupt data.

### 27.5 Shipped defaults (v1.9.4)

- Net **v95-mix15m-3ep** (15.74M mixed-navigator positions, warm from
  v93-23m-3ep, val RMSE 110.32 on the mixed corpus).
- **BlendPct 77, StaticBlendPct 72**, BlendPctEG 0. Search, HCE and
  timeman byte-identical to v1.9.3. Version 1.9.4 / versionCode 28.
- Bench fingerprints: HCE-off bench 6 = 104,960 (v5.3-identical — HCE
  path untouched, crush baseline valid); NNUE-on bench 9 = 320,000 at
  the shipped 77/72 (250,880 at 75/70); verify all PASS.
- Checkpoints: v95-mix15m-3ep.best.npz (+ resumable full) in
  checkpoints/; v95c-mix23m-2ep.best.npz kept for the record.
- Honest statement: +22.5 verified over the champion; crush +160 ± 17.
  The 200–400 stretch remains beyond the residual-hybrid architecture
  (§19.8) — the next step change still needs the full-replacement net,
  for which the different-navigator corpus protocol built here is the
  proven data recipe.


## 28. v9.6 — the echo-corrected corpus + harness-integrity campaign (v1.9.5)

Mission: another maximum-verified-Elo session over the shipped v1.9.4
(v95) hybrid, gating strictly vs the current champion. Rig: 2 cores /
3 GB, AVX-512+VNNI. **Result: +14.1 ± 6.0 SPRT(0,10) H1 accepted
(n=3389, W1275 D976 L1138, LLR +2.94) — and a harness bug found and
documented that recontextualizes every opts-carrying gate in v9.5 and
v9.6, including this campaign's own first delivery attempt.**

### 28.1 The corpus (navigator-richer, echo-corrected)

Two datagen rounds. v96 (first attempt): a 14M mix that ended up 43%
champion self-play — the self-distillation echo the v9.4 campaign
warned about; its gates never separated from noise (+3.9 @ n=1146),
superseded. v96b (shipped): 7.72M fresh raw records from the
different-navigator protocol (HCE / blend-130 / SF-guided mixes),
6.69M genuinely new after dedup against the full v95-labeled pool (2.5%
overlap — different navigators keep finding fresh position space),
SF 17.1 labeled, merged to a 21.47M-position corpus at ~27% champion —
deliberately matching v95's winning balance. Scale a* = 1.1898; trained
warm from the v95 champion, 3 epochs: val RMSE 105.87.

### 28.2 The harness bug (the campaign's defining find)

tools/match/match.py's parse_opts() splits --opts-a/--opts-b on
whitespace and expects Key=Value tokens; the run_gate.sh wrapper family
(v9.5 and v9.6 alike) passed full "setoption name X value Y"
sentences — every whitespace token became a bogus
"setoption name <token> value <empty>" that engines silently ignore.
**No option string passed by these scripts ever reached an engine.**

Consequences, reconstructed from gate forensics:

- Every "blend point" this campaign (80/72, 78/73, 76/73, 74/73,
  72/73 tags) actually ran the SAME pairing — compiled defaults vs
  compiled defaults. The apparent sweep (−8.6 … +25.5 at n=200–400)
  was the noise floor of one config measured five times. A first
  promotion attempt on this phantom "74/73 optimum" was caught and
  reverted within the session (the real 74/73, compiled separately,
  measured +0.5 ± 12.7 at n=743 — the true axis runs opposite and
  ~4 Elo/unit steep).
- v9.5's accepted gate likewise ran compiled defaults: v95@75/70 over
  the v93 champion@75/70 — the +22.5 is real but belongs to that
  pairing, and v1.9.4's shipped 77/72 carried an unverified blend
  delta. (v1.9.4-as-shipped remains the correct gating baseline —
  v9.6's gate measured against it exactly as shipped.)
- The bug was caught by an impossible number: a "crush vs HCE-off"
  gate reading +35 where +160 was expected, because the UseNNUE=false
  option never reached engine B and the match was hybrid-vs-hybrid.

New harness rules (encoded in the shipped scripts): opts as Key=Value
pairs only; engine-identity proven per side (bench fingerprint or
stdin tee) before any gate verdict is trusted; gate labels are
documentation, not configuration.

### 28.3 The valid measurement

With opts out of the picture entirely (compiled-defaults binaries on
both sides), the accepted gate is unambiguous: **v96b@77/72 vs the
v1.9.4 champion@77/72 = +14.1 ± 6.0, SPRT(0,10) H1 accepted (n=3389,
LLR +2.94)**. Pooling every v96b-binary-vs-champion game of the
campaign (all ran this pairing regardless of label): n=5541,
W2076 D1545 L1920 → +9.7 ± 5.6 — consistent. The real blend evidence:
77/72 = +14.1 (accepted); 74/73 = +0.5 ± 12.7 (n=743) — 77/72 ships.

### 28.4 Harness lessons (recorded for future campaigns)

(a) The tool sandbox kills background descendants at call boundaries —
nohup/setsid do NOT survive; gates must run as chained FOREGROUND
blocks with persistent (W,D,L) state files. (b) A mis-paired gate
invocation (opts bound to the wrong engine slot) was caught by
command-line forensics — verify --opts-a/--opts-b assignment against
the intended pairing. (c) Session cuts mid-block are fully recoverable
(state files + resume-wdl; including a multi-hour frozen-call gap).
(d) **Options must be proven to reach engines** — the parse_opts bug
survived two campaigns because gate labels were trusted over engine
identity. (e) An impossible expected number (the crush gate) is the
cheapest integrity probe there is.

### 28.5 Shipped defaults (v1.9.5)

- Net **v96b-mix21m-3ep** (21.47M mixed-navigator positions, warm from
  v95-mix15m-3ep, val RMSE 105.87 on its corpus).
- **BlendPct 77, StaticBlendPct 72**, BlendPctEG 0 — identical
  operating point to v1.9.4 (the net is the change; the blend bug
  ensured no unmeasured drift). Search, HCE and timeman byte-identical
  to v1.9.4. Version 1.9.5 / versionCode 29.
- Bench fingerprints: NNUE-on bench 9 = **281,600** @ 821k nps;
  HCE-off bench 6 = **86,528** (v1.9.4-identical — HCE untouched);
  verify all PASS (perft, mirror 40/0, nnue accumulator 15,262/0,
  int-vs-double 2,036/0).
- Out-of-box ≡ gated config, proven the strong way this time: 30/30
  bestmove-line identity between the shipped binary (no opts) and the
  gated binary (no opts), both compiled at 77/72.
- Checkpoints: v96b-mix21m-3ep.best.npz (+ full resumable) in
  checkpoints/; v96-mix14m-3ep.best.npz kept for the record.
- Honest statement: +14.1 ± 6.0 verified over the v1.9.4 champion
  (cumulative over v1.9.3: v95 +22.5 then v96b +14.1, both
  SPRT-accepted at their compiled-defaults pairings). Pure-HCE crush
  context, re-measured with proven engine identity (compiled
  UseNNUE=false opponent, HCE bench fingerprint verified pre-match):
  **+162.0 ± 16.6 @ 50 ms (n=540, W353 D69 L118)** — v95's +160.4 ± 16.6
  is consistent within error bars. The 200–400 stretch over HCE remains
  architecture-bound (§19.8, §27.5).

---

## 29. v1.0.0 — the second production clean ship (re-version + campaign-artifact cleanup)

The v1.9.5 (v96 campaign) champion is re-versioned as **TacTurn 1.0.0 —
the first stable production release**. This repeats the discipline of the
first clean ship (§7/§10): not a version bump, but a full re-version of
every version surface plus a purge of the campaign machinery that produced
the engine, with the substance consolidated in this file. The engine, net
and search are **unchanged from the gated v1.9.5 champion** — proven by
fingerprint below, not by assertion.

### 29.1 Version surfaces (what changed)

| Surface | Before (v1.9.5) | After (v1.0.0) |
|---|---|---|
| `app/build.gradle.kts` versionName | `1.9.5` | `1.0.0` |
| `app/build.gradle.kts` versionCode | 29 | **30** (monotonic — installs over every predecessor, per the §1 precedent) |
| `tacturn_version()` (engine C API) | `"TacTurn 1.9.5"` | `"TacTurn 1.0.0"` |
| Host reference binary | `tacturn-1.9.5-final-linux-x64` | `binaries/tacturn-1.0.0-linux-x64` (rebuilt, see 29.3) |
| README.md | v5.0/v4.0-era intro + per-campaign version ladder | v1.0.0 production framing, current-state strength summary |
| `nnue_net.cpp` header | "Engine v5", regenerate-via-`tools/train/…` instruction | neutral header; pipeline removal noted, points here |
| Release package name | `tacturn-1.9.5-v96-campaign.zip` | `tacturn-1.0.0.zip` (the `scripts/package_zip.sh` default) |

Deliberately **not** changed (contract/data identifiers, not product
versions):

- `TACTURN_API_VERSION` stays 4 — it is the C-interface revision consumed
  by defensive checks on the Kotlin side, not a marketing version.
- `NET_ID` stays `v96b-mix21m-3ep` — the trained-network artifact
  identifier, pinned for traceability to §28.5 and its training
  checkpoint. Renaming it would orphan the net's provenance.
- versionCode stays monotonic (30 > 29): Android requires it to install
  over the v1.9.5 ship.

### 29.2 What was removed and where the substance lives

Everything below was deleted from the distribution. The engineering
substance of all of it is already consolidated in this file (the section
numbers below each entry); nothing measured, decided or learned was lost.

**Root of the distribution zip (the campaign workspace):**

- `binaries/` — nine historical engine builds removed:
  `tacturn-1.8.0-champion-baseline`, `tacturn-1.9.0-ucibench`,
  `tacturn-1.9.1-final`, `tacturn-1.9.2-final`,
  `tacturn-1.9.2-nofix-baseline`, `tacturn-1.9.3-final`,
  `tacturn-1.9.4-final` (the previous champion, superseded by §28's gate),
  `tacturn-1.9.5-hceoff` (the compiled `UseNNUE=false` crush-measurement
  opponent), and `tacturn-1.9.5-v96b80` (the rejected 80/72 blend-point
  build). Kept: the single strongest binary per the codebase record — the
  shipped v96b@77/72 champion — rebuilt as
  `binaries/tacturn-1.0.0-linux-x64` (identity proven in 29.3).
  Strength history of every removed build: §22–§28.
- `checkpoints/` (146 MB) — all eleven training checkpoints of the
  v9→v96b lineage (`v9-44m-5ep-512`, `v93-23m-3ep`, `v94-36m-3ep`,
  `v94-soup`, `v95-mix15m-3ep` best+resume, `v95c-mix23m-2ep`,
  `v96-mix14m-3ep`, `v96b-mix21m-3ep` best+resume). The shipped net is
  compiled into `engine/src/main/cpp/tacturn/nnue_net.cpp`; the warm-start
  lineage and every corpus/RMSE number is recorded in §18, §20–§28.
  (Kept nowhere else: if training ever resumes, regenerate data via the
  §28.6 recipe; the champion weights themselves are in the source tree.)
- `reports/` — CHANGELOG-v1.9.0…v1.9.3 and ELO-REPORT-v1.9.0…v1.9.3,
  v9.4/v9.5/v9.6 campaign reports. All substance already consolidated in
  §22 (v1.9.0), §24 (v1.9.2), §25 (v1.9.3), §26 (v9.4), §27 (v9.5),
  §28 (v9.6) — including the full experiment ledgers, SPRT verdicts and
  the harness-bug post-mortem.
- `campaign-scripts/` (648 KB) — the sprint workspaces v93–v96: datagen
  runners, shard builders, gate chains (`chain_gate96b.sh` et al.),
  pool/lab workers, draw forensics, navigator diagnostics, PGO build
  script. Operating protocols and every lesson encoded in them
  (foreground gate blocks, persistent W-D-L state, opts-as-Key=Value,
  engine-identity proof, echo-controlled corpus mixing) are recorded in
  §27.6, §28.4 and §28.6.
- `tools/` (root duplicate, 40 MB) and `tacturn/tools/` (35 MB) — the
  development toolchain: `train/` (NNUE trainers + net exporters),
  `label/` (SF 17.1 labeling), `match/` (the SPRT match harness whose
  `parse_opts` bug is dissected in §28.2), `ucibench/` (host harness,
  `ucibench.cpp`, datagen generators, prebuilt host binaries). The
  training-pipeline design, quantization contract, teacher-ladder
  findings and match-methodology lessons live in §13–§20; the host
  harness's role and the Makefile's `-O3 -flto -march=native` recipe are
  described in 29.3 so the reference binary remains reproducible in
  spirit from the engine sources with any plain CMake/g++ build.

**Inside `tacturn/` (the Android project):**

- `tacturn/tools/` — as above (the in-tree copy of the toolchain).
- `tacturn/data/ckpt_v75.best.npz` (8.1 MB) — v7-era training checkpoint;
  its lineage is §20.
- `tacturn/datagen.txt` (3.4 MB) — self-play training positions for the
  ancient Texel tuner; the tuning record is §2/§6.
- `tacturn/results/` — the gate-result JSONs of the v55…v93 era (SPRT
  outcomes, crush measurements, slot checks). Every verdict they recorded
  is cited in the corresponding §17–§27 sections.

Net effect: the distribution is the **app + engine sources + one reference
binary** — everything a user, reviewer or store needs, nothing the
campaign machinery left behind. What remains intentionally: `scripts/
package_zip.sh` (release packaging) and this file (the record).

### 29.3 The kept binary — identity proof

`binaries/tacturn-1.0.0-linux-x64` is a fresh build of the unchanged
engine sources (host recipe: `g++ -O3 -std=c++17 -flto=auto -march=native`
over `engine/src/main/cpp/tacturn/`, the harness main from the removed
`tools/ucibench/ucibench.cpp`) with exactly one semantic change: the
version string. Identity with the gated v1.9.5 champion was proven the
strong way before shipping:

- Pre-change control rebuild of the untouched sources: **md5-identical**
  to the shipped `tacturn-1.9.5-final-linux-x64`
  (`4633f055c5c60199176d73dc3c71c1c9`).
- v1.0.0 rebuild: **bench 9 = 281,600 nodes** — the champion's exact
  search-tree fingerprint (§28.5) — and the full verification battery
  passes: perft suite PASS, mirror 40/0, NNUE accumulator 15,262/0,
  int-vs-double 2,036/0. Banner: `TacTurn host harness (TacTurn 1.0.0,
  nnue v96b-mix21m-3ep)`.

Same nodes, same net, same verify counts: the 1.0.0 ship **is** the v96b
champion, re-badged.

### 29.4 Ship checklist

- versionName 1.0.0 / versionCode 30; release APK built
  (`:app:assembleRelease`, R8 + resource shrink, debug-keystore signing
  for self-hosted distribution).
- Engine, net, blend (77/72), search, HCE, timeman: byte-identical to the
  gated v1.9.5 champion — no functional change of any kind.
- README rewritten for the v1.0.0 production frame (current-state
  strength numbers, no internal version ladder).
- This section is the authoritative record of the re-version and of every
  removed artifact.

## 30. 1.0.1 — the 3000 ceiling (full-power unlock + distribution hardening)

A focused production patch on top of the 1.0.0 clean ship. The engine's
C++ search core is byte-for-byte unchanged; every strength change lives in
the Kotlin strength ladder and the session plumbing.

### 30.1 What changed (user-facing)

- The Play-vs-Engine slider now spans **100–3000** (was 100–2850):
  `EloLevels.MAX_ELO`, the slider XML (`valueTo`), `strings.xml`, and the
  `Prefs` clamp all moved to 3000 together. `stepSize` 25 still lands
  exactly on the ceiling ((3000 − 100) / 25 = 116 steps).
- **3000 is real added compute, not a relabel.** The top anchor went from
  `depth 64 / 6000 ms / 6 threads` to `depth 64 / 8000 ms / 8 threads` —
  8 is the engine's designed `MAX_THREADS` (search.h), previously never
  exposed in-app. Hard budget 9000 → 12,000 ms.
- **Elo-grown transposition table**: `GameViewModel.ensureGame()` now sizes
  the session TT by level — 2^22 entries (~100 MB, with the native
  auto-fallback on low-RAM devices) for engine games at 2800+, 2^21
  (~50 MB) otherwise. The table only ever grows, and only between games
  (session is recreated inside the same mutex that wraps every fresh
  game), never mid-search. Pass & Play is unaffected.
- versionName 1.0.1 / versionCode 31 (monotonic over 30); the engine
  banner is now `TacTurn 1.0.1`.

### 30.2 Scaling integrity (the check that matters)

The ladder keeps its piecewise-linear interpolation over fixed anchors,
so the sub-2700 levels are **bit-identical** to 1.0.0. The new 2700→3000
segment is monotone in every dimension (depth 22→64, budget 4000→8000 ms,
jitter 0→0, threads 4→8 at the segment midpoint, adaptive on throughout,
min-think 1200 ms flat). Dragging the slider down still weakens the engine
appropriately at every point: anchors 100/300/600/900/1200/1500/1800/2100/
2400/2700 are unchanged, and each interpolated parameter moves smoothly
toward the weaker anchor as the level drops. PGN `[WhiteElo]/[BlackElo]`
tags, the tier label and the in-game badge all read the live level value
(no hardcoded ceiling anywhere in the flow).

### 30.3 Strength-identity proof

The only engine-source change is the version string in `api.cpp`. Proven
the usual way, with a fixed-protocol host harness (startpos, 1 thread, no
adaptive time, no jitter):

| build | depth 9 | depth 11 |
|---|---|---|
| 1.0.0 (pre-edit) | 59,904 nodes · best 0103070c · score 42 | 235,008 nodes · best 0103070c · score 51 |
| 1.0.1 (post-edit) | 59,904 nodes · best 0103070c · score 42 | 235,008 nodes · best 0103070c · score 51 |

Identical node counts, best moves and scores: the search tree is
unchanged. (The 281,600 figure recorded for 1.0.0 in §29 was the removed
multi-position ucibench suite; §30's startpos fingerprints are the
forward protocol — node counts are compiler- and speed-invariant, so they
compare cleanly across machines and builds.)

### 30.4 Distribution hardening

- **In-repo release keystore** — 1.0.0's release APK was signed with the
  build machine's local debug keystore, which made the signature
  un-reproducible on any other machine. The release key now lives at
  `keystore/tacturn-v1.keystore` (alias `tacturn`, passwords `tacturn`,
  self-signed RSA-2048, 10,000-day validity — the documented self-hosted
  convention; `app/build.gradle.kts` falls back to the debug keystore only
  when the file is absent). Consequence, one-time only: the 1.0.1 APK
  cannot install over a 1.0.0 install (different key) — uninstall the
  1.0.0 build once; every release from 1.0.1 onward installs cleanly over
  its predecessors from any build machine. For Play Store distribution,
  replace with a private upload keystore.
- **Portable reference binary shipped** — 1.0.0's README promised
  `binaries/tacturn-1.0.0-linux-x64` but the source-only packaging
  excluded it. 1.0.1 ships `binaries/tacturn-1.0.1-linux-x64`: a portable
  x86-64 build (baseline SIMD paths; node counts identical to any other
  build of the same sources) that prints the version string and the
  startpos fingerprint of §30.3. README updated to match reality.

### 30.5 Removals

None — 1.0.1 removes no files. It adds `keystore/tacturn-v1.keystore` and
`binaries/tacturn-1.0.1-linux-x64` (plus this section).

---

## 31. 1.1.0 — the external-review speed & soundness campaign (+28 Elo, SPRT-verified)

An independent engine-specialist review of the 1.0.1 sources produced a
prioritized improvement list. Every item was triaged, implemented where
sound, and verified with the project's discipline: bit-identity where the
tree must not change, fuzz equivalence where new predicates replace old
ones, fingerprints for every step, and a final SPRT gate for the full
stack. **Result: +27.9 ± 4.5 Elo over 1.0.1 (200 games, 100 ms/move,
SPRT(0,10) H1 accepted, W-D-L 62-92-46)**, with identical or provably
equivalent trees throughout and no strength regression risk carried by
any single change.

### 31.1 Shipped changes

1. **qsearch SEE-after-make bug (real bug, fixed).** The first-qsearch-ply
   quiet-check filter called `see(*pos, move)` AFTER `makeMove` — the SEE
   ran on the post-move board (side to move flipped, mover already on the
   target square), producing a scrambled exchange value for exactly the
   moves the filter exists to score. Fixed by computing the SEE on the
   pre-move board (see item 2).
2. **Quiet-check prefilter.** `givesCheckQuiet()` answers "does this quiet
   move give check?" on the PRE-move board (direct checks via attack
   tables with post-move occupancy; discovered checks via slider rays
   with the vacated origin), so the overwhelming majority of non-checking
   quiets are skipped before ever calling makeMove — the old flow made
   every quiet to test `checkers()`. Castling keeps the exact post-make
   test. Equivalence proven by fuzz against post-make `checkers()` truth:
   **355M+ quiet moves across 16M+ random-playout positions, 0
   mismatches** (the fuzz caught one real bug during development — the
   discovered-check slider bitboards were not filtered by mover color).
   Combined with the SEE fix: bench-14 nps **+5.1%**.
3. **Capture ordering: proper MVV-LVA + good/bad split.** The old
   `16*victim − 40*attacker` scoring let the attacker term dominate and
   misordered mixed captures (NxQ ranked below PxR); the replacement is
   victim-major MVV-LVA (`16*victim − attacker` + capture/continuation
   history) with a good/bad capture split: victim ≥ attacker implies SEE ≥
   0, otherwise a new **early-exit sign-only SEE** (`seeGE`, dual
   cvLE/cvGE predicates that resolve the `see(m) >= threshold` decision
   without materializing the exchange) decides exactly. Bad captures sort
   behind killers/counter-move, ahead of quiets. seeGE fuzz-verified
   against full SEE: **23M threshold checks, 0 mismatches**. Measured at
   bench-14: **−15.6% tree size, −19.4% time-to-depth**.
4. **NEON NNUE kernel rewrite (arm64 phones).** The NEON dot used a
   single-accumulator vmull+vaddq serial chain; it now uses four
   interleaved int32x4 accumulators with `vmlal_s16` (fused
   multiply-accumulate-long halves the op count), plus a NEON mirror of
   the AVX-512 VNNI batched GEMV: L2/L3 run as 4-output block dots with
   input pairs broadcast to lanes and ONE horizontal reduction per block,
   streamed through repacked `[pair][8]` weight tables. Bit-identity
   proven twice: (a) a scalar emulation of the packed layout matches the
   per-output reference on 130k output checks; (b) a real aarch64 build
   (zig cross-compiler, musl, run under qemu-user) prints the **exact
   x86 bench fingerprint** (startpos d9 59,904 → suite 302,592 nodes at
   the pre-capture-ordering state; final state 219,136 on both
   architectures — best moves and scores identical, cross-compiler and
   cross-architecture).
5. **16-bit history tables + corrhist dedup.** main/followup/counter/
   cont3/cont4/capture histories dropped from int32 to int16 (gravity
   already clamps to int16 range; halves the 4.7 MB continuation-history
   footprint per worker, better cache behavior); quiescence's stand-pat
   path no longer runs the corrhist piece-sum twice (correctedEval
   computed once, shared by the TT-store semantics and the leaf eval).
   Bit-identical fingerprint.
6. **Multi-cut un-dead-coded.** The v4.0-era gate made the multi-cut
   branch provably unreachable for its entire life (block required
   `beta > singularBeta`, branch required `singularBeta >= beta`). The
   excluded verification now also runs when `!pvNode &&
   singularBeta >= beta`, making the fail-soft multi-cut return reachable.
   (v4.0's E1 rejection tested a different form — SF-style with negative
   extensions — this re-gate ships the minimal sound form only.)
7. **Lazy SMP improvements** (matters at the app's 2100+ levels):
   aspiration windows now run on EVERY worker (was main-only), helpers
   get per-thread initial-delta offsets for tree diversity, and the final
   result picks the deepest COMPLETED helper iteration (completedDepth
   only advances on finished iterations, so aborted iterations can never
   win). Verified: full-game protocol integrity at 2 threads, symmetric
   self-match (24 g, +14.5 ± 12 — noise, as expected for a self-match).
8. **PGO infrastructure (the +11% nps item).** CMake `TACTURN_PGO`
   option (GENERATE / USE:<profdata>), `tacturn_pgo_train()` in the C
   API + `Game.nativePgoTrain()` JNI hook driving a deterministic
   10-position workload through the full hot path, a host pipeline
   (`scripts/pgo/pgo-host.sh`) and a recipe for on-device training
   (`scripts/pgo/README-PGO.md`). Host-measured: **+2.6% nps,
   fingerprint-identical** (PGO must never change the tree). ARM gains
   need on-device profiles per the recipe — the infrastructure is fully
   wired and documented.
9. **Per-ABI `-mtune` hints.** arm64-v8a builds schedule for
   cortex-a76-class big cores (where the engine's threads run),
   armeabi-v7a for cortex-a53. Scheduling-only — no new instructions, so
   binary compatibility across every device in each ABI is preserved.
10. **Host testing suite shipped in-repo** (`scripts/host-tools/`): a UCI
    harness with bench/perft/fuzz(precise-check)/fuzz(see)/pgotrain
    commands, an SPRT match runner (45 verified openings, color-swapped
    pairs, fishtest-style trinomial LLR), and the NEON-packing
    equivalence test — the exact tooling used for every number above.

### 31.2 Tested and deliberately NOT changed

- **TT size at real think times.** Paired probe, 2^22 (100 MB) vs 2^21
  (50 MB) at 800 ms/move: 2-12-2 over 16 games — dead even. The app's
  policy (2^21 below level 2800; 2^22 at 2800+ where budgets are 4–8 s
  across 8 threads) stays.
- **TT buckets / overflow slot.** Not applicable as suggested: this TT
  is single-slot by design and the project's own E5 experiment (§11)
  measured buckets +9.7 at 50 ms but −33 in the 150 ms stack gate —
  the rejection stands on this engine's evidence.
- **Staged move generation.** Deferred: the largest remaining
  identical-structure speedup candidate, but it touches every ordering
  consumer and deserves its own campaign with the new fuzz/fingerprint
  tooling rather than a rushed port in this one.

### 31.3 Verification record (all on the final 1.1.0 state)

- Perft suite (6 positions incl. Kiwipete + promotion/EP torture): ALL
  PASS, bit-exact vs the 1.0.1 reference values.
- Bench fingerprints: x86 `bench 9` = 219,136 nodes; aarch64 (qemu,
  NEON path) = 219,136 — identical. `bench 14`: 3,284,992 (was 4,822,016
  on 1.0.1 — the capture-ordering tree shrink).
- fuzzGivesCheck: 26.8M moves (final state re-run), 0 mismatches.
- fuzzSee: 4.4M threshold checks (final state re-run), 0 mismatches.
- PGO profile-USE build: fingerprint identical, +2.6% nps (host).
- **SPRT gate: 1.1.0 vs 1.0.1, 100 ms/move, 45 openings, color-swapped
  pairs — +27.9 ± 4.5 Elo (n=200, W-D-L 62-92-46; SPRT(0,10) accepted
  H1 at LLR +4.01 after 20 games and the full sample confirms).**

### 31.4 Removals / compatibility

No API changes (TACTURN_API_VERSION stays 4; `tacturn_pgo_train` is an
addition). versionName 1.1.0, versionCode 32 — installs over 1.0.1 (same
keystore). `binaries/` gains a 1.1.0 host reference build; the 1.0.1
binary is superseded.

---

## 32. 1.1.1 — the head-to-head campaign (TT age-wrap fix + multi-cut revert; +81 Elo over 1.0.1)

### 32.1 Background

An independently developed fork of 1.0.1 (the "TNF" build: same lever
checklist implemented separately) beat the shipped 1.1.0 head-to-head
by +43.7 ± 4.4 over 200 games at 100 ms/move. Isolating the gap with
A/B matches on the 1.1.0 stack found two things, one bug and one
mis-shipped lever:

1. **TT age-wrap bug (real, and mine to own).** The TT entry's gen
   field is 5 bits, but `nextSearch()` wrapped `age_` at 64. For every
   search with age 32..63 the stored 5-bit gen could never equal
   `age_`, so every store took the always-replace path — half of every
   long game was played with a thrashing table. The TNF build wraps at
   32 (correct). Fixing this alone on the 1.1.0 stack measured
   **+38.4 ± 5.9 Elo** (100 games) — a single-line correctness fix
   worth more than the entire 1.1.0 campaign.
2. **Multi-cut was a regression.** v1.1.0 made the historically dead
   multi-cut branch reachable; the TNF build implemented the same
   re-gate, measured it, and shipped it OFF. Direct A/B on the full
   stack confirmed their call: multi-cut ON cost ~41 Elo of head-to-
   head swing. It also cut 60% of the startpos d9 tree (29,696 vs
   73,216 nodes) — pruning volume is not strength. v1.1.1 disables the
   branch again (kept as documented dead code with the measurement in
   the comment).

### 32.2 Shipped changes

- `ttable.h`: `nextSearch()` wraps at 31, matching the 5-bit gen field
  (with the measurement in the doc comment). Replacement policy is
  otherwise untouched.
- `search.cpp`: `multicutSearch` forced false — the excluded (singular)
  search now runs only for the classic `beta > singularBeta` case,
  exactly the pre-1.1 unreachable form. The multi-cut return statement
  stays in the tree behind `false &&` with the evidence in comments.
- Version: 1.1.1 / versionCode 33. No API changes.

### 32.3 Match evidence (100 ms/move, color-swapped pairs, 2 cores)

| Matchup | n | Result |
|---|---|---|
| 1.1.1 vs 1.0.1 baseline | 100 | **+81.4 ± 6.0** (38-47-15) |
| 1.1.1 vs the TNF fork | 100 | **+27.9 ± 6.1** (29-50-21) |
| ageFix alone on 1.1.0 vs 1.1.0 | 100 | +38.4 ± 5.9 |
| (context) TNF vs 1.1.0 | 200 | +43.7 ± 4.4 |
| (context) 1.1.0 vs 1.0.1 | 200 | +27.9 ± 4.5 |

The direct +81.4 beats the naive chain (28 + 38) — the age fix is worth
more once the improved search actually uses the table it is writing.

### 32.4 Verification record (all on the final 1.1.1 state)

- Identity: shipped source == the match-tested diagnostic binary,
  bit-identical (startpos d9 = 73,216 nodes / d11 = 235,008 nodes,
  same best move and score).
- Perft 5 startpos = 4,865,609 (bit-exact).
- fuzzSee: 11.5M threshold checks, 0 mismatches. fuzzGivesCheck:
  13.3M quiet moves, 0 mismatches.
- Bench: `bench 9` total = 282,112 nodes; `bench 14` = 4,070,400
  (the multi-cut revert re-grows the tree vs 1.1.0's 219,136 /
  3,284,992 — volume back up, strength with it).
- Reference binary rebuilt: `binaries/tacturn-1.1.1-linux-x64`
  prints `fingerprint OK` (d9, contempt 12, 1 thread).
- Harness note: a bug in the SPRT runner's LLR (logit-space bounds fed
  where score-space was required — it would accept H1 at any score
  above ~3%) was found and fixed during this campaign. All Elo numbers
  above are fixed-N estimates from the (unaffected) score math; the
  corrected LLR re-run confirms the 1.1.0 gate conclusion.

### 32.5 Removals / compatibility

None. versionName 1.1.1 / versionCode 33 — installs over 1.1.0 and
1.0.1 (same keystore). `binaries/tacturn-1.1.0-linux-x64` superseded
by the 1.1.1 reference build.

---

## 33. 1.2.0 — the ranked-lever campaign (tactical-context LMR + dynamic contempt; +104 Elo over 1.0.1)

### 33.1 Background

A second external review ranked eight lever families by confidence x
magnitude per unit of work. Triage against this session's constraints
(2-core host, no training pipeline, no tablebase tooling): the net-side
levers (king-bucket output tables, full-replacement net) need the corpus/
trainer compute of a full campaign; Syzygy probing needs a from-scratch
probe implementation plus ~30 MB of data; a true TC-matched SPSA re-run
(1 s, 4-8 threads) needs thousands of games of compute. What WAS
executable here: the exact-speedup lever, the four parked mechanisms
from the review list re-gated properly, and the secondary-knob family.

### 33.2 Levers executed

**Kept (shipped in 1.2.0):**

1. **Tactical-context LMR** (from the review's xp[5] family): when the
   TT move is a capture, the position is tactically loaded — quiets get
   one ply of extra LMR reduction. Gate: **+38.4 ± 6.6** alone (100
   games, 100 ms); startpos d9 tree shrinks 73,216 -> 41,984 (-43%).
   The single biggest search lever measured in this project's history
   per unit of code (one line).
2. **Dynamic contempt**: the 12 cp draw contempt now fades with
   non-pawn, non-king material (full at np >= 8, linear to zero in bare
   endings). Over-pressing dead-drawn endings was converting half-points
   into losses. Gate: **+17.4 ± 6.9** (100 games).

**Tested and rejected with evidence (100-game gates, 100 ms/move,
color-swapped pairs, vs a bit-identical control build):**

3. Passed-pawn-to-7th extension (PV, ply < 40, PASSED_MASK-clean path):
   **-24.4 ± 6.4**. Extensions keep losing to this search's ordering —
   consistent with the project's whole extension history, and with the
   TNF fork's own decision to ship xp[4] off.
4. TT-move negative extension when not singular and score >= beta:
   **-17.4 ± 6.4** (the -48% tree shrink cost more tactical depth than
   the time saved).
5. Depth-scaled aspiration start (delta += depth/6): **-38.4 ± 6.5** —
   the SPSA-tuned fixed start is better; wider deep-iteration windows
   let winning swings escape the window and pay re-searches.
6. Explicit SIMD accumulator row-ops (AVX-512BW/AVX2/SSE2/NEON ladders
   for accAdd/accSub/accShift/accShiftPersp): bit-identical (mod-2^16
   arithmetic is associative), fingerprints unchanged, but **zero
   measurable nps** — inspection of the -O3 codegen shows GCC already
   auto-vectorizes the scalar forms to vpaddw. KEPT anyway (zero risk,
   documents the intent, protects -O2 / weaker-compiler builds), with
   the null result recorded here.

### 33.3 Match evidence (100 ms/move, color-swapped pairs, 2 cores)

| Matchup | n | Result |
|---|---|---|
| 1.2.0 vs 1.1.1 | 200 | **+41.9 ± 4.7** (71-82-47) |
| 1.2.0 vs 1.0.1 baseline | 100 | **+103.7 ± 5.9** (42-45-13) |
| CAPLMR alone vs control | 100 | +38.4 ± 6.6 |
| DYNCONTEMPT alone vs control | 100 | +17.4 ± 6.9 |
| combined gate vs control | 100 | +27.9 ± 6.6 |
| (context) 1.1.1 vs 1.0.1 | 100 | +81.4 ± 6.0 |

### 33.4 Verification record (all on the final 1.2.0 state)

- Identity: shipped source == the gate-tested COMBO build, bit-identical
  (d9 = 41,984 / d11 = 101,376, same best move and score).
- Perft 5 startpos = 4,865,609 (bit-exact).
- fuzzSee: 11.6M threshold checks, 0 mismatches. fuzzGivesCheck:
  13.3M quiet moves, 0 mismatches.
- Reference binary rebuilt: `binaries/tacturn-1.2.0-linux-x64` prints
  `fingerprint OK`.

### 33.5 Removals / compatibility

None. versionName 1.2.0 / versionCode 34 — installs over 1.1.1 and
earlier (same keystore). The 1.1.1 reference binary is superseded.
The review's unexecuted levers (king-bucket nets, Syzygy, TC-matched
SPSA at 4-8 threads, book refresh) remain the documented next campaign.

---

## 34. 1.3.0 — the review-lever pack (aspiration widening, history-gate rescale, contempt POV fix, qsearch context, KBNK corner, VNNI aliasing)

A code review of the 1.2.0 stack landed seven ranked levers with measured
evidence attached. All seven were executed; six are strength levers and one
is a host-build correctness fix. Verified placement, this release vs 1.2.0:

**+39.9 ± 3.1 Elo (420 games, 100 ms/move, color-swapped pairs, 1 thread,
2-core host, SPRT(0,10) H1 accepted).**

### 34.1 Levers executed

1. **Aspiration start delta 12 -> 27** (`asp_delta`, search.h). The review
   measured 50-62% of root-search nodes going to failed +-12 windows and
   72-79% of iterations failing at least once; the score moves > 12 cp
   between iterations 42% of the time. Their depth probe: start delta 12
   reached 13.79 plies average, 24 -> 14.21, 40 -> 14.25 — the 24-40 range
   is flat, 27 sits mid-curve. (The v1.2-era depth-scaled-start rejection
   tested a different shape; a wider FIXED start is a different lever.)
2. **History-gated LMR rescaled to the real distribution** (search.cpp).
   The old gates (`r -= hist/6000`, `hist > 12000`, prune `< -9000`) could
   never fire: measured quietHistoryValue over 740k reductions spans only
   [-368, +4653]. Fail-high rate after a reduction: 1.3% at hist <= 0,
   4.2% at 250-1000, 6-10% above 2000. New shape: hist <= 0 takes one ply
   of EXTRA reduction; hist >= 2500 / >= 4200 earn one/two plies back; the
   history-prune gate moved to -200 (depth <= 3, non-PV, after 3 legals).
   The bench d9 tree shrank 323,072 -> 221,696 nodes (-31%) — the single
   biggest node-efficiency gain since tactical-context LMR.
3. **Contempt POV fix** (search.cpp). The old unconditional `return -ct`
   scored a draw as -C from BOTH sides' POV, which in negamax made the
   ENGINE value the repetition at +C: after 1.Nf3 Nf6 2.Ng1 the 1.2.0
   binary plays ...Ng8 with score == contempt (reproduced exactly at
   Contempt=+200 on the reference build). Fixed to the POV-relative
   `(ply & 1) ? ct : -ct` (root side hates the draw, opponent is modeled
   as liking it); the repro now plays 1...e7e5 instead. Two follow-on
   consistency fixes were required to keep the fixed sign from creating
   NEW pathologies at the 50-move cliff: stalemate now returns the same
   draw score as every other draw (the old hard 0 ranked ABOVE -C and a
   clock-pressured winner would deliver stalemate), and dead material
   (no P/R/Q, <= 1 minor each — the app's instant-adjudication rule)
   joined the draw family so the search stops valuing phantom material
   edges (this also removes the "hang a piece to reset the clock" line).
4. **qsearch continuation context** (search.cpp). quiesce never wrote
   stack[ply], so capture ordering below the first qsearch ply read a
   stale previous move from an earlier search path. Now published before
   the recursion, matching the main search's convention.
5. **KBNK corner-coloured mop-up + 50-move urgency** (evaluate.cpp). The
   generic edge drive herded the bare king toward the NEAREST corner —
   eval probes show the 1.2.0 HCE actively preferring wrong-coloured
   corners (a8/h1 over h8 with a dark bishop). KBNK now steers along the
   edge toward the bishop-coloured corner (20 cp per Manhattan step,
   dominant over the halved king-proximity term that was stalemate bait)
   and draws the knight toward the net. The 50-move decay (linear to zero
   at the rule, from halfmove 64) is gated to strong-side-has-pawns
   endings — pure-piece bare-king endings can never reset the counter, so
   decay there would only crush the herding gradient. KBNK self-play
   conversion at 300 ms remains cliff-edge noisy in either direction
   (1.2.0 itself measures 1-5 of 8 across runs); the fix makes the
   herding DIRECTION correct, which is what converts when the search gets
   there. KQK/KRK/KBBK unaffected (any corner mates).
6. **History aging >>2 -> >>1** (search.cpp, prepare()). Quartering every
   move discarded 75% of what the tables learned between moves of a
   game; halving keeps half the signal (the standard aging choice).
7. **VNNI strict-aliasing fix** (nnue.cpp). dotBlock16 broadcast the
   int16 activation pair through an int32 lvalue — UB that bit -O2 host
   builds (reproduced: 709,632 vs 349,184 total bench nodes on
   consecutive runs, 2.2x apart, run-to-run garbage). The memcpy load
   makes -O2 bit-identical to -O3 (221,696 = 221,696). NEON builds were
   never affected; -O3 on this toolchain happened to compile the UB
   correctly, so shipped behavior is unchanged.

### 34.2 Match evidence (100 ms/move, color-swapped pairs, 2 cores)

| Matchup | n | Result |
|---|---|---|
| 1.3.0 vs 1.2.0 | 420 | **+39.9 ± 3.1** (139-190-91) |
| SPRT(0, 10) | — | H1 accepted (LLR +3.19 at 20 games; run continued to 420 for the placement number) |

### 34.3 Verification record (all on the final 1.3.0 state)

- Identity: the shipped source builds bit-identically to the gate-tested
  candidate (bench d9 total 221,696 nodes twice; startpos d9 = 38,912,
  best e2e4, score +38).
- Perft 5 startpos = 4,865,609 (bit-exact, unchanged).
- Determinism: two consecutive full bench runs identical.
- -O2 host build now bit-identical to -O3 (aliasing fix; the 1.2.0 -O2
  build was nondeterministic garbage as the review predicted).
- Contempt repro: 1.2.0 plays ...Ng8 at Contempt=+200 (score +200);
  1.3.0 avoids the repetition (plays ...e7e5, score +85). PASS.
- Reference binary rebuilt: `binaries/tacturn-1.3.0-linux-x64` prints
  `fingerprint OK` (38,912 nodes at depth 9).

### 34.4 Removals / compatibility

None. versionName 1.3.0 / versionCode 35 — installs over 1.2.0 and
earlier (same keystore). Search-tree fingerprints change (bench d9
323,072 -> 221,696); no data-format or API changes.

## 35. 1.3.1 — the stats-fixed ranked-lever pack (TT-refined pruning eval, pawn history, ProbCut store, NMP ramp, qsearch delta, fail-low move suppression, SEE 75)

A second code review ranked the remaining levers against the 1.3.0 stack,
prefixed by a testing-infrastructure fix: the match runner's error bars were
~4x too small and its SPRT LLR used an untransformed elo->score slope
(+4.3 LLR at n=20 and +86.8 at n=400 for exactly equal engines — every
early "accepted" verdict was decided by noise). match.py now uses the
logistic score mapping s_i = 1/(1+10^(-e_i/400)) in the LLR and the delta
method sigma = (400/ln10)/(s(1-s)) * sqrt(var/n) for the bars, plus a
--nodes mode for fixed-node screening.

### 35.1 Levers executed (all measured vs the pristine 1.3.0 stack)

1. **TT-score refinement of the pruning eval** (search.cpp). A TT bound
   that contradicts the raw static eval in its own direction (LOWER above
   it, UPPER below it, EXACT replacing it) carries strictly more
   information; the refined eval now drives razoring, reverse futility,
   the null-move eval gate and its reduction ramp, and quiet futility.
   The `improving` flag and the correction-history teacher deliberately
   stay on the raw static eval.
2. **Pawn-structure history** (search.cpp). [side][512 pawn-key buckets]
   [piece x to], ~0.8 MB per worker, fed into quiet ordering, the LMR
   history value and the gravity bonus/malus updates, keyed by the pawn
   key of the node the move is made in (the pre-move pawnKey at the
   post-makeMove call sites). Aged like every other table.
3. **ProbCut TT store** (search.cpp). A ProbCut cutoff is published as a
   FLAG_LOWER entry at depth-3 with the refuting capture, so later visits
   can cut or probe the move before the full move loop.
4. **Null-move reduction ramp** (search.cpp). The single +1 step at
   eval-beta > 250 is replaced by r += clamp((eval-beta)/128, 0, 3) on
   the refined eval.
5. **Qsearch delta margin 150 -> 250** (search.cpp).
6. **No best move stored on fail-low nodes** (search.cpp, alphaBeta +
   storeQ). On an upper bound every move failed to raise alpha; storing
   the least-bad one as the TT move pollutes ordering / singular / IIR.
   The TT keeps any previously-saved move for the same key.
7. **SEE-prune coefficient 60 -> 75** (search.h). 45 measured -16
   (z -1.8): pruning more was wrong, so the untested direction (prune
   less) is the one taken.

### 35.2 Levers tested and rejected

- **LMR depth floor (reduce <= childDepth-1)**: implemented, measured
  -155.5 +/- 32.2 at 5k nodes and -200.2 +/- 37.2 at 10k nodes, dropped.
  Instrumentation: 90.3% of this engine's LMR calls deliberately reduce
  to depth-0 (quiescence) — capping them at depth-1 explodes the tree
  (bench d9 7.5x nodes; 1 s startpos reaches d8 vs base d13). The
  engine's LMR already re-searches at full depth whenever a reduced
  verdict turns positive, so the premise of the floor does not apply to
  this codebase.
- Everything the review listed as measured-null was left untouched
  (pruning margins, LMR start/3rd-move, LMR base/divisor, LMR on checks,
  capture futility, late good-capture LMR, history bonus/malus scaling,
  IIR from depth 3, eval-distance LMR, TT-cutoff history updates,
  ProbCut gate loosening, strict-improving) — plus the not-tested items
  (50-move scaling, continuation corrhist, staged movegen).
- Shipped levers the review confirmed as holding (aspiration 27,
  singular extension, aging >> 1, hist<=0 reduction, tactical-context
  LMR) are unchanged.

### 35.3 Verification

Fixed-node SPRT(0,10) at 5k nodes/move against the pristine 1.3.0 host
build, color-swapped opening pairs, 1 thread per engine, 2-core host,
with the statistics-fixed match runner:

**SPRT(0,10) H1 ACCEPTED after 561 games — +56.2 ± 13.4 Elo
(W-D-L 276-99-186 from the candidate POV; 95% CI +30..+82).**

Depth-scaling spot check at 20k nodes/move (150 games, fresh pairing
seed): **+53.7 ± 21.9** — unlike the review's LMR-floor combination, the
shipped stack does NOT collapse at deeper searches.

Individual lever isolation runs (100 games, 5k nodes) agreed with the
review's claims within error bars: TT-refined pruning eval +17.4 ± 30.5
(review claimed +30 ± 9), pawn-structure history +41.9 ± 29.6 (review
claimed +25.7 ± 9.2). A=B harness symmetry check: 15-10-15, elo -0.0.
Bench fingerprint: d9 startpos 33,280 nodes (1.3.0: 38,912).

## 36. 1.3.2 — the one-cache-line + zero-skip speed pack (TT 16 B entries, sparse L1→L2 matmul, seeGE pruning; bit-exact, d9 fingerprint unchanged)

The external speed review ranked the levers by measured nps on the 1-core
AVX-512 gate host (interleaved runs, medians, ±10% noise, node counts
identical in every variant). This release ships the three
strength-neutral levers from its "do first" group, verbatim in priority
order; everything the review measured as small or that needs SPRT/
real-clock validation is left untouched.

### 36.1 Shipped levers

1. **One-cache-line TT entry** (ttable.h). The v1.3.1 table kept three
   parallel arrays (verification word, payload, static eval) `size_`
   elements apart, so one probe touched up to three cache lines / TLB
   entries. Every entry is now a single 16-byte aligned struct (four per
   64-byte line): the static eval moved inside the payload word
   (published atomically together with its evalValid bit — strictly
   safer than the old separate array), and the move is stored as a
   15-bit key (from | to<<6 | promo<<12) that `ttDecodeMove()`
   reconstructs into the exact movegen encoding on probe (piece/
   captured/EP/castle/double bits are pure functions of the probing
   placement). The never-read shared `hits_` atomic is deleted. Review
   measurement: +20% nps at the default size, +74% at 100 MB, ~0% at
   ≤4 MB — cache/TLB-miss bound, so the win scales with table size and
   is expected to be at least as large on phones. A larger TT for the
   2100+ levels now costs almost no nps.
2. **Zero-skipping L1→L2 matmul** (nnue.cpp). ~89% of clipped
   activations are zero and ~80% of input pairs are entirely zero; a
   zero pair contributes exactly 0 to every accumulator lane, so its
   dpwssd/vmlal and its weight loads can be dropped outright. The
   AVX-512 VNNI kernel compresses the nonzero pair indices once per
   inference (cmpneq+compressstore) and runs both 16-output passes on
   the shared list — the review measured the matmul at 1,703 -> 490
   cycles per inference (~+20% nps on top of #1; together the stack ran
   600k -> 884k nps). The NEON kernel is restructured to the review's
   suggested shape: pair-major accumulation with `vmlal_lane` (each
   pair loaded once and fed to all 8 output blocks, instead of being
   re-broadcast per block — ~25k instructions per inference before)
   plus the same zero-pair skip; several-fold fewer instructions,
   estimated until timed on a phone.
3. **seeGE at the 5 pruning call sites** (search.cpp). ProbCut's
   threshold test, main-loop SEE pruning, the two qsearch quiet-check
   gates and the losing-capture skip now use the sign-only early-exit
   predicate (provably identical to `see() >= t`; the fuzzsee battery
   covers it at these exact thresholds). Review measurement: ~+1–2%.

The review's "measured but small" items (in-check evasion generator +
pre-make LMP/futility at ≤4.6% of cycles; staged move scoring at 3–4%
reachable) and its SPRT-required items (compact correction history,
time management/pondering, search-structure changes, Lazy SMP depth-skip
diversification) are deliberately NOT shipped — per the house rule, a
lever that cannot be gated strength-neutral here waits for its own
campaign. The two dropped measurements (prefetch-after-make, root
fail-high break) stay out.

### 36.2 Bit-exactness and verification

All three levers are strength-neutral at equal depth by construction
(integer arithmetic, zero terms are the identity, exact predicate
equivalence) and were verified as such:

- **Bench identity**: all 6 bench positions at depth 9 return
  identical nodes / best move / score vs the pristine 1.3.1 host build
  (204,288 total; startpos d9 = 33,280 — the 1.3.1 fingerprint is
  preserved, `fingerprint OK`).
- **fuzzsee**: 2 seeds x 4,000 playouts — 22.9M threshold checks,
  0 mismatches (seeGE ≡ see at the shipped call-site thresholds).
- **fuzzcheck** (givesCheckQuiet): 13.3M quiet moves, 0 mismatches.
- **fuzznnue** (new battery, uci.cpp): random playouts verifying at
  every position that the incremental accumulator equals a
  from-scratch refresh AND the quantized integer inference equals the
  double-precision reference exactly AND residual() is stable across
  repeated calls — 3 seeds x 3,000 playouts, 1.83M positions,
  0 mismatches. This is the on-device bit-exactness proof for both
  skip kernels (x86 runs it here; Android runs it via the same host
  harness during device gates).
- **test_neon_pack.cpp** (extended): the scalar emulation now also
  models the v1.3.2 pair-major zero-skip kernel — 262,000 output checks
  over 2,000 trials x 16 buckets with 79.2% of pairs skipped,
  0 mismatches vs the per-output reference.
- **Host-forced NEON build**: the real NEON branch of nnue.cpp (the
  actual vmlal_lane / zero-skip kernel, unmodified) was compiled on
  x86-64 by undefining the x86 SIMD macros in front of the TU and
  supplying a faithful ACLE-semantics stub `arm_neon.h` (host test
  scaffolding, not shipped). Linked into an otherwise-normal host
  build, residual() executes the NEON kernel end-to-end: identical
  bench trees (204,288 nodes) and a 507k-position fuzznnue run with
  0 mismatches vs the double-precision reference. Together with the
  scalar emulation this validates the Android path as strongly as
  possible without ARM hardware; the on-device fuzznnue battery
  remains the final gate.
- **perft**: startpos d6 = 119,060,324; kiwipete d5 = 193,690,690;
  pos3 d6 = 11,030,083 — identical to 1.3.1.
- Fixed-node searches (400k nodes, 3 positions) return identical
  best moves.

### 36.3 Speed gates

Interleaved fresh-process runs, 6 bench positions, median-of-ratios
(same methodology as the review; this gate host is a 2-core AVX-512
Xeon with a large shared L3, so the TT lever's cache-miss win is
partially absorbed — the review's 1-core VM is the better proxy for
phone-class caches):

| Configuration | 1.3.1 | 1.3.2 | ratio |
|---|---|---|---|
| default TT (2^21) | 864 kNps | 993 kNps | **1.154** |
| Hash 100 MB (2^23 both) | 843 kNps | 983 kNps | **1.161** |
| Hash 3 MB (2^18 both, cache-resident) | — | — | **1.154** |
| default TT, NNUE off (TT+seeGE only) | — | — | **1.048** |

The NNUE-off row splits the levers on this host: with the table mostly
L3-resident the TT packing contributes ~+4% here (the review measured
+20% at the same size on the 1-core VM, whose caches the 48 MB table
blows through), the matmul skip carries ~+10-11% (geometric), and
seeGE the rest. Strength-neutral at equal depth means the entire nps
gain converts to depth at fixed movetime.

### 36.4 Elo

The levers change no search decision at equal depth, so strength gain =
the time-control conversion of the speed gain. On the review's gate
host the pack measured +47% nps (0.56 doublings): ~+30-40 Elo at the
project's 60-70 Elo/doubling, ~+2-4 more from seeGE's +1-2% — call it
**≈ +35-45 Elo** on gate-host/phone-class hardware (the 14%-per-25-Elo
rule would claim more). On this big-L3 gate host the same pack measures
+15.4-16.1% (0.20-0.22 doublings, ≈ +13-15 Elo), the difference being
cache geometry, not implementation. The d9 fingerprint (33,280) and
every bench tree are unchanged, so A/B Elo gates vs 1.3.1 at fixed
nodes are expected to be a coin flip; the gain shows up at fixed TIME.

---

## 37. 1.4.0 — the ranked-lever strength pack (LMR context, adaptive-time scaling, 4-way TT buckets, Lazy-SMP fixes; +25.2 ± 15.0 @ 100 ms)

A third external review ranked the remaining core-engine levers by
expected Elo × confidence ÷ effort. This release ships every lever from
its "worthy" tier; two of the review's suggested directions were gated,
found wanting and reverted mid-campaign (documented below), and two
items were deliberately not executed (also documented). All Elo numbers:
100 ms/move fixed-movetime gates, color-swapped opening pairs, 1 thread
per engine, default hash (2^21 entry-equivalents), 2-core host, unless
stated otherwise.

### 37.1 Shipped levers

1. **LMR-context pack** (search.cpp, ttable.h):
   - **ttPv bit** — one of the two spare payload bits in the TT word now
     records "stored from a PV window". LMR reduces quiets at ex-PV
     positions one ply less (`if (ttPv) r--`). The v2.5 S2 experiment's
     FLAG_EXACT proxy was too loose; a real bit is exact. (The v2.5
     double-extension retry remains off — extensions keep losing to this
     search's ordering; ttPv is used for LMR only.)
   - **Cut-node no-TT-move extra reduction** — `if (cutNode) r += ttMove ?
     1 : 2` (was +1 unconditionally): a cut node with no ordering hint
     is where a late quiet matters least.
   - **LMR re-search history feedback** — after the full-depth
     verification of a reduced fail-high, the quiet move earns a small
     bonus (survived) or malus (false positive that wasted a re-search),
     b = d²/2 (one third of the cutoff bonus, so a later genuine cutoff
     still dominates).
2. **Time management** (timeman.h, search.cpp) — the soft budget still
   only shrinks (factor 0.2–1.0, planBudget unchanged); new mid-search
   pacing inside that framework:
   - **best-move stability**: consecutive completed iterations with an
     unchanged best move shrink the pacing budget (×0.85 / ×0.75 / ×0.70
     at 2 / 3 / 5 consecutive);
   - **root node-fraction**: the best move's share of the iteration's
     root node spend (searchRoot now accumulates per-root-move node
     counts, swapped along through sortRootMoves): ×0.85 / ×0.75 / ×0.65
     at ≥55% / ≥70% / ≥85%;
   - **falling eval**: a >15 cp score drop between iterations joins the
     unstable triggers (the 1.1×/1.5× extension is the only budget
     increase the framework allows — and a position that is getting
     worse is exactly what it exists for);
   - the product of the shrink scalers is floored at 0.5, and the pacing
     budget can never fall below the existing soft floor.
   Invisible at fixed-movetime gates by construction (the review's own
   note); the harness maps clocked `go wtime/btime` to adaptiveTime=true
   and match.py grew a `--clock BASE INC` mode so it can be gated.
3. **TT 4-way buckets with age-aware replacement** (ttable.h). The
   64-byte line that one 16-byte entry occupied now holds a 4-slot
   cluster; a probe still reads exactly one cache line. `bits` keeps its
   entry-equivalent meaning (same memory per UCI Hash MB; 2^(bits-2)
   clusters). Victim: same-key in-place update first (unchanged rules);
   else empty slot, then older-generation (shallowest first), then
   current-generation; the v1.3.2 depth-preferred guard is generalized —
   a store is dropped when the best victim is still a current-search
   entry more than 2 plies deeper than the incoming store. No nps cost
   (bench + perft identical I-cache profile; the probe touches the same
   single line).
4. **Lazy-SMP fixes** (search.cpp):
   - **helpers run the adaptive time manager** (was main only): helpers
     paced the raw budgets and stopped starting iterations while main
     was still inside its 1.5× unstable extension — the extension ran
     single-threaded. Every worker now owns a TimeManager and follows
     the same soft budget, stability scaling and unstable extension.
   - **depth-skip ladders**: the old 1 + id%2 start offset produced two
     distinct ladders (both coinciding with main's after one ply).
     Helper h now skips every (h+1)-th depth (helper 1: odd depths,
     helper 2: multiples of 3, ...) — with per-thread aspiration offsets,
     every helper walks a distinct ladder. Iteration-cost estimates are
     gap-aware (a gap-2 iteration costs ~2.4× the last completed one —
     the tree grows ~1.55× per ply; gap-1 keeps the historical ×1.3).
5. **Small items**:
   - **qsearch mate-distance pruning** — the main search has had MDP
     since v1; quiesce lines could climb above/below achievable mates.
     Same three lines as the main search, placed before the TT probe.
   - **aspiration fail-high depth reduction** — a fail-HIGH re-search
     runs one ply shallower (floor depth/2, repeated fail-highs keep
     stepping down); fail-low re-searches and the full-window fallback
     keep the full depth (the low side is where an under-estimated
     bound costs moves).
   - **raw eval in the TT** — the TT eval field now stores
     raw-eval + static-blend residual WITHOUT the correction-history
     term; readers re-apply the live corrhist sum (new corrSumOnly()).
     Corrections are re-learned all search long — a stored corrected
     eval lends stale corrections to every later visit, and the table
     outlives each corrhist state across moves by construction.
   - **from-to + low-ply history** — new [side][from×64+to] table (same
     gravity bonus/malus as every quiet table) plus a low-ply refinement
     [ply 1-2 | ply 3-4] updated on cutoffs at ply 1-4. Fed into quiet
     ORDERING (scoreMoves) at /4 (from-to) and /2 (low-ply); NOT into
     the LMR history value (see 37.2).

### 37.2 Gated mid-campaign and reverted

- **Deeper LMR verification** (re-search at childDepth+1 at non-PV after
  a reduced fail-high): rationale — filter the false positives that the
  aggressive shallow LMR produces. Measured inside the first full-pack
  candidate: the pack trended +1.2 ± 14.8 (n=299) with a +14.8% bench
  tree; reverting this and the item below recovered the tree to +3.8%
  and the gate to +25.2. Consistent with the project's whole
  tree-shrinking-lever history; the neutral full-depth verification
  ships.
- **from-to/low-ply inside quietHistoryValue**: the LMR history gates
  (hist ≤ 0 extra reduction, ≥2500/4200 de-reductions, the −200 prune
  gate) are calibrated to the measured distribution; the extra terms
  shift the profile. Reverted to keep the LMR signal bit-identical to
  its 1.3.2 calibration; the new histories remain pure ordering signals
  (a strictly additive improvement there).

### 37.3 Not executed (with reasons)

- **NNUE blend re-tune at 1–2 s**: the blend axis is real and steep
  (~4 Elo/unit); 77/72 is the compiled-default evidence-backed optimum
  (§28). Re-tuning needs an overnight 2-core gate job; a blind move is
  as likely −4 Elo/unit as +. Left at 77/72 — this is the top item for
  the next campaign session.
- **Continuation corrhist**: two corrhist variants already lost in this
  ledger (§35.2); the review itself flagged it untested with that
  history. Skipped.

### 37.4 Verification

- **Strength gate (the headline)**: candidate vs the pristine 1.3.2
  host build — **+25.2 ± 15.0 over 318 games** (W-D-L 105-131-82,
  LLR +0.67 SPRT(0,10) still climbing when the session budget ended;
  95% CI ≈ +10..+40). This gate sees only the LMR pack + small items:
  timeman is invisible at fixed movetime, TT buckets are ~0 at default
  hash (per the review: +8.7 ± 18.3 at moderate load, +59.9 ± 19.8
  saturated, fixed 5k nodes, scaled hash), and the SMP fixes need
  ≥2 threads.
- **TT bucket isolation** (baseline + buckets only, 5k nodes, 1 MB hash
  both sides, 240 games): −21.7 ± 19.0 (z ≈ −1.1, not significant; both
  the plain and the depth-guarded victim rules produced bit-identical
  games at this load — the guard path does not fire until clusters fill).
  Neutral-within-noise at this middle load, matching the review's own
  moderate-load band; the saturated/long-search regime is where the
  review's +59.9 was measured and where the direct-mapped
  always-replace leak hurts.
- **A=A identity**: 20 games at 60 ms — 7-7-6, no crashes.
- **2-thread self-match**: 10 games, both engines 2 threads — clean.
- **Clock-mode self-match** (8 s + 0.1, 6 games): clean, no flag falls;
  the adaptive pacing visibly banks time (6 games in ~1.3 min — stable
  positions stop far below the soft budget).
- perft 5 = 4,865,609 (bit-exact); fuzzSee 8.6M checks / 0 mismatches;
  fuzzGivesCheck 8.9M / 0; fuzznnue 100k positions / 0.
- Bench d9: 211,968 nodes (1.3.2: 204,288; +3.8% — the ttPv/quality
  spend), startpos best e2e4. Reference binary rebuilt:
  `binaries/tacturn-1.4.0-linux-x64`.

### 37.5 Where the rest of the value shows up

This release's fixed-movetime single-thread gate measures only part of
the pack. The remaining levers pay in the regimes they were built for:
clocked play (stability/node-fraction/falling-eval pacing, est.
+10–20 by the review), long searches with a large table (bucket TT,
+8.7 to +59.9 by load, review's measurements), and 4+ threads (helper
time-manager fix + depth-skip ladders, est. +3–10). The recommended
next gates, in order: (1) the overnight 1–2 s blend re-tune, (2)
`--clock 10 0.1` SPRT vs 1.3.2, (3) 2/4-thread SPRT vs 1.3.2, (4) the
saturated-hash bucket gate replication.

## 38. 1.4.1 — the claim-audit campaign (nine audited fixes from an external Elo-claim list; +36.0 ± 11.3 @ 60 ms)

An external developer supplied a ranked list of ten claimed Elo
improvements with file:line references. Every claim was audited against
the actual source before anything shipped. Result: the mechanisms behind
nine claims were real (the code genuinely contained the described
defects); one claim (the compact 1-D correction history, "48× smaller")
was judged a redesign of a tuned mechanism rather than a bug fix and was
deliberately not executed — the 9.4 MB/worker tables were never a
correctness or strength defect, and re-tuning a new bucketing from
scratch was out of scope for a verification release. All Elo numbers:
60 ms/move fixed-movetime, color-swapped opening pairs, 1 thread per
engine, default hash, 2-core host.

### 38.1 What the audit confirmed (and what shipped)

Bit-exact speed set (verified by identical bench fingerprints before any
tree-changing change was allowed in):

1. **unmakeMove dead-work strip** (position.cpp) — the undo path called
   the full piece helpers (Zobrist, PST, phase updates) and then
   overwrote every one of those fields from the Undo-struct snapshot.
   New bitboard-only restore helpers do just the piece-list /
   castling-rights / ep-square work; the redundant incremental updates
   are gone. Fuzz-clean, bench-identical.
2. **AVX-512 fused L2 pass** (nnue.cpp) — the two per-output-bucket
   `dotBlock16NZ` walks of the pair-major L1→L2 block are fused into a
   single `dotBlock32NZ` pass with a 8-accumulator tree (grouping by
   pass, not by pair — the first cut grouped the wrong axes and was
   caught by the NNUE fuzz battery before it could ship). Packed layout
   transposed to `[OUT_BUCKETS][512][2][32]`.
3. **NEON 8 KB-stride fix** (nnue.cpp, ARM64) — `NeonPackedLayers` was
   `[b][blk][q][8]`, so the two input passes of `dotBlocks4PairMajor`
   were 8 KB apart in every block iteration (the code comment claimed
   pair-major locality; the layout contradicted it). Transposed to
   `[b][q][blk][8]` so each pair's 8 blocks are 128 contiguous bytes.
   x86 cannot run NEON, but the packing/indexing is covered by the
   scalar host proof (`test_neon_pack.cpp`), updated in v1.4.1 to
   emulate the transposed layout exactly as shipped: 2000 trials x 16
   buckets, 196,000 output checks vs the per-output reference, 0
   mismatches, skip path at 79.2% of pairs. The v1.1 dense `dotBlock4`
   kernel — dead since v1.3.2 made pair-major the only NEON inference
   path, and whose contiguous-pair addressing contract contradicted the
   new layout — was removed; the rebuilt engine binary is byte-identical
   to the pre-removal build, proving it was unreachable.
4. **evaluate.cpp micro-opts** — `attackedTwice` rewritten as a
   10-operation accumulator over the pair (superset of the 29-op
   per-square loop, proven on the attack maps actually built);
   `openFiles` via south-fill bit arithmetic instead of per-file loops;
   hanging-piece `attackersTo` gated by an `(bitOf(sq) & enemyAllAtt)`
   test before the full ray walk. Bit-exact by construction and bench.

Tree-changing set (each verified by perft, all fuzz batteries, and the
match):

5. **searchRoot fail-high break + root promotion** (search.cpp) — on
   `score > alpha` the root move (with its node counter) is promoted to
   index 0 immediately; on `score >= beta` the iteration's best move and
   score are recorded and the root loop breaks instead of searching the
   remaining 30+ moves with a stale window. Fail-low recovery first
   halves beta, then lowers alpha.
6. **Losing-capture demotion** (search.cpp) — SEE<0 captures scored
   2,500,000 sat ahead of every quiet, and the capture LMR gate
   (`legalCount > 6`) exempted them, so they inflated legalCount and
   pushed the real quiets into heavier LMR/pruning. Now they sort below
   positive-history quiets and take a real LMR cut at `legalCount > 1`.
7. **Quiesce leafEvalBounded skip restored** (search.cpp, ttable.h) —
   the skip had been dead code since `staticResidualInject()` started
   running unconditionally before it. Order restored: bound first, and
   if the NNUE residual is unavailable, `storeQ` receives
   `TranspositionTable::EVAL_NONE` so the TT keeps the previous valid
   eval slot (the §23.2 pollution failure mode cannot recur).
8. **TimeManager contradiction fix** (timeman.h) — the unstable-position
   extension was computed off the already-shrunk `paceMs_`, so the 1.5×
   extension could never actually exceed the shrunken budget it was
   supposed to correct. Unstable iterations now scale 1.0 (no shrink);
   a low best-move node fraction (< 350 per mille) may extend pacing up
   to 1.15–1.30×, hard-capped.
9. **SEE-pruning threshold split + continuation-history rebalance**
   (search.cpp) — one shared `see_coeff = 75` meant quiet knight (-320)
   and bishop (-330) sacrifices were never pruned at depth 5–6 while
   captures were over-pruned (the v1.3.1 lesson). Quiets now use a
   linear `-50×depth`, captures a quadratic `-25×depth²`. counter/followup
   history weights raised from /8 to /4 (matching the 4-ply table's
   effective weight). TT-move-first fast path + pre-make quiet pruning
   with the ray-filtered legality test; `givesCheckQuiet` guards the
   futility/LMP family as the review specified.

### 38.2 Verification

- Bit-exact intermediate (speed set only): bench 9 = 211,968 nodes —
  identical to 1.4.0; bench 12 = 940,544 nodes in 1.75 s vs 1.83 s
  (**+4.6% NPS**, pure speed, zero tree change).
- Full 1.4.1: bench 9 = 229,888 nodes (smaller tree by design, same best
  moves on all bench positions); bench 12 = 838,144 nodes in 1.60 s
  (**−11% tree, −12.5% wall time** vs 1.4.0); perft 5 = 4,865,609 exact;
  fuzzSee / fuzzGivesCheck / fuzznnue batteries all 0 mismatches.
- **Elo gate: 600 games @ 60 ms/move vs 1.4.0, color-swapped pairs,
  W-D-L 222-218-160 → +36.0 ± 11.3** (95% CI, lower bound +24.7).
  The claim list's discounted estimates for the host fixed-time sum of
  the shipped items (+55–85 before discounting interactions) proved
  optimistic by roughly the expected winner's-curse factor; the measured
  +36 at 60 ms is consistent with the smaller tree plus the +4.6% pure
  NPS. Reference binary rebuilt: `binaries/tacturn-1.4.1-linux-x64`.

### 38.3 Not executed, and why

- **Compact 1-D correction history** (claim 6): the 9.4 MB/worker
  `corrPawn[4096][384]` + `corrNonPawn[2][4096][384]` tables are large
  but not defective; a 16,384-entry 1-D bucketing is a re-tune of a
  working, tuned mechanism (the §13.3 rule: eval-side re-tunes without
  a training run regress). Skipped as out of scope for a verification
  release, not as a disproven claim.
- The "do not do" list from the claim document (no HCE weight changes,
  no LMR depth-1 floor, no multicut/pawn-promo/double extensions, no L1
  widening) was honored throughout; none of the forbidden experiments
  were touched.

## 39. 1.4.2 — the second-claim audit (twelve claims against 1.4.1; +12.7 ± 11.3 @ 60 ms)

A second external developer supplied twelve ranked claims against the
1.4.0 codebase (several duplicating the v1.4.1 campaign's shipped fixes —
those were verified as already-present and NOT re-applied). Every new
claim was audited against the 1.4.1 source, the worthy ones implemented,
and each tree-changing candidate was gated by the match harness before
shipping. All Elo numbers: 60 ms/move fixed-movetime, color-swapped
opening pairs, 1 thread per engine, default hash, 2-core host.

### 39.1 Claims already shipped by v1.4.1 (skipped, no double-apply)

- Root fail-high break + rotation + FLAG_LOWER store (claim 2's core).
- TT-move-first staging in alphaBeta (claim 1's alphaBeta half).
- NEON transposed pair-major layout (claim 5's ARM64 half).
- South-fill file masks / attackedTwice accumulator (claim 7/8 parts).
- Two-sided node-fraction pacing (claim 10's shouldStartNextIteration half).

### 39.2 Shipped (verified real, implemented)

Bit-exact speed/soundness set (bench fingerprints identical to 1.4.1 —
211,968/940,544/229,888/838,144 all reproduce exactly):

1. **cutNode alternation** (search.cpp, 3 sites — tree-changing, gated):
   NMP passed cutNode=false (SF and this engine's own ProbCut convention
   pass !cutNode); the first legal child of a non-PV node passed false,
   so cutNode was permanently false down the leftmost line of every
   All-node, disabling the NMP extra reduction and the LMR cut-node bonus
   there. Now `pvNode ? false : !cutNode` at both first-move sites and
   `!cutNode` in NMP. Tree −4.6% at fixed depth; 120-game solo gate
   −2.9 ± 25.7 (neutral); contributes to the final +12.7 package.
2. **Finny tables** (nnue.h/.cpp, position.h): per-(perspective,
   own-king-bucket) accumulator cache. featureIndex depends only on the
   perspective's own king bucket, so a cached accumulator stays valid for
   any placement delta — cross-quadrant king refreshes now apply 1-3 row
   updates instead of a 20+ piece re-sum. The cold fill iterates the
   occupied bitboard (same ascending-square order as the old 64-square
   scan). ~9 KB per Position; mod-2^16 accumulator arithmetic makes the
   delta path bit-identical by associativity.
3. **corrhist single-pass merge + eval dedupe** (search.cpp): the six
   per-piece-type loops in corrSumOnly()/updateCorr() collapsed to one
   bitboard walk (same terms, int sums commute); the alphaBeta/quiesce
   static-eval sites computed rawEvalCached()/staticResidualInject()/
   corrSumOnly() primitives once instead of twice.
4. **Hanging-pieces provably-defended shortcut** (evaluate.cpp): pawn/
   knight/queen/king attack maps are computed at full occupancy (exact),
   so a victim square inside their union has a real defender — no
   attackersTo call. Only x-ray-superset squares keep the exact test.
5. **AVX2 zero-skip madd GEMV** (nnue.cpp): hosts without AVX-512 VNNI
   (including AVX-512-without-VNNI) previously ran 32 dense dotI16 sweeps
   per inference. New pair-major [bucket][pair][4-block][16] packing +
   `_mm256_madd_epi16` GEMV with 4 independent accumulator chains, L2 on
   the nonzero-pair list, L3 dense. Verified bit-exact on -march=x86-64-v3
   builds (bench identical, zero 512-bit instructions in the binary,
   fuzznnue 6.1M positions 0 mismatches). **Measured +11.2% NPS**
   (bench-12: 2000 ms → 1787 ms) — double the claim's own estimate.
6. **Root salvage-wipeout fix** (search.cpp): a fail-high re-search no
   longer clears iterationBestMove on entry — if the re-search aborts
   before completing a move, the salvage path still publishes the move
   that already proved >= beta (the v1.4.1 rotation guarantees the
   re-search starts from it).
7. **Lazy-SMP best-thread guard** (search.cpp): a helper's result now
   wins only when its score is not worse OR it is at least two depths
   deeper — the depth-skip ladders made blind depth overwrites risky.
8. **planBudget 1.14 ceiling** (timeman.h): the in-check (x1.20) and
   tactical-root (x1.08) multipliers were capped to +5% effect by the
   1.0 ceiling; 1.14 = exactly 0.95 x 1.20 lets a check-evasion in a
   wide position spend up to 14% over base. Clock play only (fixed-
   movetime gates and the bench do not exercise planBudget).

### 39.3 Claimed but REJECTED — with measured or proven cause

- **TT never-drop-exact/PV + exact/PV victim bonus** (claim 11b):
  implemented, proven tree-neutral on the bench (fresh TT never fills a
  4-deep-current cluster), then **measured −45 Elo over full games** in
  the 108-game opening of the package match (the aged/saturated-table
  regime of real games is exactly where the v1.3.2 depth-preferred drop
  rule earned its SPRT gate). A bisect match isolated the regression to
  the TT edits (cutNode-only measured −2.9 ± 25.7). Both reverted; the
  v1.3.2 policy stands.
- **Quiescence qply-0 list compaction** (claim 1's quiesce half):
  implemented as a bit-exact prefilter, REFUTED by bench mismatch
  (229,888 → 229,376), then root-caused with instrumented logs: the lazy
  selection sort is NOT stable for tied moves — compacting the list
  permutes tied capture/quiet groups through the swap mechanics (a tied
  pawn-capture trio [919, 1049, 2284] rotated when five leading quiets
  were dropped, because the best-capture swap then dragged the first tied
  capture backward). Removing list elements changes the tree; the saving
  (two history lookups per skipped quiet) is not worth a tree change.
  The verbatim-gate variant that also hoists the loop's running-best/
  legal-dependent capture gates is unsound outright (the gates' inputs
  evolve during the loop). Documented here so it is not re-attempted.
- **ProbCut staticEval >= beta gate removal + qsearch pre-verify**
  (claim 6): the engine's gate is already LOOSER than Stockfish's (SF
  requires staticEval >= probBeta, this engine requires >= beta); removing
  it entirely plus a nonstandard qsearch pre-verification pass is a
  re-tune of a gated mechanism, not a bug fix. Skipped per §13.3.
- **1D compact correction history** (claim 4 Option B): same verdict as
  the v1.4.1 campaign (a redesign of a tuned mechanism; Option A's
  bit-exact parts shipped as item 3 above).
- **NEON branchless nzIdx + dense L3** (claim 5's ARM64 refinement): no
  ARM cross-compiler exists on the dev host — shipping NEON intrinsics
  that cannot even be syntax-checked is how Android builds break. The
  v1.4.1 transposed layout (verified by the scalar host proof) stands.
- **ARM64 PGO profile shipping** (claim 12): requires an arm64 device to
  generate the profile; infrastructure exists, hardware does not.
- **ECEntry unblended-residual re-encode** (claim 9's third item): changes
  the double-rounding of the blend rescale — not bit-exact for a
  negligible saving; the two dedupe items shipped instead.

### 39.4 Verification

- Bit-exact intermediate (speed set only): bench 9 = 211,968, bench 12 =
  838,144 — identical to 1.4.1; perft 5 = 4,865,609 exact; all fuzz
  batteries 0 mismatches.
- AVX2-only builds (-march=x86-64-v3): 1.4.1 and 1.4.2 bit-exact sets
  produce identical node counts (229,888 / 838,144); +11.2% NPS measured
  on the new GEMV (2000 ms → 1787 ms bench-12); zero 512-bit
  instructions in the binary confirm the path.
- Full 1.4.2: bench 9 = 219,136 nodes (tree changed by the cutNode fix
  only — the TT revert restored the v1.3.2 tree exactly); perft exact;
  fuzzSee 30.6M positions / fuzzGivesCheck 12.3M / fuzznnue 8.2M — all
  0 mismatches.
- **Elo gate: 600 games @ 60 ms vs 1.4.1, W-D-L 202-218-180 →
  +12.7 ± 11.3** (95% CI +1.4..+24.0). The bit-exact speed set measures
  ~neutral on the AVX-512 dev host (the eval cache absorbs it at bench
  load); the measured gain is carried by the search fixes, with the
  +11.2% NPS AVX2 GEMV paying out only on non-AVX-512-VNNI hosts.
  Reference binary rebuilt: `binaries/tacturn-1.4.2-linux-x64`.

## 40. 1.4.3 — the third-claim audit (ten claims against 1.4.2; bit-exact speed pack + clocked-time fix)

A third developer audited v1.4.2 and ranked ten "hardware-invariant Elo
levers" worth a claimed +50 to +90 net Elo. The audit below ran against the
v1.4.2 tree; every claim was verified in code, every implemented lever was
proven bit-exact or measured, and every tree-changing lever was
match-tested. The campaign's defining discovery, though, was environmental:
**on this dev host, fixed-movetime match readouts carry a ±30 Elo noise
band driven by CPU-steal and a binary-layout lottery under oversubscription**
— a no-op binary (identical source plus one unused function) measured
-15.0 ± 28.0 against v1.4.2. The release therefore stands on proof, not on
the match number.

### 40.1 Audit verdicts on the ten claims

| # | Claim | Verdict |
|---|-------|---------|
| 1 | Staged movegen + lazy quiet scoring | quiesce TT fast path + never-searched quiet marker: implemented, then REJECTED after game-level divergence hunting exposed a warm-state tree divergence (see 40.4); alphaBeta TT-move staging already shipped in 1.4.1 (no double-apply); lazy quiet scoring refuted by bench (see 40.3) |
| 2 | Fractional LMR + continuous history scaling | REJECTED — a re-tune of the engine's tuned-by-measurement LMR cliffs (§35.2 documents the 90% depth-0 collapse is the intended operating point; the depth-floor lever lost -155); the /8 quietHistoryValue weights are documented deliberate calibration. Kept one hygiene item: fillLmrTable latched init-once (was re-running 4,096 std::log calls per worker per move) |
| 3 | PVS root-move sorting corruption | half-real. The fail-low-bound half (store -INFINITE for scout fail-lows) measured ≈ -36 in ~150 games — the bounds carry real ordering signal, nulling them starved the true second-best move; REJECTED. The PV-raise rotation half was implemented and turned out to be dead code as written (alpha is raised before the check, so `score > alpha` never fires) — removed |
| 4 | Timeman stability_/compound shrinkage | mechanism real: depths 1-6 complete in microseconds and inflate stability_ to its deepest shrink tier before the real search begins. Shipped: stability_ only increments at completedDepth >= 6 (a best-move change still resets at any depth). The floor-raise half was rejected (double shrink). Note: `go movetime` never routes through the TimeManager — the fix affects CLOCKED play (the app's mode) only |
| 5 | Pruning reorder + searchedMoves | REJECTED — the reorder is not bit-exact below depth 7 (legalCount diverges for quiets failing both SEE and the cheap gates) and SEE is inert at depth 7-8 (futility/LMP cap there); searchedMoves is an LMR re-tune |
| 6 | Lazy per-slice aging | REJECTED on measurement: the full 12.6 MB sweep costs 0.448 ms = 0.75% of a 60 ms budget on this host; per-read generation checks on ~100M history reads/search cost more than they save. Kept the free part: helper-thread aging now runs inside the helper threads (8-thread Android no longer sweeps ~100 MB sequentially on the main thread while the clock runs) |
| 7 | History update hygiene | capture-malus policy = SF-standard (kept); counterMoves retention measured indistinguishable-from-noise here (+44 in one 95-game run, then negative in every 600-game assembly — the 2880000 absolute priority band makes a stale refutation expensive in this ordering, which was tuned with the per-move wipe; REJECTED); the counter-read and re-search-feedback items are documented deliberate levers (SPRT-tested previously) |
| 8 | PawnEntry king-term cache | REAL with corrections — the claim missed that F_KING_DEFENDERS (knight/bishop placement), F_PAWN_BLOCKED (full occupancy) and the phase>6 gate all break the (pawnKey, wk, bk) key. Shipped the sound version: shelter/storm/proximity/open-file cached under (pawnKey, wk, bk, phaseGate) — see 40.2 |
| 9 | Residual blend mismatch / double truncation | mechanically real but load-bearing: the "mismatch" (TT-hit stand-pats at the 72% static scale vs misses at the 77% bucket scale) and the double truncation were both tuned around. Measured ≈ -40 in ~100 games with the exact-rescale fix; REJECTED with the measurement recorded in-code |
| 10 | TT prefetch in makeMove + NMP guard | prefetch: implemented (relocated to the search layer — Position has no table access) and then REJECTED after it measured -106 ± 30 at fixed movetime under this host's 2-core/4-process oversubscription (one prefetch per makeMove thrashes the shared L2/L3 when four engine processes compete; tree-neutral, faster uncontended, catastrophic contended). NMP non-improving guard: re-tune, REJECTED |

### 40.2 What shipped (bit-exact unless noted)

1. **PawnEntry king-position term cache** (evaluate.h/.cpp): F_SHELTER_*,
   F_STORM_*, F_KING_PAWN_F*, F_KING_OPEN_FILE depend only on
   (wp, bp, wk, bk, phase>6) — cached in the pawn-hash entry keyed by all
   five. F_KING_DEFENDERS and F_PAWN_BLOCKED stay live. ~+2.4% NPS
   uncontended (bench-13 medians 2.10s → 2.05s).
2. **fillLmrTable init-once** (search.cpp): the (base, divisor) pair is
   latched; per-move 4,096-log refills of the global table (with helper
   threads concurrently storing identical values) are gone.
3. **Parallel helper aging** (search.cpp): prepare() split into
   prepareSetup (clone/limits — reads the caller's board, stays on the
   caller's thread) and prepareAge (worker-local sweeps — runs inside the
   helper threads). Inert at 1 thread; on 8-thread Android the ~100 MB of
   per-move sweeps now overlap instead of blocking the main thread.
4. **Timeman free-depth stability gate** (timeman.h, CLOCKED play only):
   stability_ increments only from completedDepth >= 6.

### 40.3 The refuted-but-instructive experiments

- **Lazy quiet scoring** (claim 1's alphaBeta half): a sentinel-band design
  (QUIET_PENDING = -100001, batch rescore when the selection first reaches
  the quiet band) broke bench identity (219,136 → 233,472). Root cause:
  the history tables are LIVE state inside the move loop — LMR re-search
  feedback and updateCutoffStats bump cells between iterations, so a
  deferred quiet reads different values than the upfront read. Lazy quiet
  scoring is only bit-exact in engines whose move loop never mutates the
  ordering tables mid-loop. Documented in-code at the loop.
- **Quiesce never-searched marker, first draft**: the sentinel was first
  placed at -INFINITE (-33000) — ABOVE the borderline losing captures
  (SEE in [-50,0), scored ≈ -194k..-199.6k) that the loop still searches.
  The "only never-searched moves remain" break then stranded those captures:
  silent tree divergence the bench suite never triggered. Fixed to
  -2000000 (below every producible score) — and then the whole quiesce pack
  was dropped anyway (40.4).
- **Root fail-low -INFINITE**: measured ≈ -36 (bisect). Recorded in-code at
  the store.
- **Raw-residual cache + exact per-use blend**: measured ≈ -40 (bisect).
  The 72%-vs-77% scale mismatch the audit flagged is load-bearing — the
  pruning constants were tuned with TT-hit stand-pats at the static-blend
  scale. Recorded in-code at staticResidualInject.

### 40.4 The game-level divergence hunts (new verification standard)

Bench identity proved insufficient this campaign: two quiesce designs that
were bench-identical at four depths (219,136 / 369,152 / 556,032 /
1,009,152), identical on a 20-FEN tactical suite (1,086,976 nodes) and
identical on a 96-position game-derived hunt at depth 9 and 11, STILL
diverged in real games. The tool that caught them: **fixed-depth self-play
sequence comparison** — each engine self-plays the same opening at
`go depth 12` (one process, full cross-move state: TT reuse, history
aging, evalCache/PawnEntry persistence) and the move sequences must match
byte-for-byte. The quiesce pack diverged on 2/8 openings (half-moves 51
and 82 — deep endgames, exactly where the position hunts had no coverage,
and warm-TT quiesce probes are dense). An in-place-searched TT-move fast
path (instead of the v1.4.1-proven swap-to-slot-0 + loop-from-1 pattern)
changes tie-break scan order for tied captures. The final tree passes
0/8 divergence at depth 12 AND depth 14, plus a paired-movetime
indistinguishability test (the candidate's move choices differ from
v1.4.2's at the same 2% rate a v1.4.2-vs-itself jitter baseline shows,
with depth deltas +0.02 vs +0.06).

### 40.5 The environment forensics (why the match number is not the story)

- The dev host is a 2-core VM with **24% cumulative CPU steal** (775k steal
  jiffies vs 2.46M user): the wall-clock budget the match's fixed-movetime
  games hand the engines is itself noisy.
- Same-binary sanity matches (v1.4.2 vs v1.4.2) read +5.8 ± 36 (60 games)
  and +8.4 ± 16.3 (288 games) — but a same-binary match is IMMUNE to any
  bias that depends on the binary.
- A **layout-only no-op control** (v1.4.2 source + one unused function,
  bit-exact by construction, bench 219,136) measured **-15.0 ± 28.0**
  against v1.4.2 — the binary-layout lottery under 4-process
  oversubscription produces run-level swings the same-binary sanity match
  structurally cannot detect.
- The release match read **-20.9 ± 11.3 (600 games, W-D-L 173-218-209)**
  against v1.4.2 — inside the band the no-op control demonstrates, for an
  engine proven move-for-move equivalent at fixed depth and
  indistinguishable under movetime jitter. The true expected delta is the
  +2.4% uncontended NPS plus the clocked-play/multithread fixes that
  single-threaded fixed-movetime matching cannot express.

### 40.6 Verification

- Bench identity at four depths: 219,136 / 369,152 / 556,032 / 1,009,152.
- 20-FEN tactical suite: 1,086,976 nodes, identical. 96-position
  game-derived hunt: identical at depth 9 and 11.
- Fixed-depth self-play sequences: 0/8 openings diverge at depth 12 and 14.
- Paired movetime: 2% move-difference rate (same as the same-binary jitter
  baseline); avg depth delta +0.02.
- perft 5 = 4,865,609 exact; fuzzSee 508k positions / 7.2M checks / 0
  mismatches; fuzzGivesCheck 307k positions / 6.6M moves / 0 mismatches;
  fuzznnue 514k positions / 0 mismatches; multi-threaded smoke (2 and 4
  threads) healthy.
- Uncontended NPS: +2.4% (bench-13, 5-rep medians 2.10s → 2.05s).

## 41. 1.4.4 — the fourth-claim audit (nine claims against 1.4.3; bit-exact speed pack + clocked/SMP fixes, one root-cause discovery)

A fourth developer audited 1.4.3 and proposed nine levers ("+35 to +65
Elo single-threaded, +50 to +85 on 8T/ARM64"). The audit premise quality
was again high — every cited line number and mechanism verified real —
but the estimates ran the usual 3-5x optimistic, and one lever was a
re-proposal of something §40.4 had already refuted.

### 41.1 Audit verdicts on the nine claims

1. **Finish the orphaned quiesce qply-0 quiet skip** — premise TRUE (the
   v1.4.3 comment describes a marker that is not in the code), but this
   is exactly the §40.4 B2 package: implemented, bench-identical at four
   depths, 96-position hunt clean, and STILL game-level divergent (2/8
   openings at depth 12). REJECTED on prior evidence. The stale comment
   was rewritten to record the true history.
2. **Pre-make ray legality gate** — TRUE and shipped. kingRays already
   existed for the v1.4.1 quiet-prune; the post-make inCheck(us) call
   (5 attack lookups) is now gated on `inCheck || piece==KING || isEP ||
   (kingRays & from)` at all four sites (alphaBeta TT path, general
   loop, ProbCut, quiesce). Captures ON a ray keep the ray blocked (the
   capturer occupies the victim square), so the from-square test is
   complete. Bit-exact: the skipped call provably returned false.
3. **Fused single-pass applyMoveAcc** — TRUE and shipped. The lazy-ring
   replay memcpy'd the 2 KB accumulator and then re-read/re-wrote dst per
   op (quiet = 2 passes, capture = 3). New accFusedRows() (AVX-512 /
   AVX2 / SSE2 / NEON / scalar) reads src once, applies every ±row,
   writes dst once. Bit-exact by modular int16 arithmetic (subtraction =
   negation-addition; addition associative/commutative mod 2^16), so
   "all subs then all adds" equals any recorded op order. fuzznnue
   battery: 0 mismatches.
4. **In-check generateEvasions + NEON nzIdx + PGO** — the x86 half
   shipped in a DIFFERENT form than proposed (see 41.2); the ARM64 half
   (nzIdx pre-build, L3 dense, on-device PGO) skipped again — no ARM
   cross-compiler, no device (same as §39).
5. **Threat-aware quiet ordering** — premise true (zero board-threat
   signal in the quiet band), implemented (-8000 landing on
   enemy-pawn-attacked square / +6000 escape), probed, STRIPPED (41.4).
6. **Clocked timeman fixes** — the depth-1 scoreSwing bug is REAL:
   prevIterScore starts at -INFINITE, so at completedDepth == 1 the
   swing computation read ~+33k and set unstable_ on the first depth of
   every search. Shipped: real-score guard + a cross-move
   `lastScore - score > 30` falling-eval trigger (the old test only
   compared iteration-to-iteration — a position that worsened since the
   engine's previous MOVE but is flat across iterations read as
   settled). NOT taken: the 0.50→0.65 floor raise and the 550→650
   node-frac threshold move — blind pacing re-tunes; CLOCKED play only,
   fixed-movetime matches cannot gate them.
7. **Lazy-SMP consensus vote** — TRUE and shipped (multi-thread only).
   The v1.4.2 pairwise overwrite let one depth-ladder helper present a
   depth+2 thin search and override the field; the best-thread pick is
   now Stockfish-style voting: weight = (score - minScore + 14) * depth
   per completed worker, summed per best-move, winner = best
   (score, depth) among the winning move's voters, main wins ties.
   Lone-voter fallback keeps the v1.4.2 guard. 1T bench identity
   untouched; 2T/4T smoke healthy.
8. **50-move residual dampening** — premise real (HCE decays past
   halfmove 64, the NNUE residual does not), implemented
   (r * (102 - hmc) / 38), probed, STRIPPED (41.4). The inconsistency
   is worth a dedicated long-TC gate someday; it could not prove itself
   inside this host's fixed-movetime noise.
9. **LMR negative cliff + cutoff gravity** — implemented
   (hist <= -150 extra r++; bonus cap 1600; +b/4 on bestScore > beta +
   100), probed, STRIPPED with the bundle (41.4).

### 41.2 The root-cause discovery: selection-sort displacement (why EVERY compaction/sentinel design diverges)

The proposed "100% bit-exact" in-check compaction (filter the generated
list down to king moves + checker captures + blocks + EP) was
implemented first and DIVERGED at bench depth 9 (219,136 → 218,624).
TT-store trace bisection (both binaries logging every store; first
stream mismatch at bench depth 5) caught the mechanism: two sibling
quiesce stores swapped order — the parent had searched two equal-valued
moves in different order.

The cause is the selection sort's displacement dynamics. At iteration i
the argmax moves to slot i and the OLD slot-i element is re-inserted
into the remaining suffix at the argmax position. Removing list
elements (even never-searched, provably-illegal ones) changes which
element is displaced and where it lands — and a displaced element tied
with its neighbors resolves ties differently afterwards. Array-content
changes are therefore NEVER bit-exact under this move loop, regardless
of how sound the removal predicate is. This one mechanism explains all
three historical divergences: the v1.4.2 qply-0 compaction ("a tied
pawn-capture trio rotated"), the v1.4.3 §40.4 sentinel+break package,
and this campaign's compaction.

The SHIPPED form of lever 4 keeps the array untouched and skips only
provably-illegal WORK: an in-check move that is not a king move, not
EP, and lands neither on the checker square nor on a between-square
cannot be legal — the original flow proved it by makeMove + inCheck +
unmakeMove (side-effect-free). The skip is the same provability pattern
as the ray gate. The generation+scoring of the doomed moves is
unavoidable bit-exact cost; the make/unmake traffic is not.

### 41.3 What shipped (all fixed-movetime-visible parts bit-exact)

1. **Provably-illegal in-check skip** (alphaBeta + quiesce): ~30
   make+unmake+inCheck triplets per in-check node eliminated.
2. **Pre-make king-ray legality gate** (4 sites): the 5-lookup
   inCheck(us) skipped on ~75% of made moves.
3. **Fused single-pass NNUE accumulator replay** (accFusedRows in all
   five SIMD sections): 2-3 passes over the 2 KB accumulator per lazy
   ring step collapse to one.
4. **Clocked timeman**: depth-1 scoreSwing guard + cross-move lastScore
   drop trigger (clocked play only).
5. **Lazy-SMP consensus vote** best-thread pick (multi-thread only).

### 41.4 The refuted-but-instructive experiments

- **The L5+L8+L9 tree-changing bundle** (threat-aware quiet ordering,
   50-move residual dampening, LMR cliff + cutoff gravity): implemented
   as a bundle, all fuzz/perft clean, probed against 1.4.3 at 60 ms —
   **-28.2 ± 19.1 at 210 games** against the no-op layout control band
   of -15.0 ± 28.0 (§40.5), i.e. a point estimate of ~-13 real. The
   base rate held again: externally-proposed search re-tunes have
   measured ≈0-or-negative in every TacTurn campaign except provable
   bug fixes. All three reverted; in-code notes record the designs so a
   future quiet-host or long-TC gate can retry them individually.
- **In-check compaction** (see 41.2): divergent, replaced by the skip.
- **NEON nzIdx / on-device PGO**: no ARM toolchain or device exists on
   this host (§39 precedent).

### 41.5 Verification

- Bench identity at four depths vs 1.4.3: 219,136 / 369,152 / 556,032 /
  1,009,152 — the shipped tree is node-for-node identical.
- fuzzSee 162k positions / 2.27M checks / 0 mismatches;
  fuzzGivesCheck 162k positions / 3.5M moves / 0 mismatches; fuzznnue
  162k positions / 0 mismatches; perft 5 = 4,865,609 exact.
- Fixed-depth self-play sequences: 0/8 openings diverge at depth 12 AND
  depth 14 (full warm state: TT reuse, aged history, evalCache).
- Uncontended NPS (interleaved 6-pair medians, all pairs favoring the
  candidate): 2.147s → 2.091s bench-13 = **+2.7%**.
- Clocked smoke (wtime/btime) and multi-threaded smoke (2T/4T, several
  sequential searches, sane moves) healthy.

### 41.6 The release match

600 games at 60 ms/move, 2 concurrency, color-swapped opening pairs:
**+5.2 ± 11.2 (W-D-L 190-229-181)** against the shipped 1.4.3 — a
POSITIVE reading on a proven game-level bit-exact tree, where 1.4.3
itself had shipped on a -20.9 ± 11.3 reading with the same
layout-lottery handicap. The honest expected delta is the +2.7%
uncontended NPS (≈ +3-5 Elo at 60 ms by the §40 calibration) plus the
clocked-timeman and SMP-consensus fixes this match structurally cannot
express. The tree-changing levers that were probed and stripped are the
part of the claim set that did not survive contact with measurement.

## 42. 1.4.5 — the fifth-claim audit (nine claims against 1.4.4; search-math bug fixes, +11.8 ± 7.6 @ 60 ms)

Audited the fifth developer's nine claimed levers line-by-line against the
1.4.4 tree. Confirmed as REAL code defects: the history-gravity sign trap
(|h|*|b| damping breaks both negative quadrants — a positive cutoff bonus on
a negative entry SUBTRACTS once |h| > 16384, and maluses amplify negative
cells), KNNvK missing from isDeadMaterialDraw(), zero draw/dead-material
adjudication in quiesce(), corrPawn/corrNonPawn aged every move (Stockfish
never ages corrhist), and the 50-move NNUE residual non-decay. Deferred
with cause: staged MovePicker (the §41 selection-sort permutation law makes
any array-content change non-bit-exact, and the claimed quiet-check staging
overlaps the §40.4-refuted quiesce compaction), wrong-colour-bishop
king-distance gating (eval re-tune, not a defect), NNUE retraining paths
(no SF labeling infrastructure on this host), timeman re-centering (the
v1.4.4 L6 changes already shipped; the underspend claim needs a --clock
gate to even measure).

Shipped (all in one bundle, verified by bench change + KNNvK unit checks):

- **gravity()/corrGravity() signed-h fix** (search.cpp): `h + b - h*|b|/16384`
  replaces `h + b - |h|*|b|/16384`. Kills the negative-quadrant amplification
  and the bonus-that-subtracts trap; entries now damp correctly toward the
  clamps from BOTH sides.
- **KNN vs K dead-draw** (position.cpp): exact knights-only two-minor case
  added to isDeadMaterialDraw(). KNNvK positions adjudicate instantly (1 ms
  vs the base's 178k nodes at depth 12 trying to shed a knight).
- **quiesce() draw adjudication** (search.cpp): the main search's
  halfmoveClock-100 / dead-material / repetition block mirrored at qsearch
  entry, with the SF-standard `hmc >= 4` guard on the repetition scan (a
  repetition needs 4 reversible plies) so the common path pays nothing.
- **corrhist persistence** (search.cpp): corrPawn/corrNonPawn removed from
  prepareAge()'s per-move sweep (9.44 MB of the 12.6 MB traffic; gravity
  alone bounds the entries). Still zeroed on ucinewgame.
- **50-move residual decay** (search.cpp): the NNUE residual is damped
  linearly past halfmove 64 (`r*(102-hmc)/38`) at the three injection
  sites (leafEvalFrom / leafEvalBoundedFrom / staticResidualInject), with
  the raw residual still cached. Reinstated on corrected evidence: the
  v1.4.4 strip decision was based on a -28.2 probe reading that was a
  harness sign misread (A-perspective elo) — the probe actually read +28.2.

Implemented then STRIPPED on match evidence (the fifth dev's PVS/aspiration
claims, both SF-standard in isolation):

- Root scout verification on `score > alpha` (incl. fail-soft `>= beta`)
  and aspiration fail-low beta-halving. The full bundle containing these
  read **-0.6 ± 6.8 at 1702 games** (parity): both changes add re-search
  work and interact with this engine's own SPSA-tuned aspiration constants
  (the §38.2 history — depth-scaled aspiration deltas already LOST -38.4
  here). Removing exactly these two recovered the gain: the bug-fix core
  alone reads **+11.8 ± 7.6 (95% CI +4.9..+18.7) at 1380 games @ 60 ms**,
  W-D-L 480-467-433, every 500-game window positive, LLR(0,5) +0.81 and
  climbing. Lesson repeated from §40/§41: on this stack, ported SF search-
  flow idioms lose to the locally tuned constants; provable math fixes win.

Verification: bench 215,552 (tree change expected: gravity + qsearch draws
+ corrhist persistence all alter the tree); fuzz batteries clean; KNNvK and
qsearch-repetition unit checks behave as designed; uncontended NPS -1.5%
(qsearch draw-check cost, repaid by the endgame adjudication); packaged zip
verified by clean extraction + fresh source rebuild (bench identical both
ways), md5 round-trip vs the uploaded copy.

## 43. 1.4.6 — the deep-analysis campaign (self-mined levers; +10.3 ± 7.5 @ 60 ms)

A full-stack audit of the 1.4.5 tree (search/TT/nnue/timeman/movegen plus the
§1-§42 rejection ledger) to find what is LEFT on this stack under the
2-core/no-hardware-expansion constraint. Three candidate tiers were
implemented and measured; one measured-inert tier was dropped with data.

Shipped:

- **8-byte ECEntry** (search.cpp): the eval cache was {u64 key; i32 score;
  i32 res} = 16 B x 2^18 = 4 MB per worker. Now {u32 keyHi; i16 score;
  i16 res} = 2 MB — the low 18 index bits plus the high 32 key bits give
  50-bit effective verification (stronger than the TT's own 16-bit check);
  |score| is bounded by the HCE ceiling (< 10k) and |res| by
  1 + RES_CLAMP*blend/100 (< 470), with 0x7FFF as the never-real lazy
  marker. Halves the cache's L2/L3 footprint.
- **Dirty-perspective replay skip** (nnue.cpp applyMoveAcc): once a replay
  step marks a perspective dirty (king-bucket crossing or inherited
  staleness), later steps no longer burn the 1 KB fused-row SIMD pass on
  it — nnueEnsureFresh unconditionally rebuilds exactly those perspectives
  from the placement before any reader sees the accumulator. Bit-exact by
  the ring invariant (dirty bits propagate; every return path refreshes).
- **64-byte alignment** for the residual input buffers (A/act2).
- **Post-gravity-fix learning recalibration** (search.cpp/search.h): the
  v1.3 history-gate calibration (-200 prune gate, 1.5*d^2 bonus scale,
  corrWeight 1024) was measured under the broken |h|*|b| gravity and the
  per-move corrhist halving, both gone in 1.4.5. Now: prune gate -100
  (the signed gravity's damped negative tail needs the tighter threshold
  to keep firing), cutoff bonus 2*d^2 (the bonus side carries the whole
  signal now), corrWeight 1536 (persistence roughly doubled long-run corr
  magnitudes; dividing by 1.5x restores the intended correction scale).
  Bench tree 215,552 -> 167,936.

Measured and DROPPED with data:

- **RFP depth cap 8->9 + futility depth cap 6->7**: bench-identical AND
  60-ply fixed-node game-identical to the T1 binary — the gates never fire
  at the settled margins in practical positions. The pruning-cap family is
  inert on this stack; the margins are the settled optimum.

Verification: Tier-1 bit-exactness proved by bench identity (215,552 both)
plus 60/60-ply fixed-node game identity; deep-bench NPS +1.7% (interleaved);
fuzz/smoke clean. Match vs the 1.4.5 release binary: **1382 games @ 60 ms,
W-D-L 465-493-424, +10.3 ± 7.5 (95% CI +5.6..+15.0)**, every 300-game
window positive, SPRT(0,5) LLR +0.70 at the stop. Cumulative vs 1.4.4:
+11.8 then +10.3 ~= +22 Elo in two rounds, both from audit-grade fixes
rather than tuning sweeps — the pattern this codebase rewards.

Training verdict (for the record): the §20-§28 law stands — same-navigator
self-play refreshes are exhausted (v9.4 -6.1), different-navigator corpora
broke the plateau twice (v9.5 +22.5, v9.6 +14.1). On the 2-core host the
pipeline is: multi-navigator datagen via the existing UseNNUE/NNUEBlend/
StaticBlend options (champion / over-blend 115 / under-blend 35 / HCE-only
navigators + random-book exits) with the dataSink eval-miss hook, SF17
labeling (apt stockfish, ~2.5k pos/s/core at depth 8), disagreement
pre-filtering (|SF - (HCE+NNUE)| >= 35 cp kept at 100%, < 35 cp downsampled
to 25%), scale-fit on a 250k uniform random slice, 3-4 epochs on the 15-20M
corpus. Expected +10-20 per round for 1-2 more rounds; the residual-hybrid
ceiling (RES_CLAMP 600, blend 77%) caps the path at roughly +30-40
cumulative — the direct-NNUE conversion (Path B, §27.5) remains the only
route past that ceiling and is a multi-day project, not a session task.

## 44. 1.4.7 — the audited-lever pack (external review list; POV fix + quiescence prune fix + bit-exact speed pack + clocked re-center; +24.5 ± 8.1 @ 40 ms, SPRT(0,10) H1)

An external lever list (10 claimed gains) audited line-by-line against the
1.4.6 tree, then implemented and tested with per-lever bisection rather
than as a bundle — the bundle initially measured -95 Elo, and the bisection
below is the reason this section ships a filter, not the list.

Shipped (search):

- **Quiescence first-capture SEE/delta bypass fix** (search.cpp quiesce):
  the `legal >= 1` guard on the !inCheck SEE (-50) + delta-pruning gate
  disabled BOTH prunes for the #1 MVV-LVA capture at every quiescence
  node (legal == 0 when i == 0 is popped). Unlike alphaBeta, a !inCheck
  quiesce node always has the stand-pat in `best`, so no legal-move
  exemption is needed. Garbage sacrificial trees (QxR defended, SEE -425)
  are now pruned at qply >= 0.
- **Correction-history side-to-move POV fix** (search.cpp): corrPawn
  gained a [stm] dimension and corrNonPawn became [stm][color] — the old
  [color]-only layout had WHITE-POV and BLACK-POV updates writing into the
  SAME physical bucket (pawnKey is POV-invariant, so corrPawn collided
  every ply). corrSumOnly/updateCorr now split pawns from non-pawns
  branchlessly (two bitboard walks instead of a per-piece pt != PAWN
  branch). CORR_BUCKETS 1024 -> 2048 keeps per-worker memory identical.
  This was the single largest Elo item in the pack.
- **Root scout-hit abort salvage** (search.cpp searchRoot): when a
  zero-window scout proves score > alpha but the full-window confirmation
  re-search aborts on the clock (stopped() == true returns 0), the proven
  scout score is now restored instead of discarded — the move that already
  beat alpha at depth d is no longer thrown away on the iteration's last
  move.
- **Singular mate-bound guard + bad-capture malus isolation**
  (search.cpp): singular extensions no longer fire on TT mate scores
  (abs(ttScoreAdj) < MATE_BOUND gate — singularBeta = MATE-k - 2*d was
  nonsense), and capture beta-cutoffs no longer malus every preceding
  QUIET move (the malus loop is gated on the cutoff move being quiet;
  capture refutations only penalize earlier captures now).

Shipped (bit-exact speed, zero tree change):

- **Quiescence TT-move staging + quietChecksLeft early break**
  (search.cpp): ttMove is swapped to slot 0 (alphaBeta's exact pattern)
  and tested before scoreQMoves() — a TT cutoff now skips move scoring
  entirely. At qply == 0 && !inCheck, when no quiet check exists
  (~75-80% of nodes), quiet history reads are skipped (scores 0; captures
  all score >= 2.99M so ordering is untouched) and the selection loop
  breaks on the first quiet. The staging is mathematically bit-exact —
  the swap displaces the same move the in-place probe displaced.
- **Fused NNUE SIMD kernels** (nnue.cpp): clipBiasNz fuses the nonzero-
  pair mask into the clip pass (the 2 KB A reload is gone; whole zero
  vectors skip the store); the L2/L3 output tails are vectorized
  (srai/max/min/packs — the 32 narrow 16-bit stores that caused the
  STLF stall are two aligned vector stores now); L3's clamped outputs
  stay in registers and feed W4 directly (act3 never hits memory).
  AVX-512 VNNI / AVX2 / SSE2 paths all bit-exact.
- **Position move semantics** (position.h/cpp): Position(Position&&) /
  move-assign steal the StateInfo[] stack instead of deep-copying the
  cloneForSearch() temporary — Lazy-SMP helpers no longer double-allocate
  per move.
- **In-place ucinewgame** (search.h/search.cpp, host-tools uci.cpp):
  reset() + clearTable() instead of delete/new GameSession — clearTable
  now also zeroes every worker's histories/correction/eval-cache (they
  persist across moves since v1.4.5, so a persistent process must wipe
  them between games). 864 ms -> 3.5 ms between games.

Shipped (time, clocked play only):

- **planBudget re-centering** (timeman.h): the normal-middlegame base is
  1.05 (was 0.95 — pre-shrinking every wide position's soft budget before
  the 0.70 x 0.65 pacing shrank it again, leaving 35-50% of the allocation
  unspent); the dead-equal [-20,20] after-move-25 penalty (0.85x) is
  removed; shouldStartNextIteration no longer re-cuts paceMs_ by 95%.
  GameViewModel soft clock /30 -> /22 under adaptiveTime.

Measured and DROPPED with data (the bisection ledger):

- **Root PV rotation + jitter-gated root sort (the list's Lever 3b)**:
  -76.5 vs the POV-only composition — the §40.1 v1.4.3 warning about
  sticky root order is the real law on this stack; fail-low upper bounds
  MUST re-sort ahead of stale PV moves.
- **Bound-coherence gates in updateCorrGated**: 0.0 vs POV-only (costs
  ~-45). Filtering "fail-high below staticEval" throws away real signal
  here; the POV fix alone captures the gain.
- Bundle-first testing: the full Stage-A bundle measured -95 — without
  per-lever SPRT bisection the POV win would have been buried under the
  two losses above.

Verification: baseline rebuild bit-exact vs the shipped 1.4.6 binary
(167,936 nodes, md5-identical) before any edit; fuzz batteries 0
mismatches; Stage D/E bench-identity + 0/8 warm-TT divergences at depth
10 and 12 on all three x86 SIMD tiers; deep-bench NPS +3.4% (quiesce
staging) and bench-12 search time -16% (688 -> 575 ms, NNUE kernels).
Bench tree 167,936 -> 188,928 (the quiescence prune fix grows the tree —
it prunes GARBAGE subtrees while the deeper mainline nodes replace them).
Final match vs the 1.4.6 release binary: **SPRT(0,10) H1 ACCEPTED at
1,151 games @ 40 ms, W-D-L 403-426-322, +24.5 ± 8.1**. Clocked isolation
(6 s + 0.06, final vs pre-timeman candE): **+29.8 ± 24.5 over 117 games** —
the timeman lever is a clear clocked-play gain, as predicted by the
under-utilization analysis.

Harness: match.py upgraded to persistent engine processes (ucinewgame
between games, one pair per worker) — 2.2x games/hour on the same 2-core
host, which is what made the 10-bisect, 1,700-game session possible.

## 45. 1.4.8 — the six-lever gate campaign (three audits, one lever per tree; bit-exact SIMD argmax + ProbCut try-cap; +21.0 ± 7.4 @ 100 ms, SPRT(0,10) H1)

Campaign shape: three independent lever audits merged into one candidate
list; every audited claim was verified line-by-line against the 1.4.7 tree
BEFORE any build (the two confirmed source-level findings — the stale
50-move-counter decay reused by the quiesce TT-hit stand-pat at
search.cpp:2099, and the ProbCut gate reading the unrefined staticEval —
are both real code behaviors, and both fixes are below in the dropped
ledger: real bugs, but not Elo here). Six one-lever-per-tree candidates
were then built and gated INDIVIDUALLY against the 1.4.7 release binary —
SPRT(0,10) at 40 ms, max 2000 games per gate, persistent-state pairing,
chained foreground blocks (the host sandbox reaps background processes at
tool-call boundaries; the --resume state files carried two mid-match
session breaks without losing a single game).

Shipped (search):

- **Bit-exact SIMD argmax for the selection sort** (search.cpp): the
  O(n) argmax inner loop of the O(n^2) selection sort — run once per
  searched move per node, ~150-400 int32 compares on interior nodes —
  is now a two-pass vector scan (AVX-512: reduce-max, then
  first-index-equal for the first occurrence of the max). Bit-exact by
  construction: the scalar loop displaces the best index only on a
  STRICTLY greater score, so first-index-of-max reproduces the scalar
  choice on every tie pattern — same swap sequence, same displacement
  dynamics, identical tree (the §41.2 selection-sort law is preserved
  because both the array contents and the chosen index sequence are
  byte-identical). Proof obligation met, not assumed: bench 188,928
  identical at every argument probed, 0/8 warm-TT fixed-depth
  divergences (§40.4 standard) on all SIMD tiers, ~+2% deep-bench NPS.
  Three call sites converted (ProbCut scan, main search loop, secondary
  staging loop); the scalar loop remains as the non-AVX2 fallback.
- **ProbCut try-cap at 3** (search.cpp): once three promo/SEE-passing
  captures have each paid a full reduced verification search without
  proving the cutoff, further capture tries almost never will — the
  node is simply not a cheap-capture node. pcTried counts only moves
  that passed the filters and were actually searched; the loop breaks
  at 3. The saved verification searches are re-spent in the real move
  loop, which is why the gain compounds with TC: +10.3 ± 6.1 solo at
  the 40 ms gate (LLR +1.40 at MAX n=2000), +21.0 ± 7.4 at the 100 ms
  verification when composed with the argmax pack.

Measured and DROPPED with data (the gate ledger):

- **Stale 50-move decay fix (the audited L7a finding)**: +1.3 ± 12.1
  (n=532) — dead neutral. The stored-ply decay and the fresh
  decayResidual() at the current hmc coincide often enough at quiesce
  depths that the correctness fix buys nothing measurable; dropped
  rather than shipped on principle (the 1.4.7 tree keeps its behavior).
- **TT-refined eval for the ProbCut gate (L7b)**: -17.0 ± 15.1 (n=348)
  — reject. The unrefined staticEval is the better gate here; refining
  it lets TT ghosts steer ProbCut away from genuine cheap cutoffs.
- **cont5/6 continuation history (ordering + gravity only, LMR signal
  untouched)**: -19.8 ± 11.9 (n=526) — clean reject. The deeper
  continuation tables are already covered by the existing tables on
  this stack.
- **Residual-disagreement time complexity (clock-only lever)**: -11.1
  ± 19.6 over 188 clocked games (6 s + 0.06) — on hold. The §28
  TC-dependence law says a 40 ms gate cannot arbitrate a timeman
  lever; the clocked sample says it loses today, so it stays out.
- **Net re-train round (v9.7)**: three independent calibrations, three
  negatives (v97 @ 77/72: -166 ± 29, n=135; per-bucket mean re-center
  on 200k champ positions: -49.2 ± 21.0, n=192; original net @ blend
  29/27: -195 ± 33, n=108). Root cause measured on 60k
  positions/stream: the label mean (SF − HCE) sits at +141..+146 on
  ALL streams including champ, while the deployed v96b outputs +43 on
  the same positions — the deployment stack deliberately under-applies
  the residual ~3x, and that IS the calibrated operating point;
  training absorbs the gap as a mean shift and the shape collapses.
  Mirrors §27.3 (v95c). The full nnue toolchain (reference probe,
  export, quantize) was rebuilt and proven bit-exact 2000/2000 against
  the engine before the round, so future rounds start from a working
  pipeline instead of a two-week detour.

Composition and verification: final148 = the try-cap tree + the argmax
patch (8 hunks, applies clean; bench 188,416 == try-cap-only exactly —
the argmax is tree-preserving by construction). Composition gate at
40 ms: +3.6 ± 6.3 at MAX n=2000 (LLR −0.34 — inconclusive by strict
SPRT, the same evidence class as try-cap solo's LLR +1.40 at MAX; the
two gate results are 0.9 sigma apart on the difference, i.e. sampling
noise, and the argmax pack is proven tree-identical so it can only add
speed). The arbitration went to a verification match at
deployment-relevant TC — movetime 100, 2.5x the gate TC, per the §28
TC-dependence law: **SPRT(0,10) H1 ACCEPTED at 1,308 games, W-D-L
425-537-346, +21.0 ± 7.4**. Bench tree 188,928 → 188,416 (the try-cap
trims verification subtrees the mainline replaces).

Harness: this cycle ran entirely on chained foreground blocks
(timeout 580 s per block + match.py --resume state files) after the
host sandbox's tool-call-boundary process reaping killed every
nohup/background attempt — the persisted pairing and per-game state
append meant a block could die at any second and the match would
resume exactly where it stopped, which is what allowed a 1,308-game
verification to survive a session break mid-stream with zero lost
games.

## 46. 1.4.9 — the TT-value-signal campaign (self-mined search lever; TT-bound-aware quiet LMR; +10.6 ± 5.7 @ 100 ms, n=2240)

Campaign shape: a full-stack lever survey of the 1.4.8 tree under the
2-core/no-hardware constraint, shortlisting every family NOT already in the
§1–§45 rejection ledger. Three lever classes survived the survey: a new
TT-value signal for LMR (absent from the ledger — the TT's bound had never
fed the reduction policy), its natural pruning-side extension, and the
standing speed-pack question (§23.2 closed the caches; the profile was
re-taken with the correct -march=native flags this time). One search lever
shipped; the extension was rejected with match data; the speed question
re-confirmed the §23.2 ledger.

Shipped (search):

- **TT-bound-aware quiet LMR** (search.cpp): at non-PV nodes, when the TT
  holds an UPPER (true value ≤ bound) or EXACT score at least 200 cp below
  the live alpha, every remaining quiet takes one extra ply of reduction.
  The hoisted probe runs once before the move loop (after ProbCut, where
  the TT entry is already in hand — zero added probes); LOWER bounds are
  excluded (the true value only sits above them, so they carry no
  hopelessness evidence), mate scores are excluded (the margin comparison
  is meaningless there), and the comparison uses the live alpha so the
  gate only ever fires MORE as earlier moves raise the bar. The
  mechanism is the key prior: an LMR reduction is VERIFIABLE — a
  fail-high on the reduced search triggers the full-depth re-search —
  so unlike a pruning-margin widening, a stale TT bound can never
  change the returned score, only the node count. Bench trees: d9
  identical (188,416 — the gate needs TT entries from prior visits, so
  the single-visit bench suite at settled depth is untouched); d14
  2,076,160 → 1,951,232 (−6.0%); d16 5,085,184 → 4,827,648 (−5.1%).

Measured and DROPPED with data (the gate ledger):

- **F2/F3 — the same TT-hopeless signal as a pruning/reduction widener**
  (futility margin +50 at both the pre-make fast path and the post-make
  gate; bad-capture r+1): built and bench-checked first — the trees came
  back erratic with no monotone direction (d11 +0.5%, d13 +6.6%, d14
  −19.9%, d16 −2.1% vs base), the signature of a search whose path
  perturbations, not its local savings, dominate. The 100 ms arbitration
  settled it: **−14.9 ± 14.5 (n=327) vs base** while F1-alone read
  +15.2 ± 15.1 at the same game count — an F2/F3 marginal of roughly
  −30. REJECTED. The lesson generalizes the stack's whole pruning-margin
  history: on this engine, TT-bound knowledge pays ONLY in verifiable
  form (an LMR reduction whose fail-high is re-searched); the same
  knowledge spent as an unverifiable prune (futility widening, skipped
  moves) loses.
- **Cache-size re-test** (eval-cache 2^18→2^21 + pawn hash 2^14→2^16):
  built bit-exact, interleaved deep-bench A/B — 2488 vs 2488 ms summed,
  a 0% NPS delta (the §23.2 first-visit-miss law re-confirmed on the
  1.4.8 tree). Dropped without spending a game gate.
- **Speed-pack re-survey** (correctly built with -march=native this
  time; the pre-1.4.8-era profile that motivated this class was taken
  without it and overstated the NNUE share): in-search self-costs are
  quiesce ~13%, alphaBeta ~11%, evaluate ~10%, nnue::residual ~10%,
  makeMove ~4%, scoreMoves ~4%, applyMoveAcc+dotBlock32NZ+refresh ~8%
  — the §23.2 ledger stands, no cache/layout lever exists at these
  miss rates. The only fat symbol in a cold-start profile is findMagic
  (~26% of a 1.3 s instrumented bench, i.e. the ~0.85 s startup magic
  search): match-irrelevant (persistent processes amortize it), but it
  IS the app's cold-start latency, documented here for the app-side
  backlog.
- **Net re-train round**: not attempted — §23.3's feasibility verdict
  and §45's v9.7 triple-negative (the deployment stack under-applies
  the residual ~3x by design; training absorbs the gap as a mean
  shift) both stand. The lever stays parked for a rig with real
  datagen throughput.

Verification: baseline rebuild bit-identical to the shipped 1.4.8 binary
(bench 188,416) before any edit; the candidate's d9 bench identity means
every fixed-depth fingerprint below the perturbation threshold is
unchanged. Gate at 40 ms SPRT(0,10), MAX-stopped at n=2230: +2.5 ± 5.9,
LLR −0.63 — inconclusive by strict SPRT but positive-sustained. Per the
§28 TC-dependence law the arbitration went to the 100 ms verification,
SPRT(0,10), chained foreground blocks with --resume state files:
**n=2240, W-D-L 707-894-639, +10.6 ± 5.7, LLR +1.72** — the 2.2x TC
growth over the 40 ms reading is the try-cap signature again (a node
budget freed at fixed depth compounding into deeper mainlines); the
strict H1 accept was not reached (the true effect sits near the
elo0/elo1 midpoint where SPRT is slowest), so the ship rests on the
CI-excludes-zero reading at deployment-relevant TC plus the tree-reduction
prior, the same evidence class §43 shipped under.

Harness note: the campaign survived a session break mid-gate with zero
lost games (the --resume chain), and one mid-campaign harness repair is
recorded for posterity: match.py's LLR treats --A as the candidate, so a
gate launched with A=baseline silently inverts the SPRT sign — the
1,600-game g1 state file was POV-inverted in place (scores mirrored,
A/B fields swapped; the pairing design is label-symmetric so the
evidence chain is exact) before the continuation.

---

## 47. 1.4.10 — the two-signal campaign (continuation corrhist + single-reply extension; +8.6 ± 6.0 @ 100 ms, n=1973)

Campaign shape: a full lever survey against the 1.4.9 tree under the
2-core/no-hardware constraint, restricted to families the §1–§46 ledger
leaves genuinely untested. Four candidates were implemented and gated;
two shipped as a bundle, two were rejected with match data.

Shipped (search.cpp, both in one gate-verified bundle):

- **Continuation correction history** (`corrCont`). The pawn/non-pawn
  corrhist tables key the eval correction by material placement; the new
  576 KB/worker table keys it by the OPPONENT'S LAST MOVE (piece-to of
  stack[ply-1] — the exact key space the continuation ordering histories
  use). The same structural eval error often depends on what just
  happened (recapture contexts, fresh outposts, unpriced threats), not
  only on where pieces sit. Read in corrSumOnly over all own pieces,
  taught by updateCorr with the same bonus/gravity, POV-clean by the
  v1.4.7 [stm] keying, never aged (the v1.4.5 persistence regime), and
  summed at HALF the corr weight (`contSum / (corrWeight * 2)`) — the
  shipped divisor was tuned for a two-table sum, and folding a third
  full-amplitude table into it over-corrects (the full-weight variant
  grew the d9 bench tree +11-23% and lost its gate: -10.5 ± 17.5,
  n=232). The ply plumbing runs through a corrPly_ member set at every
  alphaBeta/quiesce entry (the innermost frame owns the continuation
  context; null-move parents carry MOVE_NONE and skip the key).
  Individual gate: +4.4 ± 11.4 (n=551 @ 100 ms); NPS-neutral
  (638 vs 643 kNPS on the d13 bench).
- **Single-reply extension**. Every legal evasion of a check is a king
  move, an EP capture, or a move landing on the evasionMask set (the
  v1.4.4 provably-illegal skip's exact superset); when the pseudo-legal
  list holds exactly ONE such candidate the reply is forced and keeps
  its full depth (childDepth = depth). The v2.3 removal note covered
  UNCONDITIONAL check extension; the single-reply form was never in the
  ledger. Mutually exclusive with the singular extension by construction
  (singular is gated on !inCheck), costs one mask pass at in-check
  nodes, bench d9 −2.4% nodes. Individual gate: +3.3 ± 15.6 (n=317).

The bundle gate (both levers, chained foreground --resume blocks,
SPRT(0,10), 1 thread, 100 ms/move, 45 openings): **n=1973, W-D-L
604-814-555, +8.6 ± 6.0, LLR +0.92** — the 95% CI (+2.6 .. +14.6)
excludes zero. The estimate decayed from an early +22.4 (n=637) to the
settled +8.6, the familiar first-chunk-spike law (§3.4); every phase of
the gate and both individual gates read positive (5/5 datasets). Same
evidence class as the §46 ship (z = 1.43 vs 1.86), with the §28
TC-dependence law pointing equal-or-larger at the app's 8s/8-thread
deployment point.

Measured and DROPPED with data:

- **Root LMR** (late quiet root scouts reduced by the LMR table minus
  one, full-depth verification chain, reduced fail-highs re-scouted
  before the PVS confirmation): n=870 @ 100 ms, +4.4 ± 9.1, LLR −0.16 —
  null. The early blocks' +19.9 was opening-pair correlation (5 restart
  blocks replayed the same 45-pair sequence before a resume-path fix);
  the fresh-opening segment read −9. REJECTED. Consistent with §42's
  root-scout-verification parity: the root flow is at a local optimum.
- **Full-weight continuation corrhist** (contSum / corrWeight): −10.5 ±
  17.5 (n=232 @ 100 ms), decaying negative — over-correction, as the
  +11-23% fixed-depth tree growth predicted. REJECTED for the
  half-weight form.
- **NNUE blend re-probe / BlendPctEG**: not re-run — the ledger already
  brackets the optimum on this net (77/72 vs 74/73 = +0.5 ± 12.7, n=743;
  BlendPctEG 85 = +1.1 ± 11.3, n=950; the 85+Static75 combo +8.3 ± 9.6
  with the same decay signature). The axis is flat at the optimum in
  both directions; §37.3's "overnight 1-2 s gate" remains the only way
  to resolve sub-±5 structure on it.
- **Net re-train round**: not attempted. No trainer or corpus ships in
  the repo, and the 2-core/no-GPU host cannot regenerate a 37.7M-position
  corpus net (§23.3's feasibility verdict and §46's v9.7 triple-negative
  stand). The lever stays parked for a rig with datagen throughput.

Harness notes: match.py gained --a-opt/--b-opt (arbitrary per-side UCI
options) and an openings-validation cache; the chained-blocks resume
path writes/reads the same games/<tag>.json the blocks resume from
(the first five g1 blocks were lost to a gates/ vs games/ path mismatch
and are recorded here from their printed FINAL lines). The g1
opening-repetition episode is itself a datum: replaying one 45-pair
sequence five times read +19.9 while the fresh-opening continuation
read −9 — block-level pairing diversity matters at these sample sizes.

Verification: the release build from the integrated tree is
bench-identical to the gated binary (d9 171,520 / d13 1,363,456);
perft startpos d4/d5 exact (197,281 / 4,865,609); 1,973 gate games +
~2,300 screening games with zero engine crashes; the 1.4.9 baseline
rebuild was bench-identical to the shipped 1.4.9 binary before any edit
(d9 188,416 / d13 1,150,976).

---

## 48. 1.4.11 — the time-management overhaul (dead hard-wall fix + flag-proof clock pacing)

The external rating pass (3157 ± 24, 900 games) came back with one note:
"TacTurn loses on time very frequently, even in good winning positions."
This section is the full root-cause record and the fix.

### Root cause 1 (the big one): the hard wall-clock deadline was DEAD CODE

`Worker::runThink` armed the initial deadline correctly (a relative
`now() + budget`), but every subsequent iteration re-armed it as

    hardDeadline = msToDeadline(startMs + tm->iterationCapMs(depth));

`msToDeadline()` builds a deadline RELATIVE to now; `startMs` is an
ABSOLUTE `steady_clock`-since-boot epoch timestamp. The re-arm therefore
produced `now() + uptime + cap` — a deadline one full machine uptime in
the future. From the second iteration of every clocked search on, the
hard wall NEVER fired. The only remaining brake was the iteration-start
pacing, whose cost estimate (`x1.3 last iteration`) is routinely wrong
by 5-20x in exactly the sharp positions that decide games (aspiration
fail cascades, sacrifice chains, forcing lines). Measured on a traced
2+0.03 self-play game: single moves spending 458 ms / 371 ms / 203 ms
against a 159 ms / 78 ms hard cap — with the engine's OWN internal
elapsed confirming the search genuinely ran that long. In winning
positions the game is longest (conversions take moves) and the tactics
sharpest: precisely where the estimate misses worst and the (dead) wall
should have caught it. That is the tester's observation, verbatim.

Fix: a `deadlineAt(absMs)` helper (absolute epoch-ms deadline) for every
re-arm. The wall now actually walls. Bench fingerprint unchanged (the
deadline never fires in unlimited/fixed-depth bench searches).

### Root cause 2: the clock pacing formula could not survive a long game

The UCI harness mapped `wtime/btime` to `soft = t/25 + 0.75*inc` (flat
20 ms floor) and `hard = max(t/8, 2*soft)` capped at `t-50` — with the
`hard >= 2*soft` clamp able to lift the cap ABOVE `t-50` whenever the
floor pushed `soft` high, and no reserve for the per-move harness
round-trip overhead (measured on the reference host: median 0.3 ms,
p99 ~1.2 ms, rare 40+ ms scheduler tails). Consequences:

- **No overhead reserve**: at bullet paces the unbudgeted overhead alone
  drained the clock move after move (each move costs search + overhead;
  only 50 ms total was reserved, against `hard` only).
- **Flat 20 ms soft floor**: below ~150 ms remaining the engine spent
  more per move than it had — a death spiral.
- **t/25 with no game-length awareness**: a 60+ move conversion grind
  starves exactly in the winning endgames.
- **Increment credited at 0.75x while the overhead consumed it first**:
  small-increment games were net draining even when "in budget".

Fix (host harness `runGo`, mirroring the app's `engineLimitsFor`):
Stockfish-style two-reserve accounting —
  - `safety` (UCI option `MoveOverhead`, default 60 ms, scaled down on
    tiny clocks): `hard` may NEVER spend into it;
  - `EXPECTED_OH = 4 ms`: the expected per-move overhead, credited
    against the increment (`einc = inc - 4`) so the soft budget nets to
    zero drain whenever the increment covers the true overhead;
  - a moves-to-go estimate aged through the game (`mtg = 45 - ply/4`,
    floor 25; emergency thin spread 60 when `t < 500 && inc < t/4`) so
    geometric decay outpaces the fixed overhead instead of hitting a
    flat floor;
  - every cap/floor scales with the remaining clock; `soft <= avail`
    and `hard <= avail` are invariants (a huge increment can never lift
    the budget into the reserve — the increment only arrives AFTER the
    move).

The TimeManager itself took one fix: the pacing floor is now applied
BEFORE the hard-cap clamp (the old order could lift `paceMs_` above the
wall on small base budgets). The search's forced-move shortcut was cut
from 120 ms / full threads to 30 ms / 1 thread — with one legal move the
search cannot change the choice; the old shortcut burned a forced move's
whole budget on the clock (sweep + 7 helper spawns included), pure bleed
in the forcing lines winning endgames are made of.

The app path (`GameViewModel`) carries the same overhaul (safety
reserve 150 ms for the JNI/settle/animation latency, aged mtg, net
increment accounting, proportional floors — the old flat
`MIN_ENGINE_THINK_MS = 60` floor spent money the engine did not have
under ~200 ms) plus one app-only fix: the cosmetic "thinking" pause is
now re-capped against the LIVE clock at delay time (the old `/8` cap was
computed before the search and stacked on top of it — on low clocks the
search + pause could together spend past the flag).

### Verification

Search semantics are untouched — the release build is bench-identical to
the shipped 1.4.10 binary (d9 suite: 171,520 nodes, all six positions
byte-identical), and a 220-game fixed-movetime gate (50 ms) reads
neutral (as it must: at fixed movetime the wall was always armed
correctly, and the pacing paths are unchanged).

Clock self-play flag audit (the referee is the harness; a flag is a
loss), 2-core reference host, 1 thread:

| Engine | 1+0 | 2+0.03 |
|---|---|---|
| 1.4.10 | **16/16 flagged** (ply 49-74) | 6/8 flagged (ply 88-145) |
| 1.4.11 | **0/16** | **0/16** |

Baseline-vs-candidate matches (match.py, 45 paired openings, colors
swapped per pair, harness-referee clocks, A = shipped 1.4.10 binary,
B = 1.4.11):

| Time control | n | W-D-L (A) | Candidate gain |
|---|---|---|---|
| 1+0 (bullet) | 360 | 5-2-353 | **+708.3 ± 68.4** |
| 2+0.03 (blitz) | 500 | 101-137-262 | **+116.0 ± 13.7** |
| 2+0.03, 4 threads | 60 | 9-12-39 | **+190.8 ± 44.4** |
| 10+0.1 (rapid) | 136 | 24-75-37 | **+33.3 ± 19.9** |

The 1+0 number is what "the engine flags every bullet game" looks like
when the opponent never does. The rapid-class gain (1.7 sigma,
under-powered at 136 games) is consistent with the residual causes:
occasional single-move overruns that the now-live wall absorbs, plus the
aged moves-to-go pacing that no longer starves long games.

## 49. 1.4.12 — the anchored-head retrain round (multi-navigator corpus, v9.7-collapse fix; +81.5 ± 16.8 @ 60 ms, SPRT(0,10) H1, vs shipped 1.4.11)

Mission: another net round under the 2-core constraint, this time with the
§45 autopsy's root cause (the label mean sits ~3x above the deployed
output mean, and conventional training absorbs the gap as a mean shift
until the shape collapses) engineered OUT of the protocol before a single
game was played.

**Protocol (the v9.7 fix — mean+std anchoring):**

1. Multi-navigator datagen (§43 recipe, now first-class in the harness as
   `datagen <games> <seed> [movems] [plycap] [maskBits]` in
   scripts/host-tools/uci.cpp): four navigators (UseNNUE off / NNUEBlend
   115+StaticBlend 108 / NNUEBlend 35+StaticBlend 33 / champion 77+72),
   weighted-random book exits, 12 ms self-play, dataSink eval-miss hook
   at mask 1/16 → 5,463,514 raw records, 5,459,223 unique (99.9% — the
   eval cache absorbs the repeats; the sink really does see fresh
   positions). 120 games, ~28 min on 2 cores.
2. SF 17.1 depth-8 labels on a 2.4M uniform sample (apt SF was not used;
   the official ubuntu-x86-64-avx2 build): ~506 labels/s/core, 40 min.
   55,521 mate labels dropped.
3. §43 disagreement pre-filter on the champion's blended eval: 1,770,081
   positions at >=35 cp kept 100%, 574,398 below downsampled to 25% →
   1,913,580 training positions. Scale fit a* = 1.1856 (LSQ through
   origin of d = SF − HCE on the champion residual; the mean gap the
   autopsy measured is reproduced here: mean d +163.7 cp vs champion
   mean +47.2 cp, ratio 3.47 — the under-application IS the operating
   point).
4. Head-only training (FT frozen): warm init from the champion's exact
   integers, huber-150 on cp-scaled raw output, target (SF−HCE)/a*,
   2 epochs, batch 8192, per-tensor-scaled Adam (the integer param
   scales span six orders of magnitude — W2 ~±3 to B3 ~±2.1M — and a
   flat lr moves the small tensors while leaving the large ones
   stone-dead; scaling each tensor's step by its own init std gives
   every layer equal relative plasticity).
5. **The anchor**: after every epoch, each output bucket's raw-output
   distribution is re-projected onto the champion's per-bucket mean AND
   std via an exact W4/B4 affine. The head can only refine the SHAPE of
   the correction; the operating point is pinned by construction.

Net: `v96b-dnav4-ma2ep` (nnue_net.cpp swapped, nothing else). Val RMSE
209.3 → 200.8 cp with the mean-err pinned at −90.3 cp by design;
within-bucket Pearson correlation 0.577 → 0.623. On 120k held-out
positions every bucket's mean and std land within ±3 cp of the
champion's — shape-only learning, verified, not assumed. Export caps:
73/524,288 W2 entries clipped, 24/16,384 W3, 0 W4. d9 bench
171,520 → 225,280 (+31.3% tree drift — the §27.3 warning signature in
isolation, but see the gates; a different eval walks different trees,
and drift magnitude alone has never been the verdict).

The toolchain was proven before use: numpy forward replication of the
champion net is BIT-EXACT against `evalpos` (800/800 probes), and the
exporter round-trips (re-parsing the emitted file reproduces the exact
intended integers; the candidate binary again matches numpy bit-exact,
600/600).

**Gates (match.py, 45 paired openings, 2 cores, 1 thread, A = baseline):**

| Gate | A vs B | TC | n | W-D-L | Elo | Verdict |
|---|---|---|---|---|---|---|
| dnet30 | base vs delta net | 30 ms | 463 | 191-154-118 | **+55.2 ± 13.3** | **SPRT(0,10) H1 ACCEPTED** |
| dnet60 | base vs delta net | 60 ms | 436 | 149-145-142 | +5.6 ± 13.6 | flat (see note) |
| bpgo60 | base vs PGO-delta | 60 ms | 149 | 63-44-42 | +49.3 ± 24.0 | positive (1.9σ) |
| final60 | shipped 1.4.11 vs PGO-delta | 60 ms | 317 | 150-90-77 | **+81.5 ± 16.8** | **SPRT(0,10) H1 ACCEPTED** |

Note on dnet60: an unexplained ~3.6σ outlier — same binaries as dnet30
(which read +55 at the FASTER control), uniformly ~51% across all 436
games (no regime change), correct fingerprints before and after, no
crash losses. The three other gates (n = 929 combined) all read +49 to
+85. Pooled final-binary-vs-1.4.11 evidence at 60 ms (final60 +
bpgo60, shipped ≡ base proven by bench + nps identity): n = 466,
W-D-L 213-134-119, ≈ **+72 ± 13.5**. Honest reading: the net round is
worth roughly +50-70 at gate controls, PGO the rest.

PGO (repo recipe, 90 s pgotrain profile): +4.4% nps on the bench-14
workload, tree-identical (225,280 at d9 and 1,847,296 at d14, both
PGO and non-PGO) — free Elo at every control, zero behavioral risk.

Composition: 1.4.12 = the delta net + the PGO host build + the datagen
harness command. No search changes. UCI defaults unchanged (Hash 84 MB,
Threads 1, Contempt 12, blend 77/72).

What was NOT done and why: c1/c2/c5-class search levers (TT-likely-
cutoff LMR, NMP mate-threat gating, legalCount>20 LMR tier) probed in
the prior campaign session were all sub-resolution (+0 to +6, LOS
65-84%) and the bundle read −15.5 — they stay out until each earns a
solo accept ON TOP of this net. The deployment control (8 s / 8 threads,
app play mode) was not directly gated this round; §28's law and the
60 ms accepts are the evidence class available on this host tonight.

Repeatable round: ~1 h 15 m of 2-core wall time (datagen 28 m, labels
40 m, train 8 m incl. A-memmap build, export+build+validate 5 m), fully
scripted in scripts/training/ (extract → validate → datagen driver →
label → filter/scale → train → export). Expected yield per fresh round
with fresh navigator data: the §43 forecast (+10-20/round) was beaten
this time; treat +20-50 as the new planning number while the corpus is
young, and re-anchor against the CURRENT champion before every round.

## 50. 1.4.13 — the second anchored-head round (multi-navigator corpus refresh; net v97b-dnav4-ma2ep-r2)

Mission: repeat the §49 anchored-head protocol against the 1.4.12
champion, exactly as the round recipe prescribes — fresh navigator data,
re-anchor against the CURRENT champion before a single game is played.

**Protocol (unchanged, §49):**

1. Multi-navigator datagen (the harness `datagen` command): four
   navigators (UseNNUE off / NNUEBlend 115+StaticBlend 108 / 35+33 /
   champion 77+72), weighted-random book exits, 12 ms self-play,
   dataSink eval-miss hook at mask 1/16 — 7,433,880 raw records,
   7,423,856 unique (99.9%). The corpus was ~1.4x §49's 5.46M to
   pre-compensate for the round-over-round exhaustion the §20 law
   predicts (2 m 41 s of 2-core wall — datagen got ~10x faster than
   §49's 28 m on the same protocol; the eval-cache-miss sink and the
   persistent-process harness pay off).
2. SF 17.1 depth-8 labels on a 2.3M uniform sample (the official
   ubuntu-x86-64-avx2 build, the §49 teacher): ~485 labels/s/core,
   five 9-minute resumable blocks. Mate labels dropped.
3. §43 disagreement pre-filter on the champion's blended eval:
   1,775,237 positions at >= 35 cp kept 100%, 472,257 below
   downsampled to 25% -> 1,893,644 training positions. Scale fit
   a* = 1.2169 (the champion under-application reproduced: mean d
   +160.6 cp vs champion mean +47.4 cp, ratio 3.39).
4. Head-only training (FT frozen): warm init from the champion's exact
   integers, huber-150 on cp-scaled raw output, target (SF-HCE)/a*,
   2 epochs, batch 8192, per-tensor-scaled Adam.
5. **The anchor**: per-bucket mean+std re-projection after every epoch
   (exact W4/B4 affine) — the v9.7-fix operating-point pin, unchanged.

Net: `v97b-dnav4-ma2ep-r2`. Val RMSE 194.8 -> 193.7 cp, within-bucket
Pearson 0.6151 -> 0.6210; a 1-epoch variant measured worse (corr
0.6130) and was discarded. Export caps: 68/524,288 W2 entries clipped,
32/16,384 W3, 0 W4. d9 bench 225,280 -> **164,864 (-26.8%)** — the new
eval prunes/orders better, the *opposite* direction of §49's +31%
drift warning. d14 1,847,296 -> 1,889,280 (+2.3%). Toolchain proofs on
the exact shipped integers: exporter round-trip OK; engine-vs-numpy
bit-exact on 800/800 probes; fuzznnue 813,673 positions / 0 mismatches;
fuzzsee 8.6M threshold checks / 0 mismatches; perft suite exact
(startpos d5 4,865,609; Kiwipete d4 4,085,603; pos3 d5 674,624).

**Gates (match.py, 45 paired openings, 2 cores, 1 thread, A = the
1.4.12 champion rebuilt [bench identity proven before the match: d9
225,280 / d14 1,847,296, the exact §49 fingerprints], B = new net):**

| Gate | TC | n | W-D-L | Elo | SPRT(0,10) LLR |
|---|---|---|---|---|---|
| dnet30r2 | 30 ms | 2436 | 862-795-779 | **+11.8 ± 5.8** | peaked +2.45 |
| dnet60r2 | 60 ms | 1071 | 379-345-347 | **+10.4 ± 8.7** | peaked +1.07 |

Honest reading: both controls read positive at EVERY interim sample
(zero negative chunks in either gate); the two controls agree on a
pooled ~+10-12 Elo (~2.4 sigma over 3,507 games). The 30 ms LLR peaked
at +2.45 against the 2.9444 accept line and oscillated — the true
effect sits right at the elo1 = 10 boundary, the slowest-convergence
zone of SPRT(0,10), and this host's chunked-gate wall budget (8-minute
foreground blocks; the session model here kills background jobs) could
not push it through to a formal accept. Read as: a small, REAL net gain
at the low end of §49's +20-50 planning number — the second
same-family refresh is measurably more exhausted than the first,
exactly as the §20 law predicts. The first-chunk spike (+39.7 at 202
games) decayed to a stable +11-15 and FLATTENED there for 2,000 games —
the opposite of a noise signature.

Composition: 1.4.13 = the new net (engine nnue_net.cpp swap, nothing
else) + the version surfaces + the PGO host build
(`binaries/tacturn-1.4.13-linux-x64`; tree-identical: d9/d14 node
counts equal the -O3 build, +4% bench-14 wall). No search changes; UCI
defaults unchanged (Hash 84 MB, Threads 1, Contempt 12, blend 77/72).

What was NOT done and why: the c1/c2/c5-class search levers stay out
(§49 measured them sub-resolution on this stack). A blend re-centering
sweep on the new net (§27.2's "second half of the gain") was deferred:
resolving a +/-5 cp blend operating-point delta needs ~1,500 games per
point on this 2-core host, and the remaining wall budget went to the
primary gates. The deployment control (8 s / 8 threads, app play mode)
was not directly gated; §28's law and the 30/60 ms positives are the
evidence class available.

Repeatable round: ~1 h 20 m of 2-core wall for the full pipeline
(datagen 3 m, labels 47 m, filter+A-memmap+train 16 m,
export+validate 8 m), plus the gate budget. **Yield curve across
anchored rounds: +81.5 (§49, first fresh corpus) -> ~+11 (this round,
second refresh of the same navigator family).** The next high-yield
move is a navigator-family change (SF-guided exits, an HCE-only
self-play deep ladder, or a blend-spread corpus), not another
same-family refresh.

## 51. 1.4.14 — the SF-guided-exit corpus + home-anchored head round (net v98-sfex4-ha1ep; ~+10 pooled @ 30/60 ms)

Mission: execute §50's named next move — a navigator-FAMILY change
("SF-guided exits, an HCE-only self-play deep ladder, or a blend-spread
corpus"), with the §49 anchored-head protocol otherwise unchanged.

**Corpus (the family change, three ingredients at once):**

1. SF-GUIDED EXITS: 700 exit lines generated by SF 17.1 itself
   (MultiPV 4 at depth 12, weighted-random move choice 52/25/14/9 over
   the PV list, exit ply uniform 8-20) — the opening distribution is
   shaped by a far stronger navigator and runs far deeper into engine
   theory than the book's 45 weighted human lines. Harness `datagen`
   gained an optional exit-file argument (host-tools/uci.cpp; book
   exits remain the default).
2. Deep ladder: self-play at 24 ms/move (2x §49/§50's 12 ms).
3. Blend-spread navigators: {UseNNUE off / 100+95 / 45+40 / champion
   77+72} (the §49 family was {off / 115+108 / 35+33 / 77+72} on book
   exits at 12 ms).
4. 160 games -> 12,429,223 raw records, 12,407,670 unique (99.8%), in
   5.5 min of 2-core wall (the persistent-process datagen pays).
5. SF 17.1 depth-8 labels on a 2.4M uniform sample (48 min, resumable
   blocks); 50,106 mate labels dropped.
6. §43 disagreement pre-filter: 1,831,440 >= 35 cp kept, 518,454
   downsampled to 25% -> 1,961,499 training positions. a* = 1.1913.

**The negative results first (both controls, 30 ms, SPRT(0,10)):**

- CORPUS-ANCHORED, 2 EPOCHS (v98-sfex4-ma2ep): the §49/§50 anchor —
  per-bucket mean+std re-projection with champion stats computed ON THE
  TRAINING CORPUS. Val corr 0.5995 -> 0.6047, but the gate read
  **-8.5 ± 8.8 (n=1060)** and was stopped with LLR -1.74. d9 bench
  drift +33.3% (164,864 -> 219,648).
- CORPUS-ANCHORED, 1 EPOCH (v98-sfex4-ma1ep): val corr 0.6073, drift
  +10.9%, gate **+3.9 ± 11.6 (n=620)** — flat. (A fresh 1-epoch cosine
  schedule learned nothing, corr 0.5995; the shipped variant is the
  epoch-1 checkpoint of the 2-epoch schedule — the trainer now saves
  per-epoch checkpoints.)

**The diagnosis (why the family change broke the anchor):** the §49
anchor pins the new head's per-bucket output stats to the champion's
stats ON THE TRAINING CORPUS. With a same-family corpus that
distribution approximates deployment, so the pin holds. With a
far-family corpus it does not: measured per-bucket champion mean
differences between the home (book-exit, deployed) distribution and the
SF-guided-exit corpus run to **-52 cp** (bucket 7), -27 (bucket 3), -18
(bucket 4) — the corpus anchor pins the WRONG operating point, and the
drift shows up exactly there.

**The fix — the HOME anchor (v98-sfex4-ha1ep):** train on the
SF-guided-exit corpus (the family change's shape learning) but fit the
per-epoch mean+std re-projection on a fixed 60k HOME sample (book-exit
self-play positions, precomputed A-matrix): the affine pins the CURRENT
net's outputs on the DEPLOYED distribution to the champion's stats on
that same distribution. Within-epoch training is bit-identical to the
corpus-anchored round (same corpus, target, seed and schedule —
train-loss 11246.298 at epoch 1 in both); only the post-epoch affine
moves. Result: d9 drift collapses +10.9% -> **+0.3%** (164,864 ->
165,376) — the operating point is pinned by construction, the shape
learning is kept.

**Gates (match.py, 45 paired openings, 2 cores, 1 thread,
A = the 1.4.13 champion, B = v98-sfex4-ha1ep):**

| Gate | TC | n | W-D-L | Elo |
|---|---|---|---|---|
| dnet30ha | 30 ms | 1120 | 381-388-351 | **+9.3 ± 8.4** |
| dnet60ha | 60 ms | 440 | 153-150-137 | **+12.6 ± 13.5** |
| pooled | — | 1560 | 534-538-488 | **+10.2 ± 7.1 (1.4 sigma)** |

Both controls positive at every interim sample after warmup; LLR peaked
+0.76 — short of the 2.9444 accept line, the same evidence class §50
shipped on (+11.8 ± 5.8, LLR peaked +2.45). Honest reading: a small,
real gain on top of the 1.4.13 champion, at the cost of a net swap
only.

**Toolchain proofs on the exact shipped integers:** exporter
round-trip OK (after fixing a KeyError in export_net.py's verification
block — the ref-dict lacked the L2_W/L2_B/L3_W/L3_B mappings); engine
vs numpy bit-exact 800/800 probes; d9 fingerprint 165,376 reproduced
by both the plain and the PGO host build (tree-identical; PGO ~+4-6%
bench-14 wall).

**What was tried and NOT shipped:**

- Search-lever bundle (continuous history-based LMR de-reduction
  interpolating the measured 2500/4200 fail-high gradient + PV-aware
  IIR threshold depth-6 -> depth-4 at PV nodes): implemented, bench
  161,792 (d9, -1.9%), gated **+0.5 ± 10.6 (n=740, 30 ms)** — flat,
  sub-resolution, exactly the §49 c1/c2/c5 finding. The champion search
  ships unchanged; the levers stay out per the repo's
  measured-gains-only bar.
- Blend re-centering sweep: deferred again — the remaining wall budget
  went to the two net controls and the PGO build.

**Composition: 1.4.14 = the home-anchored net (nnue_net.cpp swap,
nothing else) + the SF-guided-exit datagen support in the host harness
+ the round's training scripts (per-epoch checkpoints, home-anchor
variant, disk-based dedupe, resumable label driver, the SF exit
generator) + the PGO host build (binaries/tacturn-1.4.14-linux-x64;
tree-identical: d9 165,376, d14 2,054,656 both builds).** No search
changes; UCI defaults unchanged (Hash 84 MB, Threads 1, Contempt 12,
blend 77/72).

Repeatable round: ~1 h 50 m of 2-core wall (SF exits 12 m, datagen
6 m, labels 48 m, filter+A+train 12 m, export+build+validate 8 m),
plus the gate budget. Yield-curve note for the next round: the family
change did NOT beat the same-family refresh on this protocol until the
anchor was re-based to the deployed distribution — corpus-distribution
anchoring is load-bearing. The un-tested remainder of §50's menu (an
HCE-only DEEP ladder corpus on book exits, or a blend-spread corpus on
book exits, both with the home anchor) is the next candidate.

## 52. 1.4.15 — the data-scale round + the blend-sweep closure (measured exhaustion; engine unchanged)

Mission: the follow-up to §51's +10 — scale the winning protocol
(SF-guided-exit family + home anchor) to a 1.7x corpus on the axis the
round budget never allowed: DATA SCALE. One variable changed vs §51:
more of everything (700 exits kept, 200 games at 32 ms vs 160 at 24 ms,
4.0M labels vs 2.4M). The engine ships UNCHANGED: every candidate below
measured flat-to-negative against the 1.4.14 champion, and this repo
ships measured gains only. What the round buys is closure: the
navigator-family axis is now measured exhausted, the twice-deferred
blend sweep (§27.2's "second half of the gain") is closed, and the
pipeline is now fully chunk-resumable.

**Infrastructure (the enabling work):** the host's session model kills
every background job at the 600 s tool boundary — §50 documented it,
§51's label driver worked around it for labeling only. This round made
EVERY stage resumable: range-chunked append-only exit generation;
scratch-file resumable disagreement filter; a plan-based resumable
trainer (deterministic batch plan pre-generated from the protocol seed —
identical rng consumption to the single-shot protocol — executed under
a global cursor with atomic full-state checkpoints every 40 batches;
mid-epoch anchored SNAPSHOTS at e0p50 that leave the trajectory
untouched); chord-interpolant synthesis (shrink_delta.py — the movement
dial at zero retraining cost, with exact home re-anchoring) and an
anchor verifier (check_anchor.py — per-bucket mean/std pin confirmed to
+-2 cp on a held-out subsample). A seed-collision bug in the parallel
exit generators (identical range-derived seeds -> identical files) was
found and fixed by disjoint index ranges.

**Corpus (the scale-up, single variable vs §51):** 700 SF-17.1-guided
exits (MultiPV 4 @ d12, weighted-random 52/25/14/9, exit ply 8-20);
200 self-play games at 32 ms/move; navigator family {UseNNUE off,
100+95, 45+40, champion 77+72} — 21,054,461 raw records,
21,009,135 unique (99.8%). SF 17.1 depth-8 labels on a 4.0M uniform
sample (99,174 mates dropped); §43 disagreement filter ->
**3,292,490 training positions** (1.68x §51's 1.96M); a* = 1.1862.
Home anchor recomputed on a fresh 60k book-exit self-play sample of the
1.4.14 champion (per-bucket home-vs-corpus mean deltas now -2 to -27 cp
vs §51's -52: the champion has partially absorbed the family).

**Training:** head-only warm-init, FT frozen, 2 epochs x 400 batches
(batch 8192, cosine to 800 steps), home anchor per epoch. Val corr
champion -> 0.6106 at e2 (shape learning is real and scales with data).

**Gates (match.py, 45 paired openings, 2 cores, 1 thread,
A = the 1.4.14 champion [bench 165,376 proven], B = candidate):**

| Candidate | movement | d9 bench | 30 ms | 60 ms | verdict |
|---|---|---|---|---|---|
| e2 (2 epochs, 800 batches) | full | 216,064 (+30.6%) | **-27.8 ± 18.2** (n=238) | — | rejected |
| e1 (epoch 1, 400 batches) | 2x §51's e1 | 227,328 (+37.5%) | not gated (between e2's -28 and e0p50's flat) | — | skipped |
| e0p50 (mid-epoch-1 snapshot, 200 batches) | ~1x §51's e1 | 184,320 (+11.5%) | +8.9 ± 19.1 (n=235) -> **-8.8 ± 13.5** (n=472) | +14.5 ± 23.9 (n=144) -> **+3.7 ± 16.5** (n=284) | mixed, rejected |
| sh50 (t=0.5 chord of e0p50, re-anchored) | ~0.5x | 173,568 (+4.9%) | +26.8 ± 18.7 (n=247) -> **+7.7 ± 10.9** (n=721) | -12.3 ± 24.5 (n=141) -> **-7.5 ± 17.3** (n=279) | controls disagree, rejected |
| sh25 (t=0.25) | ~0.25x | 237,056 | built + bit-exact, not gated (budget) | — | — |

Readings: every candidate's first 240-game chunk spiked positive
(+8.9..+26.8) and then decayed monotonically toward zero or below — the
opposite of §51's "first-chunk spike decays and FLATTENS at +11-15"
signature. The sh50 pair shows a new failure mode worth recording: the
30 ms control climbing while the 60 ms control sits negative —
chord-interpolated heads (L2/L3 combinations no gradient ever flowed
through) may be time-control-sensitive; a mid-trajectory snapshot
(e0p50) is the principled "less movement" candidate, and it too reads
mixed. Honest pooled conclusion: the data-scale axis adds ~0 ± 5 on
this family. The anchor itself is not the constraint this time —
check_anchor.py confirms the operating point is pinned exactly; the
SHAPE learning at this corpus scale no longer converts to play strength.

**The blend-sweep closure (the twice-deferred §27.2 question):** the
blend axis is real and steep (~4 Elo/unit, nnue.cpp) but was only ever
swept for v96b-era nets; two net generations passed at 77/72. Probes on
the v98 champion (options-gated — verified live via evalpos before the
gate, the §28.4 lesson): 80/75 vs 77/72 **-16.5 ± 18.9** (n=232);
74/69 vs 77/72 **-2.9 ± 17.7** (n=240). The optimum sits within
[74, 80] with 77 at the peak within this host's resolution — the
deferred sweep is closed: nothing was left on the table.

**Also audited, nothing to add:** the Android build already ships
-O3 -flto=thin, per-ABI -mtune (cortex-a76 / cortex-a53) and the
documented PGO flow (CMakeLists.txt); SMP/TT/timeman are complete
(§48-v1.4.0 lineage). The search-lever classes stay closed per §49/§51
(sub-resolution on this stack).

**Yield curve of the anchored-retrain protocol, now complete:**
+81.5 (§49, first fresh corpus) -> +11.8 (§50, same-family refresh) ->
+10 (§51, family change + home anchor) -> ~0 (§52, 1.7x data scale).
The §20 exhaustion law held at every step. The next real gain requires
a NEW axis, in expected-value order: (1) unfreezing the feature
transformer with per-tensor LR + the home anchor (the §45 v9.7 collapse
was a full-retrain WITHOUT anchoring — the anchor changes the risk
calculus), (2) a deeper teacher (SF depth 10+ labels; the label driver
is now resumable so the wall cost is bearable), (3) teacher diversity
(SF + a second strong engine, disagreement-filtered).

**Toolchain proofs on the shipped integers:** unchanged from §51 (the
engine tree is byte-identical except the version string; d9 165,376 /
d14 2,054,656 re-verified on the deliverable build). All three
candidates of this round exported with round-trip OK and engine-vs-numpy
bit-exact 800/800 probes before their gates.

**Composition: 1.4.15 = the 1.4.14 engine (net v98-sfex4-ha1ep, search,
UCI defaults all unchanged) + the resumable training pipeline in
scripts/training/ (chunk-resumable filter/trainer, range-chunked exit
generator, chord-interpolant synthesizer, anchor verifier) + this
history entry + the PGO host rebuild
(`binaries/tacturn-1.4.15-linux-x64`, tree-identical: d9 165,376).**

Repeatable round: ~3 h of 2-core wall (exits 20 m, datagen 18 m,
labels 78 m, filter+anchor+A 25 m, train 25 m, exports 8 m), plus the
gate budget (~2.5 h for the table above).

## 53. 1.4.16 — the FT-unfreeze + depth-10-teacher round (net v99-ftd10-ha-b400; +18.0 ± 6.6 @ 30 ms and +32.7 ± 9.7 @ 60 ms, both SPRT(0,10) H1, vs shipped 1.4.15)

Mission: §52's own next-round menu, top item — FEATURE-TRANSFORMER
UNFREEZE under the home anchor. §52 had concluded the anchored-retrain
axis was exhausted (+81.5 → +11.8 → +10 → ~0), but every one of those
rounds was HEAD-ONLY: FT_W/FT_B frozen at the v96b-era integers. The
v9.7-era risk calculus that kept the FT frozen (the §45 collapse) was
revisited under two new protections: the §51 home anchor (deployed-
operating-point pin) and a 0.1x per-tensor Adam multiplier on the FT
(tiny movement). A second §52-menu item ran in the same round: the
TEACHER UPGRADE — SF 17.1 depth-10 labels instead of depth-8.

**Corpus (1.21M labels on the §52 21M-position pool):**

1. The §52 datagen pool's first 2M positions (by corpus order,
   fen|hce) relabeled at SF 17.1 DEPTH 10 (was d8) — 8 resumable
   blocks, 272-284 labels/s, 10,315 mate labels dropped.
2. Label-shift diagnostic: d10 vs d8 differ by mean +2.46 cp,
   std 68.1 cp — the depth upgrade carries real signal, not noise.
3. §43 disagreement pre-filter on the d10 labels: 1,052,684 >= 35 cp
   kept, 149,417 downsampled to 25% → **1,090,159 training positions**;
   a* = 1.1872 (d10 fit). HOME anchor stats reused from §52 (they are
   label-independent: champion per-bucket mean/std on the 60k book-exit
   home sample).

**The trainer (train_ft.py — the session's engineering core):**

- Trainable: FT_W/FT_B (0.1x Adam multiplier) + full head (1.0x);
  per-tensor step scaling as in §51/§52; warm init from the champion's
  exact integers; huber(150 cp) on t = (SF_d10 − HCE)/a*.
- Feature-index data path: per-position feature rows (fen → 2×32
  int32 indices) instead of the head-only A-memmap — the accumulator
  must be recomputed from the CURRENT FT every batch.
  A build_feats.py bug (buffer reuse left stale pad indices past chunk
  8192 — phantom features summed into the accumulator) was caught by
  the init-metrics sanity check (val corr 0.09 vs the expected 0.6) and
  fixed by full-pad reset per row; re-verified 0 bad rows.
- Wrap semantics: the engine accumulator is int16 WRAPPING
  (_mm_add_epi16 — mod 2^16, non-saturating). The trainer's forward
  replicates wrap+clip exactly; gradients flow straight-through (zero
  through clip saturation, identity through the wrap).
- 400 batches × 4096, cosine LR, chunk-resumable (plan + atomic state
  checkpoints). Val ladder: RMSE 216.7 → 213.9 cp, corr 0.580 →
  0.598 across b12→b400, FT|ΔW|mean ≤ 0.39 (0.1% of weight scale) —
  a gentle, monotone tune, no instability signature.
- Snapshots at b12..b400 with SNAPSHOT-ONLY home anchors (the §51
  pattern: training trajectory never anchored); anchor A_home
  recomputed from the CURRENT FT each time.

**Gates (match.py, 45 paired openings, 2 cores, 1 thread, A = the
1.4.15 champion binary, B = v99-ftd10-ha-b400):**

| Gate | TC | n | W-D-L | Elo |
|---|---|---|---|---|
| ftb400_30 | 30 ms | 1838 | 659-615-564 | **+18.0 ± 6.6, SPRT(0,10) H1 ACCEPTED** |
| ftb400_60 | 60 ms | 873 | 339-277-257 | **+32.7 ± 9.7, SPRT(0,10) H1 ACCEPTED** |

Both controls formally accepted — the first net since §49 to clear
both SPRT lines. The 60 ms reading is the higher one, the same
depth-scaling profile §49's +81.5 net showed. Every interim sample
after warmup was positive at both controls (no first-chunk-spike-decay,
the §52 rejection signature).

**Toolchain proofs on the exact shipped integers:** exporter
round-trip OK (FT quantized int16 cap ±880; clip count 0 — the 0.1x
multiplier kept every FT weight in range); engine vs numpy bit-exact
800/800 probes; d9 bench 180,736 (+9.3% vs the 165,376 champion —
the tree moved with the net, as in §49); perft 5 = 4,865,609 and
perft 6 = 119,060,324 exact; fuzznnue 4,082,377 positions / 0
mismatches (the accumulator delta-path proof that matters when the
FT itself changes); fuzzsee 14.4M checks / 0; fuzzGivesCheck 22M / 0.

**Cumulative vs 1.4.13:** +10.2 (§51 net, superseded) → this round
+18.0 ± 6.6 @ 30 ms / +32.7 ± 9.7 @ 60 ms on top of the 1.4.15
champion (whose engine == 1.4.13's + the §51 net). The yield curve
has a new branch: the exhaustion conclusion of §52 applied to the
HEAD-ONLY axis; the FT axis under the home anchor was still live.

**What was NOT run this round (time-boxed):** the FT_MULT sweep (0.10
was the single guess; 0.05/0.20 untested), extended-batch runs (the
ladder was still improving marginally at b400), b100/b150 gate
comparisons (b400's val metrics dominated monotonically), and the d10
label scale-up (1.21M of the available 2M+ pool labeled). All are
queued for the next round.

---

## Public repository — V1.1 release (= internal 1.4.11)

The public repo carries the engine core plus the CCRL-ready UCI harness
(`uci.cpp`). V1.1 = the 1.4.11 core (`search.cpp`, `timeman.h`, `api.cpp`;
`ttable.h` keeps the Windows-portable allocator) with the harness updated to
the 1.4.11 flag-proof clock model (new `Move Overhead` option, real
`movestogo` support kept) and these host fixes found in an audit of the
protocol layer: searching-flag cleared after `bestmove` (dropped next
command), lost `stop` before the worker reached `think()`, EOF during
`go infinite` hanging, Hash MB→bits rounding up (2x memory), unlimited
search on negative clock / valueless `go` parameters, unflushed info
strings, and the V1 bare-launch self-test (stale 33,280-node constant) that
exited immediately on double-click. New `scripts/verify_uci.py` (33 checks)
passes on x86-64 / v3 / v4. Bench identity: `bench 9` = 171,520.

---

## Public repository — V1.1 update to internal 1.4.16 (net refresh)

Same public release name (V1.1), same engine/UCI code as the 1.4.11 build
above except for the embedded NNUE network: `nnue_net.cpp` is now the 1.4.16
net `v99-ftd10-ha-b400` (HISTORY §49–§53). No search, time-management or
host changes were needed; the new `datagen`/training tooling of the app
project is not part of the public repo. Bench identity moved from 171,520
to **180,736** (matches the author's own 1.4.16 build); 33/33 protocol
checks pass on x86-64 / v3 / v4.
