#pragma once

#include <string>

struct TranscribeOptions {
    std::string inputPath;
    std::string apiUrl;                 // e.g. https://api.openai.com/v1/audio/transcriptions
    std::string apiKey;
    std::string model = "whisper-1";
    std::string mode = "openai";        // "openai" | "raw" | "vibeasr"
    std::string language;               // optional, e.g. "it"
    std::string responseKey = "text";   // JSON field holding the transcript

    // Backend locale VibeASR.cpp (microsoft/VibeASR.cpp) — usati con mode == "vibeasr".
    std::string vibeasrBin;             // percorso di asr_infer(.exe), default "asr_infer"
    std::string vibeasrVaeModel;        // *.gguf VAE encoder
    std::string vibeasrLmModel;         // *.gguf LM decoder
    int vibeasrThreads = 4;
    std::string vibeasrContext;         // hotwords/contesto opzionale
    std::string vibeasrFormat = "text"; // "text" (1.5B BitNet) | "json" (7B, con relatori/tempi)
};

// Sends the audio to a transcription endpoint (Whisper-compatible or a custom
// "vibe voice" style REST API) and returns the recognized text.
bool transcribe(const TranscribeOptions& opts, std::string& textOut, std::string& error);
