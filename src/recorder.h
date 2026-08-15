#pragma once

#include <string>
#include <vector>

struct DeviceInfo {
    int index = 0;
    std::string id;             // Windows endpoint ID (vuoto altrove)
    std::string name;
    int maxInputChannels = 0;
    double defaultSampleRate = 0.0;
    bool isDefaultInput = false;
    bool isLoopback = false;    // true = sorgente loopback (audio riprodotto, Windows)
};

// Elenca i dispositivi audio disponibili.
// - Linux/macOS: dispositivi di input PortAudio (inclusi i "monitor" di PulseAudio/PipeWire).
// - Windows: microfoni (eCapture) e sorgenti "LOOPBACK" (eRender), che catturano
//   l'audio riprodotto dal sistema (es. la voce di Teams).
bool listDevices(std::vector<DeviceInfo>& out);

struct RecordOptions {
    int deviceIndex = -1;       // -1 => default (Windows: loopback uscita di sistema)
    double sampleRate = 16000.0;
    int channels = 1;
    std::string outputPath = "meeting.wav";
    double durationSeconds = 0.0;  // 0 => registra fino a INVIO / Ctrl+C
};

// Registra l'audio selezionato in un file WAV 16-bit PCM.
bool recordAudio(const RecordOptions& opts, std::string& error);
