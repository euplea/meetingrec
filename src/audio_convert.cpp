#include "audio_convert.h"
#include "wav_writer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

// dr_libs: decoder single-header public domain.
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extension(const std::string& path) {
    const size_t pos = path.find_last_of(".\\/");
    if (pos == std::string::npos || path[pos] != '.') return "";
    return lower(path.substr(pos + 1));
}

std::string quoteArg(const std::string& s) { return "\"" + s + "\""; }

float clampf(float v) {
    if (v > 1.0f) return 1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

// Riscrive il PCM (interleaved float, srcCh canali) in WAV 16 bit.
bool writePcmToWav(const float* pcm, uint64_t frames, uint32_t srcRate, uint32_t srcCh,
                   int targetRate, int targetCh, const std::string& outPath) {
    if (targetRate <= 0) targetRate = static_cast<int>(srcRate);
    if (targetCh <= 0) targetCh = static_cast<int>(srcCh);
    if (targetCh != 1 && targetCh != 2) targetCh = 1;
    if (frames == 0) return false;

    // Downmix / upmix al numero di canali richiesto (interleaved float).
    std::vector<float> mixed(frames * targetCh);
    for (uint64_t f = 0; f < frames; ++f) {
        if (targetCh == 1) {
            float s = 0.0f;
            for (uint32_t c = 0; c < srcCh; ++c) s += pcm[f * srcCh + c];
            mixed[f] = s / static_cast<float>(srcCh);
        } else {
            if (srcCh == 1) {
                mixed[f * 2] = mixed[f * 2 + 1] = pcm[f];
            } else {
                mixed[f * 2] = pcm[f * srcCh];
                mixed[f * 2 + 1] = pcm[f * srcCh + 1];
            }
        }
    }

    // Resampling lineare a targetRate.
    const uint64_t outFrames =
        static_cast<uint64_t>((static_cast<double>(frames) * targetRate) / srcRate) + 1;
    std::vector<int16_t> out;
    out.reserve(outFrames * targetCh);
    for (uint64_t i = 0; i < outFrames; ++i) {
        const double pos = static_cast<double>(i) * srcRate / targetRate;
        uint64_t i0 = static_cast<uint64_t>(pos);
        const uint64_t i1 = std::min<uint64_t>(i0 + 1, frames - 1);
        const float frac = static_cast<float>(pos - i0);
        for (int c = 0; c < targetCh; ++c) {
            const float v = mixed[i0 * targetCh + c] * (1.0f - frac) +
                            mixed[i1 * targetCh + c] * frac;
            out.push_back(static_cast<int16_t>(clampf(v) * 32767.0f));
        }
    }

    WavWriter w;
    if (!w.open(outPath, static_cast<uint32_t>(targetRate),
                static_cast<uint16_t>(targetCh), 16))
        return false;
    w.write(out.data(), out.size() * sizeof(int16_t));
    w.close();
    return true;
}

// Decode nativo (WAV/MP3/FLAC) -> PCM float32 interleaved.
// `kind` indica il decoder usato (1=wav, 2=mp3, 3=flac) per il free corretto.
bool decodeNative(const std::string& input, float*& pcm, uint64_t& frames, uint32_t& rate,
                  uint32_t& ch, int& kind) {
    pcm = nullptr;
    frames = rate = ch = 0;
    kind = 0;
    const std::string ext = extension(input);
    uint64_t n64 = 0;
    if (ext == "wav") {
        unsigned int c = 0, r = 0;
        drwav_uint64 n = 0;
        pcm = drwav_open_file_and_read_pcm_frames_f32(input.c_str(), &c, &r, &n, nullptr);
        ch = c; rate = r; n64 = static_cast<uint64_t>(n); kind = pcm ? 1 : 0;
    } else if (ext == "mp3") {
        drmp3_config cfg = {0, 0};  // 0 = mantieni canali e rate del sorgente
        drmp3_uint64 n = 0;
        pcm = drmp3_open_file_and_read_pcm_frames_f32(input.c_str(), &cfg, &n, nullptr);
        ch = cfg.channels; rate = cfg.sampleRate; n64 = static_cast<uint64_t>(n);
        kind = pcm ? 2 : 0;
    } else if (ext == "flac") {
        unsigned int c = 0, r = 0;
        drflac_uint64 n = 0;
        pcm = drflac_open_file_and_read_pcm_frames_f32(input.c_str(), &c, &r, &n, nullptr);
        ch = c; rate = r; n64 = static_cast<uint64_t>(n); kind = pcm ? 3 : 0;
    }
    if (!pcm) return false;
    frames = n64;
    return frames > 0;
}

void freePcm(float* pcm, int kind) {
    if (!pcm) return;
    if (kind == 1) drwav_free(pcm, nullptr);
    else if (kind == 2) drmp3_free(pcm, nullptr);
    else if (kind == 3) drflac_free(pcm, nullptr);
}

bool runSystem(const std::string& cmd) {
#ifdef _WIN32
    return std::system(cmd.c_str()) == 0;
#else
    const int rc = std::system(cmd.c_str());
    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
#endif
}

// Fallback ffmpeg (encode/decode di qualsiasi formato).
bool convertWithFfmpeg(const std::string& input, const std::string& output, int targetRate,
                       int targetCh, const std::string& extOut, std::string& error) {
    std::string codec;
    if (extOut == "wav") codec = "pcm_s16le";
    else if (extOut == "mp3") codec = "libmp3lame";
    else if (extOut == "ogg") codec = "libvorbis";
    else if (extOut == "opus") codec = "libopus";
    else if (extOut == "m4a" || extOut == "aac") codec = "aac";
    else if (extOut == "flac") codec = "flac";
    else {
        error = "Formato di output non supportato: ." + extOut +
                " (supportati: wav, mp3, ogg, opus, m4a, aac, flac)";
        return false;
    }

    std::string cmd = "ffmpeg -y -hide_banner -loglevel error -i " + quoteArg(input) +
                      " -vn -acodec " + codec;
    if (targetRate > 0) cmd += " -ar " + std::to_string(targetRate);
    if (targetCh > 0) cmd += " -ac " + std::to_string(targetCh);
    cmd += " " + quoteArg(output);

    if (!runSystem(cmd)) {
        error = "ffmpeg non riuscito. Verifica che sia installato e nel PATH, oppure usa "
                "formati nativi (wav/mp3/flac) per l'input.";
        return false;
    }
    return true;
}

}  // namespace

bool convertAudio(const std::string& input, const std::string& output, int targetRate,
                  int targetChannels, std::string& error) {
    const std::string extOut = extension(output);

    // 1) Decode nativo (WAV/MP3/FLAC) e scrittura WAV.
    if (extOut == "wav") {
        float* pcm = nullptr;
        uint64_t frames = 0;
        uint32_t rate = 0, ch = 0;
        int kind = 0;
        if (decodeNative(input, pcm, frames, rate, ch, kind)) {
            const bool ok = writePcmToWav(pcm, frames, rate, ch, targetRate, targetChannels,
                                          output);
            freePcm(pcm, kind);
            if (!ok) {
                error = "Impossibile scrivere " + output;
                return false;
            }
            return true;
        }
        // Il decoder nativo non ha riconosciuto l'input: ripiega su ffmpeg.
    }

    // 2) Fallback: ffmpeg (input di altri formati o output non-WAV).
    return convertWithFfmpeg(input, output, targetRate, targetChannels, extOut, error);
}
