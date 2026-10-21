// update.h — проверка последнего релиза LOADUCK на GitHub.
//
// Сравниваются номера версий (tag_name последнего релиза против
// LOADUCK_VERSION), а не хеши файлов: хеш нужен только апдейтер,
// чтобы понять, изменился ли сам EXE.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace update {

// Версия, вкомпилированная в EXE.
std::wstring LocalVersion();

// Приводит тег релиза к виду "1.4.4" (срезает v, пробелы, хвосты).
std::wstring NormalizeTag(std::wstring tag);

// > 0 — a новее b, < 0 — b новее a, 0 — равны. Сравнивает по числам:
// 1.10.0 > 1.9.9. Нечисловые хвосты (-beta) отбрасываются.
int CompareVersions(const std::wstring& a, const std::wstring& b);

struct VersionProbe {
    std::mutex mu;
    bool done = false;
    bool ok = false;
    std::wstring latest;
};

struct ReleaseInfo {
    std::wstring tag;
    std::wstring exeUrl;
    uint64_t exeSize = 0;
};

// Синхронно запрашивает последний релиз: номер и ссылку на LOADUCK.exe.
bool FetchLatestRelease(ReleaseInfo& out, int timeoutMs);

// Синхронно запрашивает tag_name последнего релиза. Пустая строка — ошибка.
std::wstring FetchLatestTag(int timeoutMs);

struct DownloadState {
    std::mutex mu;
    bool done = false;
    bool ok = false;
    uint64_t received = 0;
    uint64_t total = 0;
    std::wstring path;
    std::wstring error;
};

// Скачивает LOADUCK.exe последнего релиза во временную папку.
std::shared_ptr<DownloadState> StartExeDownload();

// Запускает отдельный процесс, который дождётся выхода LOADUCK, заменит EXE
// и снова запустит приложение.
bool ApplyUpdateAndRestart(const std::wstring& newExe);

// Запускает проверку в фоне. Результат забирается через VersionProbe.
std::shared_ptr<VersionProbe> StartProbe();

} // namespace update
