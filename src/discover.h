#pragma once

#include "config.h"

// Riempe i campi VUOTI di cfg (vibeasrBin, vibeasrVae, vibeasrLm) cercando in
// posizioni note: vibeasr/models, accanto all'eseguibile, nella home, e
// nelle cartelle di build di VibeASR.cpp / vibeasr.
// Non sovrascrive valori già impostati (config, env o flag).
// Ritorna true se ha riempito almeno un campo.
bool discoverVibeasr(Config& cfg);
