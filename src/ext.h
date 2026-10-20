// ext.h — интеграция с браузерным расширением через Native Messaging.
//
// Как это работает:
//   расширение → chrome.runtime.sendNativeMessage("com.loaduck.host", {...})
//   → Chrome запускает LOADUCK.exe --nm-host
//   → хост читает сообщение из stdin, отвечает в stdout и завершается
//   → если программа уже открыта, ссылка уходит ей через WM_COPYDATA,
//     иначе запускается новый экземпляр с --ext-download <ссылка>
#pragma once

#include <Windows.h>

#include <string>

namespace ext {

// Должны совпадать с extension/manifest.json.
inline constexpr const wchar_t* kHostName = L"com.loaduck.host";
inline constexpr const wchar_t* kExtensionId = L"nnbmpogcemgfdedilbiiipblcbibabkn";

// Значения dwData для WM_COPYDATA между процессами.
inline constexpr ULONG_PTR kCopyDownload = 0x4C44554Bu;   // 'LDUK' — просто скачать
inline constexpr ULONG_PTR kCopyOpen = 0x4C44554Fu;       // 'LDUO' — скачать и показать окно

// Запущен ли процесс в режиме нативного хоста (аргумент --nm-host).
bool IsHostMode();
// Читает одно сообщение из stdin, отвечает в stdout. Возвращает код выхода.
int RunHost();

// Файл описания хоста в %LOCALAPPDATA%\LOADUCK\native.
std::wstring ManifestPath();
// Папка с файлами расширения (extension\). Пусто, если не найдена.
std::wstring ExtensionDir();

// Зарегистрирован ли хост в браузерах.
bool IsRegistered();
// Записать манифест и ключи реестра для Chrome/Edge/Chromium/Brave/Opera.
bool Install();
// Удалить ключи реестра и манифест.
bool Uninstall();

// Передать ссылку уже запущенному экземпляру. false — окно не найдено.
bool SendToRunning(const std::wstring& url, bool showWindow);

} // namespace ext
