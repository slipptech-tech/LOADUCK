// main.cpp — окно, ввод, главный цикл отрисовки и глобальные действия.
#include "app.h"
#include "ui.h"
#include "pages.h"
#include "icons.h"
#include "theme.h"
#include "gfx.h"
#include "util.h"
#include "toast.h"
#include "lang.h"

#include <Windows.h>
#include <Windowsx.h>
#include <Shlobj.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

AppState* g_app = nullptr;

// ── Фоновые задачи ────────────────────────────────────────────────────────

namespace {

struct ProbeTask {
    std::wstring url;
    std::atomic<bool> done{ false };
    std::atomic<bool> valid{ true };
    bool ok = false;
    MediaInfo info;
    std::wstring err;
};

struct VersionTask {
    std::atomic<bool> done{ false };
    std::atomic<bool> valid{ true };
    std::wstring version;
};

struct ChannelTask {
    std::wstring url;
    std::atomic<bool> done{ false };
    std::atomic<bool> valid{ true };
    bool ok = false;
    ChannelInfo info;
    std::wstring err;
};

// Дозапрос статистики одного ролика канала (лайки, просмотры).
struct StatsTask {
    std::wstring url;
    std::atomic<bool> done{ false };
    VideoStats stats;
};

std::mutex g_taskMu;
std::vector<std::shared_ptr<ProbeTask>> g_probes;
std::vector<std::shared_ptr<VersionTask>> g_versions;
std::vector<std::shared_ptr<ChannelTask>> g_channels;
std::vector<std::shared_ptr<StatsTask>> g_statsTasks;

// Сколько роликов канала опрашиваем одновременно
constexpr int kMaxStatsInFlight = 2;

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_tray{};
bool g_trayAdded = false;
constexpr UINT g_trayMsg = WM_APP + 7;
UINT64 g_lastFrame = 0;
HICON g_appIcon = nullptr;
bool g_forceFrame = true;


void Invalidate() {
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

} // namespace

// ── Иконка приложения ─────────────────────────────────────────────────────

// ── Трей ──────────────────────────────────────────────────────────────────

static void TrayAdd() {
    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = g_trayMsg;
    g_tray.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, L"LOADUCK — загрузчик видео");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_tray);
}

static void TrayBalloon(const wchar_t* title, const std::wstring& text) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW n = g_tray;
    n.uFlags |= NIF_INFO;
    wcscpy_s(n.szInfoTitle, title);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void ShowFromTray() {
    ShowWindow(g_hwnd, SW_SHOW);
    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwnd);
    Invalidate();
}

// ── Действия ──────────────────────────────────────────────────────────────

std::wstring AppYtdlpPath(AppState& a) {
    return engine::ResolveTool(a.store.Get().ytdlpPath, L"yt-dlp.exe");
}

std::wstring AppFfmpegPath(AppState& a) {
    return engine::ResolveTool(a.store.Get().ffmpegPath, L"ffmpeg.exe");
}

// Название кодека для интерфейса. «Авто» подписываем результатом, а не
// словом «авто»: пользователю важно, что он получит, а не как выбрано.
const wchar_t* AppCodecName(AppState& a) {
    if (a.codec == L"h264") return L"H.264";
    if (a.codec == L"vp9")   return L"VP9";
    if (a.codec == L"av1")   return L"AV1";
    // авто
    if (a.quality != L"best" && a.quality != L"audio" && _wtoi(a.quality.c_str()) > 1080)
        return a.info.hasAV1 ? L"AV1" : L"VP9";
    return (a.preferH264 && a.info.hasH264) ? L"H.264" : L"";
}

std::wstring AppQualityLabel(AppState& a, const std::wstring& q) {
    const wchar_t* cn = AppCodecName(a);
    std::wstring codec = cn && *cn ? (L" · " + std::wstring(cn)) : L"";
    if (q == L"audio") return TR(L"Только аудио · ") + util::ToUpper(a.audioFormat);
    if (q.empty() || q == L"best") return std::wstring(TR(L"Лучшее доступное качество")) + codec;
    return q + L"p · " + util::ToUpper(a.audioOnly ? a.audioFormat : a.container) + codec;
}

std::wstring AppContainerLabel(const std::wstring& c) {
    if (c == L"auto") return TR(L"Исходный контейнер");
    return util::ToUpper(c);
}

void AppCancelProbe(AppState& a) {
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& t : g_probes) t->valid = false;
        g_probes.clear();
    }
    a.probing = false;
}

void AppCancelChannel(AppState& a) {
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& t : g_channels) t->valid = false;
        g_channels.clear();
    }
    a.channelLoading = false;
}

