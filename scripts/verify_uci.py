#!/usr/bin/env python3
"""TacTurn UCI protocol check. Usage: python3 scripts/verify_uci.py <engine-binary>"""
import subprocess, sys, time, threading, queue

class Eng:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.q = queue.Queue()
        threading.Thread(target=self._rd, daemon=True).start()
    def _rd(self):
        for l in self.p.stdout: self.q.put(l.rstrip("\r\n"))
        self.q.put(None)
    def send(self, s): self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def until(self, pred, timeout=30):
        out, end = [], time.time() + timeout
        while time.time() < end:
            try: l = self.q.get(timeout=0.1)
            except queue.Empty: continue
            if l is None: return out, False
            out.append(l)
            if pred(l): return out, True
        return out, False
    def alive(self): return self.p.poll() is None

res = []
def check(name, ok):
    res.append(ok); print(("[PASS] " if ok else "[FAIL] ") + name)

path = sys.argv[1]
e = Eng(path)
e.send("uci"); out, ok = e.until(lambda l: l == "uciok", 10)
t = "\n".join(out)
check("id name is 'TacTurn V1.2'", "id name TacTurn V1.2" in out)
check("id author present", any(l.startswith("id author ") for l in out))
check("Hash option 1..32768 MB", "option name Hash type spin default 32 min 1 max 32768" in out)
check("Move Overhead option", any("name Move Overhead type spin default 60" in l for l in out))
check("Threads option", any("name Threads" in l for l in out))
check("OwnBook default false", any("name OwnBook type check default false" in l for l in out))
check("MultiPV option", any("name MultiPV" in l for l in out))
check("Clear Hash button", any("name Clear Hash type button" in l for l in out))
check("uciok received", ok)
e.send("isready"); _, ok = e.until(lambda l: l == "readyok", 5); check("isready -> readyok", ok)
for o in ("Hash value 256", "Threads value 2", "Hash value 1", "Hash value 1024", "Hash value 64", "Threads value 1"):
    e.send("setoption name " + o)
e.send("isready"); _, ok = e.until(lambda l: l == "readyok", 20); check("setoption Hash/Threads accepted, still responsive", ok)
e.send("ucinewgame"); e.send("position startpos"); e.send("go depth 8")
out, ok = e.until(lambda l: l.startswith("bestmove"), 30); check("startpos go depth -> bestmove", ok)
e.send("position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1 moves e2a6")
e.send("go depth 7"); out, ok = e.until(lambda l: l.startswith("bestmove"), 30); check("position fen + moves + go", ok)
e.send("position startpos moves e2e4 e7e5 g1f3"); e.send("go movetime 300")
t0 = time.time(); out, ok = e.until(lambda l: l.startswith("bestmove"), 10)
check("go movetime respected", ok and time.time() - t0 < 1.5)
e.send("go nodes 20000"); out, ok = e.until(lambda l: l.startswith("bestmove"), 15); check("go nodes", ok)
e.send("go wtime 20000 btime 20000 winc 100 binc 100"); out, ok = e.until(lambda l: l.startswith("bestmove"), 10)
check("increment clock", ok)
e.send("position startpos"); e.send("go wtime 60000 btime 60000 movestogo 40")
t0 = time.time(); out, ok = e.until(lambda l: l.startswith("bestmove"), 10)
check("movestogo clock (mtg-aware budget)", ok and time.time() - t0 < 4)
e.send("go wtime 0 btime 0"); out, ok = e.until(lambda l: l.startswith("bestmove"), 5); check("zero clock does not hang", ok)
e.send("go wtime -50 btime -50"); out, ok = e.until(lambda l: l.startswith("bestmove"), 5); check("negative clock does not hang", ok)
e.send("go infinite"); time.sleep(0.5); e.send("isready")
_, ok = e.until(lambda l: l == "readyok", 3); check("isready answered during search", ok)
t0 = time.time(); e.send("stop"); out, ok = e.until(lambda l: l.startswith("bestmove"), 3)
check("go infinite + stop -> bestmove", ok)
e.send("setoption name MultiPV value 3"); e.send("go depth 6")
out, ok = e.until(lambda l: l.startswith("bestmove"), 30)
check("MultiPV 3: three lines", sum(1 for l in out if l.startswith("info multipv")) >= 3 and ok)
e.send("setoption name MultiPV value 1")
e.send("position startpos"); e.send("go depth 6"); out, ok = e.until(lambda l: l.startswith("bestmove"), 15)
check("OwnBook false: engine searches (info lines before bestmove)", any(l.startswith("info depth") for l in out))
e.send("setoption name OwnBook value true"); e.send("go depth 6"); out, ok = e.until(lambda l: l.startswith("bestmove"), 15)
check("OwnBook true works", ok); e.send("setoption name OwnBook value false")
e.send("position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1"); e.send("go depth 6")
out, ok = e.until(lambda l: l.startswith("bestmove"), 15)
check("mate in 1 -> 'score mate 1' + Ra8", any("score mate 1" in l for l in out) and out[-1].startswith("bestmove a1a8"))
for junk in ("garbage", "position", "position fen", "go depth", "setoption", "setoption name", "position startpos moves zzzz", ""):
    e.send(junk)
e.send("stop"); e.send("isready"); _, ok = e.until(lambda l: l == "readyok", 8); check("survives malformed input", ok and e.alive())
e.send("position startpos"); e.send("perft 5"); out, ok = e.until(lambda l: l.startswith("perft"), 60)
check("perft 5 = 4865609", any("4865609" in l for l in out))
e.send("position startpos"); e.send("go depth 7"); out, _ = e.until(lambda l: l.startswith("bestmove"), 15)
check("info lines include nps", any(" nps " in l for l in out))
e.send("go infinite"); time.sleep(0.3); t0 = time.time(); e.send("quit")
for _ in range(40):
    if not e.alive(): break
    time.sleep(0.1)
check("quit during search exits <4s", not e.alive() and time.time() - t0 < 4)

# stop sent immediately after go (no delay) must still stop the search
e4 = Eng(path); e4.send("position startpos"); e4.send("go infinite"); e4.send("stop")
_, ok = e4.until(lambda l: l.startswith("bestmove"), 5); check("'go infinite' immediately followed by 'stop' -> bestmove", ok)
e4.send("go depth 5"); _, ok = e4.until(lambda l: l.startswith("bestmove"), 10)
check("position/go accepted right after bestmove (no dropped command)", ok); e4.send("quit")
# stdin closed during a search must not hang
e2 = Eng(path); e2.send("position startpos"); e2.send("go infinite"); time.sleep(0.3); e2.p.stdin.close()
for _ in range(40):
    if not e2.alive(): break
    time.sleep(0.1)
check("stdin closed during 'go infinite' -> clean exit", not e2.alive())
# Hash MB must map to <= requested memory (regression: used to be 2x)
e3 = Eng(path); e3.send("setoption name Hash value 256"); e3.send("position startpos"); e3.send("go depth 14")
time.sleep(2.5)
try:
    rss = int([l for l in open(f"/proc/{e3.p.pid}/status") if l.startswith("VmRSS")][0].split()[1]) // 1024
    check(f"Hash 256 uses <= ~300 MB RSS (measured {rss} MB)", rss < 300)
except Exception:
    print("[SKIP] RSS check needs Linux /proc")
e3.send("quit")
n = len(res); p = sum(res)
print("=" * 30); print(f"RESULT: {p} passed, {n - p} failed  ({path})")
sys.exit(0 if p == n else 1)
