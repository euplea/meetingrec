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

}  // namespace tui
