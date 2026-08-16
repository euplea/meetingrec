#pragma once

#include <string>

// Converte un file audio in un altro formato.
// - Input nativi (senza ffmpeg): WAV, MP3, FLAC.
// - Altri formati (ogg/m4a/aac/wma/...) e formati di OUTPUT non-WAV richiedono
//   ffmpeg nel PATH (usato come subprocess).
// - targetRate <= 0 o targetChannels <= 0: mantiene i valori del sorgente.
bool convertAudio(const std::string& input, const std::string& output, int targetRate,
                  int targetChannels, std::string& error);
