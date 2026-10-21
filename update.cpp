// update.cpp — проверка релиза и загрузка новой версии LOADUCK с GitHub.
#include "update.h"
#include "version.h"
#include "json.h"
#include "util.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace update {

namespace {

constexpr wchar_t kApiUrl[] =
    L"https://api.github.com/repos/slipptech-tech/LOADUCK/releases/latest";

std::wstring Trim(std::wstring s) {
    auto ws = [](wchar_t c) { return std::iswspace(c) != 0; };
    while (!s.empty() && ws(s.front())) s.erase(s.begin());
    while (!s.empty() && ws(s.back())) s.pop_back();
    return s;
}

using DataSink = std::function<bool(const char*, DWORD)>;

struct HttpResult {
    bool ok = false;
    DWORD status = 0;
    uint64_t total = 0;
    std::wstring error;
};

// Одна HTTP-операция: WinHTTP сам обрабатывает HTTPS, прокси и редиректы.
// Данные отдаются по частям, чтобы большие файлы не держались в памяти.
HttpResult HttpGetStream(const std::wstring& url, int timeoutMs,
                         const wchar_t* extraHeaders, const DataSink& sink) {
    HttpResult res;

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)std::size(host) - 1;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)std::size(path) - 1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) {
        res.error = L"bad url";
        return res;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS || !host[0]) {
        res.error = L"https required";
        return res;
    }

    std::wstring target(path, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength)
        target.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (target.empty()) target = L"/";

    HINTERNET session = WinHttpOpen(L"LOADUCK/" LOADUCK_VERSION L" (Windows 10)",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(L"LOADUCK/" LOADUCK_VERSION L" (Windows 10)",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = L"network init failed";
        return res;
    }

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort ? uc.nPort : 443, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;

    if (!request) {
        res.error = L"connection failed";
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return res;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (extraHeaders && *extraHeaders)
        WinHttpAddRequestHeaders(request, extraHeaders, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        res.error = L"request failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return res;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    res.status = status;
    if (status != 200) {
        res.error = L"HTTP " + std::to_wstring(status);
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return res;
    }

    wchar_t lengthBuf[32] = {};
    DWORD lengthSize = sizeof(lengthBuf);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, lengthBuf, &lengthSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        wchar_t* end = nullptr;
        unsigned long long parsed = _wcstoui64(lengthBuf, &end, 10);
        if (end && end != lengthBuf) res.total = (uint64_t)parsed;
    }

    bool sinkOk = true;
    bool readOk = true;
    DWORD available = 0;
    for (;;) {
        if (!WinHttpQueryDataAvailable(request, &available)) {
            readOk = false;
            break;
        }
        if (available == 0) break;

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
            readOk = false;
            break;
        }
        if (!sink(chunk.data(), read)) {
            sinkOk = false;
            break;
        }
    }

    res.ok = sinkOk && readOk;
    if (!readOk) res.error = L"connection interrupted";
    else if (!sinkOk) res.error = L"write failed";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return res;
}

std::string HttpGet(const std::wstring& url, int timeoutMs) {
    std::string body;
    HttpResult result = HttpGetStream(
        url, timeoutMs,
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n",
        [&](const char* data, DWORD size) {
            if (body.size() + size > 512 * 1024) return false;
            body.append(data, size);
            return true;
        });
    return result.ok ? body : std::string();
}

bool LooksLikeExe(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    char magic[2] = {};
    file.read(magic, 2);
    return file.gcount() == 2 && magic[0] == 'M' && magic[1] == 'Z';
}

