#pragma once

#include <string>

// Configurazione salvata in meetingrec.json (JSON).
// Priorità: flag CLI > variabili d'ambiente > file di configurazione > default.
struct Config {
    std::string mode = "vibeasr";               // vibeasr | openai | raw
    std::string apiUrl = "https://api.openai.com/v1/audio/transcriptions";
    std::string apiKey;
    std::string apiModel = "whisper-1";
    std::string language;
    std::string responseKey = "text";

    std::string vibeasrBin;
    std::string vibeasrVae;
    std::string vibeasrLm;
    int vibeasrThreads = 4;
    std::string vibeasrContext;
    std::string vibeasrFormat = "json";         // json (relatori+tempi) | text
    int vibeasrCtx = 8192;                       // contesto LM (KV cache): riduci se RAM scarsa

    std::string outputDir = "meeting";
    std::string title = "Minuta riunione";

    std::string path = "meetingrec.json";
    bool loaded = false;

    bool load();    // legge da `path` (se esiste)
    bool save() const;  // scrive su `path`
};
