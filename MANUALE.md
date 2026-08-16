# 📖 MANUALE DI ISTRUZIONI — meetingrec

**meetingrec** registra l'audio riprodotto dal sistema (la voce di Microsoft Teams,
Zoom, Google Meet, ecc.), lo trascrive e genera automaticamente una **minuta di
riunione** in Markdown. Funziona su **Windows 10**, **Linux** e **macOS**.

---

## 1. Requisiti

| Piattaforma | Compilatore | Dipendenze |
|---|---|---|
| Windows 10 | Visual Studio 2019+ oppure MinGW-w64 | nessuna (WASAPI + WinHTTP nativi) |
| Linux | g++ 9+ | PortAudio, libcurl, cmake |
| macOS | clang | PortAudio, libcurl, cmake |

**Hardware consigliato** (per la trascrizione locale con VibeVoice-ASR): 8 GB di RAM
(minimo 4 GB), ~2 GB di spazio disco per i modelli.

---

## 1bis. Avvio rapido plug and play (consigliato)

Scarica l'eseguibile dalla [GitHub Release](https://github.com/euplea/meetingrec/releases)
e fai **doppio clic**:

1. si apre Windows Terminal con il menu
2. se mancano `asr_infer` o i modelli GGUF, il tool li **scarica in automatico**
   (binario precompilato dalla release + modelli ~1.7 GB da HuggingFace)
3. scegli **4) Pipeline completa** e la minuta è pronta

Da riga di comando: `meetingrec setup` esegue lo stesso download automatico.
La versione usa lo schema **AAAA.MM.nn** (es. `2026.08.1`).

## 2. Compilazione

### 2.1 Windows — Visual Studio + vcpkg

```bat
vcpkg install curl:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

Eseguibile: `build\Release\meetingrec.exe` (con icona e manifest incorporati).

### 2.2 Windows — MinGW-w64 (MSYS2)

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 2.3 Linux

```bash
sudo apt-get install -y build-essential portaudio19-dev libasound2-dev libcurl4-openssl-dev pkg-config cmake
bash scripts/build_linux.sh        # oppure: make  /  cmake -B build && cmake --build build
```

### 2.4 Cross-compilazione Windows da Linux

```bash
sudo apt-get install -y g++-mingw-w64-x86-64
bash scripts/build_windows.sh      # produce build-windows/meetingrec.exe
```

---

## 3. Avvio rapido

```bash
# 1) guarda i dispositivi audio
meetingrec list

# 2) registra l'audio di sistema (su Windows, senza --device, usa il loopback WASAPI)
meetingrec record --output riunione.wav

# 3) trascrivi (locale con VibeVoice, vedi sezione 5)
meetingrec transcribe --input riunione.wav --mode vibeasr --output transcript.txt

# 4) genera la minuta
meetingrec minutes --transcript transcript.txt --title "Sync settimanale" \
    --attendees "Anna, Mario, Luca" --output minuta.md
