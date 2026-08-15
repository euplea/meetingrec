#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Minimal 16-bit PCM WAV writer (no external dependency).
// Writes a placeholder header and patches the RIFF/data sizes on close().
class WavWriter {
public:
    WavWriter() = default;
    ~WavWriter() { close(); }

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const std::string& path, uint32_t sampleRate, uint16_t channels,
              uint16_t bitsPerSample);
    bool write(const void* data, size_t bytes);
    bool close();

private:
    FILE* f_ = nullptr;
    uint32_t dataBytes_ = 0;
    bool finalized_ = false;
};
