#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio_convert.h"
#include "config.h"
#include "discover.h"
#include "downloader.h"
#include "minutes.h"
#include "recorder.h"
#include "transcriber.h"
#include "tui.h"

namespace {

Config g_cfg;  // configurazione globale (meetingrec.json)

const char* kVersion = "2026.08.6";  // schema AAAA.MM.nn

int cmdConvert(const std::vector<std::string>& args);  // definita più avanti
int cmdSetup();  // definita più avanti

std::string getEnv(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

bool hasArg(const std::vector<std::string>& args, const std::string& name) {
    for (const auto& a : args)
        if (a == name) return true;
    return false;
}

std::string getArg(const std::vector<std::string>& args, const std::string& name,
                   const std::string& def = "") {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return def;
}

int toInt(const std::string& s, int def) {
    if (s.empty()) return def;
    try {
        return std::stoi(s);
    } catch (const std::exception&) {
        return def;
    }
}

double toDouble(const std::string& s, double def) {
    if (s.empty()) return def;
    try {
        return std::stod(s);
    } catch (const std::exception&) {
        return def;
    }
}

// Priorità: flag CLI > variabile d'ambiente > configurazione (JSON) > default.
std::string pick(const std::vector<std::string>& args, const std::string& flag,
                 const std::string& env, const std::string& cfgVal,
                 const std::string& def) {
    std::string v = getArg(args, flag);
    if (!v.empty()) return v;
    v = getEnv(env.c_str());
    if (!v.empty()) return v;
    if (!cfgVal.empty()) return cfgVal;
    return def;
}

int pickInt(const std::vector<std::string>& args, const std::string& flag,
            const std::string& env, int cfgVal, int def) {
    std::string v = getArg(args, flag);
    if (!v.empty()) return toInt(v, def);
    v = getEnv(env.c_str());
    if (!v.empty()) return toInt(v, def);
    return cfgVal;
}

std::string fmtSize(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

void usage() {
    std::cout
        << "\n"
        << "Uso:\n"
        << "  meetingrec --version   (stampa la versione)\n"
        << "  meetingrec list\n"
        << "  meetingrec record  [--device N] [--output file.wav] [--duration SEC] [--rate HZ] [--channels N]\n"
        << "  meetingrec transcribe --input file.wav [--output file.txt] [--api-url URL]\n"
        << "                      [--api-key KEY] [--model NAME] [--mode vibeasr|openai|raw]\n"
        << "                                  (default: vibeasr - VibeVoice locale)\n"
        << "                      [--language LANG] [--response-key KEY]\n"
        << "                      [--vibeasr-bin PATH] [--vibeasr-vae F.gguf] [--vibeasr-lm F.gguf]\n"
        << "                      [--vibeasr-threads N] [--vibeasr-context TEXT] [--vibeasr-format text|json]\n"
        << "                      [--vibeasr-ctx N]  (contesto LM, default 8192; riduci a 4096 se RAM scarsa)\n"
        << "                      [--vibeasr-chunk SEC] (durata chunk, default 30; la RAM VAE scala con l'audio)\n"
        << "  meetingrec minutes  --transcript file.txt [--output minuta.md|minuta.odt]\n"
        << "                      [--title T] [--attendees A,B] [--date YYYY-MM-DD] [--format md|odt]\n"
        << "  meetingrec download-models [--dir PATH] [--vae-only | --lm-only]\n"
        << "  meetingrec setup   (scarica automaticamente asr_infer + modelli GGUF)\n"
        << "  meetingrec convert --input in.wav|mp3|flac --output out.wav\n"
        << "                      [--rate HZ] [--channels N]\n"
        << "  meetingrec all      --device N [--duration SEC] [--output-dir DIR]\n"
        << "                      [--title T] [--attendees A,B] [opzioni trascrizione...]\n\n"
        << "Variabili d'ambiente:\n"
        << "  VIBE_VOICE_URL  VIBE_VOICE_API_KEY  VIBE_VOICE_MODEL  VIBE_VOICE_MODE\n"
        << "  VIBE_VOICE_LANGUAGE  VIBE_VOICE_RESPONSE_KEY\n"
        << "  VIBEASR_BIN  VIBEASR_VAE_MODEL  VIBEASR_LM_MODEL  VIBEASR_THREADS\n"
        << "  VIBEASR_CONTEXT  VIBEASR_FORMAT\n\n"
        << "File di configurazione: meetingrec.json (JSON) nella cartella corrente.\n"
        << "Priorità: flag CLI > variabili d'ambiente > meetingrec.json > default.\n\n"
        << "Nota: su Windows, senza --device viene registrato l'audio di sistema via\n"
        << "loopback WASAPI (la voce di Teams/Zoom). Con 'list' puoi scegliere un\n"
        << "microfono specifico. Su Linux seleziona il dispositivo con 'monitor'.\n"
        << "Vedi MANUALE.md per la guida completa.\n";
}

void fillVibeasrOptions(TranscribeOptions& o, const std::vector<std::string>& args) {
    o.vibeasrBin = pick(args, "--vibeasr-bin", "VIBEASR_BIN", g_cfg.vibeasrBin, "");
    o.vibeasrVaeModel = pick(args, "--vibeasr-vae", "VIBEASR_VAE_MODEL", g_cfg.vibeasrVae, "");
    o.vibeasrLmModel = pick(args, "--vibeasr-lm", "VIBEASR_LM_MODEL", g_cfg.vibeasrLm, "");
    o.vibeasrThreads =
        pickInt(args, "--vibeasr-threads", "VIBEASR_THREADS", g_cfg.vibeasrThreads, 4);
    o.vibeasrContext =
        pick(args, "--vibeasr-context", "VIBEASR_CONTEXT", g_cfg.vibeasrContext, "");
    o.vibeasrFormat =
        pick(args, "--vibeasr-format", "VIBEASR_FORMAT", g_cfg.vibeasrFormat, "json");
    o.vibeasrCtx =
        pickInt(args, "--vibeasr-ctx", "VIBEASR_CTX", g_cfg.vibeasrCtx, 8192);
    o.vibeasrChunkSec =
        pickInt(args, "--vibeasr-chunk", "VIBEASR_CHUNK", g_cfg.vibeasrChunkSec, 20);
}

void fillTranscribeCommon(TranscribeOptions& o, const std::vector<std::string>& args) {
    o.apiUrl = pick(args, "--api-url", "VIBE_VOICE_URL", g_cfg.apiUrl,
                    "https://api.openai.com/v1/audio/transcriptions");
    o.apiKey = pick(args, "--api-key", "VIBE_VOICE_API_KEY", g_cfg.apiKey, "");
    o.model = pick(args, "--model", "VIBE_VOICE_MODEL", g_cfg.apiModel, "whisper-1");
    o.mode = pick(args, "--mode", "VIBE_VOICE_MODE", g_cfg.mode, "vibeasr");
    o.language = pick(args, "--language", "VIBE_VOICE_LANGUAGE", g_cfg.language, "");
    o.responseKey =
        pick(args, "--response-key", "VIBE_VOICE_RESPONSE_KEY", g_cfg.responseKey, "text");
    fillVibeasrOptions(o, args);
}

int cmdList() {
    tui::section("Dispositivi audio disponibili");
    std::vector<DeviceInfo> devices;
    if (!listDevices(devices)) {
        tui::error("Impossibile elencare i dispositivi audio.");
        return 1;
    }
    tui::deviceList(devices);
    tui::info("Windows: 'LOOPBACK' = audio di sistema (Teams/Zoom).");
    tui::info("Linux: scegli il dispositivo con 'monitor'.");
    return 0;
}

int cmdRecord(const std::vector<std::string>& args) {
    RecordOptions o;
    o.deviceIndex = toInt(getArg(args, "--device"), -1);
    o.outputPath = getArg(args, "--output", g_cfg.outputDir + "/audio.wav");
    o.durationSeconds = toDouble(getArg(args, "--duration"), 0.0);
    o.sampleRate = toDouble(getArg(args, "--rate"), 16000.0);
    o.channels = toInt(getArg(args, "--channels"), 1);

    std::string err;
    if (!recordAudio(o, err)) {
        tui::error(err);
        return 1;
    }
    tui::ok("Registrazione salvata in " + o.outputPath);
    return 0;
}

int cmdTranscribe(const std::vector<std::string>& args) {
    TranscribeOptions o;
    o.inputPath = getArg(args, "--input");
    if (o.inputPath.empty()) {
        tui::error("--input richiesto.");
        return 1;
    }
    std::error_code fec;
    if (!std::filesystem::exists(o.inputPath, fec)) {
        tui::error("Audio non trovato: " + o.inputPath);
        tui::info("Registra prima con la Fase 1, oppure usa --input con un file esistente.");
        return 1;
    }
    fillTranscribeCommon(o, args);

    tui::section("Trascrizione in corso (backend: " + o.mode + ")...");
    std::string text, err;
    if (!transcribe(o, text, err)) {
        tui::error(err);
        return 1;
    }
    const std::string out = getArg(args, "--output", g_cfg.outputDir + "/transcript.txt");
    // Crea la cartella di destinazione se non esiste.
    {
        std::error_code ec;
        std::filesystem::path p(out);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    }
    try {
        std::ofstream f(out);
        if (!f) throw std::runtime_error("impossibile aprire " + out);
        f << text;
    } catch (const std::exception& e) {
        tui::error(std::string("Scrittura trascrizione fallita: ") + e.what());
        return 1;
    }
    tui::ok("Trascrizione salvata in " + out + " (" + std::to_string(text.size()) + " caratteri)");
    return 0;
}

int cmdMinutes(const std::vector<std::string>& args) {
    const std::string inPath = getArg(args, "--transcript");
    if (inPath.empty()) {
        tui::error("--transcript richiesto.");
        return 1;
    }
    std::error_code fec;
    if (!std::filesystem::exists(inPath, fec)) {
        tui::error("Trascrizione non trovata: " + inPath);
        tui::info("Esegui prima la Fase 2 (trascrizione) oppure la Fase 4 (pipeline completa).");
        return 1;
    }
    std::string content;
    try {
        std::ifstream f(inPath);
        if (!f) throw std::runtime_error("impossibile aprire " + inPath);
        std::stringstream ss;
        ss << f.rdbuf();
        content = ss.str();
    } catch (const std::exception& e) {
        tui::error(std::string("Lettura trascrizione fallita: ") + e.what());
        return 1;
    }

    MinutesOptions o;
    o.transcriptText = content;
    o.title = pick(args, "--title", "", g_cfg.title, "Minuta riunione");
    o.date = getArg(args, "--date");
    o.attendees = getArg(args, "--attendees");
    o.outputPath = getArg(args, "--output", g_cfg.outputDir + "/minuta.md");

    // Formato: --format md|odt, altrimenti auto dall'estensione del file.
    std::string fmt = getArg(args, "--format");
    if (fmt.empty()) {
        fmt = (o.outputPath.size() > 4 &&
               o.outputPath.compare(o.outputPath.size() - 4, 4, ".odt") == 0)
                  ? "odt"
                  : "md";
    }

    tui::section("Generazione minuta (" + fmt + ")...");
    std::string err;
    const bool ok = (fmt == "odt") ? writeMinutesOdt(o, err) : writeMinutes(o, err);
    if (!ok) {
        tui::error(err);
        return 1;
    }
    tui::ok("Minuta salvata in " + o.outputPath);
    return 0;
}

int cmdAll(const std::vector<std::string>& args) {
    const std::string dir = pick(args, "--output-dir", "", g_cfg.outputDir, "meeting");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        tui::error("Impossibile creare la directory " + dir);
        return 1;
    }

    // Formato minuta: --format md|odt (default md).
    std::string fmt = getArg(args, "--format");
    if (fmt.empty()) fmt = "md";

    const std::string wav = dir + "/audio.wav";
    const std::string txt = dir + "/transcript.txt";
    const std::string md = dir + "/minuta." + fmt;

    // 1) Registrazione
    RecordOptions ro;
    ro.deviceIndex = toInt(getArg(args, "--device"), -1);
    ro.outputPath = wav;
    ro.durationSeconds = toDouble(getArg(args, "--duration"), 0.0);
    ro.sampleRate = toDouble(getArg(args, "--rate"), 16000.0);
    ro.channels = toInt(getArg(args, "--channels"), 1);

    tui::header(" FASE 1/3 - Registrazione ");
    std::string err;
    if (!recordAudio(ro, err)) {
        tui::error(err);
        return 1;
    }
    tui::ok("Audio registrato: " + wav);

    // 2) Trascrizione
    TranscribeOptions to;
    to.inputPath = wav;
    fillTranscribeCommon(to, args);

    tui::header(" FASE 2/3 - Trascrizione (backend: " + to.mode + ") ");
    std::string text;
    if (!transcribe(to, text, err)) {
        tui::error(err);
        return 1;
    }
    try {
        std::ofstream tf(txt);
        if (!tf) throw std::runtime_error("impossibile aprire " + txt);
        tf << text;
    } catch (const std::exception& e) {
        tui::error(std::string("Scrittura trascrizione fallita: ") + e.what());
        return 1;
    }
    tui::ok("Trascrizione: " + txt);

    // 3) Minuta
    MinutesOptions mo;
    mo.transcriptText = text;
    mo.title = pick(args, "--title", "", g_cfg.title, "Minuta riunione");
    mo.date = getArg(args, "--date");
    mo.attendees = getArg(args, "--attendees");
    mo.outputPath = md;

    tui::header(" FASE 3/3 - Minuta (" + fmt + ") ");
    const bool ok = (fmt == "odt") ? writeMinutesOdt(mo, err) : writeMinutes(mo, err);
    if (!ok) {
        tui::error(err);
        return 1;
    }

    tui::ok("Pipeline completata!");
    tui::info("  audio:       " + wav);
    tui::info("  trascrizione: " + txt);
    tui::info("  minuta:      " + md);
    return 0;
}

int cmdDownloadModels(const std::vector<std::string>& args) {
    const std::string dir = getArg(args, "--dir", "vibeasr/models");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        tui::error("Impossibile creare la directory " + dir + " (" + ec.message() + ")");
        return 1;
    }

    struct ModelFile {
        const char* name;
        uint64_t size;
    };
    static const ModelFile files[] = {
        {"vibeasr-vae-encoder-i8_s.gguf", 703080064ULL},
        {"vibeasr-lm-i2_s-embed-q6_k.gguf", 992877600ULL},
    };
    const std::string base =
        "https://huggingface.co/microsoft/VibeVoice-ASR-BitNet/resolve/main/";
    const bool onlyVae = hasArg(args, "--vae-only");
    const bool onlyLm = hasArg(args, "--lm-only");

    tui::header(" Download modelli VibeVoice-ASR (GGUF) ");
    tui::info("Destinazione: " + dir);

    for (const auto& mf : files) {
        const std::string name(mf.name);
        if (onlyVae && name.find("vae") == std::string::npos) continue;
        if (onlyLm && name.find("lm") == std::string::npos) continue;

        const std::string dest = dir + "/" + name;
        std::error_code fec;
        const uintmax_t existing = std::filesystem::file_size(dest, fec);
        if (!fec && existing == mf.size) {
            tui::ok("Già presente e valido: " + dest);
            continue;
        }

        tui::section("Download " + name + " (" + fmtSize(mf.size) + ")");
        std::string err;
        const bool ok = downloadFile(
            base + name, dest, mf.size,
            [](uint64_t cur, uint64_t total) {
                std::string pct;
                if (total > 0) {
                    char b[16];
                    std::snprintf(b, sizeof(b), "%.1f%%", 100.0 * cur / total);
                    pct = b;
                }
                tui::progress("  " + fmtSize(cur) + " / " + fmtSize(total) + "  " + pct);
            },
            err);
        tui::clearLine();
        if (!ok) {
            tui::error(err);
            return 1;
        }
        tui::ok("Scaricato: " + dest);

        // Aggiorna la configurazione con i percorsi appena scaricati.
        bool cfgChanged = false;
        if (name.find("vae") != std::string::npos && g_cfg.vibeasrVae.empty()) {
            g_cfg.vibeasrVae = dest;
            cfgChanged = true;
        }
        if (name.find("lm") != std::string::npos && g_cfg.vibeasrLm.empty()) {
            g_cfg.vibeasrLm = dest;
            cfgChanged = true;
        }
        if (cfgChanged) g_cfg.save();
    }

    tui::ok("Modelli pronti in " + dir);
    if (!g_cfg.vibeasrVae.empty() || !g_cfg.vibeasrLm.empty()) {
        tui::info("Percorsi aggiornati in " + g_cfg.path);
    }
    tui::info("Configura: VIBEASR_VAE_MODEL=" + dir + "/vibeasr-vae-encoder-i8_s.gguf");
    tui::info("           VIBEASR_LM_MODEL=" + dir + "/vibeasr-lm-i2_s-embed-q6_k.gguf");
    return 0;
}

std::string maskKey(const std::string& k) {
    if (k.empty()) return "";
    if (k.size() <= 4) return "****";
    return k.substr(0, 4) + "****";
}

bool promptValue(const std::string& label, const std::string& current, std::string& out) {
    std::cout << label << " [" << current << "]: " << std::flush;
    std::string v;
    std::getline(std::cin, v);
    if (v.empty()) return false;  // INVIO = mantieni il valore corrente
    out = v;
    return true;
}

int settingsMenu() {
    for (;;) {
        tui::header(" CONFIGURAZIONE ");
        std::cout
            << "  1) Backend (vibeasr|openai|raw)  [" << g_cfg.mode << "]\n"
            << "  2) Binario asr_infer             [" << g_cfg.vibeasrBin << "]\n"
            << "  3) Modello GGUF VAE              [" << g_cfg.vibeasrVae << "]\n"
            << "  4) Modello GGUF LM               [" << g_cfg.vibeasrLm << "]\n"
            << "  5) Thread CPU                    [" << g_cfg.vibeasrThreads << "]\n"
            << "  6) Formato (json|text)           [" << g_cfg.vibeasrFormat << "]\n"
            << "  7) Hotwords (contesto)           [" << g_cfg.vibeasrContext << "]\n"
            << "  8) URL API (openai/raw)          [" << g_cfg.apiUrl << "]\n"
            << "  9) Chiave API                    [" << maskKey(g_cfg.apiKey) << "]\n"
            << "  10) Lingua                       [" << g_cfg.language << "]\n"
            << "  11) Cartella output              [" << g_cfg.outputDir << "]\n"
            << "  12) Titolo minuta                [" << g_cfg.title << "]\n"
            << "  13) Contesto LM (tokens)         [" << g_cfg.vibeasrCtx << "]\n"
            << "  14) Durata chunk (secondi)       [" << g_cfg.vibeasrChunkSec << "]\n"
            << "  15) Salva su " << g_cfg.path << "\n"
            << "  16) Torna al menu principale\n"
            << "Scelta: " << std::flush;

        std::string line, v;
        std::getline(std::cin, line);
        if (line == "1") { if (promptValue("Backend", g_cfg.mode, v)) g_cfg.mode = v; }
        else if (line == "2") { if (promptValue("Binario asr_infer", g_cfg.vibeasrBin, v)) g_cfg.vibeasrBin = v; }
        else if (line == "3") { if (promptValue("Modello GGUF VAE", g_cfg.vibeasrVae, v)) g_cfg.vibeasrVae = v; }
        else if (line == "4") { if (promptValue("Modello GGUF LM", g_cfg.vibeasrLm, v)) g_cfg.vibeasrLm = v; }
        else if (line == "5") {
            std::cout << "Thread CPU [" << g_cfg.vibeasrThreads << "]: " << std::flush;
            std::getline(std::cin, v);
            if (!v.empty()) g_cfg.vibeasrThreads = toInt(v, g_cfg.vibeasrThreads);
        }
        else if (line == "6") { if (promptValue("Formato (json|text)", g_cfg.vibeasrFormat, v)) g_cfg.vibeasrFormat = v; }
        else if (line == "7") { if (promptValue("Hotwords (contesto)", g_cfg.vibeasrContext, v)) g_cfg.vibeasrContext = v; }
        else if (line == "8") { if (promptValue("URL API", g_cfg.apiUrl, v)) g_cfg.apiUrl = v; }
        else if (line == "9") { if (promptValue("Chiave API", maskKey(g_cfg.apiKey), v)) g_cfg.apiKey = v; }
        else if (line == "10") { if (promptValue("Lingua", g_cfg.language, v)) g_cfg.language = v; }
        else if (line == "11") { if (promptValue("Cartella output", g_cfg.outputDir, v)) g_cfg.outputDir = v; }
        else if (line == "12") { if (promptValue("Titolo minuta", g_cfg.title, v)) g_cfg.title = v; }
        else if (line == "13") {
            std::cout << "Contesto LM [" << g_cfg.vibeasrCtx << "]: " << std::flush;
            std::getline(std::cin, v);
            if (!v.empty()) g_cfg.vibeasrCtx = toInt(v, g_cfg.vibeasrCtx);
        }
        else if (line == "14") {
            std::cout << "Durata chunk [" << g_cfg.vibeasrChunkSec << "]: " << std::flush;
            std::getline(std::cin, v);
            if (!v.empty()) g_cfg.vibeasrChunkSec = toInt(v, g_cfg.vibeasrChunkSec);
        }
        else if (line == "15") {
            if (g_cfg.save()) tui::ok("Configurazione salvata in " + g_cfg.path);
            else tui::error("Impossibile salvare " + g_cfg.path);
        }
        else if (line == "16" || line.empty()) return 0;
        else tui::warn("Scelta non valida.");
    }
}

int interactiveMenu() {
    tui::banner();

    // Plug and play: se mancano componenti VibeVoice, scaricali automaticamente.
    if (g_cfg.mode == "vibeasr" &&
        (g_cfg.vibeasrBin.empty() || g_cfg.vibeasrVae.empty() || g_cfg.vibeasrLm.empty())) {
        tui::warn("Componenti VibeVoice mancanti: download automatico in corso...");
        cmdSetup();
        tui::newline();
    }

    for (;;) {
        tui::header(" MENU PRINCIPALE ");
        std::cout
            << "  Passi singoli:\n"
            << "    1) Registra l'audio (Fase 1)\n"
            << "    2) Trascrivi l'audio (Fase 2)\n"
            << "    3) Genera la minuta (Fase 3)\n"
            << "  Pipeline completa:\n"
            << "    4) Registra + Trascrivi + Minuta (1+2+3)\n"
            << "  Strumenti:\n"
            << "    5) Configurazione (sotto-menu)\n"
            << "    6) Elenca i dispositivi audio\n"
            << "    7) Setup automatico (scarica asr_infer + modelli)\n"
            << "    8) Converti audio (mp3/flac -> wav)\n"
            << "    9) Esci\n"
            << "Scelta: " << std::flush;

        std::string line;
        std::getline(std::cin, line);
        if (line == "1") {
            tui::header(" FASE 1 - Registrazione ");
            cmdRecord({});
        } else if (line == "2") {
            tui::header(" FASE 2 - Trascrizione ");
            const std::string audio = g_cfg.outputDir + "/audio.wav";
            std::error_code fec;
            if (!std::filesystem::exists(audio, fec)) {
                tui::error("Nessun audio registrato: " + audio);
                tui::info("Esegui prima la Fase 1 (registrazione).");
            } else {
                std::vector<std::string> a = {"--input", audio};
                cmdTranscribe(a);
            }
        } else if (line == "3") {
            tui::header(" FASE 3 - Minuta ");
            const std::string txt = g_cfg.outputDir + "/transcript.txt";
            const std::string audio = g_cfg.outputDir + "/audio.wav";
            std::error_code fec;
            if (!std::filesystem::exists(txt, fec) && std::filesystem::exists(audio, fec)) {
                // Plug and play: la trascrizione manca, la genero dall'audio registrato.
                tui::warn("Trascrizione mancante: la genero dall'audio registrato...");
                std::vector<std::string> a = {"--input", audio};
                cmdTranscribe(a);
            }
            std::vector<std::string> a = {"--transcript", txt};
            cmdMinutes(a);
        } else if (line == "4") {
            cmdAll({});
        } else if (line == "5") {
            settingsMenu();
        } else if (line == "6") {
            cmdList();
        } else if (line == "7") {
            cmdSetup();
        } else if (line == "8") {
            tui::header(" CONVERSIONE AUDIO ");
            std::string in;
            if (promptValue("File di input", "", in)) {
                std::string out;
                if (promptValue("File di output (.wav)", "", out)) {
                    std::vector<std::string> a = {"--input", in, "--output", out};
                    cmdConvert(a);
                }
            }
        } else if (line == "9" || line.empty()) {
            return 0;
        } else {
            tui::warn("Scelta non valida.");
        }
    }
}

// Callback di progresso per i download.
void dlProgress(uint64_t cur, uint64_t total) {
    std::string pct;
    if (total > 0) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.1f%%", 100.0 * cur / total);
        pct = b;
    }
    tui::progress("  " + fmtSize(cur) + " / " + fmtSize(total) + "  " + pct);
}

