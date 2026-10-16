#include "util.h"

#include <Windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <shellapi.h>
#include <cwctype>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

#pragma comment(lib, "shlwapi.lib")

namespace util {

// ── Строки ────────────────────────────────────────────────────────────────

std::string Utf8ToMulti(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring MultiToUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring ToLower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return r;
}

std::wstring ToUpper(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return (wchar_t)towupper(c); });
    return r;
}

namespace {
// Каталоги профилей, которые yt-dlp умеет читать
struct BrowserProfile { const wchar_t* name; const wchar_t* env; const wchar_t* sub; };
const BrowserProfile kBrowsers[] = {
    { L"chrome",  L"LOCALAPPDATA", L"Google\\Chrome\\User Data" },
    { L"edge",    L"LOCALAPPDATA", L"Microsoft\\Edge\\User Data" },
    { L"brave",   L"LOCALAPPDATA", L"BraveSoftware\\Brave-Browser\\User Data" },
    { L"firefox", L"LOCALAPPDATA", L"Mozilla\\Firefox\\Profiles" },
    { L"opera",   L"APPDATA",      L"Opera Software\\Opera Stable" },
};

std::wstring EnvDir(const wchar_t* name) {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return buf;
}
} // namespace

bool BrowserInstalled(const std::wstring& browser) {
    std::wstring b = ToLower(Trim(browser));
    for (const BrowserProfile& p : kBrowsers) {
        if (b != p.name) continue;
        std::wstring root = EnvDir(p.env);
        if (root.empty()) return false;
        // Профиль браузера — это каталог, а не файл
        return DirExists(PathJoin(root, p.sub));
    }
    return false;
}

std::wstring FirstInstalledBrowser() {
    for (const BrowserProfile& p : kBrowsers)
        if (BrowserInstalled(p.name)) return p.name;
    return {};
}

bool IEquals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (towlower(a[i]) != towlower(b[i])) return false;
    return true;
}

bool IContains(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring h = ToLower(hay), n = ToLower(needle);
    return h.find(n) != std::wstring::npos;
}

bool StartsWith(const std::wstring& s, const std::wstring& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool EndsWith(const std::wstring& s, const std::wstring& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

// ── Форматирование ────────────────────────────────────────────────────────

static std::wstring GroupDigits(int64_t v) {
    std::wstring s = std::to_wstring(v);
    std::wstring r;
    int cnt = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        r.push_back(s[i]);
        if (++cnt % 3 == 0 && i > 0) r.push_back(L' ');
    }
    std::reverse(r.begin(), r.end());
    return r;
}

std::wstring FormatBytes(uint64_t bytes) {
    if (bytes == 0) return L"0 Б";
    const wchar_t* units[] = { L"Б", L"КБ", L"МБ", L"ГБ", L"ТБ" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::wostringstream os;
    if (u == 0) os << (long long)v << L" " << units[u];
    else {
        os << std::fixed << std::setprecision(v < 10 ? 2 : (v < 100 ? 1 : 0)) << v << L" " << units[u];
    }
    return os.str();
}

std::wstring FormatSpeed(double bytesPerSec) {
    if (bytesPerSec <= 0) return L"—";
    std::wstring b = FormatBytes((uint64_t)bytesPerSec);
    return b + L"/с";
}

std::wstring FormatTime(int seconds) {
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    std::wostringstream os;
    if (h > 0) os << h << L":" << std::setfill(L'0') << std::setw(2) << m << L":" << std::setw(2) << s;
    else os << m << L":" << std::setfill(L'0') << std::setw(2) << s;
    return os.str();
}

std::wstring FormatEta(int seconds) {
    if (seconds <= 0) return L"осталось меньше минуты";
    if (seconds < 60) return L"осталось " + std::to_wstring(seconds) + L" с";
    if (seconds < 3600) return L"осталось " + std::to_wstring(seconds / 60) + L" мин";
    return L"осталось " + FormatTime(seconds);
}

std::wstring FormatDateTime(time_t t) {
    struct tm tmv{};
    localtime_s(&tmv, &t);
    std::wostringstream os;
    os << std::setfill(L'0') << std::setw(2) << tmv.tm_mday << L"." << std::setw(2) << (tmv.tm_mon + 1) << L"."
       << (tmv.tm_year + 1900) << L", " << std::setw(2) << tmv.tm_hour << L":" << std::setw(2) << tmv.tm_min;
    return os.str();
}

std::wstring FormatDate(time_t t) {
    struct tm tmv{};
    localtime_s(&tmv, &t);
    std::wostringstream os;
    os << std::setfill(L'0') << std::setw(2) << tmv.tm_mday << L"." << std::setw(2) << (tmv.tm_mon + 1) << L"."
       << (tmv.tm_year + 1900);
    return os.str();
}

std::wstring FormatCount(int64_t v) {
    return GroupDigits(v);
}

std::wstring FormatDurationWords(int seconds) {
    if (seconds <= 0) return L"0 с";
    int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    std::wstring r;
    if (h) r += std::to_wstring(h) + L" ч ";
    if (m) r += std::to_wstring(m) + L" мин ";
    if (s && !h) r += std::to_wstring(s) + L" с";
    return Trim(r);
}

// ── Файлы и пути ──────────────────────────────────────────────────────────

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (b.size() > 1 && b[1] == L':') return b; // абсолютный путь
    std::wstring r = a;
    if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
    r += b;
    return r;
}

std::wstring BaseName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring DirName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"" : path.substr(0, p);
}

