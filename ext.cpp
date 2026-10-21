// ext.cpp — Native Messaging: регистрация хоста и приём запросов от расширения.
#include "ext.h"

#include "json.h"
#include "store.h"
#include "util.h"

#include <Windows.h>
#include <shellapi.h>

#include <fcntl.h>
#include <io.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace ext {
namespace {

// Ключи реестра, по которым Chrome-подобные браузеры ищут нативные хосты.
const wchar_t* kRegKeys[] = {
    L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.loaduck.host",
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.loaduck.host",
    L"Software\\Chromium\\NativeMessagingHosts\\com.loaduck.host",
    L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\com.loaduck.host",
    L"Software\\Opera Software\\Opera Stable\\NativeMessagingHosts\\com.loaduck.host",
};

// Chrome ограничивает сообщение от расширения 64 МБ; нам столько не нужно.
constexpr uint32_t kMaxMessage = 1u << 20;   // 1 МБ

Json Ok() {
    Json j = Json::object();
    j.set("ok", Json(true));
    return j;
}

Json Err(const char* code) {
    Json j = Json::object();
    j.set("ok", Json(false));
    j.set("error", Json(code));
    return j;
}

std::wstring CurrentExe() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

bool WriteAll(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) &&
              written == data.size();
    CloseHandle(h);
    return ok;
}

std::string ReadText(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string out;
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0)
        out.append(buf, got);
    CloseHandle(h);
    return out;
}

bool RegWrite(const wchar_t* sub, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr)
        != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)value.c_str(),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool RegRead(const wchar_t* sub, std::wstring& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    wchar_t buf[1024]{};
    DWORD size = sizeof buf;
    DWORD type = 0;
    LONG r = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE*)buf, &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS || type != REG_SZ) return false;
    value.assign(buf, wcslen(buf));
    return true;
}

