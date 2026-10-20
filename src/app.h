// app.h — общее состояние приложения и глобальные действия.
#pragma once

#include "store.h"
#include "dlp.h"
#include "render.h"
#include "toast.h"

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>

enum class Page { Home = 0, Channels = 1, Downloads = 2, History = 3, Settings = 4, Count = 5 };

struct AppState {
    // ── окно ────────────────────────────────────────────────────────────
    HWND hwnd = nullptr;
    RectF client;
    RectF titlebarRect;
    RectF sidebarRect;
    RectF contentRect;
    bool maximized = false;
    bool dragOverFile = false;

    // ── навигация ───────────────────────────────────────────────────────
    Page page = Page::Home;

    // ── главная страница ────────────────────────────────────────────────
    std::wstring urlInput;
    bool  probing = false;
    MediaInfo info;
    std::wstring probeError;
    std::wstring quality = L"best";
    std::wstring container = L"mp4";
    std::wstring exactFormat;
    std::wstring exactFormatLabel;
    bool  audioOnly = false;
    std::wstring codec = L"auto";   // auto | h264 | vp9 | av1 — кодек видеодорожки
    bool  preferH264 = true;        // совместимые кодеки: H.264 + AAC (по умолчанию)
    bool  h264Available = true;     // есть ли H.264-дорожка в текущем ролике
    std::wstring audioFormat = L"mp3";
    bool  advancedOpen = false;
    int   rateLimit = 0;
    std::wstring fileNameTemplate;
    std::wstring outDir;
    float scrollHome = 0;
    float formatsScroll = 0;

    // ── каналы ──────────────────────────────────────────────────────────
    std::wstring channelInput;
    bool  channelLoading = false;
    ChannelInfo channel;
    std::wstring channelError;
    float scrollChannels = 0;
    float channelListScroll = 0;
    // Лайки и просмотры роликов. Значение (-1, -1) означает «запрос уже отправлен».
    std::unordered_map<std::wstring, VideoStats> videoStats;

    // ── загрузки ────────────────────────────────────────────────────────
    std::vector<std::unique_ptr<Job>> jobs;
    float scrollDownloads = 0;
    std::wstring jobFilter;

    // ── история ─────────────────────────────────────────────────────────
    float scrollHistory = 0;
    std::wstring historySearch;

    // ── настройки ───────────────────────────────────────────────────────
    float scrollSettings = 0;
    std::shared_ptr<engine::ComponentDownload> dlYtdlp;
    std::shared_ptr<engine::ComponentDownload> dlFfmpeg;
    std::wstring ytdlpVersionCache;
    double ytdlpVersionAt = 0;

    // ── запуск из браузерного расширения ────────────────────────────────
    bool  startedFromExt = false;      // процесс запущен расширением
    bool  pendingAutoDownload = false; // начать загрузку сразу после анализа
    bool  autoExitAfterJobs = false;   // закрыться, когда всё скачается
    bool  extWorkStarted = false;      // хотя бы одна загрузка была поставлена
    bool  extFinished = false;         // работа закончена, ждём выхода
    float extExitAt = 0;               // ui::time, когда закрываться
    float extStartTime = 0;            // ui::time запуска из расширения
    bool  extRegistered = false;       // связь с браузером установлена
    double extCheckedAt = -1;          // когда проверяли регистрацию (ui::time)

    // ── данные ──────────────────────────────────────────────────────────
    Store store;

    void Init();
    void SyncFromSettings();
    void PushToSettings();
};

extern AppState* g_app;

// Действия
void AppProbe(AppState& a);
void AppStartDownload(AppState& a);
void AppPumpJobs(AppState& a);
void AppCancelProbe(AppState& a);
void AppProbeChannel(AppState& a);
void AppCancelChannel(AppState& a);
// Ссылка из браузера: сразу запускает анализ и (если включено) загрузку.
void AppExtensionDownload(AppState& a, const std::wstring& url, bool showWindow);
void AppRequestVideoStats(AppState& a, const std::string& url);
void AppUpdateComponents(AppState& a);
std::wstring AppYtdlpPath(AppState& a);
std::wstring AppFfmpegPath(AppState& a);
std::wstring AppQualityLabel(AppState& a, const std::wstring& q);