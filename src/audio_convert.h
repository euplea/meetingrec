#pragma once

#include <string>
#include <vector>

// Converte un file audio in un altro formato.
// - Input nativi (senza ffmpeg): WAV, MP3, FLAC.
// - Altri formati (ogg/m4a/aac/wma/...) e formati di OUTPUT non-WAV richiedono
//   ffmpeg nel PATH (usato come subprocess).
// - targetRate <= 0 o targetChannels <= 0: mantiene i valori del sorgente.
bool convertAudio(const std::string& input, const std::string& output, int targetRate,
                  int targetChannels, std::string& error);

// Durata in secondi di un file WAV (o -1 se non leggibile).
double wavDurationSeconds(const std::string& wavPath);

// Divide un WAV in chunk da `chunkSeconds` (con `overlapSeconds` di
// sovrapposizione), ognuno in un WAV 16 kHz mono. Ritorna i percorsi creati.
bool splitWavIntoChunks(const std::string& wavPath, double chunkSeconds,
                        double overlapSeconds, std::vector<std::string>& chunkPaths,
                        std::string& error);