bool RegDelete(const wchar_t* sub) {
    LONG r = RegDeleteKeyW(HKEY_CURRENT_USER, sub);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

// Читает "path" из манифеста, чтобы понять, указывает ли он на текущий exe.
std::wstring ManifestExe() {
    std::string text = ReadText(ManifestPath());
    if (text.empty()) return {};
    Json j;
    if (!Json::Parse(text, j) || !j.isObj()) return {};
    return util::Utf8ToWide(j["path"].asStr());
}

bool LaunchApp(const std::wstring& url) {
    std::wstring exe = CurrentExe();
    std::wstring args = url.empty() ? std::wstring()
                                    : (L"--ext-download \"" + url + L"\"");
    // ShellExecute, а не CreateProcess: Chrome может держать хост в job-объекте,
    // и дочерний процесс умер бы вместе с ним. Оболочка создаёт его отдельно.
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(),
                                args.empty() ? nullptr : args.c_str(),
                                util::ExeDir().c_str(), SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

// Строгая проверка хоста: util::IsSupportedUrl ищет подстроку, а здесь нужно
// совпадение домена или поддомена — чтобы site.com/youtube.com не считался YouTube.
bool HostAllowed(const std::wstring& url) {
    const size_t p = url.find(L"://");
    if (p == std::wstring::npos) return false;
    const size_t start = p + 3;
    const size_t end = url.find_first_of(L"/?#", start);
    std::wstring host = util::ToLower(
        url.substr(start, end == std::wstring::npos ? url.size() - start : end - start));

    if (size_t at = host.rfind(L'@'); at != std::wstring::npos) host = host.substr(at + 1);
    if (size_t colon = host.find(L':'); colon != std::wstring::npos) host = host.substr(0, colon);
    if (host.empty()) return false;

    static const wchar_t* kHosts[] = {
        L"youtube.com", L"youtu.be", L"youtube-nocookie.com",
        L"tiktok.com", L"tiktokcdn.com",
        L"instagram.com", L"twitter.com", L"x.com", L"facebook.com", L"fb.watch",
        L"rutube.ru", L"twitch.tv", L"soundcloud.com", L"bilibili.com",
        L"vimeo.com", L"dailymotion.com", L"likee.video", L"snackvideo.video",
        L"pixabay.com", L"reddit.com", L"imgur.com",
    };
    for (const wchar_t* h : kHosts) {
        if (host == h) return true;
        const size_t n = wcslen(h);
        if (host.size() > n && host[host.size() - n - 1] == L'.' &&
            host.compare(host.size() - n, n, h) == 0)
            return true;
    }
    return false;
}

bool ReadMessage(std::string& out) {    unsigned char hdr[4];
    if (fread(hdr, 1, 4, stdin) != 4) return false;
    const uint32_t len = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                         ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    if (len == 0 || len > kMaxMessage) return false;
    out.resize(len);
    return fread(out.data(), 1, len, stdin) == len;
}

void WriteMessage(const std::string& s) {
    const uint32_t len = (uint32_t)s.size();
    unsigned char hdr[4] = {
        (unsigned char)(len & 0xFF),
        (unsigned char)((len >> 8) & 0xFF),
        (unsigned char)((len >> 16) & 0xFF),
        (unsigned char)((len >> 24) & 0xFF),
    };
    fwrite(hdr, 1, 4, stdout);
    fwrite(s.data(), 1, s.size(), stdout);
    fflush(stdout);
}

// Разбирает запрос и выполняет его. Ничего не пишет на диск и не запускает
// оболочку: ссылка проверяется по белому списку сайтов и уходит как аргумент.
Json Handle(const std::string& raw) {
    Json req;
    if (!Json::Parse(raw, req) || !req.isObj()) return Err("bad-request");

    const std::string action = req["action"].asStr("download");

    Store store;
    store.Load();
    const Settings& st = store.Get();

    if (action == "ping") {
        Json j = Ok();
        j.set("enabled", Json(st.extensionEnabled));
        j.set("version", Json("1.0"));
        // Язык интерфейса LOADUCK — чтобы попап расширения говорил на том же языке.
        const char* code = st.lang == 1 ? "en" : st.lang == 2 ? "uk" : "ru";
        j.set("lang", Json(code));
        return j;
    }

    if (!st.extensionEnabled) return Err("disabled");

    if (action == "open") {
        if (!SendToRunning(L"", true) && !LaunchApp(L"")) return Err("host");
        return Ok();
    }

    std::string su = req["url"].asStr();
    if (su.size() < 10 || su.size() > 2048) return Err("unsupported");

    std::wstring url = util::Utf8ToWide(su);
    std::wstring low = util::ToLower(url);
    if (low.rfind(L"http://", 0) != 0 && low.rfind(L"https://", 0) != 0)
        return Err("unsupported");
    // Кавычка, пробел или управляющий символ сломали бы аргумент командной строки
    for (wchar_t c : url)
        if ((unsigned)c <= 0x20 || c == L'"' || c == L'<' || c == L'>')
            return Err("unsupported");
    if (!HostAllowed(url)) return Err("unsupported");

    // Срезаем utm-метки и прочий мусор — программе нужен только сам ролик
    std::wstring clean = util::CleanUrl(url);
    if (clean.empty()) clean = url;

    if (SendToRunning(clean, false)) return Ok();
    if (!LaunchApp(clean)) return Err("host");
    return Ok();
}

} // namespace

bool IsAllowedHost(const std::wstring& url) { return HostAllowed(url); }

std::wstring ManifestPath() {
    return util::PathJoin(util::PathJoin(util::AppDataDir(), L"native"),
                          std::wstring(kHostName) + L".json");
}

std::wstring ExtensionDir() {
    std::wstring dir = util::ExeDir();
    for (int i = 0; i < 6; ++i) {
        std::wstring cand = util::PathJoin(dir, L"extension");
        if (util::FileExists(util::PathJoin(cand, L"manifest.json"))) return cand;
        std::wstring up = util::DirName(dir);
        if (up.empty() || up == dir) break;
        dir = up;
    }
    std::wstring app = util::PathJoin(util::AppDataDir(), L"extension");
    if (util::FileExists(util::PathJoin(app, L"manifest.json"))) return app;
    return {};
}

bool IsHostMode() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; i < argc && !found; ++i) {
        std::wstring a = argv[i];
        if (util::IEquals(a, L"--nm-host")) found = true;
        // Chrome и Edge запускают хост без своих аргументов, но добавляют
        // --parent-window=<hwnd>.
        else if (a.rfind(L"--parent-window=", 0) == 0) found = true;
    }
    if (argv) LocalFree(argv);
    if (found) return true;

    // Браузер подключает stdin каналом; при обычном запуске (двойной щелчок,
    // ярлык) stdin — пустой/недействительный дескриптор. Это последняя
    // страховка для версий браузера без --parent-window.
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in && in != INVALID_HANDLE_VALUE && GetFileType(in) == FILE_TYPE_PIPE) {
        DWORD avail = 0;
        if (PeekNamedPipe(in, nullptr, 0, nullptr, &avail, nullptr) && avail >= 4)
            return true;
    }
    return false;
}