void AppProbeChannel(AppState& a) {
    std::wstring raw = util::Trim(a.channelInput);
    if (raw.empty()) {
        toast::Show(TR(L"Введите адрес или имя канала"), toast::Kind::Warn);
        Invalidate();
        return;
    }
    std::wstring url = engine::NormalizeChannelUrl(raw);
    if (url.empty()) {
        toast::Show(TR(L"Это не похоже на канал"), toast::Kind::Error);
        Invalidate();
        return;
    }
    if (a.channelLoading) {
        toast::Show(TR(L"Дождитесь окончания загрузки канала"), toast::Kind::Info, 2.0f);
        return;
    }
    if (!engine::HasYtdlp(AppYtdlpPath(a))) {
        toast::Show(TR(L"Не установлен yt-dlp — откройте Настройки"), toast::Kind::Error, 5.0f);
        a.page = Page::Settings;
        Invalidate();
        return;
    }

    a.channelLoading = true;
    a.channel = ChannelInfo();
    a.channelError.clear();
    a.channelListScroll = 0;
    a.channelInput = raw;

    auto task = std::make_shared<ChannelTask>();
    task->url = url;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& t : g_channels) t->valid = false;
        g_channels.push_back(task);
    }
    const Settings& st = a.store.Get();
    const bool useCookies = st.cookiesFromBrowser;
    const std::wstring browser = st.cookiesBrowser;
    std::thread([task, useCookies, browser] {
        try {
            task->ok = engine::ProbeChannel(task->url, task->info, task->err, useCookies, browser);
        } catch (...) {
            task->err = TR(L"Непредвиденная ошибка при разборе канала");
        }
        task->done = true;
    }).detach();
    Invalidate();
}

static void ApplyChannelResults(AppState& a) {
    std::vector<std::shared_ptr<ChannelTask>> finished;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (size_t i = 0; i < g_channels.size();) {
            if (g_channels[i]->done && g_channels[i]->valid) {
                finished.push_back(g_channels[i]);
                g_channels.erase(g_channels.begin() + i);
                continue;
            }
            ++i;
        }
    }
    for (auto& t : finished) {
        if (!t->valid) continue;
        a.channelLoading = false;
        if (t->ok) {
            a.channel = t->info;
            a.channelError.clear();
            // Превью первых роликов качаем сразу после разбора: пока пользователь
            // читает шапку, картинки уже лежат в кэше и не «догоняют» список.
            for (size_t i = 0; i < a.channel.videos.size() && i < 18; ++i)
                engine::FetchImage(util::Utf8ToWide(a.channel.videos[i].thumbnail), false);
        } else {
            a.channel = ChannelInfo();
            a.channelError = t->err.empty() ? TR(L"Неизвестная ошибка") : t->err;
        }
        Invalidate();
    }
}

// Лайки и просмотры: отправляем запрос только если для этого ролика его ещё не
// было, и не больше двух одновременно — иначе на канале из сотни роликов
// приложение забьёт сеть и YouTube включит проверку бота.
void AppRequestVideoStats(AppState& a, const std::string& url) {
    if (url.empty()) return;
    const std::wstring key = util::Utf8ToWide(url);
    {
        auto it = a.videoStats.find(key);
        if (it != a.videoStats.end()) return;          // уже известно или уже в работе
    }
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        g_statsTasks.erase(std::remove_if(g_statsTasks.begin(), g_statsTasks.end(),
                                          [](const std::shared_ptr<StatsTask>& t) { return t->done.load(); }),
                           g_statsTasks.end());
        if ((int)g_statsTasks.size() >= kMaxStatsInFlight) return;
        a.videoStats[key] = VideoStats();              // метка «запрос отправлен»
        auto task = std::make_shared<StatsTask>();
        task->url = key;
        g_statsTasks.push_back(task);
        const Settings& st = a.store.Get();
        const bool useCookies = st.cookiesFromBrowser;
        const std::wstring browser = st.cookiesBrowser;
        std::thread([task, useCookies, browser] {
            task->stats = engine::ProbeVideoStats(task->url, useCookies, browser);
            task->done = true;
        }).detach();
    }
}

static void ApplyStatsResults(AppState& a) {
    std::vector<std::shared_ptr<StatsTask>> finished;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (size_t i = 0; i < g_statsTasks.size();) {
            if (g_statsTasks[i]->done) {
                finished.push_back(g_statsTasks[i]);
                g_statsTasks.erase(g_statsTasks.begin() + i);
                continue;
            }
            ++i;
        }
    }
    if (finished.empty()) return;
    for (auto& t : finished) {
        VideoStats& dst = a.videoStats[t->url];
        dst = t->stats;
    }
    Invalidate();
}