// Scarica il binario asr_infer mancante dalla release GitHub di meetingrec.
bool ensureAsrInfer(std::string& err) {
    if (!g_cfg.vibeasrBin.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories("vibeasr/build/bin", ec);
    if (ec) {
        err = "Impossibile creare vibeasr/build/bin: " + ec.message();
        return false;
    }
#ifdef _WIN32
    const std::string asset = "asr_infer-windows-x64.exe";
    const std::string dest = "vibeasr/build/bin/asr_infer.exe";
#else
    const std::string asset = "asr_infer-linux-x64";
    const std::string dest = "vibeasr/build/bin/asr_infer";
#endif
    const std::string url =
        "https://github.com/euplea/meetingrec/releases/latest/download/" + asset;
    tui::section("Download " + asset + " (VibeASR.cpp)...");
    if (!downloadFile(url, dest, 0, dlProgress, err)) return false;
#ifndef _WIN32
    std::error_code pec;
    std::filesystem::permissions(
        dest, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                  std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, pec);
#endif
    tui::clearLine();
    g_cfg.vibeasrBin = dest;
    tui::ok("asr_infer pronto: " + dest);
    return true;
}

int cmdSetup() {
    tui::header(" SETUP AUTOMATICO VibeVoice ");
    if (!g_cfg.vibeasrBin.empty() && !g_cfg.vibeasrVae.empty() && !g_cfg.vibeasrLm.empty()) {
        tui::ok("Niente da scaricare: asr_infer e modelli GGUF gia presenti.");
        return 0;
    }
    std::string err;
    if (!ensureAsrInfer(err)) {
        tui::error(err);
        return 1;
    }
    if (g_cfg.vibeasrVae.empty() || g_cfg.vibeasrLm.empty()) {
        if (cmdDownloadModels({}) != 0) {
            tui::error("Download dei modelli GGUF fallito.");
            return 1;
        }
    }
    g_cfg.save();
    tui::ok("Tutto pronto per la trascrizione locale!");
    tui::info("Ora puoi usare: meetingrec all --duration 3600");
    return 0;
}

int cmdConvert(const std::vector<std::string>& args) {
    const std::string in = getArg(args, "--input");
    const std::string out = getArg(args, "--output");
    if (in.empty() || out.empty()) {
        tui::error("--input e --output richiesti.");
        return 1;
    }
    const int rate = toInt(getArg(args, "--rate"), 16000);
    const int channels = toInt(getArg(args, "--channels"), 1);

    tui::section("Conversione audio...");
    std::string err;
    if (!convertAudio(in, out, rate, channels, err)) {
        tui::error(err);
        return 1;
    }
    tui::ok("Convertito: " + in + " -> " + out + " (" + std::to_string(rate) + " Hz, " +
            std::to_string(channels) + " ch)");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    tui::init();

    // Doppio clic su Windows: riapri in Windows Terminal (se disponibile).
    if (tui::ensureWindowsTerminal()) return 0;

    g_cfg.load();  // carica meetingrec.json se presente

    // Auto-discovery: binario asr_infer e modelli GGUF in posizioni note.
    if (discoverVibeasr(g_cfg) && tui::colorEnabled()) {
        tui::info("VibeASR rilevato automaticamente:");
        if (!g_cfg.vibeasrBin.empty()) tui::info("  bin: " + g_cfg.vibeasrBin);
        if (!g_cfg.vibeasrVae.empty()) tui::info("  vae: " + g_cfg.vibeasrVae);
        if (!g_cfg.vibeasrLm.empty()) tui::info("  lm:  " + g_cfg.vibeasrLm);
    }

    std::vector<std::string> args(argv + 1, argv + argc);
    int rc = 0;

    try {
        if (hasArg(args, "--version") || hasArg(args, "-V")) {
            std::cout << "meetingrec " << kVersion << "\n";
            rc = 0;
        } else if (args.empty() && tui::isDoubleClicked()) {
            // Doppio clic senza argomenti: menu interattivo.
            rc = interactiveMenu();
        } else if (args.empty() || hasArg(args, "-h") || hasArg(args, "--help")) {
            tui::banner();
            usage();
            rc = args.empty() ? 1 : 0;
        } else {
            tui::banner();

            const std::string cmd = args[0];
            if (cmd == "list")
                rc = cmdList();
            else if (cmd == "record")
                rc = cmdRecord(args);
            else if (cmd == "transcribe")
                rc = cmdTranscribe(args);
            else if (cmd == "minutes")
                rc = cmdMinutes(args);
            else if (cmd == "download-models" || cmd == "dl")
                rc = cmdDownloadModels(args);
            else if (cmd == "setup")
                rc = cmdSetup();
            else if (cmd == "convert")
                rc = cmdConvert(args);
            else if (cmd == "all")
                rc = cmdAll(args);
            else {
                tui::error("Comando sconosciuto: " + cmd);
                usage();
                rc = 1;
            }
        }
    } catch (const std::exception& e) {
        tui::error(std::string("Errore imprevisto: ") + e.what());
        rc = 1;
    } catch (...) {
        tui::error("Errore imprevisto (eccezione sconosciuta).");
        rc = 1;
    }

    tui::pauseIfNeeded();
    return rc;
}
