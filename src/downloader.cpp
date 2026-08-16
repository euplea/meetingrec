#include "downloader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace {

#ifdef _WIN32
std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
#endif

}  // namespace

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Download via WinHTTP (Windows nativo).
// ---------------------------------------------------------------------------
bool downloadFile(const std::string& url, const std::string& dest, uint64_t expectedSize,
                  const std::function<void(uint64_t, uint64_t)>& progressCb,
                  std::string& error) {
    URL_COMPONENTS uc;
    std::memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    const std::wstring wurl = wide(url);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        error = "URL non valido: " + url;
        return false;
    }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"meetingrec-dl/1.1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "WinHttpOpen fallito (0x" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WinHttpSetTimeouts(hSession, 0, 30000, 30000, 0);  // nessun timeout totale

    // Segui i redirect (HF resolve -> CDN).
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect) {
        error = "WinHttpConnect fallito (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        error = "WinHttpOpenRequest fallito (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpSendRequest(hRequest, nullptr, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        error = "Richiesta HTTP fallita (0x" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD status = 0;
    DWORD statusLen = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen,
                        WINHTTP_NO_HEADER_INDEX);
    if (status >= 400) {
        error = "HTTP " + std::to_string(status) + " per " + url;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        error = "Impossibile creare il file " + dest;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    uint64_t total = 0;
    DWORD avail = 0;
    std::vector<char> buf(64 * 1024);
    for (;;) {
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        const DWORD toRead = (std::min)(avail, static_cast<DWORD>(buf.size()));
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf.data(), toRead, &read) || read == 0) break;
        out.write(buf.data(), read);
        total += read;
        if (progressCb) progressCb(total, expectedSize);
    }
    out.close();

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (expectedSize > 0 && total != expectedSize) {
        std::remove(dest.c_str());
        error = "Download incompleto: attesi " + std::to_string(expectedSize) +
                " byte, ricevuti " + std::to_string(total);
        return false;
    }
    if (total == 0) {
        std::remove(dest.c_str());
        error = "Nessun dato ricevuto da " + url;
        return false;
    }
    return true;
}

#else
// ---------------------------------------------------------------------------
// Download via libcurl (Linux/macOS).
// ---------------------------------------------------------------------------

struct WriteCtx {
    std::ofstream* out = nullptr;
    uint64_t total = 0;
    uint64_t expected = 0;
    const std::function<void(uint64_t, uint64_t)>* cb = nullptr;
};

size_t curlWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
    WriteCtx* ctx = static_cast<WriteCtx*>(userdata);
    const size_t bytes = size * nmemb;
    ctx->out->write(ptr, static_cast<std::streamsize>(bytes));
    ctx->total += bytes;
    if (ctx->cb) (*ctx->cb)(ctx->total, ctx->expected);
    return bytes;
}

int curlProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t,
                 curl_off_t) {
    WriteCtx* ctx = static_cast<WriteCtx*>(userdata);
    if (ctx->cb) {
        const uint64_t total = ctx->expected ? ctx->expected : static_cast<uint64_t>(dltotal);
        (*ctx->cb)(static_cast<uint64_t>(dlnow), total);
    }
    return 0;
}

bool downloadFile(const std::string& url, const std::string& dest, uint64_t expectedSize,
                  const std::function<void(uint64_t, uint64_t)>& progressCb,
                  std::string& error) {
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        error = "Impossibile creare il file " + dest;
        return false;
    }

    WriteCtx ctx;
    ctx.out = &out;
    ctx.expected = expectedSize;
    ctx.cb = progressCb ? &progressCb : nullptr;

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init() fallito";
        out.close();
        std::remove(dest.c_str());
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    const CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    out.close();

    if (res != CURLE_OK) {
        std::remove(dest.c_str());
        error = std::string("Download fallito: ") + curl_easy_strerror(res);
        return false;
    }
    if (httpCode >= 400) {
        std::remove(dest.c_str());
        error = "HTTP " + std::to_string(httpCode) + " per " + url;
        return false;
    }
    if (expectedSize > 0 && ctx.total != expectedSize) {
        std::remove(dest.c_str());
        error = "Download incompleto: attesi " + std::to_string(expectedSize) +
                " byte, ricevuti " + std::to_string(ctx.total);
        return false;
    }
    if (ctx.total == 0) {
        std::remove(dest.c_str());
        error = "Nessun dato ricevuto da " + url;
        return false;
    }
    return true;
}
#endif
