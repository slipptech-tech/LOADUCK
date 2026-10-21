// app.h — общее состояние приложения и глобальные действия.
#pragma once

#include "store.h"
#include "dlp.h"
#include "render.h"
#include "toast.h"
#include "update.h"

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>

enum class Page { Home = 0, Channels = 1, Downloads = 2, History = 3, Settings = 4, Count = 5 };

// Вопрос, который показывается, если такое видео уже скачивалось раньше.
struct DuplicatePrompt {
    bool open = false;
    bool compare = false;       // раскрыто сравнение старого и нового файла
    JobOptions opt;             // задача, которую запустим после решения
    std::wstring oldPath;
    std::wstring oldUrl;
    std::wstring oldQuality;
    uint64_t oldSize = 0;
    time_t oldWhen = 0;
    std::wstring newLabel;
};

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
    bool  downloadPlaylist = false;  // качать весь плейлист, а не один ролик
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
    bool  pendingFromExt = false;      // качество берём из настроек расширения
    bool  autoExitAfterJobs = false;   // закрыться, когда всё скачается
    bool  extWorkStarted = false;      // хотя бы одна загрузка была поставлена
    bool  extFinished = false;         // работа закончена, ждём выхода
    float extExitAt = 0;               // ui::time, когда закрываться
    float extStartTime = 0;            // ui::time запуска из расширения
    bool  extRegistered = false;       // связь с браузером установлена
    double extCheckedAt = -1;          // когда проверяли регистрацию (ui::time)

    // ── автоматизация ───────────────────────────────────────────────────
    std::wstring lastClipUrl;          // последняя ссылка из буфера
    int  restoredJobs = 0;             // сколько задач вернули после перезапуска
    bool restoredToast = false;
    DuplicatePrompt dup;               // вопрос про уже скачанный файл

    // ── заставка при запуске ────────────────────────────────────────────
    bool  splashEnabled = false;       // показывается вместо страниц
    bool  splashDone = false;          // проверка версии завершилась
    bool  splashFailed = false;        // нет сети / GitHub недоступен
    bool  splashFade = false;          // плавно уходим в интерфейс
    double splashStart = 0;            // NowSec(), когда показали заставку
    double splashDoneAt = 0;           // NowSec(), когда пришёл результат
    double splashFadeAt = 0;           // NowSec(), когда началось затухание
    double uiRevealAt = 0;             // NowSec(), когда проявляется интерфейс
    std::wstring splashLocal;          // версия этого EXE
    std::wstring splashLatest;         // tag_name последнего релиза
    bool  splashHasUpdate = false;
    std::shared_ptr<update::VersionProbe> verCheck;

    // ── обновление приложения ───────────────────────────────────────────
    bool  updatePrompt = false;        // карточка «Обновить приложение?»
    bool  updateDismissed = false;     // пользователь нажал «Позже»
    bool  updateScreen = false;        // экран загрузки/установки
    bool  updateDownloading = false;   // прямо сейчас качается EXE
    bool  updateReady = false;         // файл скачан, ждём перезапуска
    bool  updateFailed = false;
    double updatePromptAt = 0;         // когда можно показать карточку
    double updatePromptShownAt = 0;    // для плавного появления
    double updateScreenAt = 0;         // для плавного появления экрана
    float updateProgress = 0;
    uint64_t updateReceived = 0;
    uint64_t updateTotal = 0;
    std::wstring updateError;
    std::shared_ptr<update::DownloadState> updateDl;

    // ── данные ──────────────────────────────────────────────────────────
    Store store;

    void Init();
    void StartSplash();
    void SyncFromSettings();
    void PushToSettings();
};

extern AppState* g_app;

// Действия
void AppProbe(AppState& a);
void AppStartDownload(AppState& a);
void AppEnqueue(AppState& a, const JobOptions& opt);
void AppSaveQueue(AppState& a);
void AppLoadQueue(AppState& a);
void AppPumpJobs(AppState& a);
void AppCancelProbe(AppState& a);
void AppProbeChannel(AppState& a);
void AppCancelChannel(AppState& a);
// Ссылка из браузера: сразу запускает анализ и (если включено) загрузку.
void AppExtensionDownload(AppState& a, const std::wstring& url, bool showWindow);
void AppRequestVideoStats(AppState& a, const std::string& url);
void AppUpdateComponents(AppState& a);
void AppStartUpdateDownload(AppState& a);
void AppRestartForUpdate(AppState& a);
std::wstring AppYtdlpPath(AppState& a);
std::wstring AppFfmpegPath(AppState& a);
std::wstring AppQualityLabel(AppState& a, const std::wstring& q);