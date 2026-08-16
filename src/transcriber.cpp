#include "transcriber.h"

#include "audio_convert.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace {

// Minimal JSON helper: returns the string value of the first `"key"` field.
std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";

    const size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return "";

    const size_t start = json.find('"', colon + 1);
    if (start == std::string::npos) return "";

    std::string out;
    for (size_t i = start + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < json.size()) {
            const char n = json[i + 1];
            if (n == 'n') out += '\n';
            else if (n == 't') out += '\t';
            else if (n == 'r') out += '\r';
            else if (n == '"') out += '"';
            else if (n == '\\') out += '\\';
            else out += n;
            ++i;
            continue;
        }
        out += c;
    }
    return out;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool fileExistsNonEmpty(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    return f.tellg() > 0;
}

std::string quoteArg(const std::string& s) { return "\"" + s + "\""; }

// Runs a shell command, captures stdout, and redirects stderr to a temp file.
bool runCommandCapture(const std::string& cmd, const std::string& stderrFile,
                       std::string& stdoutOut, std::string& stderrOut, int& exitCode) {
    const std::string full = cmd + " 2>" + quoteArg(stderrFile);
#ifdef _WIN32
    FILE* p = _popen(full.c_str(), "r");
#else
    FILE* p = popen(full.c_str(), "r");
#endif
    if (!p) return false;

    stdoutOut.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) stdoutOut.append(buf, n);

#ifdef _WIN32
    exitCode = _pclose(p);
#else
    const int rc = pclose(p);
    exitCode = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif

    std::ifstream f(stderrFile, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    stderrOut = ss.str();
    f.close();
    std::remove(stderrFile.c_str());
    return true;
}

bool transcribeVibeAsr(const TranscribeOptions& opts, std::string& textOut,
                       std::string& error) {
    if (opts.vibeasrVaeModel.empty() || opts.vibeasrLmModel.empty()) {
        error = "Per --mode vibeasr servono --vibeasr-vae e --vibeasr-lm "
                "(percorsi dei modelli GGUF di VibeASR.cpp)";
        return false;
    }

    // Verifica preventiva: i modelli GGUF devono esistere davvero.
    if (!fileExistsNonEmpty(opts.vibeasrVaeModel)) {
        error = "Modello GGUF (VAE) non trovato o vuoto: " + opts.vibeasrVaeModel +
                "\nScaricalo con:  bash scripts/setup_vibeasr.sh"
                "   (Windows: scripts\\setup_vibeasr.bat)";
        return false;
    }
    if (!fileExistsNonEmpty(opts.vibeasrLmModel)) {
        error = "Modello GGUF (LM) non trovato o vuoto: " + opts.vibeasrLmModel +
                "\nScaricalo con:  bash scripts/setup_vibeasr.sh"
                "   (Windows: scripts\\setup_vibeasr.bat)";
        return false;
    }

    const std::string bin = opts.vibeasrBin.empty() ? "asr_infer" : opts.vibeasrBin;

    // Se il binario è indicato con un percorso, verifica che esista.
    if (bin.find('/') != std::string::npos || bin.find('\\') != std::string::npos) {
        if (!fileExistsNonEmpty(bin)) {
            error = "Binario asr_infer non trovato: " + bin +
                    "\nCompilalo con VibeASR.cpp (vedi scripts/setup_vibeasr.sh)";
            return false;
        }
    }

    const std::string format = opts.vibeasrFormat.empty() ? "text" : opts.vibeasrFormat;

    // VibeASR.cpp accetta WAV nativamente: converte gli altri formati (mp3/flac/...)
    // in un WAV temporaneo 16 kHz mono prima di inviargli il file.
    std::string audioPath = opts.inputPath;
    std::string tempWav;
    {
        const size_t dot = opts.inputPath.find_last_of('.');
        const std::string ext =
            dot == std::string::npos ? "" : opts.inputPath.substr(dot + 1);
        std::string lowerExt;
        for (char c : ext) lowerExt += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lowerExt != "wav") {
            tempWav = opts.inputPath + ".conv.wav";
            std::string convErr;
            if (!convertAudio(opts.inputPath, tempWav, 16000, 1, convErr)) {
                error = "Conversione audio fallita: " + convErr;
                return false;
            }
            audioPath = tempWav;
        }
    }

    std::string cmd = quoteArg(bin) +
                      " --vae-model " + quoteArg(opts.vibeasrVaeModel) +
                      " --lm-model " + quoteArg(opts.vibeasrLmModel) +
                      " --audio " + quoteArg(audioPath) +
                      " -t " + std::to_string(opts.vibeasrThreads) +
                      " --prompt-format " + format + " --greedy";
    if (!opts.vibeasrContext.empty()) cmd += " --context " + quoteArg(opts.vibeasrContext);

    const std::string stderrFile = opts.inputPath + ".asr.log";
    std::string out, errOut;
    int exitCode = -1;
    if (!runCommandCapture(cmd, stderrFile, out, errOut, exitCode)) {
        error = "Impossibile eseguire il processo '" + bin + "' (verifica che sia nel PATH)";
        return false;
    }
    if (exitCode != 0) {
        const std::string tail = errOut.size() > 400 ? errOut.substr(errOut.size() - 400) : errOut;
        error = "asr_infer terminato con codice " + std::to_string(exitCode) + ":\n" + tail;
        return false;
    }

    textOut = trim(out);
    if (textOut.empty()) {
        error = "Nessun testo prodotto da asr_infer";
        if (!tempWav.empty()) std::remove(tempWav.c_str());
        return false;
    }
    if (!tempWav.empty()) std::remove(tempWav.c_str());
    return true;
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Trasporto HTTP via WinHTTP (nativo Windows, nessuna dipendenza esterna).
// ---------------------------------------------------------------------------