```

Oppure **tutto in un colpo solo**:

```bash
meetingrec all --duration 3600 --output-dir riunione --title "Sync settimanale"
```

---

## 4. Comandi

### 4.1 `list`

Elenca i dispositivi audio:

- **Windows**: le voci `[LOOPBACK]` sono le uscite audio (catturano ciò che Teams/Zoom
  riproducono); le altre sono i microfoni.
- **Linux**: scegli la voce con `monitor` nel nome (es. `Monitor of Built-in Audio`).

### 4.2 `record`

| Opzione | Default | Descrizione |
|---|---|---|
| `--device N` | -1 (default) | Indice dispositivo. Windows senza valore → loopback uscita di sistema |
| `--output file.wav` | `meeting.wav` | File di destinazione |
| `--duration SEC` | 0 (fino a INVIO) | Durata massima in secondi |
| `--rate HZ` | 16000 | Frequenza (Linux); su Windows viene usata quella nativa |
| `--channels N` | 1 | Canali |

Durante la registrazione: **INVIO** o **Ctrl+C** per fermare.

### 4.3 `transcribe`

| Opzione | Default | Descrizione |
|---|---|---|
| `--input file.wav` | — | File audio (richiesto) |
| `--output file.txt` | `transcript.txt` | File di testo |
| `--mode` | `vibeasr` | **default `vibeasr`** (locale) / `openai` (multipart) / `raw` (body grezzo) |
| `--api-url` | OpenAI | Endpoint HTTP |
| `--api-key` | env | Chiave API |
| `--model` | `whisper-1` | Modello HTTP |
| `--language` | — | Lingua (es. `it`) |
| `--vibeasr-bin` | `asr_infer` | Binario VibeASR.cpp |
| `--vibeasr-vae` / `--vibeasr-lm` | — | Modelli GGUF |
| `--vibeasr-threads` | 4 | Thread CPU |
| `--vibeasr-context` | — | Hotwords |
| `--vibeasr-format` | `json` | **default `json`** (relatori+tempi) / `text` (1.5B) |

### 4.4 `minutes`

| Opzione | Default | Descrizione |
|---|---|---|
| `--transcript file.txt` | — | Trascrizione (richiesto) |
| `--output file.md` | `minuta.md` | Minuta (Markdown `.md` o documento `.odt`) |
| `--format` | auto | `md` o `odt` (default: dall'estensione di `--output`) |
| `--title T` | `Minuta riunione` | Titolo |
| `--attendees A,B` | — | Partecipanti |
| `--date YYYY-MM-DD` | oggi | Data |

La minuta contiene: **Punti discussi · Decisioni · Azioni/To-do · Rischi/Blocker ·
Trascrizione integrale** (e **Trascrizione per relatori** se la trascrizione è
strutturata con orari e relatori).

### 4.4bis `convert`

Converte file audio (WAV/MP3/FLAC nativi; altri formati richiedono `ffmpeg`):

```bash
meetingrec convert --input riunione.mp3 --output riunione.wav --rate 16000 --channels 1
```

Con `transcribe --mode vibeasr` gli input diversi da WAV vengono **convertiti
automaticamente** in WAV 16 kHz mono prima della trascrizione.

### 4.5 `all`

Esegue registrazione → trascrizione → minuta. Accetta le opzioni di `record`,
`transcribe` e `minutes`, più `--output-dir` (default `meeting/`).

---

## 5. Trascrizione locale con VibeVoice-ASR (consigliata)

[VibeVoice](https://github.com/microsoft/VibeVoice) è la famiglia di modelli vocali
open-source Microsoft; [VibeASR.cpp](https://github.com/microsoft/VibeASR.cpp) è il
runtime C++/CPU del suo modello speech-to-text **VibeVoice-ASR-BitNet**.

### Setup automatico

**Alternativa rapida — scarica i modelli GGUF direttamente con l'eseguibile**
(dalla [GitHub Release](https://github.com/euplea/meetingrec/releases)):

```bash
meetingrec download-models                 # scarica VAE + LM (~1.7 GB) in vibeasr/models
meetingrec download-models --vae-only      # solo VAE
meetingrec download-models --lm-only       # solo LM
meetingrec download-models --dir miodir    # cartella personalizzata
```

Il comando mostra il progresso e verifica la dimensione di ogni file.

Oppure, per clonare + compilare anche `asr_infer`:

```bash
bash scripts/setup_vibeasr.sh        # Linux/macOS
scripts\setup_vibeasr.bat            # Windows (git + cmake + MinGW)
```

Lo script clona VibeASR.cpp, compila `asr_infer` e scarica i modelli GGUF (~1.7 GB):

- `vibeasr-vae-encoder-i8_s.gguf` (703 MB)
- `vibeasr-lm-i2_s-embed-q6_k.gguf` (993 MB)

### Configurazione

```bash
export VIBE_VOICE_MODE=vibeasr
export VIBEASR_BIN="$HOME/vibeasr/build/bin/asr_infer"        # .exe su Windows
export VIBEASR_VAE_MODEL="$HOME/vibeasr/models/vibeasr-vae-encoder-i8_s.gguf"
export VIBEASR_LM_MODEL="$HOME/vibeasr/models/vibeasr-lm-i2_s-embed-q6_k.gguf"
export VIBEASR_THREADS=4
```

Su Windows: `set VIBE_VOICE_MODE=vibeasr` ecc.

### Hotwords

Migliorano l'accuratezza su nomi, acronimi e termini tecnici:

```bash
export VIBEASR_CONTEXT="Mario Rossi, progetto Alpha, QNAP, GDPR"
```

---

## 5bis. Memoria RAM, durata chunk e qualità del WAV

### Attenzione: ridurre la qualità del WAV NON riduce la RAM

`asr_infer` (VibeASR.cpp) **ricampiona sempre l'audio in ingresso a 24 kHz
float32** internamente. Quindi registrare o convertire a 8/16 kHz non cambia
nulla: il VAE del modello processa comunque gli stessi campioni a 24 kHz.

La **RAM usata dal VAE dipende solo dalla DURATA dell'audio** che gli viene
passato, con questa formula approssimativa:

```
RAM VAE ≈ durata(secondi) × ~234 MB/s  (+ 512 MB fissi)
```

Esempi (solo l'arena del VAE, senza modelli e cache):

| Durata | RAM VAE |
|---|---|
| 10 s | ~2.8 GB |
| 20 s | ~5.2 GB |
| 30 s | ~7.5 GB |
| 60 s | ~14.5 GB |
| 4 min | ~59 GB ❌ (crash anche su 16 GB) |

Per questo motivo `meetingrec` **spezzetta automaticamente l'audio in chunk**
prima di passarlo al modello (vedi sotto).

### Durata dei chunk (`--vibeasr-chunk`)

Default: **20 secondi** (sicuro per PC con 16 GB). Ogni chunk viene trascritto
separatamente e i risultati vengono uniti; tra un chunk e l'altro c'è una
sovrapposizione di 2 s per non tagliare le parole.

```bash
meetingrec transcribe --input riunione.wav --vibeasr-chunk 20   # default
meetingrec transcribe --input riunione.wav --vibeasr-chunk 15   # PC con poca RAM
meetingrec transcribe --input riunione.wav --vibeasr-chunk 40   # RAM abbondante (32 GB)
```

Oppure in `meetingrec.json`: `"vibeasr_chunk_sec": 20`
Oppure dal menu: **Configurazione → 14) Durata chunk**.

### Contesto LM (`--vibeasr-ctx`)

È la dimensione della KV-cache del modello linguistico (default **8192** token).
Ridurlo abbassa ulteriormente la RAM:

```bash
meetingrec transcribe --input riunione.wav --vibeasr-ctx 4096   # ~600 MB di cache
```

> Regola pratica: per un meeting da 4 minuti il default (chunk 20 s + ctx 8192)
> occupa ~8.5 GB totali e gira bene su 16 GB. Se il PC è da 8 GB, usa
> `--vibeasr-chunk 15 --vibeasr-ctx 4096`.

### Dimensioni consigliate per RAM del PC

| RAM PC | chunk | ctx |
|---|---|---|
| 8 GB | 15 s | 4096 |
| 16 GB | 20 s | 8192 |
| 32 GB+ | 30–40 s | 8192–16384 |

---

## 6. Endpoint HTTP alternativi

Modalità `openai` (multipart, contratto Whisper) e `raw` (body audio):

```bash
export VIBE_VOICE_URL="https://api.openai.com/v1/audio/transcriptions"
export VIBE_VOICE_API_KEY="sk-..."
meetingrec transcribe --input riunione.wav --language it
```

---

## 7. Variabili d'ambiente (riepilogo)

| Variabile | Flag equivalente | Scopo |
|---|---|---|
| `VIBE_VOICE_MODE` | `--mode` | `vibeasr` / `openai` / `raw` |
| `VIBE_VOICE_URL` | `--api-url` | endpoint HTTP |
| `VIBE_VOICE_API_KEY` | `--api-key` | chiave API |
| `VIBE_VOICE_MODEL` | `--model` | modello HTTP |
| `VIBE_VOICE_LANGUAGE` | `--language` | lingua |
| `VIBE_VOICE_RESPONSE_KEY` | `--response-key` | chiave JSON del testo |
| `VIBEASR_BIN` | `--vibeasr-bin` | binario asr_infer |
| `VIBEASR_VAE_MODEL` | `--vibeasr-vae` | GGUF VAE |
| `VIBEASR_LM_MODEL` | `--vibeasr-lm` | GGUF LM |
| `VIBEASR_THREADS` | `--vibeasr-threads` | thread |
| `VIBEASR_CONTEXT` | `--vibeasr-context` | hotwords |
| `VIBEASR_FORMAT` | `--vibeasr-format` | `json` / `text` |
| `VIBEASR_CTX` | `--vibeasr-ctx` | contesto LM (KV-cache, default 8192) |
| `VIBEASR_CHUNK` | `--vibeasr-chunk` | durata chunk in secondi (default 20) |

---

## 7bis. File di configurazione JSON (`meetingrec.json`)

Tutti i settaggi possono essere salvati in un file JSON **`meetingrec.json`** nella
cartella corrente (es. accanto all'eseguibile). Priorità di risoluzione:
**flag CLI > variabili d'ambiente > meetingrec.json > default**.

Esempio:

```json
{
  "mode": "vibeasr",
  "vibeasr_bin": "C:\vibeasr\build\bin\asr_infer.exe",
  "vibeasr_vae": "C:\vibeasr\models\vibeasr-vae-encoder-i8_s.gguf",
  "vibeasr_lm": "C:\vibeasr\models\vibeasr-lm-i2_s-embed-q6_k.gguf",
  "vibeasr_threads": 4,
  "vibeasr_format": "json",
  "vibeasr_context": "Mario Rossi, progetto Alpha",
  "api_url": "https://api.openai.com/v1/audio/transcriptions",
  "api_key": "sk-...",
  "language": "it",
  "output_dir": "riunione",
  "title": "Minuta riunione"
}
```

> Il modo più semplice per crearle/modificarle: **menu interattivo → 5) Configurazione**
> (doppio clic sull'eseguibile su Windows), poi `13) Salva`.

## 8. Menu interattivo (doppio clic su Windows)

Doppio clic su `meetingrec.exe` (in Windows Terminal) apre un menu:

```
MENU PRINCIPALE
  Passi singoli:
    1) Registra l'audio (Fase 1)
    2) Trascrivi l'audio (Fase 2)
    3) Genera la minuta (Fase 3)
  Pipeline completa:
    4) Registra + Trascrivi + Minuta (1+2+3)
  Strumenti:
    5) Configurazione (sotto-menu)
    6) Elenca i dispositivi audio
    7) Scarica i modelli VibeVoice (GGUF)
    8) Esci