void CleanupOldDownloads() {
    wchar_t dir[MAX_PATH] = {};
    DWORD len = GetTempPathW((DWORD)std::size(dir), dir);
    if (len == 0 || len >= std::size(dir)) return;

    std::wstring pattern = std::wstring(dir, len) + L"LOADUCK_update_*.exe";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = std::wstring(dir, len) + fd.cFileName;
        DeleteFileW(path.c_str());
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

bool DownloadExe(const std::wstring& url, const std::wstring& path,
                 uint64_t expectedSize, DownloadState& state) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::lock_guard<std::mutex> lk(state.mu);
        state.error = L"cannot create temporary file";
        return false;
    }

    uint64_t received = 0;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        state.total = expectedSize;
    }

    uint64_t total = expectedSize;
    HttpResult result = HttpGetStream(
        url, 8000, L"Accept: application/octet-stream\r\n",
        [&](const char* data, DWORD size) {
            file.write(data, size);
            if (!file) return false;
            received += size;
            std::lock_guard<std::mutex> lk(state.mu);
            state.received = received;
            state.total = total;
            return true;
        });
    file.close();

    // Content-Length приходит уже с конечного адреса после редиректа GitHub.
    if (result.total > 0) total = result.total;

    bool ok = result.ok && received > 0;
    if (ok && total > 0 && received != total) ok = false;
    if (ok && expectedSize > 0 && received != expectedSize) ok = false;
    if (ok && !LooksLikeExe(path)) ok = false;

    if (!ok) {
        DeleteFileW(path.c_str());
        std::lock_guard<std::mutex> lk(state.mu);
        state.error = result.status ? result.error : L"download failed";
        if (state.error.empty()) state.error = L"download failed";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(state.mu);
        state.received = received;
        state.total = total ? total : received;
    }
    return true;
}

bool ParseRelease(const std::string& body, ReleaseInfo& out) {
    Json root;
    if (!Json::Parse(body, root)) return false;

    out.tag = NormalizeTag(util::Utf8ToWide(root["tag_name"].asStr()));
    if (out.tag.empty()) return false;

    const Json& assets = root["assets"];
    for (size_t i = 0; i < assets.size(); ++i) {
        const Json& asset = assets[i];
        std::wstring name = util::Utf8ToWide(asset["name"].asStr());
        if (!util::IEquals(name, L"LOADUCK.exe")) continue;
        out.exeUrl = util::Utf8ToWide(asset["browser_download_url"].asStr());
        out.exeSize = (uint64_t)asset["size"].asNum();
        break;
    }
    return true;
}

std::wstring PsQuote(const std::wstring& s) {
    return L"'" + util::ReplaceAll(s, L"'", L"''") + L"'";
}

std::wstring Base64(const std::vector<BYTE>& data) {
    static const wchar_t* alphabet =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        UINT32 v = ((UINT32)data[i] << 16) | ((UINT32)data[i + 1] << 8) | data[i + 2];
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(alphabet[(v >> 6) & 63]);
        out.push_back(alphabet[v & 63]);
        i += 3;
    }
    if (i < data.size()) {
        UINT32 v = (UINT32)data[i] << 16;
        out.push_back(alphabet[(v >> 18) & 63]);
        if (i + 1 < data.size()) {
            v |= (UINT32)data[i + 1] << 8;
            out.push_back(alphabet[(v >> 12) & 63]);
            out.push_back(alphabet[(v >> 6) & 63]);
            out.push_back(L'=');
        } else {
            out.push_back(alphabet[(v >> 12) & 63]);
            out.push_back(L'=');
            out.push_back(L'=');
        }
    }
    return out;
}

} // namespace

std::wstring LocalVersion() {
    return LOADUCK_VERSION;
}

std::wstring NormalizeTag(std::wstring tag) {
    tag = Trim(std::move(tag));

    // Ищем первое число: теги бывают "v1.4.4", "1.4.4" или "release-1.4.4".
    size_t start = 0;
    while (start < tag.size() && !std::iswdigit(tag[start])) ++start;
    if (start >= tag.size()) return tag;

    std::wstring number;
    bool lastDot = false;
    for (size_t i = start; i < tag.size(); ++i) {
        wchar_t c = tag[i];
        if (std::iswdigit(c)) {
            number.push_back(c);
            lastDot = false;
        } else if (c == L'.' && !lastDot && !number.empty()) {
            number.push_back(c);
            lastDot = true;
        } else {
            break;
        }
    }
    while (!number.empty() && number.back() == L'.') number.pop_back();
    return number.empty() ? tag : number;
}

