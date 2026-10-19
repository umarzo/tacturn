#!/usr/bin/env python3
"""TacTurn SPRT match runner.

Plays engine-vs-engine games over UCI pipes, adjudicates with the engine's
own game-over detection, and runs a sequential SPRT (fishtest trinomial
LLR, alpha=beta=0.05).

usage: match.py --A bin/tacturn-base --B bin/tacturn-cand \
                --movetime 100 --max-games 4000 --sprt 0 10 \
                --concurrency 2 --tag mytest
"""
import argparse
import json
import math
import os
import random
import subprocess
import sys
import threading
import time

# 40 fixed opening lines (UCI move sequences from startpos), varied theory.
OPENINGS = [
    "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7",
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 b1c3 f8b4",
    "e2e4 e7e5 g1f3 b8c6 c2c3 g8f6 d2d4 e5d4 c3d4 d7d5",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6",
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5",
    "e2e4 c7c5 c2c3 g8f6 e4e5 f6d5 d2d4 c5d4 g1f3 b8c6",
    "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 e4e5 f6d7 f2f4 c7c5",
    "e2e4 e7e6 d2d4 d7d5 b1d2 c7c5 e4d5 e6d5 g1f3 b8c6",
    "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6",
    "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1d3 f5d3 d1d3 e7e6",
    "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c7c6",
    "e2e4 g8f6 e4e5 f6d5 g1f3 d7d6 d2d4 d6e5 f3e5 b8c6",
    "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 f2f4 f8g7 g1f3 e8g8",
    "e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 f2f4 g8f6 g1f3 e8g8",
    "e2e4 f7f5 g1f3 g7g6 d2d4 f8g7 b1c3 g8f6 f1c4 d7d6",
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8",
    "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5",
    "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 e7e6 c1g5 f8e7",
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5",
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8",
    "d2d4 g8f6 c2c4 g7g6 b1c3 d7d6 e2e4 e7e6 g1f3 f8g7",
    "d2d4 g8f6 c2c4 c7c6 g1f3 d7d5 b1c3 e7e6 c1g5 h7h6",
    "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7",
    "d2d4 g8f6 c2c4 g7g6 g2g3 f8g7 f1g2 e8g8 g1f3 d7d6",
    "d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 b1c3 c7c6 e2e3 b8d7",
    "d2d4 d7d5 g1f3 g8f6 c1f4 c7c6 e2e3 b8d7 f1d3 e7e6 e1g1 f8e7",
    "d2d4 g8f6 g1f3 e7e6 c1g5 h7h6 g5h4 b7b6 b1c3 c8b7",
    "d2d4 d7d5 g1f3 g8f6 c1f4 c7c5 e2e3 b8c6 b1c3 e7e6",
    "d2d4 f7f5 g1f3 g8f6 g2g3 e7e6 f1g2 f8e7 e1g1 e8g8",
    "d2d4 d7d5 c1f4 g8f6 e2e3 c7c5 c2c3 b8c6 g1f3 e7e6",
    "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5 c4d5 f6d5",
    "c2c4 g8f6 b1c3 e7e6 g1f3 d7d5 d2d4 d5c4 g2g3 c7c6 f1g2",
    "c2c4 c7c6 g1f3 d7d5 b1c3 d5c4 a2a4 c8f5 e2e3 e7e6",
    "c2c4 e7e6 g1f3 d7d5 b1c3 g8f6 d2d4 f8e7 c1g5 e8g8",
    "g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8",
    "g1f3 g8f6 c2c4 g7g6 b1c3 f8g7 d2d4 e8g8 g2g3 d7d6 f1g2",
    "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5",
    "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 d2d3 g8f6 c2c3 d7d6 e1g1 e8g8",
    "e2e4 e7e5 b1c3 g8f6 f2f4 d7d5 e4d5 f6e4 g1f3 b8c6",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 e1g1 f8e7",
    "e2e4 e7e5 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4",
    "e2e4 e7e5 g1f3 g8f6 b1c3 b8c6 f1b5 f8b4",
    "d2d4 d7d5 c2c4 e7e6 g1f3 g8f6 b1c3 c7c6 e2e3 b8d7",
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 e8g8 a2a3 b4c3",
]

GR = {0: "ongoing", 1: "1-0", 2: "0-1", 3: "1/2", 4: "1/2", 5: "1/2", 6: "1/2", 7: "1/2"}