void AppProbe(AppState& a) {
    std::wstring raw = util::Trim(a.urlInput);
    if (raw.empty()) {
        toast::Show(TR(L"Сначала вставьте ссылку"), toast::Kind::Warn);
        Invalidate();
        return;
    }
    std::wstring url = util::CleanUrl(raw);
    if (!util::IsSupportedUrl(url)) {
        toast::Show(TR(L"Это не похоже на ссылку на видео"), toast::Kind::Error);
        Invalidate();
        return;
    }
    if (a.probing) {
        toast::Show(TR(L"Дождитесь окончания анализа"), toast::Kind::Info, 2.0f);
        return;
    }

    a.probing = true;
    a.info = MediaInfo();
    a.probeError.clear();
    a.exactFormat.clear();
    a.exactFormatLabel.clear();
    a.urlInput = url;

    auto task = std::make_shared<ProbeTask>();
    task->url = url;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& t : g_probes) t->valid = false;
        g_probes.push_back(task);
    }
    const Settings& st = a.store.Get();
    const bool useCookies = st.cookiesFromBrowser;
    const std::wstring browser = st.cookiesBrowser;

    std::thread([task, useCookies, browser] {
        MediaInfo info;
        std::wstring err;
        bool ok = false;
        try {
            ok = engine::Probe(task->url, info, err, useCookies, browser);
        } catch (...) {
            err = TR(L"Непредвиденная ошибка при анализе ссылки");
        }
        task->ok = ok;
        task->info = std::move(info);
        task->err = std::move(err);
        task->done = true;
    }).detach();
    Invalidate();
}

static void ApplyProbeResults(AppState& a) {
    std::vector<std::shared_ptr<ProbeTask>> finished;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (size_t i = 0; i < g_probes.size();) {
            if (g_probes[i]->done && g_probes[i]->valid) {
                finished.push_back(g_probes[i]);
                g_probes.erase(g_probes.begin() + i);
            } else {
                ++i;
            }
        }
    }
    if (finished.empty()) return;
    auto& t = finished.back();
    a.probing = false;
    if (t->ok) {
        a.info = t->info;
        a.probeError.clear();
        const Settings& st = a.store.Get();
        a.quality = st.defaultQuality;
        a.container = st.defaultContainer;
        a.audioOnly = (a.quality == L"audio");
        a.preferH264 = st.preferH264;
        a.h264Available = a.info.hasH264;
        if (a.preferH264 && !a.info.hasH264) a.preferH264 = false;
        // Приводим качество к тому, что реально есть в ролике. Разрешения выше 1080
        // в H.264 не бывает, поэтому для них совместимость снимается, а не сбрасывается
        // выбор на «Максимум» — иначе 4K был бы недостижим из интерфейса.
        if (a.quality != L"best" && a.quality != L"audio") {
            const int qh = _wtoi(a.quality.c_str());
            bool any = false, inH264 = false;
            for (int h : a.info.heights) if (h == qh) { any = true; break; }
            for (int h : a.info.heightsH264) if (h == qh) { inH264 = true; break; }
            if (!any) {
                a.quality = L"best";
            } else if (a.preferH264 && qh > 1080) {
                a.preferH264 = false;
                a.store.Get().preferH264 = false;
                a.store.Save();
            } else if (a.preferH264 && !inH264) {
                a.quality = L"best";
            }
        }
        a.scrollHome = 0;
        toast::Show(TR(L"Ссылка проанализирована"), toast::Kind::Success, 2.4f);
    } else {
        a.info = MediaInfo();
        a.probeError = t->err.empty() ? TR(L"Неизвестная ошибка") : t->err;
        toast::Show(TR(L"Не удалось получить видео"), toast::Kind::Error, 3.5f);
    }
    Invalidate();
}

