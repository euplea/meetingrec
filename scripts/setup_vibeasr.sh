#!/usr/bin/env bash
# Scarica e compila VibeASR.cpp (VibeVoice-ASR su CPU) e i modelli GGUF.
# Uso:  bash scripts/setup_vibeasr.sh
set -euo pipefail

BASE="${VIBEASR_HOME:-$HOME/vibeasr}"
REPO="https://github.com/microsoft/VibeASR.cpp.git"
HF="https://huggingface.co/microsoft/VibeVoice-ASR-BitNet/resolve/main"
VAE="vibeasr-vae-encoder-i8_s.gguf"
LM="vibeasr-lm-i2_s-embed-q6_k.gguf"

for tool in git cmake curl; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Errore: '$tool' non trovato nel PATH."; exit 1; }
done

echo "== 1/3 Clonazione VibeASR.cpp in $BASE =="
if [ -d "$BASE/.git" ]; then
  echo "  repository già presente, salto il clone"
else
  git clone --recursive "$REPO" "$BASE"
fi

echo "== 2/3 Build asr_infer =="
cmake -S "$BASE" -B "$BASE/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BASE/build" --target asr_infer -j"$(nproc)"

echo "== 3/3 Download modelli GGUF (~1.7 GB) =="
mkdir -p "$BASE/models"
for f in "$VAE" "$LM"; do
  if [ -s "$BASE/models/$f" ]; then
    echo "  $f già presente, salto"
  else
    echo "  scarico $f ..."
    curl -L --fail -C - -o "$BASE/models/$f" "$HF/$f"
  fi
done

cat <<EOF

Fatto. Modelli e binario pronti. Aggiungi al tuo ambiente:

export VIBE_VOICE_MODE=vibeasr
export VIBEASR_BIN="$BASE/build/bin/asr_infer"
export VIBEASR_VAE_MODEL="$BASE/models/$VAE"
export VIBEASR_LM_MODEL="$BASE/models/$LM"
export VIBEASR_THREADS=\$(nproc)
EOF