int RunHost() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    std::string raw;
    if (!ReadMessage(raw)) return 1;

    Json resp;
    try {
        resp = Handle(raw);
    } catch (...) {
        resp = Err("bad-request");
    }
    WriteMessage(resp.dump());
    return 0;
}

bool IsRegistered() {
    const std::wstring manifest = ManifestPath();
    if (!util::FileExists(manifest)) return false;
    // Если программу перенесли, запись осталась старой — нужен повторный «Подключить»
    if (!util::IEquals(ManifestExe(), CurrentExe())) return false;

    for (const wchar_t* key : kRegKeys) {
        std::wstring value;
        if (RegRead(key, value) && util::IEquals(value, manifest)) return true;
    }
    return false;
}

bool Install() {
    util::EnsureDir(util::PathJoin(util::AppDataDir(), L"native"));

    const std::wstring exe = CurrentExe();
    Json j = Json::object();
    j.set("name", Json(util::WideToUtf8(kHostName)));
    j.set("description", Json("LOADUCK — загрузка видео из браузера"));
    j.set("path", Json(util::WideToUtf8(exe)));
    j.set("type", Json("stdio"));
    Json origins = Json::array();
    origins.push(Json("chrome-extension://" + util::WideToUtf8(kExtensionId) + "/"));
    j.set("allowed_origins", origins);

    const std::wstring manifest = ManifestPath();
    if (!WriteAll(manifest, j.dump(2))) return false;

    bool any = false;
    for (const wchar_t* key : kRegKeys) {
        if (RegWrite(key, manifest)) any = true;
    }
    return any;
}

bool Uninstall() {
    for (const wchar_t* key : kRegKeys) RegDelete(key);
    const std::wstring manifest = ManifestPath();
    if (util::FileExists(manifest)) DeleteFileW(manifest.c_str());
    return true;
}

bool SendToRunning(const std::wstring& url, bool showWindow) {
    HWND wnd = FindWindowW(L"LOADUCK_MAIN", nullptr);
    if (!wnd) return false;

    std::wstring data = url;
    data.push_back(L'\0');

    COPYDATASTRUCT cds{};
    cds.dwData = showWindow ? kCopyOpen : kCopyDownload;
    cds.cbData = (DWORD)(data.size() * sizeof(wchar_t));
    cds.lpData = (void*)data.c_str();

    DWORD_PTR result = 0;
    LRESULT sent = SendMessageTimeoutW(wnd, WM_COPYDATA, 0, (LPARAM)&cds,
                                       SMTO_ABORTIFHUNG | SMTO_BLOCK, 4000, &result);
    return sent != 0;
}

} // namespace ext