std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

bool transcribeHttp(const TranscribeOptions& opts, std::string& textOut,
                    std::string& error) {
    // 1) Legge il file audio in memoria.
    std::ifstream in(opts.inputPath, std::ios::binary);
    if (!in) {
        error = "Impossibile aprire " + opts.inputPath;
        return false;
    }
    std::vector<char> audio((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    in.close();
    if (audio.empty()) {
        error = "File audio vuoto: " + opts.inputPath;
        return false;
    }

    // 2) Costruisce il body (multipart per il contratto Whisper, raw altrimenti).
    const bool multipart = (opts.mode == "openai");
    const std::string boundary = "----meetingrecBoundary7MA4YWxk";
    std::vector<char> body;
    std::string contentType;

    if (multipart) {
        std::string head = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                           "Content-Type: audio/wav\r\n\r\n";
        body.insert(body.end(), head.begin(), head.end());
        body.insert(body.end(), audio.begin(), audio.end());
        std::string mid = "\r\n--" + boundary + "\r\n"
                          "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                          opts.model + "\r\n";
        if (!opts.language.empty()) {
            mid += "--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"language\"\r\n\r\n" +
                   opts.language + "\r\n";
        }
        mid += "--" + boundary + "--\r\n";
        body.insert(body.end(), mid.begin(), mid.end());
        contentType = "multipart/form-data; boundary=" + boundary;
    } else {
        body = audio;
        contentType = "audio/wav";
    }

    // 3) Parserizza l'URL.
    URL_COMPONENTS uc;
    std::memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    const std::wstring wurl = wide(opts.apiUrl);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        error = "URL non valido: " + opts.apiUrl;
        return false;
    }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const INTERNET_PORT port = uc.nPort;
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    // 4) Effettua la richiesta.
    HINTERNET hSession = WinHttpOpen(L"meetingrec/1.2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "WinHttpOpen fallito (0x" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WinHttpSetTimeouts(hSession, 0, 30000, 30000, 900000);

    HINTERNET hConnect =
        WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        error = "WinHttpConnect fallito (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        error = "WinHttpOpenRequest fallito (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers = L"Content-Type: " + wide(contentType) + L"\r\n";
    if (!opts.apiKey.empty())
        headers += L"Authorization: Bearer " + wide(opts.apiKey) + L"\r\n";

    BOOL sent = WinHttpSendRequest(hRequest, headers.c_str(),
                                   static_cast<DWORD>(headers.size()),
                                   body.data(), static_cast<DWORD>(body.size()),
                                   static_cast<DWORD>(body.size()), 0);
    if (!sent) {
        error = "WinHttpSendRequest fallito (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        error = "WinHttpReceiveResponse fallito (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 5) Legge la risposta.
    std::string response;
    DWORD available = 0;
    do {
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        std::vector<char> buf(available);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf.data(), available, &read)) break;
        response.append(buf.data(), read);
    } while (available > 0);

    // 6) Verifica lo stato HTTP (prima di chiudere le handle).
    DWORD status = 0;
    DWORD statusLen = sizeof(status);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen,
                             WINHTTP_NO_HEADER_INDEX)) {
        status = 0;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (status >= 400) {
        error = "HTTP " + std::to_string(status) + ": " +
                (response.size() > 300 ? response.substr(0, 300) : response);
        return false;
    }

    textOut = extractJsonString(response, opts.responseKey);
    if (textOut.empty() && response.find('"') == std::string::npos) {
        textOut = response;  // risposta in testo puro
    }
    if (textOut.empty()) {
        error = "Nessun testo trovato nella risposta (chiave: \"" + opts.responseKey +
                "\"): " + (response.size() > 200 ? response.substr(0, 200) : response);
        return false;
    }
    return true;
}

#else
// ---------------------------------------------------------------------------
// Trasporto HTTP via libcurl (Linux/macOS).
// ---------------------------------------------------------------------------

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, total);
    return total;
}

