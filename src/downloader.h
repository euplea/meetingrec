#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Scarica un file da URL in `dest` con supporto ai redirect, mostrando progresso.
// Se `expectedSize` > 0, verifica che la dimensione finale coincida (e cancella
// il file in caso di errore). `progressCb(current, total)` viene invocata a blocchi
// (total può essere 0 se sconosciuto). Ritorna false in caso di errore.
bool downloadFile(const std::string& url, const std::string& dest, uint64_t expectedSize,
                  const std::function<void(uint64_t, uint64_t)>& progressCb,
                  std::string& error);
