#include "tui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace tui {

namespace {
bool g_color = false;

#ifdef _WIN32
bool enableVt() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return false;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) return false;
    // SetConsoleOutputCP(65001) per gli accenti su console Windows.
    SetConsoleOutputCP(CP_UTF8);
    return true;
}
#endif

bool isattyStream(FILE* f) {
#ifdef _WIN32
    return _isatty(_fileno(f)) != 0;
#else
    return isatty(fileno(f)) != 0;
#endif
}

void emit(const char* code, const std::string& s, FILE* stream) {
    if (g_color && stream == stdout) {
        std::cout << "\033[" << code << "m" << s << "\033[0m";
    } else if (g_color && stream == stderr) {
        std::cerr << "\033[" << code << "m" << s << "\033[0m";
    } else if (stream == stdout) {
        std::cout << s;
    } else {
        std::cerr << s;
    }
}

}  // namespace

bool init() {
    g_color = isattyStream(stdout) || isattyStream(stderr);
#ifdef _WIN32
    if (g_color) enableVt();
#endif
    return g_color;
}

bool colorEnabled() { return g_color; }

void banner() {
    if (!g_color) return;  // banner solo su terminale interattivo
    std::cout << "\n";
    header(" meetingrec - registra, trascrivi, minuta ");
    std::cout << "\n";
}

void header(const std::string& s) {
    const std::string line(static_cast<size_t>(s.size() + 2), '=');
    if (g_color) {
        std::cout << "\033[1;36m" << line << "\033[0m\n"
                  << "\033[1;36m=\033[0m" << s << "\033[1;36m=\033[0m\n"
                  << "\033[1;36m" << line << "\033[0m\n";
    } else {
        std::cout << line << "\n" << s << "\n" << line << "\n";
    }
}

void section(const std::string& s) {
    if (g_color) {
        std::cout << "\033[1;34m>> " << s << "\033[0m\n";
    } else {
        std::cout << ">> " << s << "\n";
    }
}

void ok(const std::string& s) { emit("1;32", "[OK] " + s, stdout); std::cout << "\n"; }
void warn(const std::string& s) { emit("1;33", "[!] " + s, stderr); std::cerr << "\n"; }
void error(const std::string& s) { emit("1;31", "[ERRORE] " + s, stderr); std::cerr << "\n"; }
void info(const std::string& s) { emit("0;37", "  " + s, stdout); std::cout << "\n"; }
void raw(const std::string& s) { std::cout << s; }

void deviceList(const std::vector<DeviceInfo>& devices) {
    std::cout << "\n";
    for (const auto& d : devices) {
        std::string flags;
        if (d.isLoopback) flags += " [LOOPBACK]";
        if (d.isDefaultInput) flags += " [DEFAULT]";
        std::string name = d.name + flags;
        if (g_color) {
            if (d.isLoopback)
                std::cout << "  \033[1;35m[" << d.index << "]\033[0m \033[1;33m" << name
                          << "\033[0m";
            else
                std::cout << "  \033[1;35m[" << d.index << "]\033[0m " << name;
        } else {
            std::cout << "  [" << d.index << "] " << name;
        }
        std::cout << "  (in-ch=" << d.maxInputChannels << ", " << d.defaultSampleRate
                  << " Hz)\n";
    }
    std::cout << "\n";
}

void progress(const std::string& text) {
    if (g_color) {
        std::cout << "\r\033[2K\033[1;33m" << text << "\033[0m" << std::flush;
    } else {
        std::cout << "\r" << text << std::flush;
    }
}

void clearLine() {
    if (g_color) std::cout << "\r\033[2K" << std::flush;
    else std::cout << "\r\033[2K" << std::flush;
}

void newline() { std::cout << "\n"; }

bool isDoubleClicked() {
#ifdef _WIN32
    DWORD pids[2] = {0, 0};
    // Se questo processo è l'unico collegato alla console, è stato avviato
    // con doppio clic da Explorer (console di proprietà).
    const DWORD n = GetConsoleProcessList(pids, 2);
    return n == 1;
#else
    return false;
#endif
}

bool ensureWindowsTerminal() {
#ifdef _WIN32
    // Già dentro Windows Terminal? Non fare nulla.
    if (std::getenv("WT_SESSION")) return false;
    // Avviato da cmd/powershell/script oppure senza console? Non toccare nulla.
    DWORD pids[2] = {0, 0};
    const DWORD n = GetConsoleProcessList(pids, 2);
    if (n == 0 || n > 1) return false;

    // Cerca wt.exe nel PATH (app execution alias di Windows Terminal).
    wchar_t wt[MAX_PATH];
    if (SearchPathW(nullptr, L"wt.exe", nullptr, MAX_PATH, wt, nullptr) == 0) return false;

    wchar_t dir[MAX_PATH] = {0};
    GetCurrentDirectoryW(MAX_PATH, dir);

    // Rilancia dentro wt.exe nella stessa directory, con gli stessi argomenti.
    std::wstring cmd = L"wt.exe -d \"" + std::wstring(dir) + L"\" " + GetCommandLineW();

    STARTUPINFOW si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    std::memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(wt, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    return false;
#endif
}

void pauseIfNeeded() {
#ifdef _WIN32
    // Windows Terminal tiene la tab aperta anche a processo terminato.
    if (std::getenv("WT_SESSION")) return;
    if (!isDoubleClicked()) return;
    std::cout << "\nPremi INVIO per chiudere... " << std::flush;
    std::cin.get();
#endif
}

}  // namespace tui
