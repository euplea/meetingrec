#include "recorder.h"
#include "wav_writer.h"
#include "tui.h"

#include <cstdio>
#include <string>

namespace {
std::string fmtElapsed(double sec) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", static_cast<int>(sec) / 60,
                  static_cast<int>(sec) % 60);
    return buf;
}
}  // namespace

#ifdef _WIN32

// ---------------------------------------------------------------------------
// Windows 10: cattura via WASAPI (loopback per l'audio di sistema + microfoni)
// ---------------------------------------------------------------------------

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <initguid.h>
#include <windows.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <mmreg.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// GUID dei formati audio (definiti localmente per non dipendere da ksmedia/ksguid).
DEFINE_GUID(guidPcm, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(guidIeeeFloat, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

namespace {

std::atomic<bool> g_sigint{false};
void signalHandler(int) { g_sigint = true; }

std::string narrow(const wchar_t* w) {
    if (!w) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::string narrow(const std::wstring& w) { return narrow(w.c_str()); }

std::string hresultString(HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

struct WinEp {
    IMMDevice* dev = nullptr;
    std::wstring id;
    std::string name;
    bool isLoopback = false;
    bool isDefault = false;
};

std::wstring getDefaultId(IMMDeviceEnumerator* en, EDataFlow flow) {
    IMMDevice* d = nullptr;
    std::wstring r;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &d))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(d->GetId(&id)) && id) r = id;
        if (id) CoTaskMemFree(id);
        d->Release();
    }
    return r;
}

std::wstring getId(IMMDevice* d) {
    LPWSTR id = nullptr;
    std::wstring r;
    if (SUCCEEDED(d->GetId(&id)) && id) r = id;
    if (id) CoTaskMemFree(id);
    return r;
}

std::string friendlyName(IMMDevice* d) {
    IPropertyStore* store = nullptr;
    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
            std::string r = narrow(v.pwszVal);
            PropVariantClear(&v);
            store->Release();
            return r;
        }
        PropVariantClear(&v);
        store->Release();
    }
    return "(dispositivo)";
}

void queryFormat(IMMDevice* d, double* rate, int* ch) {
    *rate = 0.0;
    *ch = 0;
    IAudioClient* ac = nullptr;
    if (SUCCEEDED(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&ac)))) {
        WAVEFORMATEX* wfx = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&wfx))) {
            *rate = static_cast<double>(wfx->nSamplesPerSec);
            *ch = static_cast<int>(wfx->nChannels);
            CoTaskMemFree(wfx);
        }
        ac->Release();
    }
}

void enumerateEndpoints(IMMDeviceEnumerator* en, std::vector<WinEp>& out) {
    out.clear();
    const std::wstring defCap = getDefaultId(en, eCapture);
    const std::wstring defRend = getDefaultId(en, eRender);

    for (int pass = 0; pass < 2; ++pass) {
        const EDataFlow flow = (pass == 0) ? eCapture : eRender;
        const bool loop = (pass == 1);
        IMMDeviceCollection* coll = nullptr;
        if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) continue;

        UINT n = 0;
        coll->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* d = nullptr;
            if (FAILED(coll->Item(i, &d))) continue;
            WinEp e;
            e.dev = d;
            e.id = getId(d);
            e.name = (loop ? "LOOPBACK: " : std::string()) + friendlyName(d);
            e.isLoopback = loop;
            e.isDefault = loop ? (e.id == defRend) : (e.id == defCap);
            out.push_back(e);
        }
        coll->Release();
    }
}