int CompareVersions(const std::wstring& a, const std::wstring& b) {
    auto parts = [](const std::wstring& v) {
        std::vector<int> out;
        std::wstring cur;
        for (wchar_t c : v) {
            if (std::iswdigit(c)) cur.push_back(c);
            else {
                if (!cur.empty()) {
                    out.push_back(_wtoi(cur.c_str()));
                    cur.clear();
                }
                if (out.size() >= 4) break;
            }
        }
        if (!cur.empty() && out.size() < 4) out.push_back(_wtoi(cur.c_str()));
        while (out.size() < 3) out.push_back(0);
        return out;
    };

    std::vector<int> va = parts(a), vb = parts(b);
    for (size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
        int x = i < va.size() ? va[i] : 0;
        int y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool FetchLatestRelease(ReleaseInfo& out, int timeoutMs) {
    std::string body = HttpGet(kApiUrl, timeoutMs);
    if (body.empty()) return false;
    return ParseRelease(body, out);
}

std::wstring FetchLatestTag(int timeoutMs) {
    ReleaseInfo info;
    return FetchLatestRelease(info, timeoutMs) ? info.tag : std::wstring();
}

std::shared_ptr<DownloadState> StartExeDownload() {
    auto state = std::make_shared<DownloadState>();

    std::thread([state] {
        CleanupOldDownloads();

        ReleaseInfo release;
        const bool found = FetchLatestRelease(release, 5000);
        if (!found) {
            std::lock_guard<std::mutex> lk(state->mu);
            state->error = L"release request failed";
            state->done = true;
            return;
        }
        if (release.exeUrl.empty()) {
            std::lock_guard<std::mutex> lk(state->mu);
            state->error = L"LOADUCK.exe not found in the latest release";
            state->done = true;
            return;
        }

        wchar_t tempDir[MAX_PATH] = {};
        DWORD tempLen = GetTempPathW((DWORD)std::size(tempDir), tempDir);
        std::wstring path = (tempLen > 0 && tempLen < std::size(tempDir))
            ? std::wstring(tempDir, tempLen)
            : std::wstring(L".\\");
        path += L"LOADUCK_update_" + std::to_wstring(GetCurrentProcessId()) + L".exe";

        bool ok = DownloadExe(release.exeUrl, path, release.exeSize, *state);
        if (ok) {
            std::lock_guard<std::mutex> lk(state->mu);
            state->path = path;
        }

        std::lock_guard<std::mutex> lk(state->mu);
        state->ok = ok;
        state->done = true;
    }).detach();

    return state;
}

bool ApplyUpdateAndRestart(const std::wstring& newExe) {
    if (newExe.empty() || !util::FileExists(newExe)) return false;

    wchar_t appPath[MAX_PATH] = {};
    DWORD appLen = GetModuleFileNameW(nullptr, appPath, (DWORD)std::size(appPath));
    if (appLen == 0 || appLen >= std::size(appPath)) return false;

    // PowerShell ждёт завершения LOADUCK, копирует новый EXE с повторами
    // (антивирус может не сразу отпустить файл) и запускает его заново.
    std::wstring script =
        L"$ErrorActionPreference='SilentlyContinue';"
        L"Start-Sleep -Milliseconds 600;"
        L"Wait-Process -Id " + std::to_wstring(GetCurrentProcessId()) + L";"
        L"$done=$false;"
        L"for($i=0;$i -lt 40;$i++){"
        L"  try { Copy-Item -LiteralPath " + PsQuote(newExe) +
        L" -Destination " + PsQuote(appPath) + L" -Force -ErrorAction Stop;"
        L"        $done=$true; break }"
        L"  catch { Start-Sleep -Milliseconds 500 }"
        L"};"
        L"Start-Process -FilePath " + PsQuote(appPath) + L";"
        L"Start-Sleep -Milliseconds 700;"
        L"Remove-Item -LiteralPath " + PsQuote(newExe) + L" -Force;";

    std::vector<BYTE> bytes(script.size() * sizeof(wchar_t));
    if (!bytes.empty())
        memcpy(bytes.data(), script.data(), bytes.size());
    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand " +
        Base64(bytes);

    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    DWORD flags = CREATE_NO_WINDOW;
    BOOL launched = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                   flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
    if (!launched)
        launched = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                  flags, nullptr, nullptr, &si, &pi);
    if (!launched) return false;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::shared_ptr<VersionProbe> StartProbe() {
    auto probe = std::make_shared<VersionProbe>();
    std::thread([probe] {
        std::wstring tag = FetchLatestTag(4000);
        std::lock_guard<std::mutex> lk(probe->mu);
        probe->latest = tag;
        probe->ok = !tag.empty();
        probe->done = true;
    }).detach();
    return probe;
}

} // namespace update
