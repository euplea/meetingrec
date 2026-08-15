#pragma once

#include <string>
#include <vector>

#include "recorder.h"

// Piccola libreria TUI (colori ANSI + strutture di stampa).
// Su Windows abilita la modalità VT del terminale (Windows 10+).
namespace tui {

// Da chiamare una volta all'avvio. Ritorna false se il terminale non supporta i colori.
bool init();

bool colorEnabled();

void banner();                                   // intestazione colorata
void header(const std::string& s);               // titolo comando
void section(const std::string& s);              // sottosezione
void ok(const std::string& s);
void warn(const std::string& s);
void error(const std::string& s);
void info(const std::string& s);
void raw(const std::string& s);

void deviceList(const std::vector<DeviceInfo>& devices);

void progress(const std::string& text);          // riga aggiornabile con \r
void clearLine();
void newline();

// Su Windows: se il programma è stato avviato con doppio clic (console propria,
// nessun terminale attorno), attende un INVIO prima di chiudere la finestra.
// No-op su Linux/macOS e quando lanciato da un terminale o uno script.
void pauseIfNeeded();

// True su Windows quando avviato con doppio clic da Explorer (console propria).
bool isDoubleClicked();

// Su Windows: se avviato con doppio clic e Windows Terminal è disponibile,
// riavvia il programma dentro wt.exe e ritorna true (il chiamante deve uscire).
// No-op su Linux/macOS e quando già dentro Windows Terminal.
bool ensureWindowsTerminal();

}  // namespace tui
