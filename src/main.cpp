#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "minutes.h"
#include "recorder.h"
#include "transcriber.h"
#include "tui.h"

namespace {

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

void usage() {
    std::cout
        << "\n"
        << "Uso:\n"
        << "  meetingrec list\n"
        << "  meetingrec record  [--device N] [--output file.wav] [--duration SEC] [--rate HZ] [--channels N]\n"
        << "  meetingrec transcribe --input file.wav [--output file.txt] [--api-url URL]\n"
        << "                      [--api-key KEY] [--model NAME] [--mode openai|raw|vibeasr]\n"
        << "                      [--language LANG] [--response-key KEY]\n"
        << "                      [--vibeasr-bin PATH] [--vibeasr-vae F.gguf] [--vibeasr-lm F.gguf]\n"
        << "                      [--vibeasr-threads N] [--vibeasr-context TEXT] [--vibeasr-format text|json]\n"
        << "  meetingrec minutes  --transcript file.txt [--output minuta.md]\n"
        << "                      [--title T] [--attendees A,B] [--date YYYY-MM-DD]\n"
        << "  meetingrec all      --device N [--duration SEC] [--output-dir DIR]\n"
        << "                      [--title T] [--attendees A,B] [opzioni trascrizione...]\n\n"
        << "Variabili d'ambiente:\n"
        << "  VIBE_VOICE_URL  VIBE_VOICE_API_KEY  VIBE_VOICE_MODEL  VIBE_VOICE_MODE\n"
        << "  VIBE_VOICE_LANGUAGE  VIBE_VOICE_RESPONSE_KEY\n"
        << "  VIBEASR_BIN  VIBEASR_VAE_MODEL  VIBEASR_LM_MODEL  VIBEASR_THREADS\n"
        << "  VIBEASR_CONTEXT  VIBEASR_FORMAT\n\n"
        << "Nota: su Windows, senza --device viene registrato l'audio di sistema via\n"
        << "loopback WASAPI (la voce di Teams/Zoom). Con 'list' puoi scegliere un\n"
        << "microfono specifico. Su Linux seleziona il dispositivo con 'monitor'.\n"
        << "Vedi MANUALE.md per la guida completa.\n";
}

void fillVibeasrOptions(TranscribeOptions& o, const std::vector<std::string>& args) {
    o.vibeasrBin = getArg(args, "--vibeasr-bin", getEnv("VIBEASR_BIN"));
    o.vibeasrVaeModel = getArg(args, "--vibeasr-vae", getEnv("VIBEASR_VAE_MODEL"));
    o.vibeasrLmModel = getArg(args, "--vibeasr-lm", getEnv("VIBEASR_LM_MODEL"));
    o.vibeasrThreads = toInt(getArg(args, "--vibeasr-threads", getEnv("VIBEASR_THREADS")), 4);
    o.vibeasrContext = getArg(args, "--vibeasr-context", getEnv("VIBEASR_CONTEXT"));
    o.vibeasrFormat = getArg(args, "--vibeasr-format", getEnv("VIBEASR_FORMAT"));
    if (o.vibeasrFormat.empty()) o.vibeasrFormat = "text";
}

void fillTranscribeCommon(TranscribeOptions& o, const std::vector<std::string>& args) {
    o.apiUrl = getArg(args, "--api-url", getEnv("VIBE_VOICE_URL"));
    if (o.apiUrl.empty()) o.apiUrl = "https://api.openai.com/v1/audio/transcriptions";
    o.apiKey = getArg(args, "--api-key", getEnv("VIBE_VOICE_API_KEY"));
    o.model = getArg(args, "--model", getEnv("VIBE_VOICE_MODEL"));
    if (o.model.empty()) o.model = "whisper-1";
    o.mode = getArg(args, "--mode", getEnv("VIBE_VOICE_MODE"));
    if (o.mode.empty()) o.mode = "openai";
    o.language = getArg(args, "--language", getEnv("VIBE_VOICE_LANGUAGE"));
    o.responseKey = getArg(args, "--response-key", getEnv("VIBE_VOICE_RESPONSE_KEY"));
    if (o.responseKey.empty()) o.responseKey = "text";
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
    o.outputPath = getArg(args, "--output", "meeting.wav");
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
    fillTranscribeCommon(o, args);

    tui::section("Trascrizione in corso (backend: " + o.mode + ")...");
    std::string text, err;
    if (!transcribe(o, text, err)) {
        tui::error(err);
        return 1;
    }
    const std::string out = getArg(args, "--output", "transcript.txt");
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
    o.title = getArg(args, "--title", "Minuta riunione");
    o.date = getArg(args, "--date");
    o.attendees = getArg(args, "--attendees");
    o.outputPath = getArg(args, "--output", "minuta.md");

    tui::section("Generazione minuta...");
    std::string err;
    if (!writeMinutes(o, err)) {
        tui::error(err);
        return 1;
    }
    tui::ok("Minuta salvata in " + o.outputPath);
    return 0;
}

int cmdAll(const std::vector<std::string>& args) {
    const std::string dir = getArg(args, "--output-dir", "meeting");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        tui::error("Impossibile creare la directory " + dir);
        return 1;
    }

    const std::string wav = dir + "/audio.wav";
    const std::string txt = dir + "/transcript.txt";
    const std::string md = dir + "/minuta.md";

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
    mo.title = getArg(args, "--title", "Minuta riunione");
    mo.date = getArg(args, "--date");
    mo.attendees = getArg(args, "--attendees");
    mo.outputPath = md;

    tui::header(" FASE 3/3 - Minuta ");
    if (!writeMinutes(mo, err)) {
        tui::error(err);
        return 1;
    }

    tui::ok("Pipeline completata!");
    tui::info("  audio:       " + wav);
    tui::info("  trascrizione: " + txt);
    tui::info("  minuta:      " + md);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    tui::init();
    std::vector<std::string> args(argv + 1, argv + argc);

    try {
        if (args.empty() || hasArg(args, "-h") || hasArg(args, "--help")) {
            tui::banner();
            usage();
            return args.empty() ? 1 : 0;
        }
        tui::banner();

        const std::string cmd = args[0];
        if (cmd == "list") return cmdList();
        if (cmd == "record") return cmdRecord(args);
        if (cmd == "transcribe") return cmdTranscribe(args);
        if (cmd == "minutes") return cmdMinutes(args);
        if (cmd == "all") return cmdAll(args);

        tui::error("Comando sconosciuto: " + cmd);
        usage();
        return 1;
    } catch (const std::exception& e) {
        tui::error(std::string("Errore imprevisto: ") + e.what());
        return 1;
    } catch (...) {
        tui::error("Errore imprevisto (eccezione sconosciuta).");
        return 1;
    }
}