void AppStartDownload(AppState& a) {
    if (!a.info.ok()) {
        toast::Show(TR(L"Сначала проанализируйте ссылку"), toast::Kind::Warn);
        Invalidate();
        return;
    }
    if (!engine::HasYtdlp(AppYtdlpPath(a))) {
        toast::Show(TR(L"Не установлен yt-dlp — откройте Настройки"), toast::Kind::Error, 5.0f);
        a.page = Page::Settings;
        Invalidate();
        return;
    }
    const Settings& st = a.store.Get();

    // Не добавляем дубль, если эта ссылка уже в очереди или качается
    {
        std::wstring target = a.info.webpageUrl.empty() ? util::CleanUrl(a.urlInput)
                                                        : util::Utf8ToWide(a.info.webpageUrl);
        for (auto& j : a.jobs) {
            JobState s = j->GetState();
            if ((s == JobState::Queued || s == JobState::Running) && j->GetUrl() == target) {
                toast::Show(TR(L"Эта ссылка уже в очереди"), toast::Kind::Info, 2.2f);
                a.page = Page::Downloads;
                a.jobFilter = L"active";
                Invalidate();
                return;
            }
        }
    }

    JobOptions opt;
    opt.url = a.info.webpageUrl.empty() ? util::CleanUrl(a.urlInput) : util::Utf8ToWide(a.info.webpageUrl);
    opt.title = util::Utf8ToWide(a.info.title);
    opt.qualityLabel = a.exactFormat.empty() ? AppQualityLabel(a, a.audioOnly ? L"audio" : a.quality)
                                             : a.exactFormatLabel;
    opt.audioOnly = a.audioOnly;
    opt.audioFormat = a.audioFormat;
    opt.formatSel = engine::FormatSelector(a.audioOnly ? L"audio" : a.quality,
                                           a.audioOnly ? a.audioFormat : a.container, a.exactFormat,
                                           a.audioOnly ? L"auto" : a.codec);
    opt.container = a.audioOnly ? a.audioFormat : a.container;
    opt.mergeTo = !a.audioOnly && a.container != L"auto";
    opt.embedMeta = st.embedMeta;
    opt.embedThumb = st.embedThumbnail;
    opt.writeSubs = st.writeSubs;
    opt.embedSubs = st.embedSubs;
    opt.useCookies = st.cookiesFromBrowser;
    opt.cookiesBrowser = st.cookiesBrowser;
    opt.rateLimit = a.rateLimit;
    opt.outDir = a.outDir.empty() ? st.downloadDir : a.outDir;
    opt.outTemplate = a.fileNameTemplate.empty() ? st.fileTemplate : a.fileNameTemplate;
    opt.ffmpegPath = AppFfmpegPath(a);
    util::EnsureDir(opt.outDir);

    if (!a.audioOnly && a.container == L"mp4" && !engine::HasFfmpeg(AppFfmpegPath(a)))
        toast::Show(TR(L"Без ffmpeg склейка дорожек недоступна"), toast::Kind::Warn, 4.0f);

    std::wstring shown = opt.title;
    if (shown.size() > 40) shown.resize(40);
    a.jobs.push_back(std::make_unique<Job>(opt));
    toast::Show(TR(L"Добавлено в очередь: ") + shown, toast::Kind::Success, 2.6f);
    AppPumpJobs(a);
    a.page = Page::Downloads;
    a.jobFilter = L"active";
    Invalidate();
}

void AppPumpJobs(AppState& a) {
    int running = 0;
    for (auto& j : a.jobs)
        if (j->GetState() == JobState::Running) running++;

    int maxPar = std::max(1, a.store.Get().maxParallel);
    for (auto& j : a.jobs) {
        if (running >= maxPar) break;
        if (j->GetState() == JobState::Queued) {
            j->Start(AppYtdlpPath(a));
            running++;
        }
    }
}

void AppUpdateComponents(AppState& a) {
    UINT64 now = GetTickCount64();
    if (now - a.ytdlpVersionAt < 60000) return;

    bool busy = false;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& v : g_versions)
            if (!v->done) busy = true;
    }
    if (busy) return;

    a.ytdlpVersionAt = (double)now;
    std::wstring path = AppYtdlpPath(a);
    auto task = std::make_shared<VersionTask>();
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& v : g_versions) v->valid = false;
        g_versions.push_back(task);
    }
    std::thread([task, path] {
        task->version = engine::YtdlpVersion(path);
        task->done = true;
    }).detach();
}

static void ApplyVersionResults(AppState& a) {
    std::vector<std::shared_ptr<VersionTask>> finished;
    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (size_t i = 0; i < g_versions.size();) {
            if (g_versions[i]->done && g_versions[i]->valid) {
                finished.push_back(g_versions[i]);
                g_versions.erase(g_versions.begin() + i);
            } else ++i;
        }
    }
    for (auto& t : finished)
        if (!t->version.empty()) a.ytdlpVersionCache = t->version;
}

// ── Состояние приложения ──────────────────────────────────────────────────

void AppState::Init() {
    store.Load();
    SyncFromSettings();
}