void convertToMono16(const BYTE* data, UINT32 frames, const WAVEFORMATEX* wfx,
                     std::vector<int16_t>& out) {
    out.resize(frames);
    const UINT32 ch = wfx->nChannels;
    const UINT32 bits = wfx->wBitsPerSample;
    const UINT32 block = wfx->nBlockAlign;

    bool isFloat = (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* ext =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (IsEqualGUID(ext->SubFormat, guidIeeeFloat)) isFloat = true;
    }

    for (UINT32 f = 0; f < frames; ++f) {
        const BYTE* frame = data + static_cast<size_t>(f) * block;
        float sum = 0.0f;
        for (UINT32 c = 0; c < ch; ++c) {
            float s = 0.0f;
            if (isFloat && bits == 32) {
                float v;
                std::memcpy(&v, frame + c * 4, 4);
                s = v;
            } else if (!isFloat && bits == 16) {
                int16_t v;
                std::memcpy(&v, frame + c * 2, 2);
                s = v / 32768.0f;
            } else if (!isFloat && bits == 24) {
                const BYTE* p = frame + c * 3;
                int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(p[2]) << 16);
                if (v & 0x800000) v -= 0x1000000;
                s = v / 8388608.0f;
            } else if (!isFloat && bits == 32) {
                int32_t v;
                std::memcpy(&v, frame + c * 4, 4);
                s = v / 2147483648.0f;
            } else if (!isFloat && bits == 8) {
                const int v = static_cast<int>(frame[c]) - 128;
                s = v / 128.0f;
            }
            sum += s;
        }
        sum /= static_cast<float>(ch);
        if (sum > 1.0f) sum = 1.0f;
        else if (sum < -1.0f) sum = -1.0f;
        out[f] = static_cast<int16_t>(sum * 32767.0f);
    }
}

}  // namespace

bool listDevices(std::vector<DeviceInfo>& out) {
    out.clear();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return false;

    IMMDeviceEnumerator* en = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    std::vector<WinEp> eps;
    enumerateEndpoints(en, eps);
    for (auto& e : eps) {
        DeviceInfo d;
        d.index = static_cast<int>(out.size());
        d.id = narrow(e.id);
        d.name = e.name;
        d.isLoopback = e.isLoopback;
        d.isDefaultInput = e.isDefault;
        queryFormat(e.dev, &d.defaultSampleRate, &d.maxInputChannels);
        out.push_back(d);
        e.dev->Release();
    }

    en->Release();
    CoUninitialize();
    return true;
}

