// util.h — вспомогательные функции: строки, пути, форматирование, диалоги.
#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace util {

// ── Строки ────────────────────────────────────────────────────────────────
std::string  Utf8ToMulti(const std::wstring& s);
std::wstring MultiToUtf8(const std::string& s);
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

std::wstring Trim(const std::wstring& s);
std::wstring ToLower(const std::wstring& s);
std::wstring ToUpper(const std::wstring& s);
bool IEquals(const std::wstring& a, const std::wstring& b);
bool IContains(const std::wstring& hay, const std::wstring& needle);
bool StartsWith(const std::wstring& s, const std::wstring& p);
bool EndsWith(const std::wstring& s, const std::wstring& p);
std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to);
std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep);

// ── Форматирование ────────────────────────────────────────────────────────
std::wstring FormatBytes(uint64_t bytes);
std::wstring FormatSpeed(double bytesPerSec);
std::wstring FormatTime(int seconds);          // 3:07 / 1:02:03
std::wstring FormatEta(int seconds);           // "осталось 3 мин"
std::wstring FormatDateTime(time_t t);
std::wstring FormatDate(time_t t);
std::wstring FormatCount(int64_t v);           // 1 234 567 просмотров
std::wstring FormatDurationWords(int seconds);

// ── Файлы и пути ──────────────────────────────────────────────────────────
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring BaseName(const std::wstring& path);
std::wstring DirName(const std::wstring& path);
std::wstring StripExt(const std::wstring& path);
std::wstring FileExt(const std::wstring& path);
bool        FileExists(const std::wstring& path);
bool        DirExists(const std::wstring& path);
bool        EnsureDir(const std::wstring& path);
uint64_t    FileSize(const std::wstring& path);
std::wstring SanitizeFileName(const std::wstring& name, int maxLen = 150);
std::wstring ExeDir();
std::wstring AppDataDir();                     // %LOCALAPPDATA%\LOADUCK
std::wstring BinDir();                         // папка с yt-dlp/ffmpeg
std::wstring FindOnPath(const std::wstring& exeName);
std::wstring UniquePath(const std::wstring& path);   // (1), (2) ...
void        RevealInExplorer(const std::wstring& path);

// ── Диалоги / буфер обмена ────────────────────────────────────────────────
std::wstring SelectFolderDialog(HWND owner, const std::wstring& init);
std::wstring SelectFileDialog(HWND owner, const std::wstring& init, const std::wstring& filter);
std::wstring ClipboardText();
void        SetClipboardText(const std::wstring& text);

// ── URL ───────────────────────────────────────────────────────────────────
bool IsSupportedUrl(const std::wstring& url);
std::vector<std::wstring> ExtractUrls(const std::wstring& text);
std::wstring UrlHost(const std::wstring& url);
std::wstring CleanUrl(const std::wstring& url);

// ── Прочее ────────────────────────────────────────────────────────────────
uint64_t NowMillis();
std::wstring Quote(const std::wstring& s);   // экранирование аргумента командной строки

} // namespace util