void AppState::SyncFromSettings() {
    Settings& s = store.Get();
    outDir = s.downloadDir;
    fileNameTemplate = s.fileTemplate;
    quality = s.defaultQuality;
    container = s.defaultContainer;
    audioOnly = (quality == L"audio");
    preferH264 = s.preferH264;
    codec = s.codec;
    if (codec != L"h264" && codec != L"vp9" && codec != L"av1") codec = L"auto";
    lang::Set((lang::Id)std::clamp(s.lang, 0, 2));
    theme::gAccent = theme::Accents()[theme::AccentIndexByName(s.accent)];
    util::EnsureDir(outDir);

    // Если куки включены, а выбранного браузера на machine нет — молча брать куки
    // неоткуда. Подставляем первый установленный, иначе отключаем флаг, чтобы
    // интерфейс не показывал «включено», когда на деле куки не используются.
    if (s.cookiesFromBrowser) {
        if (!util::BrowserInstalled(s.cookiesBrowser)) {
            std::wstring first = util::FirstInstalledBrowser();
            if (!first.empty()) {
                s.cookiesBrowser = first;
                store.Save();
            } else {
                s.cookiesFromBrowser = false;
                store.Save();
            }
        }
    }
}

void AppState::PushToSettings() {
    Settings& s = store.Get();
    s.downloadDir = outDir;
    s.fileTemplate = fileNameTemplate;
    s.defaultQuality = quality;
    s.defaultContainer = container;
    s.accent = theme::gAccent.name;
    store.Save();
}

// ── Отрисовка кадра ───────────────────────────────────────────────────────

static void DrawFrame(AppState& a) {
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;

    a.client = RectF(0, 0, (float)W, (float)H);

    UINT64 now = GetTickCount64();
    float delta = g_lastFrame ? (float)(now - g_lastFrame) / 1000.0f : 0.016f;
    g_lastFrame = now;
    ui::dt = std::clamp(delta, 0.001f, 0.05f);
    ui::time = (double)now / 1000.0;
    ui::frameAnim = 0;

    Renderer& r = GR();
    r.Begin();

    float sbW = std::min(gfx::S(252), a.client.w * 0.24f);
    float tbH = gfx::S(56);

    RectF titlebar(0, 0, a.client.w, tbH);
    RectF sidebar(0, tbH, sbW, a.client.h - tbH);
    RectF content(sbW, tbH, a.client.w - sbW, a.client.h - tbH);

    ui::Begin();
    pages::DrawBackground(a, a.client);
    pages::DrawTitleBar(a, titlebar);
    pages::DrawSidebar(a, sidebar);
    pages::DrawContent(a, content);
    ui::DrawTooltip();
    toast::Tick();
    toast::Draw(r, content);
    ui::End();
    r.End();
}

static void ResetInputFrame() {
    // pressed живёт до кадра, в котором отпускание было обработано
    if (ui::in.released || !ui::in.down) ui::in.pressed = false;
    ui::in.released = false;
    ui::in.wheel = 0;
    ui::in.text.clear();
    ui::in.vkBack = ui::in.vkDel = ui::in.vkLeft = ui::in.vkRight = ui::in.vkHome = ui::in.vkEnd = 0;
    ui::in.enter = ui::in.esc = ui::in.tab = false;
}

// Отрисовка кадра прямо в переданный DC (WM_PAINT / WM_PRINTCLIENT)
static void RenderInto(AppState& a, HDC hdc) {
    if (!hdc) return;
    if (!GR().Ready()) {
        RECT r{};
        GetClientRect(g_hwnd, &r);
        FillRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }
    DrawFrame(a);
    GR().PresentTo(hdc);
}

static void OnPaint(AppState& a, HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc) {
        RenderInto(a, hdc);
        EndPaint(hwnd, &ps);
    }
    ResetInputFrame();
    // Ссылки открываем здесь, а не из DrawFrame: ShellExecute внутри WM_PAINT
    // запускает вложенный цикл сообщений, и клик срабатывает многократно.
    std::wstring url;
    if (pages::TakePendingUrl(url))
        util::OpenUrl(url.c_str());
}

// ── Тик ───────────────────────────────────────────────────────────────────

