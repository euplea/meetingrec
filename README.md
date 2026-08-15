# meetingrec

Strumento C++ per registrare **l'audio riprodotto dal sistema** (la voce che esce da
Microsoft Teams, Zoom, Google Meet, ecc.), trascriverlo e generare automaticamente una
**minuta di riunione** in Markdown.

Funziona su **Windows 10**, **Linux** e **macOS**.

> 📥 **Eseguibili pronti:** scaricali dalla [GitHub Release](https://github.com/euplea/meetingrec/releases)
> (`meetingrec-windows-x64.exe`, `meetingrec-linux-x64`) — includono il comando
> `download-models` per scaricare i modelli GGUF di VibeVoice.
>
> 📖 **Guida completa:** vedi [MANUALE.md](MANUALE.md) (compilazione, comandi,
> configurazione, risoluzione problemi).

## Backend di trascrizione

- **VibeASR.cpp** (`microsoft/VibeASR.cpp`) → **VibeVoice-ASR** locale su CPU, senza GPU.
  È il backend consigliato: modello quantizzato (~1.6 GB), multilingua (50+ lingue,
  italiano incluso), con supporto a *hotwords* e (col modello 7B) diarizzazione
  "chi parla / quando".
- Endpoint HTTP Whisper-compatibili (OpenAI o un servizio "vibe voice" custom).

## Funzionalità

- `list`       → elenca i dispositivi audio (su Windows mostra le sorgenti `LOOPBACK`)
- `record`     → registra in un file WAV (16 bit PCM)
- `transcribe` → trascrive (VibeASR locale o API HTTP) e salva il testo
- `minutes`    → trasforma la trascrizione in minuta Markdown (decisioni, azioni, rischi)
- `all`        → pipeline completa: registra → trascrive → minuta

## Build su Windows 10

Cross-compile da Linux: `bash scripts/build_windows.sh` (produce
`build-windows/meetingrec.exe` con icona e manifest).

### Opzione A — Visual Studio + vcpkg

```bat
vcpkg install curl:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

Eseguibile: `build\Release\meetingrec.exe`.

### Opzione B — MinGW-w64 + MSYS2

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-curl
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

> Su Windows la cattura audio usa **WASAPI** nativo (nessuna dipendenza da PortAudio).

## Build su Linux

```bash
sudo apt-get install -y build-essential portaudio19-dev libasound2-dev libcurl4-openssl-dev pkg-config cmake
make            # oppure: cmake -B build && cmake --build build
```

## 1. Impostare VibeASR.cpp (trascrizione locale, consigliata)

[VibeASR.cpp](https://github.com/microsoft/VibeASR.cpp) è il runtime C++/CPU di
**VibeVoice-ASR** ([microsoft/VibeVoice](https://github.com/microsoft/VibeVoice)).

**Setup automatico** (clona, compila `asr_infer` e scarica i modelli GGUF ~1.7 GB):

```bash
# Linux / macOS
bash scripts/setup_vibeasr.sh

# Windows (richiede git + cmake + MinGW-w64)
scripts\setup_vibeasr.bat
```

**Setup manuale** (in alternativa):

```bash
git clone --recursive https://github.com/microsoft/VibeASR.cpp.git
cd VibeASR.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asr_infer -j

# modelli quantizzati (~1.7 GB)
pip install huggingface_hub
huggingface-cli download microsoft/VibeVoice-ASR-BitNet --local-dir models/vibeasr
```

Su Windows usa MinGW (MSVC non è supportato da VibeASR.cpp):

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asr_infer -j
```

Poi configura `meetingrec` (il binario è `build/bin/asr_infer[.exe]`, i modelli GGUF
sono `vibeasr-vae-encoder-i8_s.gguf` e `vibeasr-lm-i2_s-embed-q6_k.gguf`):

```bash
export VIBE_VOICE_MODE=vibeasr
export VIBEASR_BIN=/percorso/VibeASR.cpp/build/bin/asr_infer      # .exe su Windows
export VIBEASR_VAE_MODEL=/percorso/VibeASR.cpp/models/vibeasr/vibeasr-vae-encoder-i8_s.gguf
export VIBEASR_LM_MODEL=/percorso/VibeASR.cpp/models/vibeasr/vibeasr-lm-i2_s-embed-q6_k.gguf
export VIBEASR_THREADS=4
```

Hotwords opzionali (nomi, acronimi, termini tecnici → più accuratezza):

```bash
export VIBEASR_CONTEXT="Mario Rossi, progetto Alpha, QNAP"
```

> Il modello CPU (1.5B BitNet) produce testo puro. Se usi il modello **7B** (GPU/vLLM/HF)
> con output strutturato, imposta `VIBEASR_FORMAT=json`: la minuta includerà
> **relatori e orari** automaticamente.

## 2. Registrare

```bash
meetingrec list      # individua il dispositivo

# Windows: senza --device registra l'audio di sistema via loopback WASAPI (Teams/Zoom)
meetingrec record --output riunione.wav

# Linux: scegli la voce con "monitor"
meetingrec record --device 3 --output riunione.wav
```

## 3. Trascrivere e generare la minuta

```bash
meetingrec all --duration 3600 --output-dir riunione \
    --title "Sync settimanale" --attendees "Anna, Mario, Luca"
```

Output in `riunione/`: `audio.wav`, `transcript.txt`, `minuta.md`.

## Configurazione trascrizione

| Variabile                 | Flag              | Default                                                   |
|---------------------------|-------------------|-----------------------------------------------------------|
| `VIBE_VOICE_MODE`         | `--mode`          | `openai`                                                  |
| `VIBE_VOICE_URL`          | `--api-url`       | `https://api.openai.com/v1/audio/transcriptions`          |
| `VIBE_VOICE_API_KEY`      | `--api-key`       | _(vuoto)_                                                 |
| `VIBE_VOICE_MODEL`        | `--model`         | `whisper-1`                                               |
| `VIBE_VOICE_LANGUAGE`     | `--language`      | _(vuoto)_                                                 |
| `VIBE_VOICE_RESPONSE_KEY` | `--response-key`  | `text`                                                    |
| `VIBEASR_BIN`             | `--vibeasr-bin`   | `asr_infer`                                               |
| `VIBEASR_VAE_MODEL`       | `--vibeasr-vae`   | _(richiesto per vibeasr)_                                 |
| `VIBEASR_LM_MODEL`        | `--vibeasr-lm`    | _(richiesto per vibeasr)_                                 |
| `VIBEASR_THREADS`         | `--vibeasr-threads`| `4`                                                      |
| `VIBEASR_CONTEXT`         | `--vibeasr-context`| _(vuoto)_                                                |
| `VIBEASR_FORMAT`          | `--vibeasr-format`| `text`                                                    |

Modalità `--mode`:

- **`vibeasr`** → VibeASR.cpp locale (subprocess `asr_infer`).
- **`openai`** → POST multipart/form-data (contratto Whisper API).
- **`raw`** → POST del file come corpo raw (`Content-Type: audio/wav`).

## Minuta

```bash
meetingrec minutes --transcript transcript.txt --output minuta.md \
    --title "Sync settimanale" --attendees "Anna, Mario, Luca"
```

Sezione della minuta: **Punti discussi / Decisioni / Azioni (To-do) / Rischi /
Trascrizione integrale**. Se la trascrizione è strutturata (relatori + tempi, es.
formato `json` di VibeVoice-ASR), viene aggiunta anche la sezione
**Trascrizione per relatori** con orari.

## Note tecniche

- **Windows**: cattura WASAPI loopback; conversione automatica in 16 bit mono.
- **Linux/macOS**: cattura PortAudio (monitor PulseAudio/PipeWire su Linux).
- Con `--mode vibeasr`, all'avvio viene verificata la presenza dei due `.gguf` e del
  binario `asr_infer`: se mancano, viene mostrato un errore con le istruzioni di download.
- Il WAV standard supera il limite del chunk `data` (4 GB) oltre ~3 ore di audio.