std::wstring StripExt(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return path;
    return path.substr(0, dot);
}

std::wstring FileExt(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    return path.substr(dot + 1);
}

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDir(const std::wstring& path) {
    if (path.empty()) return false;
    if (DirExists(path)) return true;
    return SHCreateDirectoryExW(nullptr, path.c_str(), nullptr) == S_OK || DirExists(path);
}

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fd{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fd)) return 0;
    return ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
}

std::wstring SanitizeFileName(const std::wstring& name, int maxLen) {
    static const wchar_t bad[] = L"\\/:*?\"<>|\r\n\t";
    std::wstring r;
    for (wchar_t c : name) {
        if (wcschr(bad, c)) r.push_back(L'_');
        else if (c < 32) r.push_back(L'_');
        else r.push_back(c);
    }
    r = Trim(r);
    if (r.empty()) r = L"video";
    if ((int)r.size() > maxLen) r.resize(maxLen);
    return r;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return DirName(std::wstring(buf, n));
}

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring base(buf, n);
    std::wstring dir = PathJoin(base, L"LOADUCK");
    EnsureDir(dir);
    return dir;
}

std::wstring BinDir() {
    // 1) bin рядом с exe, 2..4) bin на несколько уровней выше, 5) путь, зашитый при сборке
    std::wstring d = PathJoin(ExeDir(), L"bin");
    if (DirExists(d)) return d;
    std::wstring cur = ExeDir();
    for (int i = 0; i < 4; ++i) {
        cur = DirName(cur);
        if (cur.empty()) break;
        std::wstring up = PathJoin(cur, L"bin");
        if (DirExists(up)) return up;
    }
#ifdef LOADUCK_BIN_DIR
    std::wstring compiled = LOADUCK_BIN_DIR;
    if (DirExists(compiled)) return compiled;
    return compiled;
#else
    return d;
#endif
}

std::wstring FindOnPath(const std::wstring& exeName) {
    wchar_t buf[32768]{};
    DWORD n = GetEnvironmentVariableW(L"PATH", buf, 32768);
    if (n == 0 || n >= 32768) return {};
    std::wstring path(buf, n);
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t sep = path.find(L';', pos);
        std::wstring dir = path.substr(pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos);
        if (!dir.empty()) {
            std::wstring full = PathJoin(dir, exeName);
            if (FileExists(full)) return full;
        }
        if (sep == std::wstring::npos) break;
        pos = sep + 1;
    }
    return {};
}

std::wstring UniquePath(const std::wstring& path) {
    if (!FileExists(path)) return path;
    std::wstring dir = DirName(path), base = StripExt(BaseName(path)), ext = FileExt(path);
    for (int i = 1; i < 1000; ++i) {
        std::wstring cand = PathJoin(dir, base + L" (" + std::to_wstring(i) + L")" + (ext.empty() ? L"" : L"." + ext));
        if (!FileExists(cand)) return cand;
    }
    return path;
}