static void OnTick(AppState& a) {
    static bool startChecked = false;
    if (!startChecked && ui::time > 0.8) {
        startChecked = true;
        if (!engine::HasYtdlp(AppYtdlpPath(a)))
            toast::Show(TR(L"Установите yt-dlp в настройках, чтобы начать"), toast::Kind::Warn, 7.0f);
        else if (!engine::HasFfmpeg(AppFfmpegPath(a)))
            toast::Show(TR(L"ffmpeg не найден: склейка дорожек недоступна"), toast::Kind::Info, 7.0f);
        else
            toast::Show(TR(L"Готово к работе. Вставьте ссылку на видео"), toast::Kind::Success, 4.0f);
    }

    ApplyProbeResults(a);
    ApplyChannelResults(a);
    ApplyStatsResults(a);
    ApplyVersionResults(a);
    AppUpdateComponents(a);
    AppPumpJobs(a);

    // завершённые загрузки → история
    for (auto& j : a.jobs) {
        if (j->WasNotified()) continue;
        JobState s = j->GetState();
        if (s == JobState::Done) {
            HistoryItem h;
            h.title = j->GetTitle();
            h.url = j->GetUrl();
            h.filePath = j->GetFile();
            h.quality = j->GetQualityLabel();
            h.container = j->GetFile().empty() ? L"" : util::ToLower(util::FileExt(j->GetFile()));
            h.size = j->GetResultSize();
            h.when = time(nullptr);
            a.store.AddHistory(h);
            std::wstring t = j->GetTitle();
            if (t.size() > 38) t.resize(38);
            toast::Show(TR(L"Готово: ") + t, toast::Kind::Success, 4.0f);
            TrayBalloon(TR(L"Загрузка завершена"), j->GetTitle());
            j->SetNotified();
        } else if (s == JobState::Failed) {
            std::wstring t = j->GetTitle();
            if (t.size() > 28) t.resize(28);
            toast::Show(TR(L"Ошибка загрузки: ") + t, toast::Kind::Error, 5.0f);
            TrayBalloon(TR(L"Ошибка загрузки"), j->GetError());
            j->SetNotified();
        }
    }
// Перерисовываем не каждый кадр, а только когда есть что показать:
    // идут анимации, живут уведомления, изменилось состояние задач или был ввод.
    static uint64_t lastSig = 0;
    static uint64_t lastImgVer = 0;
    uint64_t sig = (uint64_t)a.probing * 131 + (uint64_t)a.jobs.size() * 7919;
    // Догрузилось превью — кадр пора перерисовать, иначе картинка «висит»
    // до первого движения мыши.
    const uint64_t imgVer = ImageCache::Version();
    sig += imgVer * 2654435761ull;
    if (imgVer != lastImgVer) {
        lastImgVer = imgVer;
        // Чистим только здесь — между кадрами, когда ничего не рисуется.
        ImageCache::Trim(120);
    }
    for (auto& j : a.jobs) {
        sig = sig * 1000003ull + (uint64_t)j->GetState();
        JobProgress pr = j->GetProgress();
        sig ^= (uint64_t)(pr.pct * 2000.0) * 31 + (uint64_t)pr.speed * 131;
    }
    bool stateChanged = (sig != lastSig);
    lastSig = sig;

    bool needFrame = stateChanged || ui::frameAnim > 0 || toast::Alive() ||
                     ui::in.down || g_forceFrame;
    if (needFrame) {
        g_forceFrame = false;
        Invalidate();
    }
}

// ── Окно ──────────────────────────────────────────────────────────────────