bool transcribeHttp(const TranscribeOptions& opts, std::string& textOut,
                    std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init() fallito";
        return false;
    }

    curl_mime* mime = nullptr;
    curl_slist* headers = nullptr;
    FILE* uploadFile = nullptr;
    std::string response;

    const bool multipart = (opts.mode == "openai");

    if (multipart) {
        mime = curl_mime_init(curl);

        curl_mimepart* file = curl_mime_addpart(mime);
        curl_mime_name(file, "file");
        curl_mime_filedata(file, opts.inputPath.c_str());
        curl_mime_filename(file, "audio.wav");
        curl_mime_type(file, "audio/wav");

        curl_mimepart* model = curl_mime_addpart(mime);
        curl_mime_name(model, "model");
        curl_mime_data(model, opts.model.c_str(), CURL_ZERO_TERMINATED);

        if (!opts.language.empty()) {
            curl_mimepart* lang = curl_mime_addpart(mime);
            curl_mime_name(lang, "language");
            curl_mime_data(lang, opts.language.c_str(), CURL_ZERO_TERMINATED);
        }

        if (!opts.apiKey.empty()) {
            headers = curl_slist_append(
                headers, ("Authorization: Bearer " + opts.apiKey).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    } else {
        uploadFile = std::fopen(opts.inputPath.c_str(), "rb");
        if (!uploadFile) {
            error = "Impossibile aprire " + opts.inputPath;
            curl_easy_cleanup(curl);
            return false;
        }
        std::fseek(uploadFile, 0, SEEK_END);
        const long fsize = std::ftell(uploadFile);
        std::fseek(uploadFile, 0, SEEK_SET);

        headers = curl_slist_append(headers, "Content-Type: audio/wav");
        if (!opts.apiKey.empty()) {
            headers = curl_slist_append(
                headers, ("Authorization: Bearer " + opts.apiKey).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, uploadFile);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(fsize));
    }

    curl_easy_setopt(curl, CURLOPT_URL, opts.apiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);  // 15 min per lunghi meeting

    const CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (uploadFile) std::fclose(uploadFile);
    if (mime) curl_mime_free(mime);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error = std::string("Richiesta HTTP fallita: ") + curl_easy_strerror(res);
        return false;
    }
    if (httpCode >= 400) {
        error = "HTTP " + std::to_string(httpCode) + ": " +
                (response.size() > 300 ? response.substr(0, 300) : response);
        return false;
    }

    textOut = extractJsonString(response, opts.responseKey);
    if (textOut.empty() && response.find('"') == std::string::npos) {
        textOut = response;  // risposta in testo puro
    }
    if (textOut.empty()) {
        error = "Nessun testo trovato nella risposta (chiave: \"" + opts.responseKey + "\")";
        return false;
    }
    return true;
}
#endif

}  // namespace

bool transcribe(const TranscribeOptions& opts, std::string& textOut, std::string& error) {
    if (opts.mode == "vibeasr") {
        return transcribeVibeAsr(opts, textOut, error);
    }

    // Errore chiaro quando si usa il backend OpenAI senza chiave API.
    if (opts.apiKey.empty() && opts.apiUrl.find("api.openai.com") != std::string::npos) {
        error = "Nessuna chiave API (VIBE_VOICE_API_KEY) per il backend '" + opts.mode +
                "' verso api.openai.com.\n"
                "Suggerimento: usa la trascrizione LOCALE gratuita con VibeVoice-ASR:\n"
                "  1) meetingrec download-models\n"
                "  2) set VIBE_VOICE_MODE=vibeasr\n"
                "  3) set VIBEASR_VAE_MODEL=<percorso GGUF VAE>  e  "
                "VIBEASR_LM_MODEL=<percorso GGUF LM>\n"
                "Oppure imposta una chiave valida: set VIBE_VOICE_API_KEY=sk-...";
        return false;
    }

    return transcribeHttp(opts, textOut, error);
}