```

Il sotto-menu **Configurazione** (5) permette di modificare tutti i valori e salvarli
su `meetingrec.json` (INVIO su un campo = mantieni il valore corrente).

## 8. Struttura del progetto

```
meeting-recorder/
├── src/
│   ├── main.cpp          # CLI + orchestrazione + gestione eccezioni
│   ├── recorder.cpp      # cattura audio (WASAPI su Windows, PortAudio altrove)
│   ├── transcriber.cpp   # trascrizione (VibeASR locale / WinHTTP / libcurl)
│   ├── minutes.cpp       # generazione minuta Markdown
│   ├── wav_writer.cpp    # scrittura WAV
│   └── tui.cpp           # interfaccia a colori (ANSI/VT)
├── resources/            # icona (app.ico), manifest, app.rc
├── scripts/
│   ├── build_linux.sh    # build Linux
│   ├── build_windows.sh  # cross-compile Windows (MinGW)
│   ├── setup_vibeasr.sh  # setup VibeASR.cpp + modelli (Linux)
│   ├── setup_vibeasr.bat # setup VibeASR.cpp + modelli (Windows)
│   └── make_icon.py      # rigenera l'icona
├── CMakeLists.txt / Makefile
└── MANUALE.md / README.md
```

---

## 9. Risoluzione dei problemi

| Problema | Soluzione |
|---|---|
| `list` non mostra il loopback su Windows | Aggiorna il driver audio; verifica che l'uscita di default sia attiva |
| Nessun audio registrato da Teams | Su Windows avvia la registrazione **dopo** aver aperto Teams; verifica il volume dell'app |
| `Modello GGUF non trovato` | Esegui `scripts/setup_vibeasr.sh` / `.bat` |
| `asr_infer non trovato` | Compila VibeASR.cpp oppure imposta `VIBEASR_BIN` con il percorso completo |
| Trascrizione lenta | Aumenta `VIBEASR_THREADS` (prova 6-8) |
| **Crash `GGML_ASSERT(ctx->mem_buffer != NULL)`** | RAM insufficiente: riduci la durata chunk (`--vibeasr-chunk 15`) o il contesto (`--vibeasr-ctx 4096`); chiudi altri programmi |
| La trascrizione è a pezzi senza continuità | È normale: l'audio lungo viene trascritto a chunk da 20 s (vedi sezione 5bis). Aumenta `--vibeasr-chunk` se hai RAM |
| Su Linux non sento l'audio di sistema | Seleziona il dispositivo `monitor` con `--device N` |
| La minuta non separa i relatori | Il modello CPU 1.5B produce testo puro; usa il 7B con `VIBEASR_FORMAT=json` |
| Testo con caratteri strani su console Windows | Usa Windows Terminal (consigliato) oppure `chcp 65001` |

---

## 10. Privacy

- Con `--mode vibeasr` l'audio **non esce mai dalla macchina**: registrazione e
  trascrizione sono interamente locali.
- Con `--mode openai/raw` l'audio viene inviato all'endpoint configurato: usalo solo
  con riunioni per cui è consentito e con endpoint di tua fiducia.
