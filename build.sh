#!/bin/bash
# TacTurn V1.2 — reproducible release build (all 3 ISA tiers).
#   native:  ./build.sh                 -> ./bin/ (Linux)
#   mingw:   ./build.sh x86_64-w64-mingw32-g++-posix
#            (Windows exes, statically linked)
# Bench identity check: every build must print "Total nodes: 230912"
# for `bench 9`.
set -e
cd "$(dirname "$0")"
mkdir -p bin

CXX="${1:-g++}"
WIN=0
case "$CXX" in *mingw*) WIN=1 ;; esac

for TIER in x86-64 x86-64-v3 x86-64-v4; do
  OUT="bin/TacTurn-V1.2-$TIER"
  FLAGS="-std=c++17 -O3 -flto -DNDEBUG -march=$TIER"
  if [ "$WIN" = 1 ]; then
    OUT="$OUT.exe"
    FLAGS="$FLAGS -pthread -static -static-libgcc -static-libstdc++"
  fi
  echo "=== building $OUT ==="
  $CXX $FLAGS -I. -Itacturn \
      uci.cpp tacturn/*.cpp \
      -o "$OUT" -lpthread
  if [ "$WIN" = 1 ]; then
    x86_64-w64-mingw32-strip "$OUT" 2>/dev/null || true
  else
    strip "$OUT"
    echo -n "bench 9: "; "./$OUT" bench 9 | tail -1
  fi
done
echo "done."