static void ApplyAccent(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState& a = *g_app;
    switch (msg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            ApplyAccent(hwnd);
            if (!gfx::Init(hwnd)) {
                MessageBoxW(hwnd, TR(L"Не удалось инициализировать графический движок (Direct2D)."),
                            L"LOADUCK", MB_ICONERROR);
                return -1;
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == 1) OnTick(a);
            return 0;

        case WM_PAINT:
            OnPaint(a, hwnd);
            return 0;

        case WM_PRINTCLIENT:
            RenderInto(a, (HDC)wp);
            return 0;

        case WM_SIZE: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            a.client = RectF(0, 0, (float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
            a.maximized = (wp == SIZE_MAXIMIZED);
            GR().Resize(0, 0);
            Invalidate();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            float s = theme::Scale();
            mmi->ptMinTrackSize.x = (LONG)(1020 * s);
            mmi->ptMinTrackSize.y = (LONG)(680 * s);
            return 0;
        }
        case WM_DPICHANGED: {
            int dpi = LOWORD(lp);
            theme::SetScale(dpi / 96.0f);
            RECT* rec = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rec->left, rec->top, rec->right - rec->left, rec->bottom - rec->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Invalidate();
            return 0;
        }
        case WM_NCHITTEST: {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &p);
            float f = theme::Scale();
            float tb = 56.0f * f;
            float btnW = 46.0f * f;
            if (p.y >= 0 && p.y < tb && p.x < a.client.w - 3 * btnW - 4.0f)
                return HTCAPTION;
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_NCLBUTTONDBLCLK:
            if (wp == HTCAPTION) {
                SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE + (a.maximized ? 1 : 0), 0);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&t);
            ui::in.x = (float)GET_X_LPARAM(lp);
            ui::in.y = (float)GET_Y_LPARAM(lp);
            ui::in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            ui::in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            Invalidate();
            g_forceFrame = true;
            return 0;
        }
        case WM_MOUSELEAVE:
            ui::in.x = -1000;
            ui::in.y = -1000;
            Invalidate();
            g_forceFrame = true;
            return 0;

        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            ui::in.x = (float)GET_X_LPARAM(lp);
            ui::in.y = (float)GET_Y_LPARAM(lp);
            ui::in.down = true;
            ui::in.pressed = true;
            ui::in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            ui::in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            Invalidate();
            g_forceFrame = true;
            return 0;

        case WM_LBUTTONUP:
            ui::in.x = (float)GET_X_LPARAM(lp);
            ui::in.y = (float)GET_Y_LPARAM(lp);
            ui::in.released = true;
            ui::in.down = false;
            Invalidate();
            g_forceFrame = true;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;

        case WM_MOUSEWHEEL: {
            // Курсор должен быть «внутри» окна — иначе прокрутка не найдёт область
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            ui::in.x = (float)pt.x;
            ui::in.y = (float)pt.y;
            ui::in.wheel = (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            Invalidate();
            g_forceFrame = true;
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int vk = (int)wp;
            int rep = (int)(lp & 0xFFFF);
            ui::in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            ui::in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            if (vk == VK_BACK) ui::in.vkBack += std::max(1, rep);
            else if (vk == VK_DELETE) ui::in.vkDel += std::max(1, rep);
            else if (vk == VK_LEFT) ui::in.vkLeft += std::max(1, rep);
            else if (vk == VK_RIGHT) ui::in.vkRight += std::max(1, rep);
            else if (vk == VK_HOME) ui::in.vkHome += 1;
            else if (vk == VK_END) ui::in.vkEnd += 1;
            else if (vk == VK_RETURN) ui::in.enter = true;
            else if (vk == VK_ESCAPE) {
                ui::in.esc = true;
                if (a.advancedOpen) { a.advancedOpen = false; Invalidate(); return 0; }
            } else if (vk == VK_TAB) ui::in.tab = true;

            if (ui::in.ctrl && !ui::AnyFieldFocused()) {
                if (vk == 'V') {
                    std::vector<std::wstring> urls = util::ExtractUrls(util::ClipboardText());
                    if (!urls.empty()) {
                        a.urlInput = urls.front();
                        a.page = Page::Home;
                        if (!a.info.ok()) AppProbe(a);
                        else toast::Show(TR(L"Ссылка вставлена"), toast::Kind::Info, 1.8f);
                    } else {
                        toast::Show(TR(L"В буфере обмена нет ссылки"), toast::Kind::Warn, 2.2f);
                    }
                } else if (vk == VK_RETURN) {
                    if (a.info.ok()) AppStartDownload(a);
                    else AppProbe(a);
                } else if (vk >= '1' && vk <= '5') {
                    a.page = (Page)(vk - '1');
                    ui::BlurAllFields();
                }
            }
            Invalidate();
            g_forceFrame = true;
            return 0;
        }

        case WM_CHAR: {
            wchar_t c = (wchar_t)wp;
            g_forceFrame = true;
            if (c == VK_RETURN) {
                // Ctrl+Enter уже обработан в WM_KEYDOWN — не запускаем второй раз
                if (!ui::in.ctrl && !ui::in.shift && !ui::AnyFieldFocused()) {
                    if (a.info.ok()) AppStartDownload(a);
                    else AppProbe(a);
                }
                Invalidate();
                return 0;
            }
            if (c == VK_ESCAPE || c == VK_BACK) {
                Invalidate();
                return 0;
            }
            if (c >= 32) ui::in.text.push_back(c);
            Invalidate();
            return 0;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t buf[4096]{};
            if (DragQueryFileW(drop, 0, buf, 4096)) {
                std::vector<std::wstring> urls = util::ExtractUrls(std::wstring(buf));
                a.urlInput = urls.empty() ? std::wstring(buf) : urls.front();
                a.page = Page::Home;
                a.dragOverFile = false;
                if (!urls.empty()) AppProbe(a);
            }
            DragFinish(drop);
            Invalidate();
            return 0;
        }

        case WM_QUERYENDSESSION:
        case WM_ENDSESSION: {
            if (wp) {
                for (auto& j : a.jobs)
                    if (j->IsActive()) j->Cancel();
                a.store.Save();
            }
            return wp ? TRUE : DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_CLOSE: {
            bool active = false;
            for (auto& j : a.jobs)
                if (j->IsActive()) active = true;
            if (a.store.Get().closeToTray && active) {
                ShowWindow(hwnd, SW_HIDE);
                TrayBalloon(TR(L"LOADUCK свернулся"), TR(L"Загрузки продолжаются в фоне"));
                return 0;
            }
            if (active && a.store.Get().confirmExit) {
                int r = MessageBoxW(hwnd, TR(L"Загрузки ещё не завершены. Выйти из программы?"),
                                     L"LOADUCK", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
                if (r != IDYES) return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, 1);
            AppCancelProbe(a);
            for (auto& j : a.jobs)
                if (j->IsActive()) j->Cancel();
            for (auto& j : a.jobs) {
                int guard = 0;
                while (j->IsActive() && guard++ < 200) Sleep(30);
            }
            a.store.Save();
            if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_tray);
            PostQuitMessage(0);
            return 0;
        }

        case g_trayMsg: {
            if (lp == WM_LBUTTONDBLCLK) {
                ShowFromTray();
            } else if (lp == WM_RBUTTONUP) {
                POINT p{};
                GetCursorPos(&p);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 100, TR(L"Открыть LOADUCK"));
                AppendMenuW(menu, MF_STRING, 101, TR(L"Новая ссылка"));
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 102, TR(L"Выход"));
                SetForegroundWindow(hwnd);
                int cmd = (int)TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 100) ShowFromTray();
                else if (cmd == 101) {
                    ShowFromTray();
                    a.page = Page::Home;
                    a.urlInput.clear();
                    a.info = MediaInfo();
                    a.probeError.clear();
                    Invalidate();
                } else if (cmd == 102) DestroyWindow(hwnd);
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Точка входа ───────────────────────────────────────────────────────────

// Разбор аргумента командной строки вида  -Url "https://…"
static std::wstring CmdUrl() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring out;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (util::ToLower(a) == L"-url" && i + 1 < argc) {
            out = argv[i + 1];
            break;
        }
        if (util::ToLower(a).rfind(L"-url=", 0) == 0) {
            out = a.substr(5);
            break;
        }
    }
    if (argv) LocalFree(argv);
    out = util::Trim(out);
    if (out.size() >= 2 && out.front() == L'"' && out.back() == L'"')
        out = out.substr(1, out.size() - 2);
    return out;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g_instance = hInst;

    // Per-monitor DPI awareness (функция есть не во всех SDK — грузим динамически)
    using SetDpiCtx = BOOL(WINAPI*)(HANDLE);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto fn = (SetDpiCtx)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
            fn((HANDLE)-4);
        }
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // DPI окна: вычисляем до создания, чтобы стартовая геометрия была верной
    UINT dpi = 96;
    {
        HDC screen = GetDC(nullptr);
        int logpx = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
        if (screen) ReleaseDC(nullptr, screen);
        if (logpx > 0) dpi = (UINT)logpx;
    }
    theme::SetScale(dpi / 96.0f);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    static AppState app;
    g_app = &app;
    app.Init();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"LOADUCK_MAIN";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    float s = theme::Scale();
    // Гарантируем, что окно помещается в рабочую область монитора
    MONITORINFO mi{ sizeof(mi) };
    RECT work{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi))
        work = mi.rcWork;
    int availW = std::max<int>(640, work.right - work.left - 40);
    int availH = std::max<int>(480, work.bottom - work.top - 40);
    int w = std::min((int)(1320 * s), availW);
    int h = std::min((int)(860 * s), availH);
    int x = work.left + (work.right - work.left - w) / 2;
    int y = work.top + (work.bottom - work.top - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"LOADUCK_MAIN", L"LOADUCK",
                                WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                                x, y, w, h, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Не удалось создать окно", L"LOADUCK", MB_ICONERROR);
        return 1;
    }
    g_hwnd = hwnd;
    app.hwnd = hwnd;

    if (GR().Ready()) {
        g_appIcon = gfx::RenderAppIcon(64);
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon);
        TrayAdd();
    }

    // Точный таймер 16 мс: без этого Windows округляет интервал до 15.6 мс и кадры
    // «плавают», что заметно на анимациях.
    timeBeginPeriod(1);

    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    SetTimer(hwnd, 1, 16, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);

    std::wstring url = CmdUrl();
    if (!url.empty()) {
        app.urlInput = util::CleanUrl(url);
        if (util::IsSupportedUrl(app.urlInput))
            AppProbe(app);
        else
            toast::Show(TR(L"Ссылка из командной строки не поддерживается"), toast::Kind::Warn, 4.0f);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    {
        std::lock_guard<std::mutex> lk(g_taskMu);
        for (auto& t : g_probes) t->valid = false;
        for (auto& v : g_versions) v->valid = false;
    }
    if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_tray);
    if (g_appIcon) DestroyIcon(g_appIcon);
    gfx::Shutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
