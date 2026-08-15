@echo off
rem Scarica e compila VibeASR.cpp (VibeVoice-ASR su CPU) e i modelli GGUF.
rem Tutto viene creato in una cartella "vibeasr" RELATIVA a questo script
rem (accanto a scripts/), cosi' il setup e' portabile: sposta la cartella
rem del progetto e i percorsi restano validi.
rem Richiede: git, cmake, MinGW-w64 (g++/mingw32-make) e curl (incluso in Windows 10).
setlocal
rem BASE = <cartella progetto>\vibeasr  (derivata dalla posizione di questo script)
for %%i in ("%~dp0..") do set "BASE=%%~fi\vibeasr"
set "HF=https://huggingface.co/microsoft/VibeVoice-ASR-BitNet/resolve/main"
set "VAE=vibeasr-vae-encoder-i8_s.gguf"
set "LM=vibeasr-lm-i2_s-embed-q6_k.gguf"

echo == 1/3 Clonazione VibeASR.cpp in "%BASE%" ==
if not exist "%BASE%\.git" (
    git clone --recursive https://github.com/microsoft/VibeASR.cpp.git "%BASE%"
) else (
    echo   repository gia presente, salto il clone
)

echo == 2/3 Build asr_infer ==
cmake -S "%BASE%" -B "%BASE%\build" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build "%BASE%\build" --target asr_infer -j

echo == 3/3 Download modelli GGUF (~1.7 GB) ==
if not exist "%BASE%\models" mkdir "%BASE%\models"
if not exist "%BASE%\models\%VAE%" (
    echo   scarico %VAE% ...
    curl -L -o "%BASE%\models\%VAE%" "%HF%/%VAE%"
) else (
    echo   %VAE% gia presente
)
if not exist "%BASE%\models\%LM%" (
    echo   scarico %LM% ...
    curl -L -o "%BASE%\models\%LM%" "%HF%/%LM%"
) else (
    echo   %LM% gia presente
)

echo.
echo Fatto. Imposta le variabili d'ambiente (percorsi RELATIVI al progetto):
echo   set VIBE_VOICE_MODE=vibeasr
echo   set VIBEASR_BIN=%BASE%\build\bin\asr_infer.exe
echo   set VIBEASR_VAE_MODEL=%BASE%\models\%VAE%
echo   set VIBEASR_LM_MODEL=%BASE%\models\%LM%
echo   set VIBEASR_THREADS=4
endlocal