class Engine:
    def __init__(self, path, args=None):
        self.path = path
        self.p = subprocess.Popen(
            [path] + (args or []), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def send(self, cmd):
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def readline(self, timeout=None):
        return self.p.stdout.readline()

    def uci(self):
        self.send("uci")
        while True:
            l = self.readline().strip()
            if l == "uciok":
                return

    def newgame(self):
        self.send("ucinewgame")
        self.send("isready")
        while self.readline().strip() != "readyok":
            pass

    def setoption(self, name, value):
        self.send(f"setoption name {name} value {value}")

    def position(self, moves):
        self.send("position startpos" + ((" moves " + " ".join(moves)) if moves else ""))

    def go_movetime(self, ms):
        self.send(f"go movetime {ms}")
        while True:
            l = self.readline().strip()
            if l.startswith("bestmove"):
                parts = l.split()
                if len(parts) >= 2 and parts[1] != "(none)":
                    return parts[1]
                return None

    def go_nodes(self, n):
        self.send(f"go nodes {n}")
        while True:
            l = self.readline().strip()
            if l.startswith("bestmove"):
                parts = l.split()
                if len(parts) >= 2 and parts[1] != "(none)":
                    return parts[1]
                return None

    def go_clock(self, wtime_ms, btime_ms, winc_ms, binc_ms):
        # v1.4.0: clock-based games (wtime/btime) — exercises the adaptive
        # time manager (the harness maps clocked `go` to adaptiveTime=true).
        self.send(f"go wtime {int(wtime_ms)} btime {int(btime_ms)} "
                  f"winc {int(winc_ms)} binc {int(binc_ms)}")
        while True:
            l = self.readline().strip()
            if l.startswith("bestmove"):
                parts = l.split()
                if len(parts) >= 2 and parts[1] != "(none)":
                    return parts[1]
                return None

    def isover(self):
        self.send("isover")
        while True:
            l = self.readline().strip()
            if l.startswith("isover"):
                return int(l.split()[1])

    def quit(self):
        try:
            self.send("quit")
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


class EnginePair:
    """v1.4.7: persistent engine processes, one pair per worker thread.

    The old runner spawned two fresh OS processes per game; every startup
    pays ~0.9 s of magic-bitboard search + TT allocation + first-touch page
    faults (measured on the 2-core gate host) — more than half the
    wall-clock of a fast fixed-node game. One pair per worker with
    `ucinewgame` between games removes that overhead entirely. Both
    engines' ucinewgame handlers fully reset state (TT + histories +
    correction tables), so each game starts from the same clean conditions
    a fresh process provided."""

    def __init__(self, path_a, path_b):
        self.A = Engine(path_a)
        self.B = Engine(path_b)
        self.A.uci()
        self.B.uci()

    def get(self, a_is_white):
        """Returns (white_engine, black_engine) for this game."""
        return (self.A, self.B) if a_is_white else (self.B, self.A)

    def quit(self):
        self.A.quit()
        self.B.quit()


def play_game_wrapper(W, Bk, opening, movetime, a_is_white, opts):
    """Play one game with PERSISTENT engine instances (v1.4.7: the caller
    owns the processes; ucinewgame resets both between games).
    Returns result from A's POV."""
    nodes = opts.get("nodes", 0)
    clock = opts.get("clock")  # (base_ms, inc_ms) or None
    try:
        W.newgame(); Bk.newgame()
        if opts.get("w_hash"): W.setoption("Hash", opts["w_hash"])
        if opts.get("b_hash"): Bk.setoption("Hash", opts["b_hash"])
        if opts.get("w_threads", 1) != 1: W.setoption("Threads", opts["w_threads"])
        if opts.get("b_threads", 1) != 1: Bk.setoption("Threads", opts["b_threads"])
        moves = []
        W.position(moves)
        for mv in opening:
            moves.append(mv)
            W.position(moves)  # validates silently (illegal -> error string, ignored)
        Bk.position(moves)
        ply = len(moves)
        to_move = 0
        # v1.4.0 clock state (clock mode only).
        base_ms, inc_ms = clock if clock else (0, 0)
        wtime_ms, btime_ms = base_ms, base_ms
        while ply < opts.get("ply_cap", 300):
            mover = W if to_move == 0 else Bk
            if clock:
                t0 = time.monotonic()
                mv = mover.go_clock(wtime_ms, btime_ms, inc_ms, inc_ms)
                spent = (time.monotonic() - t0) * 1000.0
                if to_move == 0:
                    wtime_ms = wtime_ms - spent + inc_ms
                else:
                    btime_ms = btime_ms - spent + inc_ms
                # Flag fall (the engine's own hard cap makes this rare, but
                # the runner is the clock's referee).
                if wtime_ms <= 0:
                    return 0.0 if a_is_white else 1.0
                if btime_ms <= 0:
                    return 1.0 if a_is_white else 0.0
            elif nodes > 0:
                mv = mover.go_nodes(nodes)
            else:
                mv = mover.go_movetime(movetime)
            if mv is None:
                r = W.isover()
                if r == 0:
                    return None
                break
            moves.append(mv)
            W.position(moves)
            Bk.position(moves)
            ply += 1
            to_move ^= 1
            r = W.isover()
            if r != 0:
                break
        else:
            r = 3  # ply cap -> draw
        # r: 1 white wins, 2 black wins, 3..7 draw
        if r in (3, 4, 5, 6, 7):
            return 0.5
        white_won = (r == 1)
        a_won = white_won == a_is_white
        return 1.0 if a_won else 0.0
    except Exception:
        return None


def sprt_llr(W, D, L, elo0, elo1):
    n = W + D + L
    if n < 20:
        return 0.0
    s = (W + 0.5 * D) / n
    m2 = (W + 0.25 * D) / n
    var = max(m2 - s * s, 1e-4)
    # s_i is the expected score implied by elo_i under the logistic model:
    # s_i = 1 / (1 + 10^(-e_i/400)). The old code used s_i = elo_i*ln10/400
    # (an untransformed slope), which produced absurd LLRs for equal engines
    # (+4.3 at n=20, +86.8 at n=400) because s0/s1 collapsed toward 0.
    s0 = 1.0 / (1.0 + 10.0 ** (-elo0 / 400.0))
    s1 = 1.0 / (1.0 + 10.0 ** (-elo1 / 400.0))
    return (s1 - s0) * (2 * s - s0 - s1) * n / (2 * var)


def elo_of(W, D, L):
    n = W + D + L
    if n == 0:
        return 0.0, 0.0
    s = (W + 0.5 * D) / n
    s = min(max(s, 1e-3), 1 - 1e-3)
    elo = -400 * math.log10(1 / s - 1)
    n = W + D + L
    var = (W + 0.25 * D) / n - s * s
    # Delta method: d(elo)/ds = (400/ln10) / (s(1-s)), so
    # sigma = (400/ln10)/(s(1-s)) * sqrt(var/n).
    # The old formula dropped the 1/(s(1-s)) factor, giving error bars about
    # 4x too small at s=0.5 (a 100-game gate showed +/-12 instead of +/-50).
    sigma = math.sqrt(max(var, 1e-4) / n) * 400 / math.log(10) / (s * (1 - s))
    return elo, sigma


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--A", required=True)
    ap.add_argument("--B", required=True)
    ap.add_argument("--movetime", type=int, default=100)
    ap.add_argument("--nodes", type=int, default=0,
                    help="fixed nodes per move (0 = use movetime)")
    ap.add_argument("--clock", nargs=2, type=float, default=None,
                    metavar=("BASE_S", "INC_S"),
                    help="clock-based games, e.g. --clock 10 0.1 (overrides "
                         "movetime/nodes; exercises the adaptive time "
                         "manager via the harness's wtime/btime path)")
    ap.add_argument("--max-games", type=int, default=2000)
    ap.add_argument("--wall-min", type=float, default=0,
                    help="stop starting games after this many minutes "
                         "(clean exit; combine with --resume to chunk long "
                         "gates across sessions)")
    ap.add_argument("--sprt", nargs=2, type=float, default=None, help="elo0 elo1")
    ap.add_argument("--concurrency", type=int, default=2)
    ap.add_argument("--tag", default="test")
    ap.add_argument("--a-hash", default=None)
    ap.add_argument("--b-hash", default=None)
    ap.add_argument("--a-threads", type=int, default=1)
    ap.add_argument("--b-threads", type=int, default=1)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--resume", default=None, help="resume from results json")
    args = ap.parse_args()

    LLR_ACCEPT = 2.9444  # alpha=beta=0.05
    LLR_REJECT = -2.9444

    results = {"A": args.A, "B": args.B, "movetime": args.movetime,
               "nodes": args.nodes, "clock": args.clock, "games": [], "tag": args.tag}
    start_idx = 0
    if args.resume and os.path.exists(args.resume):
        with open(args.resume) as f:
            results = json.load(f)
        start_idx = len(results["games"])
        print(f"resumed with {start_idx} games")

    # Verify openings once (fast: one engine instance)
    valid_openings = []
    probe = Engine(args.A)
    probe.uci()
    for op in OPENINGS:
        moves = []
        ok = True
        for mv in op.split():
            moves.append(mv)
            probe.send("position startpos moves " + " ".join(moves))
            probe.send("isready")
            saw_illegal = False
            while True:
                line = probe.readline().strip()
                if line.startswith("info string illegal"):
                    saw_illegal = True
                if line == "readyok":
                    break
            if saw_illegal:
                ok = False
                break
        if ok:
            valid_openings.append(op.split())
    probe.quit()
    print(f"{len(valid_openings)}/{len(OPENINGS)} openings valid")
    if len(valid_openings) < 20:
        print("TOO FEW VALID OPENINGS — aborting")
        sys.exit(1)

    rng = random.Random(args.seed)
    order = list(range(len(valid_openings)))
    rng.shuffle(order)

    W = D = L = 0
    for g in results["games"]:
        if g == 1.0: W += 1
        elif g == 0.5: D += 1
        else: L += 1

    lock = threading.Lock()
    stop_flag = [False]
    t_start = time.time()

    import queue
    q = queue.Queue()

    def worker(wid):
        # v1.4.7: one persistent engine pair per worker thread. The pair is
        # recreated only if a game crashes (a dead process would poison
        # every later game on this worker).
        pair = [None]

        def get_pair():
            if pair[0] is None:
                pair[0] = EnginePair(args.A, args.B)
            return pair[0]

        while not stop_flag[0]:
            with lock:
                if counter[0] >= args.max_games or stop_flag[0]:
                    return
                if args.wall_min > 0 and (time.time() - t_start) > args.wall_min * 60:
                    stop_flag[0] = True
                    return
                game_no = counter[0]
                counter[0] += 1
            # pairing: game 2k and 2k+1 share an opening with swapped colors
            opening_idx = order[(game_no // 2) % len(order)]
            opening = valid_openings[opening_idx]
            a_is_white = (game_no % 2 == 0)
            opts = {"ply_cap": 300, "nodes": args.nodes}
            if args.clock:
                opts["clock"] = (int(args.clock[0] * 1000), int(args.clock[1] * 1000))
            if a_is_white:
                opts["w_hash"], opts["b_hash"] = args.a_hash, args.b_hash
                opts["w_threads"], opts["b_threads"] = args.a_threads, args.b_threads
            else:
                opts["w_hash"], opts["b_hash"] = args.b_hash, args.a_hash
                opts["w_threads"], opts["b_threads"] = args.b_threads, args.a_threads
            try:
                p = get_pair()
                W, Bk = p.get(a_is_white)
                res = play_game_wrapper(W, Bk, opening, args.movetime,
                                        a_is_white, opts)
                if res is None:
                    # the pair may be poisoned — rebuild before the next game
                    try:
                        p.quit()
                    except Exception:
                        pass
                    pair[0] = None
            except Exception:
                res = None
                try:
                    if pair[0] is not None:
                        pair[0].quit()
                except Exception:
                    pass
                pair[0] = None
            q.put((game_no, res))

    counter = [start_idx]

    threads = []
    for wid in range(args.concurrency):
        t = threading.Thread(target=worker, args=(wid,), daemon=True)
        t.start()
        threads.append(t)

    out_path = f"games/{args.tag}.json"
    os.makedirs("games", exist_ok=True)
    t0 = time.time()
    try:
        while True:
            try:
                game_no, res = q.get(timeout=2.0)
            except queue.Empty:
                if not any(t.is_alive() for t in threads):
                    break
                continue
            if res is None:
                print(f"game {game_no}: ERROR — counted as loss for A (crash/protocol)")
                res = 0.0
            results["games"].append(res)
            if res == 1.0: W += 1
            elif res == 0.5: D += 1
            else: L += 1
            n = W + D + L
            if n % 20 == 0 or n == args.max_games:
                elo, sig = elo_of(W, D, L)
                msg = (f"[{args.tag}] games={n} W-D-L {W}-{D}-{L} "
                       f"elo {elo:+.1f} ± {sig:.1f} "
                       f"({(time.time()-t0)/60:.1f} min)")
                if args.sprt:
                    llr = sprt_llr(W, D, L, args.sprt[0], args.sprt[1])
                    msg += f" LLR {llr:+.2f} ({args.sprt[0]},{args.sprt[1]})"
                print(msg, flush=True)
                with open(out_path, "w") as f:
                    json.dump(results, f)
            if args.sprt:
                llr = sprt_llr(W, D, L, args.sprt[0], args.sprt[1])
                if llr >= LLR_ACCEPT:
                    print(f"SPRT: H1 ACCEPTED (candidate gains >= {args.sprt[1]} Elo) "
                          f"after {n} games: W-D-L {W}-{D}-{L}")
                    verdict = "H1"
                    break
                if llr <= LLR_REJECT:
                    print(f"SPRT: H0 ACCEPTED (no gain / reject) after {n} games: "
                          f"W-D-L {W}-{D}-{L}")
                    verdict = "H0"
                    break
            if n >= args.max_games:
                print(f"max games reached: W-D-L {W}-{D}-{L}")
                verdict = "MAX"
                break
    finally:
        stop_flag[0] = True
        for t in threads:
            t.join(timeout=5)
        with open(out_path, "w") as f:
            json.dump(results, f)
    elo, sig = elo_of(W, D, L)
    print(f"FINAL [{args.tag}]: {W+D+L} games, W-D-L {W}-{D}-{L}, "
          f"Elo {elo:+.1f} ± {sig:.1f}")


if __name__ == "__main__":
    main()