void RevealInExplorer(const std::wstring& path) {
    std::wstring dir = DirExists(path) ? path : DirName(path);
    std::wstring arg = L"/select,\"" + path + L"\"";
    if (!FileExists(path)) arg = L"\"" + dir + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

// ── Диалоги ───────────────────────────────────────────────────────────────

std::wstring SelectFolderDialog(HWND owner, const std::wstring& init) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Выберите папку для загрузок";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t buf[MAX_PATH]{};
    SHGetPathFromIDListW(pidl, buf);
    CoTaskMemFree(pidl);
    return buf;
}

std::wstring SelectFileDialog(HWND owner, const std::wstring& init, const std::wstring& filter) {
    std::vector<wchar_t> buf(4096, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.empty() ? L"Все файлы (*.*)\0*.*\0\0" : filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    std::wstring dir = init;
    if (!dir.empty() && !DirExists(dir)) dir = DirName(dir);
    ofn.lpstrInitialDir = dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return {};
    return buf.data();
}

std::wstring ClipboardText() {
    if (!OpenClipboard(nullptr)) return {};
    wchar_t* p = nullptr;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) p = (wchar_t*)GlobalLock(h);
    std::wstring r = p ? std::wstring(p) : L"";
    if (h) GlobalUnlock(h);
    CloseClipboard();
    return r;
}

void SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* p = GlobalLock(g);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

// ── URL ───────────────────────────────────────────────────────────────────

bool IsSupportedUrl(const std::wstring& url) {
    std::wstring u = ToLower(Trim(url));
    if (u.empty()) return false;
    // Только поддерживаемые сервисы (российские площадки исключены по требованию)
    static const wchar_t* hosts[] = {
        L"youtube.com", L"youtu.be", L"music.youtube.com", L"youtube-nocookie.com",
        L"tiktok.com", L"vt.tiktok.com", L"vm.tiktok.com", L"m.tiktok.com",
        L"instagram.com", L"twitter.com", L"x.com", L"facebook.com", L"fb.watch",
        L"rutube.ru", L"twitch.tv", L"soundcloud.com",
        L"bilibili.com", L"vimeo.com", L"dailymotion.com", L"likee.video",
        L"tiktokcdn.com", L"snackvideo", L"pixabay.com", L"reddit.com", L"imgur.com"
    };
    for (const wchar_t* h : hosts)
        if (u.find(h) != std::wstring::npos) return true;
    return false;
}

std::vector<std::wstring> ExtractUrls(const std::wstring& text) {
    std::vector<std::wstring> out;
    std::wstring cur;
    bool valid = false;
    auto flush = [&]() {
        std::wstring u = Trim(cur);
        if (!u.empty() && util::IsSupportedUrl(u)) out.push_back(u);
        cur.clear(); valid = false;
    };
    for (wchar_t c : text) {
        if (iswspace(c) || c == L'"' || c == L'\'' || c == L'<' || c == L'>' || c == L',' || c == L'\n' || c == L'\r') {
            flush();
        } else {
            if (cur.empty()) {
                valid = (c == L'h' || c == L'H');
            }
            if (valid) cur.push_back(c);
        }
    }
    flush();
    return out;
}

std::wstring UrlHost(const std::wstring& url) {
    size_t p = url.find(L"://");
    if (p == std::wstring::npos) return {};
    size_t s = p + 3, e = url.find_first_of(L"/?#", s);
    std::wstring host = url.substr(s, e == std::wstring::npos ? std::wstring::npos : e - s);
    size_t at = host.find(L'@');
    if (at != std::wstring::npos) host = host.substr(at + 1);
    return util::ToLower(host);
}

std::wstring CleanUrl(const std::wstring& url) {
    std::wstring u = Trim(url);
    while (!u.empty() && (u.back() == L'\n' || u.back() == L'\r' || u.back() == L' ')) u.pop_back();
    if (u.find(L"://") == std::wstring::npos) u = L"https://" + u;
    return u;
}

// ── Прочее ────────────────────────────────────────────────────────────────

uint64_t NowMillis() {
    return (uint64_t)GetTickCount64();
}

std::wstring Quote(const std::wstring& s) {
    std::wstring r = L"\"";
    for (wchar_t c : s) {
        if (c == L'\\') r += L"\\\\";
        else if (c == L'"') r += L"\\\"";
        else r.push_back(c);
    }
    r += L'"';
    return r;
}

} // namespace util