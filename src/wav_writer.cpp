#include "wav_writer.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

bool WavWriter::open(const std::string& path, uint32_t sampleRate,
                     uint16_t channels, uint16_t bitsPerSample) {
    close();
#ifdef _WIN32
    // Supporta percorsi UTF-8 (es. nomi con accenti) anche su Windows.
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], n);
    f_ = _wfopen(wide.c_str(), L"wb");
#else
    f_ = std::fopen(path.c_str(), "wb");
#endif
    if (!f_) return false;

    dataBytes_ = 0;
    finalized_ = false;

    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t byteRate = sampleRate * blockAlign;

    uint8_t hdr[44];
    std::memset(hdr, 0, sizeof(hdr));
    std::memcpy(hdr + 0, "RIFF", 4);
    std::memcpy(hdr + 8, "WAVE", 4);
    std::memcpy(hdr + 12, "fmt ", 4);
    hdr[16] = 16;  // fmt chunk size (PCM)
    hdr[20] = 1;   // audio format: PCM
    hdr[22] = static_cast<uint8_t>(channels & 0xff);
    hdr[23] = static_cast<uint8_t>(channels >> 8);
    hdr[24] = static_cast<uint8_t>(sampleRate & 0xff);
    hdr[25] = static_cast<uint8_t>((sampleRate >> 8) & 0xff);
    hdr[26] = static_cast<uint8_t>((sampleRate >> 16) & 0xff);
    hdr[27] = static_cast<uint8_t>((sampleRate >> 24) & 0xff);
    hdr[28] = static_cast<uint8_t>(byteRate & 0xff);
    hdr[29] = static_cast<uint8_t>((byteRate >> 8) & 0xff);
    hdr[30] = static_cast<uint8_t>((byteRate >> 16) & 0xff);
    hdr[31] = static_cast<uint8_t>((byteRate >> 24) & 0xff);
    hdr[32] = static_cast<uint8_t>(blockAlign & 0xff);
    hdr[33] = static_cast<uint8_t>(blockAlign >> 8);
    hdr[34] = static_cast<uint8_t>(bitsPerSample & 0xff);
    hdr[35] = static_cast<uint8_t>(bitsPerSample >> 8);
    std::memcpy(hdr + 36, "data", 4);
    // hdr[40..43] data size left at 0 for now

    if (std::fwrite(hdr, 1, sizeof(hdr), f_) != sizeof(hdr)) {
        close();
        return false;
    }
    return true;
}

bool WavWriter::write(const void* data, size_t bytes) {
    if (!f_) return false;
    if (std::fwrite(data, 1, bytes, f_) != bytes) return false;
    dataBytes_ += static_cast<uint32_t>(bytes);
    return true;
}

bool WavWriter::close() {
    if (!f_) return true;
    if (!finalized_) {
        const uint32_t riffSize = 36 + dataBytes_;
        std::fseek(f_, 4, SEEK_SET);
        std::fwrite(&riffSize, sizeof(riffSize), 1, f_);
        std::fseek(f_, 40, SEEK_SET);
        std::fwrite(&dataBytes_, sizeof(dataBytes_), 1, f_);
        finalized_ = true;
    }
    std::fclose(f_);
    f_ = nullptr;
    return true;
}