bool recordAudio(const RecordOptions& opts, std::string& error) {
    g_sigint = false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInit = SUCCEEDED(hr);
    }
    if (!comInit) {
        error = "CoInitializeEx fallito: " + hresultString(hr);
        return false;
    }

    IMMDeviceEnumerator* en = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) {
        error = "Creazione enumeratore fallita: " + hresultString(hr);
        CoUninitialize();
        return false;
    }

    std::vector<WinEp> eps;
    enumerateEndpoints(en, eps);

    IMMDevice* target = nullptr;
    bool targetIsExtra = false;  // true se il target non proviene da eps
    bool isLoopback = false;
    std::string targetName;

    if (opts.deviceIndex >= 0) {
        if (opts.deviceIndex < static_cast<int>(eps.size())) {
            target = eps[opts.deviceIndex].dev;
            isLoopback = eps[opts.deviceIndex].isLoopback;
            targetName = eps[opts.deviceIndex].name;
        } else {
            error = "Indice dispositivo non valido (usa 'list' per vedere gli indici)";
            for (auto& e : eps) e.dev->Release();
            en->Release();
            CoUninitialize();
            return false;
        }
    }

    if (!target) {
        // Default: loopback dell'uscita di sistema (cattura ciò che Teams/Zoom riproducono).
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &target))) {
            isLoopback = true;
            targetIsExtra = true;
            targetName = "LOOPBACK: " + friendlyName(target);
        } else if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &target))) {
            isLoopback = false;
            targetIsExtra = true;
            targetName = friendlyName(target);
        }
    }
    if (!target) {
        error = "Nessun dispositivo audio trovato";
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    IAudioClient* ac = nullptr;
    hr = target->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&ac));
    if (FAILED(hr)) {
        error = "Attivazione IAudioClient fallita: " + hresultString(hr);
        if (targetIsExtra) target->Release();
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    WAVEFORMATEX* wfx = nullptr;
    hr = ac->GetMixFormat(&wfx);
    if (FAILED(hr)) {
        error = "GetMixFormat fallito: " + hresultString(hr);
        ac->Release();
        if (targetIsExtra) target->Release();
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    const DWORD flags = isLoopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000 /* 100 ms */, 0, wfx,
                        nullptr);
    if (FAILED(hr)) {
        error = "Initialize IAudioClient fallito: " + hresultString(hr) +
                (isLoopback ? " (il loopback richiede la shared mode)" : "");
        CoTaskMemFree(wfx);
        ac->Release();
        if (targetIsExtra) target->Release();
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    IAudioCaptureClient* cap = nullptr;
    hr = ac->GetService(IID_PPV_ARGS(&cap));
    if (FAILED(hr)) {
        error = "GetService(IAudioCaptureClient) fallito: " + hresultString(hr);
        CoTaskMemFree(wfx);
        ac->Release();
        if (targetIsExtra) target->Release();
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    if (FAILED(ac->Start())) {
        error = "Impossibile avviare la cattura audio";
        cap->Release();
        CoTaskMemFree(wfx);
        ac->Release();
        if (targetIsExtra) target->Release();
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    WavWriter wav;
    if (!wav.open(opts.outputPath, wfx->nSamplesPerSec, 1, 16)) {
        error = "Impossibile aprire il file di output " + opts.outputPath;
        ac->Stop();
        cap->Release();
        CoTaskMemFree(wfx);
        ac->Release();
        if (targetIsExtra) target->Release();
        for (auto& e : eps) e.dev->Release();
        en->Release();
        CoUninitialize();
        return false;
    }

    std::signal(SIGINT, signalHandler);

    std::atomic<bool> stop{false};
    std::thread inputThread;
    if (opts.durationSeconds <= 0) {
        inputThread = std::thread([&stop]() {
            std::string line;
            std::getline(std::cin, line);
            stop = true;
        });
        inputThread.detach();
    }

    tui::info("Dispositivo: " + targetName + (isLoopback ? " [loopback]" : " [microfono]"));
    tui::info("Formato: " + std::to_string(wfx->nSamplesPerSec) + " Hz, " +
              std::to_string(wfx->nChannels) + " canale/i -> salvato 16 bit mono");
    if (opts.durationSeconds > 0)
        tui::info("Durata: " + std::to_string(opts.durationSeconds) + " s (Ctrl+C per fermare prima)");
    else
        tui::info("Premi INVIO (o Ctrl+C) per fermare");
    tui::info("Output: " + opts.outputPath);
    tui::newline();

    std::vector<int16_t> mono;
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = true;
    UINT32 packetLen = 0;

    while (!stop.load() && !g_sigint.load()) {
        hr = cap->GetNextPacketSize(&packetLen);
        if (FAILED(hr)) {
            error = "GetNextPacketSize fallito: " + hresultString(hr);
            ok = false;
            break;
        }
        while (packetLen != 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD fl = 0;
            hr = cap->GetBuffer(&data, &frames, &fl, nullptr, nullptr);
            if (FAILED(hr)) {
                error = "GetBuffer fallito: " + hresultString(hr);
                ok = false;
                break;
            }
            if (fl & AUDCLNT_BUFFERFLAGS_SILENT) {
                mono.assign(frames, 0);
            } else {
                convertToMono16(data, frames, wfx, mono);
            }
            wav.write(mono.data(), static_cast<size_t>(frames) * sizeof(int16_t));
            cap->ReleaseBuffer(frames);
            cap->GetNextPacketSize(&packetLen);
        }
        if (!ok) break;
        const double elapsedNow =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (opts.durationSeconds > 0 && elapsedNow >= opts.durationSeconds) break;
        tui::progress("Registrazione in corso... " + fmtElapsed(elapsedNow) +
                      "  (INVIO o Ctrl+C per fermare)");
        Sleep(5);
    }
    tui::clearLine();

    ac->Stop();
    wav.close();
    cap->Release();
    CoTaskMemFree(wfx);
    ac->Release();
    if (targetIsExtra) target->Release();
    for (auto& e : eps) e.dev->Release();
    en->Release();
    CoUninitialize();

    std::signal(SIGINT, SIG_DFL);
    return ok;
}

#else  // ---------------------------------------------------------------------
// Linux / macOS: cattura via PortAudio (monitor PulseAudio/PipeWire, ecc.)
// ---------------------------------------------------------------------------

#include <portaudio.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_sigint{false};
void signalHandler(int) { g_sigint = true; }

}  // namespace

bool listDevices(std::vector<DeviceInfo>& out) {
    out.clear();
    PaError err = Pa_Initialize();
    if (err != paNoError) return false;

    const PaDeviceIndex defaultIn = Pa_GetDefaultInputDevice();
    const int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
        if (!di || di->maxInputChannels <= 0) continue;

        DeviceInfo d;
        d.index = i;
        d.name = di->name;
        d.maxInputChannels = di->maxInputChannels;
        d.defaultSampleRate = di->defaultSampleRate;
        d.isDefaultInput = (i == defaultIn);

        const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
        if (hai) d.name = std::string(hai->name) + " :: " + d.name;
        out.push_back(d);
    }
    Pa_Terminate();
    return true;
}

bool recordAudio(const RecordOptions& opts, std::string& error) {
    g_sigint = false;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        error = Pa_GetErrorText(err);
        return false;
    }

    PaDeviceIndex dev = opts.deviceIndex;
    if (dev < 0) {
        dev = Pa_GetDefaultInputDevice();
        if (dev == paNoDevice) {
            error = "Nessun dispositivo di input di default trovato";
            Pa_Terminate();
            return false;
        }
    }

    const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
    if (!di) {
        error = "Dispositivo non valido";
        Pa_Terminate();
        return false;
    }

    PaStreamParameters in;
    std::memset(&in, 0, sizeof(in));
    in.device = dev;
    in.channelCount = opts.channels;
    in.sampleFormat = paInt16;
    in.suggestedLatency = di->defaultLowInputLatency;
    in.hostApiSpecificStreamInfo = nullptr;

    const unsigned long framesPerBuffer = 1024;
    double rate = opts.sampleRate;
    PaStream* stream = nullptr;

    err = Pa_OpenStream(&stream, &in, nullptr, rate, framesPerBuffer, paClipOff, nullptr,
                        nullptr);
    if (err != paNoError) {
        double nativeRate = di->defaultSampleRate > 0 ? di->defaultSampleRate : 48000.0;
        err = Pa_OpenStream(&stream, &in, nullptr, nativeRate, framesPerBuffer, paClipOff,
                            nullptr, nullptr);
        if (err != paNoError) {
            error = std::string("Impossibile aprire lo stream: ") + Pa_GetErrorText(err);
            Pa_Terminate();
            return false;
        }
        rate = nativeRate;
    }

    const PaStreamInfo* si = Pa_GetStreamInfo(stream);
    if (si && si->sampleRate > 0) rate = si->sampleRate;

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        error = std::string("Impossibile avviare lo stream: ") + Pa_GetErrorText(err);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return false;
    }

    WavWriter wav;
    if (!wav.open(opts.outputPath, static_cast<uint32_t>(rate),
                  static_cast<uint16_t>(opts.channels), 16)) {
        error = "Impossibile aprire il file di output " + opts.outputPath;
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return false;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::atomic<bool> stop{false};
    std::thread inputThread;
    if (opts.durationSeconds <= 0) {
        inputThread = std::thread([&stop]() {
            std::string line;
            std::getline(std::cin, line);
            stop = true;
        });
        inputThread.detach();
    }

    tui::info("Dispositivo: [" + std::to_string(dev) + "] " + di->name);
    tui::info("Formato: " + std::to_string(rate) + " Hz, " + std::to_string(opts.channels) +
              " canale/i, 16 bit");
    if (opts.durationSeconds > 0)
        tui::info("Durata: " + std::to_string(opts.durationSeconds) + " s (Ctrl+C per fermare prima)");
    else
        tui::info("Premi INVIO (o Ctrl+C) per fermare");
    tui::info("Output: " + opts.outputPath);
    tui::newline();

    std::vector<int16_t> buffer(framesPerBuffer * opts.channels);
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = true;

    while (!stop.load() && !g_sigint.load()) {
        err = Pa_ReadStream(stream, buffer.data(), framesPerBuffer);
        if (err != paNoError) {
            if (err == paInputOverflowed) continue;
            error = std::string("Errore di lettura: ") + Pa_GetErrorText(err);
            ok = false;
            break;
        }
        const size_t bytes =
            static_cast<size_t>(framesPerBuffer) * opts.channels * sizeof(int16_t);
        if (!wav.write(buffer.data(), bytes)) {
            error = "Errore di scrittura sul file";
            ok = false;
            break;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (opts.durationSeconds > 0 && elapsed >= opts.durationSeconds) break;
        tui::progress("Registrazione in corso... " + fmtElapsed(elapsed) +
                      "  (INVIO o Ctrl+C per fermare)");
    }
    tui::clearLine();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    wav.close();

    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);

    return ok;
}

#endif
