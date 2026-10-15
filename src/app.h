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

enum class Page { Home = 0, Downloads = 1, History = 2, Settings = 3, Count = 4 };

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
    bool  preferH264 = true;        // совместимые кодеки: H.264 + AAC (по умолчанию)
    bool  h264Available = true;     // есть ли H.264-дорожка в текущем ролике
    std::wstring audioFormat = L"mp3";
    bool  advancedOpen = false;
    int   rateLimit = 0;
    std::wstring fileNameTemplate;
    std::wstring outDir;
    float scrollHome = 0;
    float formatsScroll = 0;

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
void AppUpdateComponents(AppState& a);
std::wstring AppYtdlpPath(AppState& a);
std::wstring AppFfmpegPath(AppState& a);
std::wstring AppQualityLabel(AppState& a, const std::wstring& q);