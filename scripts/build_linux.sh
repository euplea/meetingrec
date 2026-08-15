#!/usr/bin/env bash
# Compila meetingrec per Linux (g++ + PortAudio + libcurl).
# Uso: bash scripts/build_linux.sh
set -euo pipefail
cd "$(dirname "$0")/.."

make clean >/dev/null 2>&1 || true
make -j"$(nproc)"

echo
echo "OK: ./meetingrec"
./meetingrec --help | head -3
