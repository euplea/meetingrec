#!/usr/bin/env bash
# Cross-compila meetingrec per Windows (MinGW-w64) con icona e manifest.
# Richiede: x86_64-w64-mingw32-g++ e windres.
# Uso: bash scripts/build_windows.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX=x86_64-w64-mingw32-g++
WINDRES=x86_64-w64-mingw32-windres
OUT=build-windows
mkdir -p "$OUT"

echo "== 1/2 Compilazione risorse (icona + manifest) =="
"$WINDRES" resources/app.rc -O coff -o "$OUT/app_res.o"

echo "== 2/2 Compilazione sorgenti =="
SRCS=(src/main.cpp src/minutes.cpp src/recorder.cpp src/transcriber.cpp src/tui.cpp src/wav_writer.cpp)
OBJS=()
for f in "${SRCS[@]}"; do
  obj="$OUT/$(basename "${f%.cpp}").o"
  "$CXX" -std=c++17 -O2 -Wall -Wextra -Isrc -c "$f" -o "$obj"
  OBJS+=("$obj")
done

"$CXX" -std=c++17 -O2 -o "$OUT/meetingrec.exe" \
    "${OBJS[@]}" "$OUT/app_res.o" \
    -lole32 -lwinhttp -static -static-libgcc -static-libstdc++

echo
echo "OK: $OUT/meetingrec.exe"
file "$OUT/meetingrec.exe"
