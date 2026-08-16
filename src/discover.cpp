#include "discover.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    const size_t pos = p.find_last_of(L"\\/");
    if (pos != std::string::npos) p = p.substr(0, pos);
    return narrow(p);
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string p(buf);
        const size_t pos = p.find_last_of('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return ".";
#endif
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    if (!h || !*h) h = std::getenv("USERPROFILE");
    return h ? h : "";
}

bool isFile(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

}  // namespace

bool discoverVibeasr(Config& cfg) {
    bool found = false;
    const std::string exe = exeDir();
    const std::string home = homeDir();

    // 1) Modelli GGUF (VAE + LM).
    if (cfg.vibeasrVae.empty() || cfg.vibeasrLm.empty()) {
        std::vector<std::string> dirs;
        dirs.push_back("vibeasr/models");
        dirs.push_back(exe + "/vibeasr/models");
        dirs.push_back(exe + "/models");
        if (!home.empty()) {
            dirs.push_back(home + "/vibeasr/models");
            dirs.push_back(home + "/models");
        }

        static const char* vaeNames[] = {"vibeasr-vae-encoder-i8_s.gguf"};
        static const char* lmNames[] = {"vibeasr-lm-i2_s-embed-q6_k.gguf",
                                        "vibeasr-lm-i2_s.gguf"};

        for (const auto& d : dirs) {
            if (cfg.vibeasrVae.empty()) {
                for (const char* n : vaeNames) {
                    const std::string p = d + "/" + n;
                    if (isFile(p)) {
                        cfg.vibeasrVae = p;
                        found = true;
                        break;
                    }
                }
            }
            if (cfg.vibeasrLm.empty()) {
                for (const char* n : lmNames) {
                    const std::string p = d + "/" + n;
                    if (isFile(p)) {
                        cfg.vibeasrLm = p;
                        found = true;
                        break;
                    }
                }
            }
            if (!cfg.vibeasrVae.empty() && !cfg.vibeasrLm.empty()) break;
        }
    }

    // 2) Binario asr_infer.
    if (cfg.vibeasrBin.empty()) {
#ifdef _WIN32
        const char* binName = "asr_infer.exe";
#else
        const char* binName = "asr_infer";
#endif
        std::vector<std::string> dirs;
        dirs.push_back("vibeasr/build/bin");
        dirs.push_back(exe + "/vibeasr/build/bin");
        dirs.push_back("VibeASR.cpp/build/bin");
        dirs.push_back(exe + "/VibeASR.cpp/build/bin");
        if (!home.empty()) {
            dirs.push_back(home + "/vibeasr/build/bin");
            dirs.push_back(home + "/VibeASR.cpp/build/bin");
        }
        for (const auto& d : dirs) {
            const std::string p = d + "/" + binName;
            if (isFile(p)) {
                cfg.vibeasrBin = p;
                found = true;
                break;
            }
        }
    }

    return found;
}
