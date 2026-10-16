#include "pages.h"
#include "ui.h"
#include "icons.h"
#include "util.h"
#include "theme.h"
#include "gfx.h"
#include "lang.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <functional>

namespace pages {

static Renderer& R() { return GR(); }
static float S(float v) { return gfx::S(v); }

// ── Раскладка элементов в строку ──────────────────────────────────────────

struct Flow {
    float x = 0, y = 0, left = 0, maxX = 0, rowH = 0, gap = 0;
    void Init(const RectF& r, float rowH_, float gap_) {
        left = r.x; x = r.x; y = r.y; maxX = r.right(); rowH = rowH_; gap = gap_;
    }
    RectF Take(float w, float h) {
        if (x > left && x + w > maxX) { x = left; y += rowH; }
        RectF res(x, y, w, h);
        x += w + gap;
        return res;
    }
    float Bottom() const { return y + rowH; }
};

// ── Помощники ─────────────────────────────────────────────────────────────

static std::wstring CodecShort(const std::string& codec) {
    if (codec.empty() || codec == "none") return L"—";
    std::wstring c = util::ToLower(util::Utf8ToWide(codec));
    if (util::StartsWith(c, L"avc")) return L"H.264";
    if (util::StartsWith(c, L"hev") || util::StartsWith(c, L"hvc")) return L"H.265";
    if (util::StartsWith(c, L"vp9")) return L"VP9";
    if (util::StartsWith(c, L"vp8")) return L"VP8";
    if (util::StartsWith(c, L"av01")) return L"AV1";
    if (util::StartsWith(c, L"mp4a")) return L"AAC";
    if (util::StartsWith(c, L"opus")) return L"Opus";
    if (util::StartsWith(c, L"vorbis")) return L"Vorbis";
    if (util::StartsWith(c, L"mp3")) return L"MP3";
    return util::Utf8ToWide(codec);
}

static uint64_t EstimateSize(const MediaInfo& info, const std::wstring& quality,
                             bool audioOnly, bool compat) {
    uint64_t audio = 0;
    for (const auto& f : info.formats)
        if (f.filesize && !f.hasVideo) audio = std::max(audio, f.filesize);
    if (audioOnly) return audio;

    auto isH264 = [](const MediaFormat& f) {
        return f.vcodec.rfind("avc", 0) == 0 || f.vcodec.rfind("h264", 0) == 0;
    };
    uint64_t video = 0;
    int maxH = (quality == L"best") ? 99999 : _wtoi(quality.c_str());
    for (const auto& f : info.formats) {
        if (f.filesize == 0 || !f.hasVideo || f.hasAudio) continue;
        if (f.height <= 0 || f.height > maxH) continue;
        if (compat && !isH264(f)) continue;
        video = std::max(video, f.filesize);
    }
    if (video == 0 && compat) {          // H.264 не нашлось — считаем по всем
        for (const auto& f : info.formats) {
            if (f.filesize == 0 || !f.hasVideo || f.hasAudio) continue;
            if (f.height > 0 && f.height <= maxH) video = std::max(video, f.filesize);
        }
    }
    return video + audio;
}
static const wchar_t* PageTitle(Page p) {
    switch (p) {
        case Page::Home: return TR(L"Загрузка видео");
        case Page::Downloads: return TR(L"Очередь загрузок");
        case Page::History: return TR(L"История");
        default: return TR(L"Настройки");
    }
}

static const wchar_t* PageSubtitle(Page p) {
    switch (p) {
        case Page::Home: return TR(L"YouTube, TikTok, Instagram, X, Twitch и ещё 20+ сервисов");
        case Page::Downloads: return TR(L"Активные, завершённые и ошибочные загрузки");
        case Page::History: return TR(L"Все файлы, скачанные ранее");
        default: return TR(L"Компоненты, папки и поведение программы");
    }
}

static void Badge(Renderer& r, const RectF& box, const std::wstring& text, Color c, Icon ic = Icon::None) {
    r.FillRound(box, box.h / 2, Color(c.r, c.g, c.b, 36));
    float x = box.x + gfx::S(10);
    if (ic != Icon::None) {
        icons::Draw(r, ic, RectF(x, box.cy() - gfx::S(7), gfx::S(14), gfx::S(14)), c, 1.8f);
        x += gfx::S(20);
    }
    r.Text(text, fonts::TINY, 600, c, RectF(x, box.y, box.right() - x - gfx::S(10), box.h),
           Align::Left, VAlign::Middle, true);
}

static float BadgeWidth(Renderer& r, const std::wstring& text, Icon ic = Icon::None) {
    return r.MeasureText(text, fonts::TINY, 600) + gfx::S(20) + (ic != Icon::None ? gfx::S(20) : 0) + gfx::S(10);
}

// ── Фон ───────────────────────────────────────────────────────────────────

void DrawBackground(AppState& a, const RectF& r) {
    (void)a;
    // Мягкое статичное свечение: пятна считаются один раз и кэшируются.
    std::vector<gfx::Blob> blobs = {
        { 0.12f, 0.06f, 0.34f, 1.05f, theme::A1(), 46.0f },
        { 0.94f, 0.96f, 0.30f, 1.10f, theme::A2(), 38.0f },
        { 0.72f, 0.42f, 0.22f, 1.0f, Mix(theme::A1(), theme::A2(), 0.5f), 20.0f },
    };
    gfx::PaintBackground(r, theme::bg, Color(8, 11, 17), blobs);
}

// ── Заголовок окна ────────────────────────────────────────────────────────

void DrawTitleBar(AppState& a, const RectF& r) {
    a.titlebarRect = r;
    R().FillGradV(r, 0, Color(12, 16, 24), Color(10, 13, 20));

    float pad = S(20);
    icons::DrawLogo(R(), RectF(r.x + pad, r.y + (r.h - S(30)) / 2, S(30), S(30)));
    R().Text(L"LOADUCK", fonts::H3, 700, theme::text, RectF(r.x + pad + S(40), r.y, S(200), r.h),
             Align::Left, VAlign::Middle, false);

    int active = 0;
    double speed = 0;
    for (auto& j : a.jobs)
        if (j->GetState() == JobState::Running) { active++; speed += j->GetProgress().speed; }
    if (active > 0) {
        float bx = r.x + S(160);
        Color c = theme::A1();
        R().FillRound(RectF(bx, r.y + (r.h - S(27)) / 2, S(30) + S(78), S(27)), S(14), Color(c.r, c.g, c.b, 34));
        icons::Draw(R(), Icon::Download, RectF(bx + S(8), r.y + r.h / 2 - S(7), S(14), S(14)), c, 2.0f);
        R().Text(util::FormatSpeed(speed), fonts::TINY, 400, theme::textDim,
                 RectF(bx + S(28), r.y, S(90), r.h), Align::Left, VAlign::Middle, true);
    }

    // кнопки окна
    struct WBtn { Icon ic; const wchar_t* tip; UINT msg; };
    const WBtn btns[3] = {
        { Icon::Minus, TR(L"Свернуть"),   SC_MINIMIZE },
        { Icon::Maximize, TR(L"Развернуть"), SC_MAXIMIZE },
        { Icon::Close, TR(L"Закрыть"),    SC_CLOSE },
    };
    float bw = S(46);
    float x = r.right();
    for (int i = 2; i >= 0; --i) {
        RectF br(x - bw, r.y, bw, r.h);
        int id = 900 + i;
        bool hov = ui::Hover(br);
        float h = ui::Ani(id, hov ? 1.0f : 0.0f, 16.0f);
        if (hov) {
            Color c = (i == 2) ? theme::danger : theme::surfaceHi;
            R().Fill(br, Color(c.r, c.g, c.b, (BYTE)(i == 2 ? 228 : 150 * h)));
        }
        Color fg = (i == 2 && h > 0.02f) ? Color(255, 255, 255, 255) : Mix(theme::textDim, theme::text, h);
        Icon ic = (i == 1 && a.maximized) ? Icon::Restore : btns[i].ic;
        float sz = S(14);
        icons::Draw(R(), ic, RectF(br.x + (bw - sz) / 2, br.cy() - sz / 2, sz, sz), fg, 1.8f);
        if (hov) ui::SetTooltip(br, btns[i].tip);
        if (ui::in.pressed && br.Contains(ui::in.x, ui::in.y) && ui::in.released)
            SendMessageW(a.hwnd, WM_SYSCOMMAND, btns[i].msg, 0);
        x -= bw;
    }
}

// ── Боковая панель ────────────────────────────────────────────────────────

namespace {
// Ссылка, выбранная кликом. Открывается вызывающим кодом после отрисовки
// кадра — из DrawFrame запускать ShellExecute нельзя.
std::wstring g_pendingUrl;
} // namespace

bool TakePendingUrl(std::wstring& out) {
    if (g_pendingUrl.empty()) return false;
    out = std::move(g_pendingUrl);
    g_pendingUrl.clear();
    return true;
}

void DrawSidebar(AppState& a, const RectF& r) {
    a.sidebarRect = r;
    R().FillGradV(r, 0, Color(14, 18, 27), Color(11, 15, 22));
    R().Fill(RectF(r.right() - S(1), r.y, S(1), r.h), Color(255, 255, 255, 12));

    float pad = S(14);
    float y = r.y + S(18);

    int activeJobs = 0;
    for (auto& j : a.jobs) {
        JobState s = j->GetState();
        if (s == JobState::Running || s == JobState::Queued) activeJobs++;
    }

    struct NavItem { Page page; Icon ic; const wchar_t* label; int badge; };
    NavItem items[4] = {
        { Page::Home,      Icon::Home,    TR(L"Загрузка"),  0 },
        { Page::Downloads, Icon::Queue,   TR(L"Очередь"),   activeJobs },
        { Page::History,   Icon::History, TR(L"История"),   0 },
        { Page::Settings,  Icon::Settings,TR(L"Настройки"), 0 },
    };

    for (int i = 0; i < 4; ++i) {
        float h = S(48);
        RectF ir(r.x + pad, y, r.w - pad * 2, h);
        bool sel = a.page == items[i].page;
        float selA = ui::Ani(700 + i, sel ? 1.0f : 0.0f, 15.0f);
        bool hov = ui::Hover(ir) && !sel;
        float hovA = ui::Ani(710 + i, hov ? 1.0f : 0.0f, 16.0f);

        if (selA > 0.01f) {
            R().FillGradH(ir, S(12), Color(theme::A1().r, theme::A1().g, theme::A1().b, (BYTE)(52 * selA)),
                          Color(theme::A2().r, theme::A2().g, theme::A2().b, (BYTE)(28 * selA)));
            R().FillGradV(RectF(ir.x + S(2), ir.y + S(12), S(3.5f), h - S(24)), S(2), theme::A1(), theme::A2());
        } else if (hovA > 0.01f) {
            R().FillRound(ir, S(12), Color(255, 255, 255, (BYTE)(16 * hovA)));
        }

        Color fg = Mix(Mix(theme::textDim, theme::text, hovA), theme::A1(), selA);
        icons::Draw(R(), items[i].ic, RectF(ir.x + S(14), ir.cy() - S(10), S(20), S(20)), fg, 1.9f);
        R().Text(items[i].label, fonts::BODY, sel ? 600.f : 400.f, fg,
                 RectF(ir.x + S(45), ir.y, ir.w - S(86), h), Align::Left, VAlign::Middle, false);

        int badge = items[i].badge;
        if (i == 2) badge = (int)a.store.HistorySize();
        if (badge > 0) {
            std::wstring bt = badge > 99 ? L"99+" : std::to_wstring(badge);
            float bwd = R().MeasureText(bt, fonts::TINY, 600) + gfx::S(14);
            RectF br(ir.right() - bwd - S(12), ir.cy() - S(10), bwd, S(20));
            Color bc = sel ? theme::A1() : theme::strokeHi;
            R().FillRound(br, S(8), Color(bc.r, bc.g, bc.b, (BYTE)(sel ? 235 : 170)));
            R().Text(bt, fonts::TINY, 600, sel ? Color(255, 255, 255, 255) : theme::textDim, br,
                     Align::Center, VAlign::Middle, false);
        }

        if (ui::in.pressed && ir.Contains(ui::in.x, ui::in.y) && ui::in.released && !sel)
            a.page = items[i].page;
        y += h + S(6);
    }

    // папка загрузок
    RectF folder(r.x + pad, y + S(20), r.w - pad * 2, S(78));
    ui::Card(760, folder, true, S(14));
    icons::Draw(R(), Icon::Folder, RectF(folder.x + S(14), folder.y + S(14), S(18), S(18)), theme::A1(), 1.9f);
    R().Text(TR(L"Папка загрузок"), fonts::TINY, 400, theme::textMute,
             RectF(folder.x + S(40), folder.y + S(11), folder.w - S(54), S(18)), Align::Left, VAlign::Middle, false);
    R().Text(a.store.Get().downloadDir, fonts::SMALL, 400, theme::text,
             RectF(folder.x + S(14), folder.y + S(38), folder.w - S(28), S(28)), Align::Left, VAlign::Middle, true);
    if (ui::in.pressed && folder.Contains(ui::in.x, ui::in.y) && ui::in.released)
        util::RevealInExplorer(a.store.Get().downloadDir);

    // ── Ссылки на проект ────────────────────────────────────────────────
    // Стоят сразу под папкой загрузок и выше блока компонентов.
    {
        const float lw = S(36);
        const float ly = folder.bottom() + S(18);
        struct Link { Icon ic; const wchar_t* tip; const wchar_t* url; Color c; };
        const Link links[2] = {
            { Icon::Telegram, TR(L"Telegram — написать автору"), L"https://t.me/LOADUCK",
              Color::Rgb(42, 171, 238) },
            { Icon::Github,   TR(L"Исходный код на GitHub"),   L"https://github.com/slipptech-tech/LOADUCK",
              Color(255, 255, 255, 225) },
        };
        for (int i = 0; i < 2; ++i) {
            RectF br(folder.x + S(2) + lw * i, ly, lw, lw);
            bool hov = ui::Hover(br);
            float ha = ui::Ani(770 + i, hov ? 1.f : 0.f, 16.f);
            if (ha > 0.01f) {
                R().FillRound(br, S(11), Color(255, 255, 255, (BYTE)(16 * ha)));
                R().Stroke(br, S(11), Color(255, 255, 255, (BYTE)(34 * ha)), 1.f);
            }
            // Значки узнаются по фирменным цветам, поэтому рисуем их брендовыми,
            // а не серым, как остальные элементы сайдбара.
            icons::Draw(R(), links[i].ic, br, Mix(links[i].c, Color(255, 255, 255, 255), ha * 0.35f), 2.1f);
            ui::SetTooltip(br, links[i].tip);
            // Только запоминаем ссылку — открываем её после отрисовки кадра
            if (ui::in.pressed && br.Contains(ui::in.x, ui::in.y) && ui::in.released)
                g_pendingUrl = links[i].url;
        }
    }

    // статус компонентов
    RectF status(r.x + pad, r.bottom() - S(80), r.w - pad * 2, S(62));
    R().FillRound(status, S(14), Color(255, 255, 255, 8));
    R().Stroke(status, S(14), Color(255, 255, 255, 18), 1.0f);
    bool hasYtdlp = engine::HasYtdlp(AppYtdlpPath(a));
    bool hasFfmpeg = engine::HasFfmpeg(AppFfmpegPath(a));
    R().Text(TR(L"Компоненты"), fonts::TINY, 400, theme::textMute,
             RectF(status.x + S(14), status.y + S(8), status.w - S(28), S(16)), Align::Left, VAlign::Middle, false);
    auto chip = [&](float cx, float cy, bool ok, const wchar_t* tip) {
        R().Ellipse(cx, cy, S(4), S(4), ok ? theme::success : theme::warning);
        if (ui::Hover(status)) ui::SetTooltip(RectF(cx - S(10), cy - S(10), S(20), S(20)), tip);
    };
    chip(status.x + S(19), status.y + S(39), hasYtdlp, hasYtdlp ? TR(L"yt-dlp установлен") : TR(L"yt-dlp не найден"));
    R().Text(L"yt-dlp", fonts::SMALL, 400, hasYtdlp ? theme::textDim : theme::warning,
             RectF(status.x + S(28), status.y + S(29), S(72), S(20)), Align::Left, VAlign::Middle, false);
    chip(status.x + S(108), status.y + S(39), hasFfmpeg, hasFfmpeg ? TR(L"ffmpeg найден") : TR(L"ffmpeg не найден"));
    R().Text(L"ffmpeg", fonts::SMALL, 400, hasFfmpeg ? theme::textDim : theme::warning,
             RectF(status.x + S(117), status.y + S(29), S(80), S(20)), Align::Left, VAlign::Middle, false);
}

// ── Шапка ─────────────────────────────────────────────────────────────────

void DrawHeader(AppState& a, const RectF& r) {
    a.contentRect = r;
    float pad = S(32);
    ui::PageHeading(RectF(r.x + pad, r.y + S(16), r.w * 0.6f, S(58)), PageTitle(a.page), PageSubtitle(a.page));

    float by = r.y + (r.h - S(40)) / 2;
    float x = r.right() - pad;
    if (a.page == Page::Home) {
        if (ui::Button(801, RectF(x - S(172), by, S(172), S(40)), TR(L"Новая ссылка"), Icon::Plus, ui::Btn::Primary)) {
            a.urlInput.clear();
            a.info = MediaInfo();
            a.probeError.clear();
            a.exactFormat.clear();
            a.exactFormatLabel.clear();
        }
        x -= S(182);
    }
    if (ui::IconButton(802, RectF(x - S(44), by, S(44), S(40)), Icon::External, TR(L"Открыть папку загрузок")))
        util::RevealInExplorer(a.store.Get().downloadDir);
}

// ── Главная: поле ввода ───────────────────────────────────────────────────

static void DrawUrlCard(AppState& a, const RectF& r) {
    ui::Card(1000, r, false, S(20));

    float pad = S(26);
    float x = r.x + pad;
    float w = r.w - pad * 2;

    R().Text(TR(L"Вставьте ссылку на видео"), fonts::H2, 600, theme::text,
             RectF(x, r.y + S(20), w, S(26)), Align::Left, VAlign::Middle, false);

    float rowY = r.y + S(60);
    float rowH = S(54);
    RectF paste(r.right() - pad - S(116) - S(10) - S(112), rowY, S(116), rowH);
    RectF input(x, rowY, paste.x - x - S(14), rowH);

    // иконка
    R().FillRound(RectF(input.x + S(4), input.y + S(4), input.h - S(8), input.h - S(8)), S(10),
                  Color(255, 255, 255, 9));
    icons::Draw(R(), Icon::Link, RectF(input.x + S(16), input.cy() - S(9), S(18), S(18)), theme::textMute, 1.9f);

    RectF field(input.x + S(46), input.y, input.w - S(54), input.h);
    std::wstring val = a.urlInput;
    ui::TextField(1001, field, val, L"https://www.youtube.com/watch?v=…");
    a.urlInput = val;
    if (ui::Button(1002, paste, TR(L"Вставить"), Icon::Clipboard, ui::Btn::Soft)) {
        std::wstring clip = util::ClipboardText();
        std::vector<std::wstring> urls = util::ExtractUrls(clip);
        if (!urls.empty()) { a.urlInput = urls.front(); AppProbe(a); }
        else if (!util::Trim(clip).empty()) a.urlInput = util::Trim(clip);
    }

    RectF go(paste.right() + S(10), rowY, S(112), rowH);
    if (a.probing) {
        ui::Button(1003, go, TR(L"Анализ…"), Icon::None, ui::Btn::Primary, false);
        ui::Spinner(RectF(go.right() - S(36), go.cy() - S(9), S(18), S(18)), Color(255, 255, 255, 255));
    } else if (ui::Button(1003, go, TR(L"Найти"), Icon::Search, ui::Btn::Primary)) {
        AppProbe(a);
    }

    const wchar_t* services =
        L"YouTube · TikTok · Instagram · X · Facebook · Twitch · SoundCloud · Vimeo · Bilibili";
    R().Text(services, fonts::TINY, 400, theme::textMute, RectF(x, rowY + rowH + S(16), w, S(18)),
             Align::Left, VAlign::Middle, true);

    if (!a.dragOverFile) return;
    R().Stroke(RectF(r.x + S(7), r.y + S(7), r.w - S(14), r.h - S(14)), S(18), theme::A1(), 2.0f);
    R().FillRound(RectF(r.x + S(7), r.y + S(7), r.w - S(14), r.h - S(14)), S(18),
                  Color(theme::A1().r, theme::A1().g, theme::A1().b, 30));
    R().Text(TR(L"Отпустите ссылку"), fonts::H3, 600, theme::text, r, Align::Center, VAlign::Middle, false);
}

// ── Главная: состояния ────────────────────────────────────────────────────

static void DrawProbing(AppState& a, const RectF& r) {
    ui::Card(1101, r, false, S(20));
    RectF thumb(r.x + S(22), r.y + S(22), S(280), S(158));
    ui::Skeleton(thumb, S(12));
    float x = thumb.right() + S(26);
    ui::Skeleton(RectF(x, r.y + S(32), r.right() - x - S(26), S(20)), S(6));
    ui::Skeleton(RectF(x, r.y + S(64), r.right() - x - S(140), S(16)), S(6));
    ui::Skeleton(RectF(x, r.y + S(92), S(210), S(14)), S(6));
    ui::Spinner(RectF(thumb.cx() - S(20), thumb.cy() - S(20), S(40), S(40)), theme::A1());
    R().Text(TR(L"Получаем информацию о видео…"), fonts::BODY, 400, theme::textDim,
             RectF(x, r.y + S(124), S(340), S(20)), Align::Left, VAlign::Middle, false);
    (void)a;
}

static void DrawError(AppState& a, const RectF& r) {
    ui::Card(1200, r, false, S(20));
    R().FillGradH(RectF(r.x + S(4), r.y + S(4), r.w - S(8), r.h - S(8)), S(17),
                  Color(theme::danger.r, theme::danger.g, theme::danger.b, 34),
                  Color(theme::danger.r, theme::danger.g, theme::danger.b, 8));
    R().Stroke(RectF(r.x + S(4), r.y + S(4), r.w - S(8), r.h - S(8)), S(17),
               Color(theme::danger.r, theme::danger.g, theme::danger.b, 62), 1.0f);

    float x = r.x + S(28);
    icons::Draw(R(), Icon::Error, RectF(x, r.y + S(24), S(24), S(24)), theme::danger, 2.0f);
    R().Text(TR(L"Не удалось получить видео"), fonts::H2, 600, theme::text,
             RectF(x + S(36), r.y + S(20), r.w - S(80), S(28)), Align::Left, VAlign::Middle, false);
    // Высота текста ошибки считается по объёму сообщения: к некоторым ошибкам
// добавляется многострочная подсказка про куки, и обрезать её нельзя —
// иначе пользователь не поймёт, что делать.
    float lines = 2.f;
    for (size_t i = 0; i < a.probeError.size(); ++i)
        if (a.probeError[i] == L'\n') lines += 1.f;
    if (R().MeasureText(a.probeError, fonts::SMALL, 400) > (r.w - S(84)) * 1.02f)
        lines += 1.f;                       // текст не влезет в одну строку
    lines = std::min(lines, 7.f);
    R().Text(a.probeError, fonts::SMALL, 400, theme::textDim,
             RectF(x + S(36), r.y + S(52), r.w - S(84), lines * S(19) + S(4)),
             Align::Left, VAlign::Top, true);

    float by = r.bottom() - S(62);
    if (ui::Button(1201, RectF(x + S(36), by, S(156), S(42)), TR(L"Повторить"), Icon::Refresh, ui::Btn::Secondary))
        AppProbe(a);
    if (ui::Button(1202, RectF(x + S(202), by, S(200), S(42)), TR(L"Открыть настройки"), Icon::Settings, ui::Btn::Ghost))
        a.page = Page::Settings;
}

// ── Главная: карточка видео (превью + качество) ───────────────────────────

static float MediaCardHeight(const MediaInfo& info, float width) {
    float chipsX = S(26) + S(340) + S(26);
    float chipsW = width - chipsX - S(26);
    float perRow = std::max(1.0f, std::floor((chipsW + S(8)) / S(150)));
    int n = 2 + (int)info.heights.size();
    int rows = (int)std::ceil((float)n / perRow);
    // Подсказка про H.264/1080p рисуется, только если в ролике есть что-то выше 1080
    int maxH = 0;
    for (int h : info.heights) maxH = std::max(maxH, h);
    float hint = (info.hasH264 && maxH > 1080) ? S(22) : 0.f;
    return S(328) + rows * S(40) + hint;
}

static void DrawMediaCard(AppState& a, const RectF& r) {
    const MediaInfo& info = a.info;
    ui::Card(1300, r, true, S(20));

    float pad = S(26);

    // ── Превью 16:9 ──
    RectF thumb(r.x + pad, r.y + pad, S(340), S(191));
    // Кандидаты обложки: основной URL плюс запасные размеры
    std::vector<std::wstring> keys;
    if (!info.thumbnail.empty()) keys.push_back(util::Utf8ToWide(info.thumbnail));
    size_t vi = info.thumbnail.find("/vi");
    if (vi != std::string::npos) {
        std::string rest = info.thumbnail.substr(vi + 3);
        size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            std::wstring id = util::Utf8ToWide(rest.substr(0, slash));
            keys.push_back(L"https://i.ytimg.com/vi/" + id + L"/hqdefault.jpg");
            keys.push_back(L"https://i.ytimg.com/vi/" + id + L"/mqdefault.jpg");
        }
    }
    GfxImage* img = nullptr;
    for (size_t ki = 0; ki < keys.size(); ++ki) {
        engine::FetchImage(keys[ki]);
        img = ImageCache::Get(keys[ki]);
        if (img && img->Valid()) break;
    }

    R().FillGradV(thumb, S(14), theme::surfaceAlt, theme::surface);
    if (img && img->Valid()) {
        R().DrawImage(img, thumb);
        R().Stroke(thumb, S(14), Color(255, 255, 255, 22), 1.0f);
    } else {
        icons::Draw(R(), Icon::Video, RectF(thumb.cx() - S(24), thumb.cy() - S(24), S(48), S(48)),
                    theme::textMute, 1.7f);
        ui::Spinner(RectF(thumb.right() - S(40), thumb.bottom() - S(40), S(22), S(22)), theme::A1());
    }

    // длительность
    if (info.duration > 0) {
        std::wstring dur = util::FormatTime((int)info.duration);
        float tw = R().MeasureText(dur, fonts::TINY, 600);
        RectF dr(thumb.right() - tw - gfx::S(24), thumb.bottom() - gfx::S(34), tw + gfx::S(16), gfx::S(24));
        R().FillRound(dr, S(6), Color(0, 0, 0, 200));
        R().Text(dur, fonts::TINY, 600, Color(255, 255, 255, 255), dr, Align::Center, VAlign::Middle, false);
    }

    float x = thumb.right() + S(26);
    float right = r.right() - pad;

    // ── Источник ──
    std::wstring src = util::Utf8ToWide(info.extractor);
    if (src.empty()) src = TR(L"Видео");
    for (auto& c : src) if (c == L'_') c = L' ';
    float srcW = BadgeWidth(R(), src);
    Badge(R(), RectF(x, r.y + pad, srcW, gfx::S(24)), src, theme::A1());
    if (info.isLive)
        Badge(R(), RectF(x + srcW + gfx::S(8), r.y + pad, gfx::S(72), gfx::S(24)), L"LIVE", theme::danger, Icon::Flame);

    // ── Название ──
    R().Text(util::Utf8ToWide(info.title), fonts::H2, 600, theme::text,
             RectF(x, r.y + pad + gfx::S(36), right - x, gfx::S(62)), Align::Left, VAlign::Top, true);

    // ── Метаданные ──
    float my = r.y + pad + gfx::S(106);
    float mx = x;
    auto meta = [&](Icon ic, const std::wstring& text) {
        if (text.empty()) return;
        float tw = R().MeasureText(text, fonts::SMALL, 400);
        if (mx + tw + gfx::S(30) > right) return;
        icons::Draw(R(), ic, RectF(mx, my + gfx::S(2), gfx::S(14), gfx::S(14)), theme::textMute, 1.8f);
        R().Text(text, fonts::SMALL, 400, theme::textDim, RectF(mx + gfx::S(20), my, tw + gfx::S(4), gfx::S(20)),
                 Align::Left, VAlign::Middle, false);
        mx += tw + gfx::S(32);
    };
    if (!info.uploader.empty()) meta(Icon::Home, util::Utf8ToWide(info.uploader));
    if (info.viewCount > 0) meta(Icon::Play, util::FormatCount(info.viewCount) + TR(L" просмотров"));
    if (info.duration > 0) meta(Icon::Clock, util::FormatDurationWords((int)info.duration));

    // копировать ссылку
    if (ui::IconButton(1301, RectF(right - gfx::S(40), r.y + pad, gfx::S(40), gfx::S(36)), Icon::Copy, TR(L"Скопировать ссылку"))) {
        util::SetClipboardText(util::Utf8ToWide(info.webpageUrl.empty() ? info.title : info.webpageUrl));
        toast::Show(TR(L"Ссылка скопирована"), toast::Kind::Success, 2.2f);
    }

    // ── Качество ──
    float cy = r.y + pad + gfx::S(152);
    bool compat = a.preferH264 && a.h264Available;

    R().Text(TR(L"Качество"), fonts::H3, 600, theme::textDim, RectF(x, cy, gfx::S(200), gfx::S(20)),
             Align::Left, VAlign::Middle, false);

    // Переключатель совместимости: H.264 + AAC открывается любым системным плеером,
    // AV1/VP9 — нет (в них часто «пропадает» звук в штатном плеере Windows).
    if (info.hasH264) {
        RectF sw(right - gfx::S(52), cy, gfx::S(52), gfx::S(30));
        bool before = a.preferH264;
        R().Text(TR(L"Совместимый кодек H.264"), fonts::TINY, 400, theme::textMute,
                 RectF(right - gfx::S(52) - gfx::S(210), cy, gfx::S(198), gfx::S(30)),
                 Align::Right, VAlign::Middle, true);
        if (ui::Toggle(1390, sw, a.preferH264, Icon::Shield) && before != a.preferH264) {
            a.store.Get().preferH264 = a.preferH264;
            a.store.Save();
            if (a.preferH264 && !a.exactFormat.empty()) {
                a.exactFormat.clear();
                a.exactFormatLabel.clear();
            }
            // Текущее разрешение может отсутствовать в H.264 — откатываемся на «Максимум»
            if (a.preferH264 && a.quality != L"best" && a.quality != L"audio") {
                bool allowed = false;
                for (int h : info.heightsH264)
                    if (std::to_wstring(h) == a.quality) { allowed = true; break; }
                if (!allowed) a.quality = L"best";
            }
        }
        compat = a.preferH264;
    }
    cy += gfx::S(28);

    // Подсказка под переключателем: без неё кажется, будто 4K недоступен
    // из-за режима совместимости.
    if (info.hasH264) {
        int maxH = 0;
        for (int h : info.heights) maxH = std::max(maxH, h);
        if (maxH > 1080) {
            R().Text(TR(L"H.264 — до 1080p; выше нужен VP9/AV1"), fonts::TINY, 400, theme::textMute,
                     RectF(x, cy, gfx::S(460), gfx::S(16)),
                     Align::Left, VAlign::Middle, true);
            cy += gfx::S(22);
        }
    }

    std::vector<std::wstring> qualities;
    qualities.push_back(L"best");
    // Показываем все разрешения ролика, а не только те, что есть в H.264:
    // 4K на YouTube существует лишь в VP9/AV1, и прятать его из списка
    // означало бы запретить пользователю выбрать высокое качество.
    for (int h : info.heights) qualities.push_back(std::to_wstring(h));
    qualities.push_back(L"audio");

    Flow flow;
    flow.Init(RectF(x, cy, right - x, 0), gfx::S(40), gfx::S(8));
    for (size_t i = 0; i < qualities.size(); ++i) {
        const std::wstring& q = qualities[i];
        std::wstring label = (q == L"best") ? TR(L"Максимум") : (q == L"audio" ? TR(L"Только аудио") : q + L"p");
        bool sel = (a.exactFormat.empty() && a.quality == q);
        // Совместимость по кодеку действует только там, где H.264 вообще бывает (≤1080)
        bool qCompat = compat && q != L"audio" && (q == L"best" || _wtoi(q.c_str()) <= 1080);
        uint64_t sz = EstimateSize(info, q, q == L"audio", qCompat);
        std::wstring sub = sz ? (L"~" + util::FormatBytes(sz)) : L"";

        Icon ic = (q == L"audio") ? Icon::Music : (q == L"best" ? Icon::Sparkles : Icon::Video);
        float w = ui::ChipWithSubWidth(label, sub, ic);
        RectF cr = flow.Take(w, gfx::S(40));

        if (ui::ChipWithSub(1400 + (int)i, cr, label, sub, sel, ic)) {
            // Выбрали разрешение выше 1080 — H.264 там не существует, поэтому
            // режим совместимости молча отключаем, иначе обещание не выполнимо.
            if (q != L"best" && q != L"audio" && _wtoi(q.c_str()) > 1080 && a.preferH264) {
                a.preferH264 = false;
                a.store.Get().preferH264 = false;
                a.store.Save();
                a.exactFormat.clear();
                a.exactFormatLabel.clear();
                toast::Show(TR(L"H.264 бывает только до 1080p — для этого качества нужен VP9/AV1"),
                            toast::Kind::Info, 5.0f);
                toast::Show(TR(L"4K доступно только в VP9/AV1 — штатный плеер Windows может его не открыть"),
                            toast::Kind::Warn, 5.0f);
            }
            a.quality = q;
            a.exactFormat.clear();
            a.exactFormatLabel.clear();
            a.audioOnly = (q == L"audio");
            a.store.Get().defaultQuality = q;
        }
    }
    cy = flow.Bottom() + gfx::S(18);

    // ── Контейнер / аудиоформат ──
    R().Text(a.audioOnly ? TR(L"Формат аудио") : TR(L"Формат контейнера"), fonts::H3, 600, theme::textDim,
             RectF(x, cy, gfx::S(240), gfx::S(20)), Align::Left, VAlign::Middle, false);
    cy += gfx::S(28);

    std::vector<std::wstring> opts;
    if (a.audioOnly) opts = { L"mp3", L"m4a", L"opus", L"wav", L"flac" };
    else opts = { L"mp4", L"mkv", L"webm", L"auto" };

    Flow flow2;
    flow2.Init(RectF(x, cy, right - x, 0), gfx::S(36), gfx::S(8));
    for (size_t i = 0; i < opts.size(); ++i) {
        std::wstring txt = (opts[i] == L"auto") ? TR(L"Исходный") : util::ToUpper(opts[i]);
        float w = R().MeasureText(txt, fonts::SMALL, 600) + gfx::S(38);
        RectF cr = flow2.Take(w, gfx::S(36));
        bool sel = ((a.audioOnly ? a.audioFormat : a.container) == opts[i]);
        if (ui::Chip(1500 + (int)i, cr, txt, sel)) {
            if (a.audioOnly) a.audioFormat = opts[i];
            else a.container = opts[i];
        }
    }
}

// ── Главная: параметры сохранения ─────────────────────────────────────────

static void DrawOptions(AppState& a, const RectF& r) {
    ui::Card(1600, r, false, S(20));
    float pad = S(26);
    float x = r.x + pad;
    float right = r.right() - pad;
    float y = r.y + S(20);

    R().Text(TR(L"Куда сохранять"), fonts::H2, 600, theme::text, RectF(x, y, gfx::S(300), gfx::S(26)),
             Align::Left, VAlign::Middle, false);
    y += gfx::S(40);

    // папка
    RectF openBtn(right - gfx::S(42), y, gfx::S(42), gfx::S(42));
    RectF folderBtn(openBtn.x - gfx::S(52), y, gfx::S(42), gfx::S(42));
    RectF folderField(x, y, folderBtn.x - x - gfx::S(14), gfx::S(42));
    icons::Draw(R(), Icon::Folder, RectF(folderField.x + gfx::S(13), folderField.cy() - gfx::S(9), gfx::S(18), gfx::S(18)),
                theme::textMute, 1.9f);
    std::wstring dir = a.outDir;
    ui::TextField(1601, RectF(folderField.x + gfx::S(44), folderField.y, folderField.w - gfx::S(52), folderField.h),
                  dir, TR(L"Папка для загрузок"));
    a.outDir = dir;
    if (ui::IconButton(1602, folderBtn, Icon::Folder, TR(L"Выбрать папку"))) {
        std::wstring pick = util::SelectFolderDialog(a.hwnd, a.outDir);
        if (!pick.empty()) {
            a.outDir = pick;
            a.store.Get().downloadDir = pick;
            util::EnsureDir(pick);
            a.store.Save();
            toast::Show(TR(L"Папка загрузок изменена"), toast::Kind::Success, 2.2f);
        }
    }
    if (ui::IconButton(1603, openBtn, Icon::External, TR(L"Открыть папку")))
        util::RevealInExplorer(a.outDir);
    y += gfx::S(54);

    // имя файла
    R().Text(TR(L"Имя файла"), fonts::H3, 600, theme::textDim, RectF(x, y + gfx::S(10), gfx::S(110), gfx::S(18)),
             Align::Left, VAlign::Middle, false);
    std::wstring tpl = a.fileNameTemplate;
    ui::TextField(1604, RectF(x + gfx::S(112), y, right - (x + gfx::S(112)), gfx::S(38)), tpl, L"%(title)s.%(ext)s");
    a.fileNameTemplate = tpl;
    y += gfx::S(46);

    R().Text(TR(L"Поддерживаются: %(title)s  %(id)s  %(ext)s  %(upload_date)s  %(duration)s"), fonts::TINY, 400,
             theme::textMute, RectF(x, y, right - x, gfx::S(18)), Align::Left, VAlign::Middle, true);
    y += gfx::S(26);

    // переключатели
    Settings& st = a.store.Get();
    float colW = (right - x - gfx::S(24)) / 2;
    struct Sw { const wchar_t* label; bool* val; Icon ic; int id; };
    std::vector<Sw> sws = {
        { TR(L"Встроить обложку"), &st.embedThumbnail, Icon::Thumbnail, 1605 },
        { TR(L"Метаданные в файл"), &st.embedMeta, Icon::Info, 1607 },
        { TR(L"Скачать субтитры"), &st.writeSubs, Icon::Subtitles, 1606 },
    };
    for (size_t i = 0; i < sws.size(); ++i) {
        float sx = x + (float)(i % 2) * (colW + gfx::S(24));
        float sy = y + (float)(i / 2) * gfx::S(44);
        R().Text(sws[i].label, fonts::BODY, 400, theme::text, RectF(sx, sy, colW - gfx::S(58), gfx::S(32)),
                 Align::Left, VAlign::Middle, true);
        bool before = *sws[i].val;
        if (ui::Toggle(sws[i].id, RectF(sx + colW - gfx::S(52), sy + gfx::S(2), gfx::S(52), gfx::S(30)),
                       *sws[i].val, sws[i].ic) && before != *sws[i].val)
            a.store.Save();
    }
}

// ── Главная: дополнительно ───────────────────────────────────────────────

static void DrawAdvanced(AppState& a, const RectF& r) {
    if (r.h < gfx::S(60)) return;
    float full = gfx::S(470);
    R().PushClip(r);
    ui::Card(1701, RectF(r.x, r.y, r.w, full), false, S(20));

    float pad = gfx::S(26);
    float x = r.x + pad;
    float right = r.right() - pad;
    float y = r.y + gfx::S(20);

    R().Text(TR(L"Дополнительно"), fonts::H2, 600, theme::text, RectF(x, y, gfx::S(300), gfx::S(26)),
             Align::Left, VAlign::Middle, false);
    y += gfx::S(42);

    Settings& st = a.store.Get();
    float colW = (right - x - gfx::S(32)) / 2;

    // лимит скорости
    R().Text(TR(L"Ограничение скорости"), fonts::BODY, 400, theme::text,
             RectF(x, y, colW - gfx::S(140), gfx::S(24)), Align::Left, VAlign::Middle, false);
    std::wstring rateText = a.rateLimit <= 0 ? TR(L"Без ограничений") : (std::to_wstring(a.rateLimit) + TR(L" КБ/с"));
    R().Text(rateText, fonts::SMALL, 400, theme::textDim, RectF(x + colW - gfx::S(150), y, gfx::S(150), gfx::S(24)),
             Align::Right, VAlign::Middle, false);

    float trackW = colW;
    float ty = y + gfx::S(36);
    R().FillRound(RectF(x, ty + gfx::S(8), trackW, gfx::S(6)), gfx::S(3), theme::surfaceAlt);
    float frac = std::clamp(a.rateLimit / 8192.0f, 0.0f, 1.0f);
    if (frac > 0.002f)
        R().FillGradH(RectF(x, ty + gfx::S(8), trackW * frac, gfx::S(6)), gfx::S(3), theme::A1(), theme::A2());
    float kx = x + trackW * frac;
    for (int i = 0; i < 4; ++i) {
        float t = (float)(i + 1) / 4.0f;
        float alpha = 64.0f * (1.0f - t);
        if (alpha < 2) continue;
        R().Ellipse(kx, ty + gfx::S(11), gfx::S(16) * t, gfx::S(16) * t,
                    Color(theme::A1().r, theme::A1().g, theme::A1().b, (BYTE)alpha));
    }
    R().Ellipse(kx, ty + gfx::S(11), gfx::S(9), gfx::S(9), Color(242, 246, 253));
    RectF track(x, ty, trackW, gfx::S(24));
    if (ui::in.down && track.Contains(ui::in.x, ui::in.y)) {
        float f = std::clamp((ui::in.x - x) / std::max(1.0f, trackW), 0.0f, 1.0f);
        a.rateLimit = (int)(f * 8192.0f / 256.0f) * 256;
    }

    // куки
    float cx = x + colW + gfx::S(32);
    R().Text(TR(L"Куки из браузера"), fonts::BODY, 400, theme::text,
             RectF(cx, y, colW - gfx::S(60), gfx::S(24)), Align::Left, VAlign::Middle, false);
    // Куки из браузера. Важно: ui::Toggle уже переключает значение — второй раз
    // переворачивать нельзя, иначе флаг всегда возвращался обратно.
    bool cb = st.cookiesFromBrowser;
    if (ui::Toggle(1702, RectF(cx + colW - gfx::S(52), y - gfx::S(2), gfx::S(52), gfx::S(30)),
                   st.cookiesFromBrowser, Icon::Cookie) && cb != st.cookiesFromBrowser) {
        a.store.Save();
        // Предупреждаем сразу: пока браузер открыт, его база кук заблокирована
        // и прочитать её невозможно. Иначе куки выглядят включёнными, но не работают.
        if (st.cookiesFromBrowser) {
            if (util::BrowserRunning(st.cookiesBrowser))
                toast::Show(TR(L"Браузер запущен — закройте его, иначе куки не прочитаются"),
                            toast::Kind::Warn, 6.0f);
            else
                toast::Show(TR(L"Куки будут взяты из выбранного браузера"), toast::Kind::Info, 3.5f);
        }
    }

    Flow bf;
    bf.Init(RectF(cx, y + gfx::S(34), colW, 0), gfx::S(32), gfx::S(7));
    const wchar_t* browsers[] = { L"chrome", L"firefox", L"edge", L"opera", L"brave" };
    std::wstring found = util::FirstInstalledBrowser();
    for (int i = 0; i < 5; ++i) {
        std::wstring b = browsers[i];
        bool has = util::BrowserInstalled(b);
        if (!found.empty() && !has) continue;      // показываем только установленные
        float w = R().MeasureText(b, fonts::TINY, 400) + gfx::S(28);
        if (ui::Chip(1710 + i, bf.Take(w, gfx::S(32)), b, util::ToLower(st.cookiesBrowser) == b)) {
            st.cookiesBrowser = b;
            a.store.Save();
            if (util::BrowserRunning(b))
                toast::Show(TR(L"Браузер запущен — закройте его, иначе куки не прочитаются"),
                            toast::Kind::Warn, 6.0f);
        }
    }
    if (found.empty())
        R().Text(TR(L"Браузер не найден — куки отключены"), fonts::TINY, 400, theme::textMute,
                 RectF(cx, y + gfx::S(72), colW, gfx::S(18)), Align::Left, VAlign::Middle, true);
    else if (st.cookiesFromBrowser && util::BrowserRunning(st.cookiesBrowser))
        // Пометка висит постоянно, пока браузер открыт: куки сейчас не сработают
        R().Text(TR(L"Браузер запущен — закройте его"), fonts::TINY, 400, theme::warning,
                 RectF(cx, y + gfx::S(72), colW, gfx::S(18)), Align::Left, VAlign::Middle, true);

    // список форматов
    y += gfx::S(100);
    R().Text(TR(L"Все форматы (точный выбор)"), fonts::H3, 600, theme::textDim,
             RectF(x, y, gfx::S(320), gfx::S(20)), Align::Left, VAlign::Middle, false);
    y += gfx::S(26);

    RectF listRect(x, y, right - x, r.y + full - y - gfx::S(22));
    float rowH = gfx::S(44);
    float contentH = (float)a.info.formats.size() * (rowH + gfx::S(7));
    ui::ScrollArea(1720, listRect, a.formatsScroll, std::max(contentH, listRect.h), [&](const RectF& content) {
        float fy = content.y;
        // Отрисовываем только то, что реально попадает в видимую область списка.
        // (Раньше сравнение шло с начала контента — при прокрутке список выглядел пустым.)
        const float visTop = listRect.y - gfx::S(70);
        const float visBottom = listRect.bottom() + gfx::S(70);
        for (size_t i = 0; i < a.info.formats.size(); ++i) {
            if (fy + rowH > visBottom) break;
            if (fy + rowH > visTop) {
            const MediaFormat& f = a.info.formats[i];
            RectF fr(content.x, fy, content.w, rowH);
            std::wstring label = f.height > 0 ? (std::to_wstring(f.height) + L"p") : TR(L"Аудио");
            if (f.height == 0 && !f.resolution.empty()) label = util::Utf8ToWide(f.resolution);
            std::wstring tag = util::ToUpper(util::Utf8ToWide(f.ext));
            bool sel = a.exactFormat == util::Utf8ToWide(f.id);

            R().FillRound(fr, gfx::S(11), sel ? Color(theme::A1().r, theme::A1().g, theme::A1().b, 42) : theme::surface);
            R().Stroke(fr, gfx::S(11), sel ? theme::A1() : theme::stroke, sel ? 1.5f : 1.0f);

            float ix = fr.x + gfx::S(13);
            icons::Draw(R(), f.hasAudio && !f.hasVideo ? Icon::Music : Icon::Video,
                        RectF(ix, fr.cy() - gfx::S(9), gfx::S(18), gfx::S(18)),
                        sel ? theme::A1() : theme::textMute, 1.9f);
            R().Text(label, fonts::SMALL, 600, sel ? theme::text : theme::textDim,
                     RectF(ix + gfx::S(26), fr.y, gfx::S(90), rowH), Align::Left, VAlign::Middle, false);

            std::wstring meta = tag + L"  ·  " + CodecShort(f.vcodec);
            if (f.hasAudio) meta += L" / " + CodecShort(f.acodec);
            if (f.fps > 0) meta += L"  ·  " + std::to_wstring((int)(f.fps + 0.5)) + L" fps";
            R().Text(meta, fonts::TINY, 400, theme::textMute, RectF(fr.x + gfx::S(130), fr.y, fr.w * 0.5f, rowH),
                     Align::Left, VAlign::Middle, true);
            if (f.filesize)
                R().Text(util::FormatBytes(f.filesize), fonts::TINY, 400, theme::textDim,
                         RectF(fr.right() - gfx::S(150), fr.y, gfx::S(88), rowH), Align::Right, VAlign::Middle, false);
            R().Text(util::Utf8ToWide(f.id), fonts::TINY, 400, theme::textMute,
                     RectF(fr.right() - gfx::S(58), fr.y, gfx::S(48), rowH), Align::Right, VAlign::Middle, true);

            if (ui::in.pressed && fr.Contains(ui::in.x, ui::in.y) && ui::in.released) {
                if (sel) { a.exactFormat.clear(); a.exactFormatLabel.clear(); }
                else { a.exactFormat = util::Utf8ToWide(f.id); a.exactFormatLabel = label + L" · " + tag; }
            }
            }
            fy += rowH + gfx::S(7);
        }
    });
    R().Pop();
}

// ── Главная: панель действий ─────────────────────────────────────────────

static void DrawActionBar(AppState& a, const RectF& r) {
    float pad = gfx::S(26);
    R().FillGradH(r, gfx::S(20), Color(25, 32, 46), Color(19, 24, 36));
    R().Stroke(r, gfx::S(20), Color(255, 255, 255, 22), 1.0f);

    std::wstring hint = a.exactFormat.empty() ? AppQualityLabel(a, a.audioOnly ? L"audio" : a.quality)
                                              : TR(L"Формат: ") + a.exactFormatLabel;
    R().Text(TR(L"Будет сохранено как"), fonts::TINY, 400, theme::textMute,
             RectF(r.x + pad, r.y + gfx::S(16), gfx::S(360), gfx::S(18)), Align::Left, VAlign::Middle, false);
    R().Text(hint, fonts::BODY, 500, theme::text, RectF(r.x + pad, r.y + gfx::S(36), gfx::S(400), gfx::S(24)),
             Align::Left, VAlign::Middle, true);

    float by = r.y + (r.h - gfx::S(50)) / 2;
    float bh = gfx::S(50);
    float x = r.right() - pad;

    if (ui::Button(1801, RectF(x - gfx::S(48), by, gfx::S(48), bh), L"", Icon::Clipboard, ui::Btn::Ghost)) {
        util::SetClipboardText(util::Utf8ToWide(a.info.webpageUrl));
        toast::Show(TR(L"Ссылка скопирована"), toast::Kind::Success, 2.0f);
    }
    x -= gfx::S(58);

    if (ui::Button(1803, RectF(x - gfx::S(184), by, gfx::S(184), bh),
                   a.advancedOpen ? TR(L"Скрыть дополнительно") : TR(L"Дополнительно"),
                   a.advancedOpen ? Icon::ChevronDown : Icon::ChevronRight, ui::Btn::Secondary))
        a.advancedOpen = !a.advancedOpen;
    x -= gfx::S(194);

    if (ui::Button(1802, RectF(x - gfx::S(220), by, gfx::S(220), bh), TR(L"Скачать"), Icon::Download, ui::Btn::Primary))
        AppStartDownload(a);
}

// ── Главная ───────────────────────────────────────────────────────────────

static void DrawTips(AppState& a, const RectF& r) {
    ui::Card(2000, r, true, S(20));
    float x = r.x + gfx::S(28);
    R().Text(TR(L"Как пользоваться"), fonts::H2, 600, theme::text, RectF(x, r.y + gfx::S(20), gfx::S(300), gfx::S(26)),
             Align::Left, VAlign::Middle, false);
    const wchar_t* steps[3] = {
        TR(L"Скопируйте ссылку на видео из YouTube, TikTok или другого сервиса"),
        TR(L"Вставьте её в поле выше (Ctrl+V) и нажмите «Найти»"),
        TR(L"Выберите качество и формат, затем нажмите «Скачать»"),
    };
    float sy = r.y + gfx::S(60);
    for (int i = 0; i < 3; ++i) {
        Color c = theme::A1();
        R().Ellipse(x + gfx::S(10), sy + gfx::S(12), gfx::S(10), gfx::S(10), Color(c.r, c.g, c.b, 44));
        R().Text(std::to_wstring(i + 1), fonts::TINY, 700, c, RectF(x, sy + gfx::S(2), gfx::S(20), gfx::S(20)),
                 Align::Center, VAlign::Middle, false);
        R().Text(steps[i], fonts::SMALL, 400, theme::textDim, RectF(x + gfx::S(30), sy, r.w - gfx::S(64), gfx::S(24)),
                 Align::Left, VAlign::Middle, true);
        sy += gfx::S(30);
    }
    (void)a;
}

static void DrawHome(AppState& a, const RectF& clip) {
    float pad = gfx::S(32);
    RectF inner(clip.x + pad, clip.y + pad, clip.w - pad * 2, clip.h - pad * 2);

    float urlH = gfx::S(172);
    float mediaH = a.info.ok() ? MediaCardHeight(a.info, inner.w) : 0;
    float advH = gfx::S(480);
    float advAnim = ui::Ani(1910, a.advancedOpen ? 1.0f : 0.0f, 13.0f);

    // Высота содержимого считается теми же величинами и в том же порядке, что и
    // сама вёрстка ниже. Раньше сумма велась отдельно и со временем разошлась с
    // реальными блоками — нижние элементы просто не доезжали до конца прокрутки.
    const float gap = gfx::S(22);
    float contentH = urlH + gap;
    if (a.probing) {
        contentH += gfx::S(200) + gap;
    } else if (!a.probeError.empty()) {
        contentH += gfx::S(238) + gap;
    } else if (a.info.ok()) {
        contentH += mediaH + gap;
        contentH += gfx::S(318);
        if (advAnim > 0.01f) contentH += advH * advAnim + gap;
        contentH += gfx::S(112);
    } else {
        contentH += gfx::S(184);
    }

    ui::ScrollArea(1900, inner, a.scrollHome, contentH, [&](const RectF& content) {
        float y = content.y;
        DrawUrlCard(a, RectF(content.x, y, content.w, urlH));
        y += urlH + gfx::S(22);

        if (a.probing) {
            DrawProbing(a, RectF(content.x, y, content.w, gfx::S(178)));
            y += gfx::S(200);
        } else if (!a.probeError.empty()) {
            DrawError(a, RectF(content.x, y, content.w, gfx::S(216)));
            y += gfx::S(238);
        } else if (a.info.ok()) {
            DrawMediaCard(a, RectF(content.x, y, content.w, mediaH));
            y += mediaH + gfx::S(22);

            DrawOptions(a, RectF(content.x, y, content.w, gfx::S(296)));
            y += gfx::S(318);

            if (advAnim > 0.01f) {
                DrawAdvanced(a, RectF(content.x, y, content.w, advH * advAnim));
                y += advH * advAnim + gfx::S(22);
            }

            DrawActionBar(a, RectF(content.x, y, content.w, gfx::S(92)));
            y += gfx::S(112);
        } else {
            DrawTips(a, RectF(content.x, y, content.w, gfx::S(162)));
            y += gfx::S(184);
        }
    });
}

// ── Загрузки ──────────────────────────────────────────────────────────────

static void DrawJobCard(AppState& a, Job* job, const RectF& r, int id) {
    JobState st = job->GetState();
    JobProgress pr = job->GetProgress();
    bool active = (st == JobState::Running || st == JobState::Queued);
    float appear = ui::Ani(3000 + id, 1.0f, 12.0f);
    RectF cr(r.x, r.y + (1.0f - appear) * gfx::S(12), r.w, r.h);
    ui::Card(3010 + id, cr, true, S(18));

    float pad = gfx::S(18);
    float x = cr.x + pad;
    float right = cr.right() - pad;

    Icon ic = Icon::Download;
    Color c = theme::A1();
    switch (st) {
        case JobState::Running: ic = Icon::Download;    c = theme::A1(); break;
        case JobState::Queued:  ic = Icon::Queue;       c = theme::textDim; break;
        case JobState::Paused:  ic = Icon::Pause;       c = theme::warning; break;
        case JobState::Done:    ic = Icon::CheckCircle; c = theme::success; break;
        case JobState::Failed:  ic = Icon::Error;       c = theme::danger; break;
        default: ic = Icon::Close; c = theme::textMute; break;
    }
    R().FillGradV(RectF(x, cr.y + gfx::S(16), gfx::S(44), gfx::S(44)), gfx::S(13),
                  Color(c.r, c.g, c.b, 54), Color(c.r, c.g, c.b, 22));
    icons::Draw(R(), ic, RectF(x + gfx::S(12), cr.y + gfx::S(28), gfx::S(20), gfx::S(20)), c, 2.0f);
    if (st == JobState::Running)
        ui::ProgressRing(RectF(x, cr.y + gfx::S(16), gfx::S(44), gfx::S(44)), (float)pr.pct,
                         Color(c.r, c.g, c.b, 70), c);

    float tx = x + gfx::S(58);
    R().Text(job->GetTitle().empty() ? TR(L"Загрузка…") : job->GetTitle(), fonts::BODY, 600, theme::text,
             RectF(tx, cr.y + gfx::S(14), right - tx - gfx::S(230), gfx::S(22)), Align::Left, VAlign::Middle, true);

    std::wstring status = job->GetStage();
    if (st == JobState::Running && pr.speed > 0)
        status = util::FormatSpeed(pr.speed) + L"   ·   " + util::FormatEta(pr.eta);
    R().Text(status, fonts::TINY, 400, st == JobState::Failed ? theme::danger : theme::textMute,
             RectF(tx, cr.y + gfx::S(38), right - tx - gfx::S(240), gfx::S(18)), Align::Left, VAlign::Middle, true);

    float barY = cr.y + gfx::S(68);
    // процент рисуем слева от кнопок, иначе накладывается на них
    float pctRight = right - gfx::S(104);
    R().Text(std::to_wstring((int)(pr.pct * 100)) + L"%", fonts::H3, 600, theme::textDim,
             RectF(pctRight - gfx::S(64), cr.y + gfx::S(14), gfx::S(64), gfx::S(24)), Align::Right, VAlign::Middle, false);
    ui::Progress(RectF(tx, barY, right - tx, gfx::S(8)), (float)pr.pct, theme::A1(), theme::A2(), active ? 1.0f : 0.0f);

    float ly = cr.bottom() - gfx::S(30);
    std::wstring meta;
    if (st == JobState::Done) {
        // У готового файла размер известен точно — он больше, чем оценка по дорожкам
        uint64_t real = job->GetResultSize();
        if (real) meta = util::FormatBytes(real);
        else if (pr.total) meta = util::FormatBytes(pr.total);
    } else if (pr.total > 0) {
        meta = util::FormatBytes(pr.downloaded) + TR(L" из ") + util::FormatBytes(pr.total);
    }
    if (!job->GetQualityLabel().empty()) {
        if (!meta.empty()) meta += L"   ·   ";
        meta += job->GetQualityLabel();
    }
    R().Text(meta, fonts::TINY, 400, theme::textMute, RectF(tx, ly, gfx::S(440), gfx::S(18)),
             Align::Left, VAlign::Middle, true);
    if (st == JobState::Failed) {
        std::wstring err = job->GetError();
        if (!err.empty())
            R().Text(err, fonts::TINY, 400, theme::danger, RectF(tx + gfx::S(450), ly, right - tx - gfx::S(720), gfx::S(18)),
                     Align::Left, VAlign::Middle, true);
    }

    float bx = right;
    float bs = gfx::S(36);
    int btnSeq = 0;
    auto btn = [&](Icon icon, const wchar_t* tip, Color tint, std::function<void()> act) {
        bx -= bs + gfx::S(8);
        if (ui::IconButton(31000 + id * 16 + btnSeq++, RectF(bx, cr.y + gfx::S(14), bs, bs), icon, tip, tint))
            act();
    };
    auto remove = [&] {
        for (auto it = a.jobs.begin(); it != a.jobs.end(); ++it)
            if (it->get() == job) { a.jobs.erase(it); break; }
    };
    if (st == JobState::Running || st == JobState::Queued) {
        btn(Icon::Pause, TR(L"Пауза (файл сохранится)"), theme::warning, [&] { job->Pause(); });
        btn(Icon::Close, TR(L"Отменить"), theme::danger, [&] { job->Cancel(); });
    } else if (st == JobState::Paused) {
        btn(Icon::Play, TR(L"Продолжить"), Color(), [&] { job->Resume(AppYtdlpPath(a)); });
        btn(Icon::Trash, TR(L"Убрать из списка"), theme::danger, remove);
    } else {
        std::wstring file = job->GetFile();
        if (!file.empty()) btn(Icon::External, TR(L"Показать файл"), Color(), [&] { util::RevealInExplorer(file); });
        if (st == JobState::Failed)
            btn(Icon::Refresh, TR(L"Повторить"), theme::A1(), [&] { job->Retry(AppYtdlpPath(a)); });
        btn(Icon::Trash, TR(L"Убрать из списка"), theme::danger, remove);
    }
}

static void DrawEmptyState(const RectF& r, Icon ic, const wchar_t* title, const wchar_t* sub, int id) {
    float k = ui::Ani(id, 1.0f, 8.0f);
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2 - gfx::S(30);
    Color c = theme::A1();
    for (int i = 0; i < 4; ++i) {
        float t = (float)(i + 1) / 4.0f;
        float alpha = 26.0f * k * (1.0f - t);
        if (alpha < 1.5f) continue;
        R().Ellipse(cx, cy - gfx::S(6), gfx::S(88) * t, gfx::S(88) * t, Color(c.r, c.g, c.b, (BYTE)alpha));
    }
    R().FillGradV(RectF(cx - gfx::S(40), cy - gfx::S(40), gfx::S(80), gfx::S(80)), gfx::S(26),
                  Color(c.r, c.g, c.b, (BYTE)(42 * k)), Color(theme::A2().r, theme::A2().g, theme::A2().b, (BYTE)(20 * k)));
    R().Stroke(RectF(cx - gfx::S(40), cy - gfx::S(40), gfx::S(80), gfx::S(80)), gfx::S(26),
               Color(c.r, c.g, c.b, (BYTE)(75 * k)), 1.0f);
    icons::Draw(R(), ic, RectF(cx - gfx::S(19), cy - gfx::S(19), gfx::S(38), gfx::S(38)),
                Mix(theme::textMute, c, 0.55f * k), 1.8f);
    R().Text(title, fonts::H2, 600, theme::textDim, RectF(r.x, cy + gfx::S(52), r.w, gfx::S(26)),
             Align::Center, VAlign::Middle, false);
    R().Text(sub, fonts::SMALL, 400, theme::textMute, RectF(r.x, cy + gfx::S(80), r.w, gfx::S(22)),
             Align::Center, VAlign::Middle, true);
}

static void DrawDownloads(AppState& a, const RectF& clip) {
    float pad = gfx::S(32);
    RectF inner(clip.x + pad, clip.y + pad, clip.w - pad * 2, clip.h - pad * 2);

    if (a.jobFilter.empty()) a.jobFilter = L"active";

    std::vector<std::pair<std::wstring, std::wstring>> filters = {
        { TR(L"Активные"), L"active" }, { TR(L"Все"), L"all" }, { TR(L"Завершённые"), L"done" }, { TR(L"Ошибки"), L"error" },
    };
    auto countFor = [&](const std::wstring& f) {
        int c = 0;
        for (auto& j : a.jobs) {
            JobState s = j->GetState();
            if (f == L"active" && (s == JobState::Running || s == JobState::Queued)) c++;
            else if (f == L"all") c++;
            else if (f == L"done" && s == JobState::Done) c++;
            else if (f == L"error" && s == JobState::Failed) c++;
        }
        return c;
    };
    float fy = inner.y;
    float fx = inner.x;
    for (size_t i = 0; i < filters.size(); ++i) {
        int cnt = countFor(filters[i].second);
        std::wstring label = filters[i].first + (cnt ? (L"   " + std::to_wstring(cnt)) : L"");
        float w = R().MeasureText(label, fonts::SMALL, 600) + gfx::S(42);
        if (ui::Chip(2100 + (int)i, RectF(fx, fy, w, gfx::S(36)), label, a.jobFilter == filters[i].second))
            a.jobFilter = filters[i].second;
        fx += w + gfx::S(8);
    }
    if (ui::Button(2110, RectF(inner.right() - gfx::S(190), fy, gfx::S(190), gfx::S(36)), TR(L"Очистить завершённые"),
                   Icon::Trash, ui::Btn::Ghost))
        a.jobs.erase(std::remove_if(a.jobs.begin(), a.jobs.end(),
                                    [](const std::unique_ptr<Job>& j) {
                                        JobState s = j->GetState();
                                        return s == JobState::Done || s == JobState::Canceled;
                                    }),
                     a.jobs.end());

    fy += gfx::S(52);

    std::vector<Job*> list;
    for (auto& j : a.jobs) {
        JobState s = j->GetState();
        if (a.jobFilter == L"active" && !(s == JobState::Running || s == JobState::Queued)) continue;
        if (a.jobFilter == L"done" && s != JobState::Done) continue;
        if (a.jobFilter == L"error" && s != JobState::Failed) continue;
        list.push_back(j.get());
    }

    if (list.empty()) {
        DrawEmptyState(RectF(inner.x, fy, inner.w, inner.bottom() - fy - gfx::S(20)), Icon::Queue,
                       TR(L"Здесь пока пусто"), TR(L"Запустите загрузку с главной страницы"), 2200);
        return;
    }

    float cardH = gfx::S(116);
    float contentH = (float)list.size() * (cardH + gfx::S(12));
    ui::ScrollArea(2300, RectF(inner.x, fy, inner.w, inner.bottom() - fy), a.scrollDownloads,
                   std::max(contentH, 1.0f), [&](const RectF& content) {
        float y = content.y;
        for (size_t i = 0; i < list.size(); ++i) {
            DrawJobCard(a, list[i], RectF(content.x, y, content.w, cardH), (int)i);
            y += cardH + gfx::S(12);
        }
    });
}

// ── История ───────────────────────────────────────────────────────────────

static void DrawHistory(AppState& a, const RectF& clip) {
    float pad = gfx::S(32);
    RectF inner(clip.x + pad, clip.y + pad, clip.w - pad * 2, clip.h - pad * 2);

    RectF stats(inner.x, inner.y, inner.w, gfx::S(76));
    ui::Card(2400, stats, true, S(18));
    int count = (int)a.store.HistorySize();
    uint64_t bytes = a.store.HistoryBytes();
    auto stat = [&](float cx, const std::wstring& value, const std::wstring& label, Icon ic, Color c) {
        R().FillGradV(RectF(cx, stats.y + gfx::S(17), gfx::S(42), gfx::S(42)), gfx::S(13),
                      Color(c.r, c.g, c.b, 50), Color(c.r, c.g, c.b, 20));
        icons::Draw(R(), ic, RectF(cx + gfx::S(11), stats.y + gfx::S(28), gfx::S(20), gfx::S(20)), c, 1.9f);
        R().Text(value, fonts::H2, 600, theme::text, RectF(cx + gfx::S(54), stats.y + gfx::S(16), gfx::S(260), gfx::S(24)),
                 Align::Left, VAlign::Middle, true);
        R().Text(label, fonts::TINY, 400, theme::textMute, RectF(cx + gfx::S(54), stats.y + gfx::S(42), gfx::S(260), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);
    };
    stat(inner.x + gfx::S(22), std::to_wstring(count), TR(L"файлов в истории"), Icon::Video, theme::A1());
    stat(inner.x + gfx::S(260), util::FormatBytes(bytes), TR(L"суммарный объём"), Icon::Download, theme::success);
    stat(inner.x + gfx::S(500), a.store.Get().downloadDir, TR(L"папка загрузок"), Icon::Folder, theme::warning);

    float fy = inner.y + gfx::S(94);
    RectF search(inner.x, fy, inner.w - gfx::S(200), gfx::S(44));
    icons::Draw(R(), Icon::Search, RectF(search.x + gfx::S(14), search.cy() - gfx::S(9), gfx::S(18), gfx::S(18)),
                theme::textMute, 1.9f);
    std::wstring q = a.historySearch;
    ui::TextField(2450, RectF(search.x + gfx::S(44), search.y, search.w - gfx::S(54), search.h), q,
                  TR(L"Поиск по названию…"));
    a.historySearch = q;
    if (ui::Button(2451, RectF(inner.right() - gfx::S(190), fy, gfx::S(190), gfx::S(44)), TR(L"Очистить историю"),
                   Icon::Trash, ui::Btn::Soft))
        a.store.ClearHistory();
    fy += gfx::S(60);

    std::vector<HistoryItem> items;
    for (const auto& h : a.store.History()) {
        if (a.historySearch.empty() || util::IContains(h.title, a.historySearch) ||
            util::IContains(h.url, a.historySearch))
            items.push_back(h);
    }

    if (items.empty()) {
        DrawEmptyState(RectF(inner.x, fy, inner.w, inner.bottom() - fy), Icon::History,
                       TR(L"История пуста"), TR(L"Скачанные файлы появятся здесь"), 2500);
        return;
    }

    float rowH = gfx::S(78);
    float contentH = (float)items.size() * (rowH + gfx::S(10));
    int seq = 0;
    ui::ScrollArea(2550, RectF(inner.x, fy, inner.w, inner.bottom() - fy), a.scrollHistory,
                   std::max(contentH, 1.0f), [&](const RectF& content) {
        float y = content.y;
        for (size_t i = 0; i < items.size(); ++i) {
            const HistoryItem& h = items[i];
            RectF fr(content.x, y, content.w, rowH);
            bool exists = util::FileExists(h.filePath);
            ui::Card(2600 + (int)i, fr, true, S(16));

            Color fc = exists ? theme::A1() : theme::textMute;
            R().FillGradV(RectF(fr.x + gfx::S(14), fr.y + gfx::S(19), gfx::S(40), gfx::S(40)), gfx::S(12),
                          Color(fc.r, fc.g, fc.b, 50), Color(fc.r, fc.g, fc.b, 18));
            icons::Draw(R(), exists ? ((h.container == L"mp3") ? Icon::Music : Icon::Video) : Icon::Warn,
                        RectF(fr.x + gfx::S(24), fr.y + gfx::S(29), gfx::S(20), gfx::S(20)), fc, 1.9f);

            R().Text(h.title, fonts::BODY, 600, exists ? theme::text : theme::textDim,
                     RectF(fr.x + gfx::S(68), fr.y + gfx::S(15), fr.w - gfx::S(330), gfx::S(22)),
                     Align::Left, VAlign::Middle, true);

            std::wstring meta = util::FormatDateTime(h.when);
            if (!h.quality.empty()) meta += L"   ·   " + h.quality;
            if (h.size) meta += L"   ·   " + util::FormatBytes(h.size);
            if (!exists) meta += TR(L"   ·   файл не найден");
            R().Text(meta, fonts::TINY, 400, exists ? theme::textMute : theme::warning,
                     RectF(fr.x + gfx::S(68), fr.y + gfx::S(39), fr.w - gfx::S(330), gfx::S(18)),
                     Align::Left, VAlign::Middle, true);

            float bx = fr.right() - gfx::S(16);
            auto btn = [&](Icon ic, const wchar_t* tip, Color tint, std::function<void()> act) {
                bx -= gfx::S(36) + gfx::S(8);
                if (ui::IconButton(27000 + seq++, RectF(bx, fr.cy() - gfx::S(18), gfx::S(36), gfx::S(36)), ic, tip, tint))
                    act();
            };
            btn(Icon::External, exists ? TR(L"Показать файл") : TR(L"Открыть папку"), Color(), [&] {
                if (exists) { util::RevealInExplorer(h.filePath); a.store.MarkOpened(h.url); }
                else util::RevealInExplorer(util::DirName(h.filePath));
            });
            btn(Icon::Link, TR(L"Скопировать ссылку"), Color(), [&] {
                util::SetClipboardText(h.url);
                toast::Show(TR(L"Ссылка скопирована"), toast::Kind::Success, 2.0f);
            });
            btn(Icon::Trash, TR(L"Убрать из истории"), theme::danger, [&] {
                a.store.RemoveHistory(h.url);
                a.store.Save();
            });

            y += rowH + gfx::S(10);
        }
    });
}

// ── Настройки ─────────────────────────────────────────────────────────────

static void DrawSettings(AppState& a, const RectF& clip) {
    float pad = gfx::S(32);
    RectF inner(clip.x + pad, clip.y + pad, clip.w - pad * 2, clip.h - pad * 2);
    Settings& st = a.store.Get();

    // 230 (компоненты) + 364 (загрузки) + 220 (формат) + 150 (куки)
// + 234 (интерфейс) + 150 (данные) + запас снизу
float contentH = gfx::S(1348);
    ui::ScrollArea(3000, inner, a.scrollSettings, contentH, [&](const RectF& content) {
        float y = content.y;
        float W = content.w;
        bool dirty = false;

        // ── Компоненты ───────────────────────────────────────────────────
        RectF comp(content.x, y, W, gfx::S(210));
        ui::Card(3100, comp, false, S(20));
        R().Text(TR(L"Компоненты"), fonts::H2, 600, theme::text, RectF(comp.x + gfx::S(26), comp.y + gfx::S(18), gfx::S(320), gfx::S(26)),
                 Align::Left, VAlign::Middle, false);
        R().Text(TR(L"yt-dlp скачивает видео, ffmpeg склеивает дорожки и извлекает звук"), fonts::TINY, 400,
                 theme::textMute, RectF(comp.x + gfx::S(26), comp.y + gfx::S(46), gfx::S(600), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);

        std::wstring ytdlp = AppYtdlpPath(a);
        std::wstring ffmpeg = AppFfmpegPath(a);
        bool hasY = engine::HasYtdlp(ytdlp), hasF = engine::HasFfmpeg(ffmpeg);

        for (int i = 0; i < 2; ++i) {
            const wchar_t* name = (i == 0) ? L"yt-dlp.exe" : L"ffmpeg.exe";
            bool has = (i == 0) ? hasY : hasF;
            std::wstring path = (i == 0) ? ytdlp : ffmpeg;
            RectF cr(comp.x + gfx::S(26), comp.y + gfx::S(74) + i * gfx::S(62), W - gfx::S(52), gfx::S(54));
            R().FillGradH(cr, gfx::S(13), Color(255, 255, 255, 10), Color(255, 255, 255, 5));
            R().Stroke(cr, gfx::S(13), Color(255, 255, 255, 16), 1.0f);

            Color stc = has ? theme::success : theme::warning;
            R().Ellipse(cr.x + gfx::S(21), cr.y + gfx::S(27), gfx::S(5), gfx::S(5), stc);
            R().Text(name, fonts::BODY, 600, theme::text, RectF(cr.x + gfx::S(38), cr.y + gfx::S(8), gfx::S(160), gfx::S(20)),
                     Align::Left, VAlign::Middle, false);
            std::wstring sub = has ? util::BaseName(path) : TR(L"не установлен");
            if (i == 0 && has && !a.ytdlpVersionCache.empty()) sub += L"   ·   " + a.ytdlpVersionCache;
            R().Text(sub, fonts::TINY, 400, theme::textMute, RectF(cr.x + gfx::S(38), cr.y + gfx::S(28), gfx::S(380), gfx::S(18)),
                     Align::Left, VAlign::Middle, true);

            float x = cr.right() - gfx::S(14);
            int seq = 0;
            auto btn = [&](const wchar_t* label, Icon ic, std::function<void()> act, bool primary = false) {
                float w = R().MeasureText(label, fonts::SMALL, 600) + gfx::S(44);
                x -= w;
                if (ui::Button(3200 + i * 10 + seq++, RectF(x, cr.y + gfx::S(8), w, gfx::S(38)), label, ic,
                               primary ? ui::Btn::Primary : ui::Btn::Soft))
                    act();
                x -= gfx::S(8);
            };
            if (has) {
                btn(TR(L"Обзор"), Icon::Folder, [&] {
                    std::wstring pick = util::SelectFileDialog(a.hwnd, util::BinDir(), TR(L"Исполняемые файлы (*.exe)\0*.exe\0\0"));
                    if (!pick.empty()) { if (i == 0) { st.ytdlpPath = pick; dirty = true; } else { st.ffmpegPath = pick; dirty = true; } }
                });
                btn(TR(L"Обновить"), Icon::Refresh, [&] {
                    const wchar_t* url = (i == 0)
                        ? L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
                        : L"https://github.com/eugeneware/ffmpeg-static/releases/download/b6.0/ffmpeg-win32-x64";
                    std::wstring dest = util::PathJoin(util::BinDir(), name);
                    auto state = std::make_shared<engine::ComponentDownload>();
                    if (i == 0) a.dlYtdlp = state; else a.dlFfmpeg = state;
                    engine::StartComponentDownload(url, dest, state);
                    toast::Show(std::wstring(TR(L"Загрузка ")) + name + L"…", toast::Kind::Info);
                });
            } else {
                btn(TR(L"Установить"), Icon::Download, [&] {
                    const wchar_t* url = (i == 0)
                        ? L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
                        : L"https://github.com/eugeneware/ffmpeg-static/releases/download/b6.0/ffmpeg-win32-x64";
                    std::wstring dest = util::PathJoin(util::BinDir(), name);
                    auto state = std::make_shared<engine::ComponentDownload>();
                    if (i == 0) a.dlYtdlp = state; else a.dlFfmpeg = state;
                    engine::StartComponentDownload(url, dest, state);
                    toast::Show(std::wstring(TR(L"Загрузка ")) + name + L"…", toast::Kind::Info);
                }, true);
            }

            auto& dl = (i == 0) ? a.dlYtdlp : a.dlFfmpeg;
            if (dl && dl->running) {
                float p = dl->total ? (float)((double)dl->received / (double)dl->total) : 0.0f;
                ui::Progress(RectF(cr.x + gfx::S(38), cr.y + gfx::S(44), cr.w - gfx::S(52), gfx::S(4)),
                             (float)p, theme::A1(), theme::A2(), 1.0f);
            }
        }
        R().Text(TR(L"Папка компонентов: ") + util::BinDir(), fonts::TINY, 400, theme::textMute,
                 RectF(comp.x + gfx::S(26), comp.bottom() - gfx::S(22), W - gfx::S(52), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);
        y += gfx::S(230);

        // ── Загрузки ─────────────────────────────────────────────────────
        RectF down(content.x, y, W, gfx::S(344));
        ui::Card(3300, down, false, S(20));
        R().Text(L"Загрузки", fonts::H2, 600, theme::text, RectF(down.x + gfx::S(26), down.y + gfx::S(18), gfx::S(320), gfx::S(26)),
                 Align::Left, VAlign::Middle, false);
        R().Text(TR(L"Куда сохранять и как называть файлы"), fonts::TINY, 400, theme::textMute,
                 RectF(down.x + gfx::S(26), down.y + gfx::S(46), gfx::S(400), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);

        float x = down.x + gfx::S(26);
        float ry = down.y + gfx::S(76);
        R().Text(TR(L"Папка загрузок"), fonts::BODY, 400, theme::text, RectF(x, ry, gfx::S(160), gfx::S(22)),
                 Align::Left, VAlign::Middle, false);
        RectF dirBtn(down.right() - gfx::S(26) - gfx::S(40) - gfx::S(10), ry - gfx::S(4), gfx::S(40), gfx::S(40));
        RectF dirField(x + gfx::S(170), ry - gfx::S(4), dirBtn.x - (x + gfx::S(170)) - gfx::S(12), gfx::S(40));
        std::wstring dir = st.downloadDir;
        ui::TextField(3310, dirField, dir, TR(L"Папка"));
        if (dir != st.downloadDir) { st.downloadDir = dir; dirty = true; }
        if (ui::IconButton(3311, dirBtn, Icon::Folder, TR(L"Обзор"))) {
            std::wstring pick = util::SelectFolderDialog(a.hwnd, st.downloadDir);
            if (!pick.empty()) { st.downloadDir = pick; util::EnsureDir(pick); dirty = true; }
        }
        ry += gfx::S(50);

        R().Text(TR(L"Шаблон имени"), fonts::BODY, 400, theme::text, RectF(x, ry, gfx::S(160), gfx::S(22)),
                 Align::Left, VAlign::Middle, false);
        RectF tplField(x + gfx::S(170), ry - gfx::S(4), down.right() - gfx::S(26) - (x + gfx::S(170)), gfx::S(40));
        std::wstring tpl = st.fileTemplate;
        ui::TextField(3313, tplField, tpl, L"%(title)s.%(ext)s");
        if (tpl != st.fileTemplate) { st.fileTemplate = tpl; dirty = true; }
        ry += gfx::S(46);
        R().Text(L"%(title)s  ·  %(id)s  ·  %(ext)s  ·  %(upload_date)s  ·  %(duration)s", fonts::TINY, 400,
                 theme::textMute, RectF(x + gfx::S(170), ry - gfx::S(8), gfx::S(520), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);
        ry += gfx::S(30);

        R().Text(TR(L"Одновременных загрузок"), fonts::BODY, 400, theme::text, RectF(x, ry, gfx::S(200), gfx::S(22)),
                 Align::Left, VAlign::Middle, false);
        RectF minus(x + gfx::S(210), ry - gfx::S(4), gfx::S(36), gfx::S(36));
        RectF counter(minus.right() + gfx::S(5), ry - gfx::S(4), gfx::S(46), gfx::S(36));
        RectF plus(counter.right() + gfx::S(5), ry - gfx::S(4), gfx::S(36), gfx::S(36));
        R().FillRound(counter, gfx::S(10), theme::surfaceAlt);
        R().Stroke(counter, gfx::S(10), theme::stroke, 1.0f);
        R().Text(std::to_wstring(st.maxParallel), fonts::BODY, 600, theme::text, counter,
                 Align::Center, VAlign::Middle, false);
        if (ui::IconButton(3314, minus, Icon::Minus, TR(L"Меньше"))) { st.maxParallel = std::max(1, st.maxParallel - 1); dirty = true; }
        if (ui::IconButton(3315, plus, Icon::Plus, TR(L"Больше"))) { st.maxParallel = std::min(8, st.maxParallel + 1); dirty = true; }
        R().Text(TR(L"Можно поставить в очередь сразу несколько ссылок"), fonts::TINY, 400, theme::textMute,
                 RectF(x + gfx::S(360), ry, gfx::S(400), gfx::S(22)), Align::Left, VAlign::Middle, true);
        ry += gfx::S(46);

        auto switchRow = [&](float sx, float sy2, float sw, const wchar_t* label, bool& val, Icon ic, int id) {
            R().Text(label, fonts::BODY, 400, theme::text, RectF(sx, sy2, sw - gfx::S(62), gfx::S(32)),
                     Align::Left, VAlign::Middle, true);
            bool before = val;
            if (ui::Toggle(id, RectF(sx + sw - gfx::S(52), sy2, gfx::S(52), gfx::S(30)), val, ic) && before != val)
                dirty = true;
        };
        float colW = (down.w - gfx::S(52) - gfx::S(24)) / 2;
        switchRow(x, ry, colW, TR(L"Уведомления о завершении"), st.notifications, Icon::Info, 3320);
        switchRow(x + colW + gfx::S(24), ry, colW, TR(L"Спрашивать перед выходом"), st.confirmExit, Icon::Warn, 3321);
        ry += gfx::S(44);
        switchRow(x, ry, colW, TR(L"Сворачивать в трей вместо выхода"), st.closeToTray, Icon::Queue, 3322);
        y += gfx::S(364);

        // ── Формат по умолчанию ──────────────────────────────────────────
        RectF def(content.x, y, W, gfx::S(200));
        ui::Card(3400, def, false, S(20));
        R().Text(TR(L"Формат по умолчанию"), fonts::H2, 600, theme::text, RectF(def.x + gfx::S(26), def.y + gfx::S(16), gfx::S(400), gfx::S(26)),
                 Align::Left, VAlign::Middle, false);

        float qx = def.x + gfx::S(26);
        float qy = def.y + gfx::S(52);
        R().Text(TR(L"Качество"), fonts::TINY, 400, theme::textMute, RectF(qx, qy, gfx::S(100), gfx::S(16)),
                 Align::Left, VAlign::Middle, false);
        std::vector<std::wstring> qs = { L"best", L"2160", L"1440", L"1080", L"720", L"480", L"360", L"audio" };
        Flow qf;
        qf.Init(RectF(qx, qy + gfx::S(20), W - gfx::S(52), 0), gfx::S(34), gfx::S(8));
        for (size_t i = 0; i < qs.size(); ++i) {
            std::wstring label = (qs[i] == L"best") ? TR(L"Максимум") : (qs[i] == L"audio" ? TR(L"Аудио") : qs[i] + L"p");
            float w = R().MeasureText(label, fonts::SMALL, 600) + gfx::S(36);
            if (ui::Chip(3410 + (int)i, qf.Take(w, gfx::S(34)), label, st.defaultQuality == qs[i])) {
                st.defaultQuality = qs[i];
                dirty = true;
            }
        }
        qy = qf.Bottom() + gfx::S(12);

        R().Text(TR(L"Контейнер"), fonts::TINY, 400, theme::textMute, RectF(qx, qy, gfx::S(100), gfx::S(16)),
                 Align::Left, VAlign::Middle, false);
        std::vector<std::wstring> cs = { L"mp4", L"mkv", L"webm", L"auto" };
        Flow cf;
        cf.Init(RectF(qx, qy + gfx::S(20), W - gfx::S(52), 0), gfx::S(34), gfx::S(8));
        for (size_t i = 0; i < cs.size(); ++i) {
            std::wstring label = (cs[i] == L"auto") ? TR(L"Исходный") : util::ToUpper(cs[i]);
            float w = R().MeasureText(label, fonts::SMALL, 600) + gfx::S(36);
            if (ui::Chip(3430 + (int)i, cf.Take(w, gfx::S(34)), label, st.defaultContainer == cs[i])) {
                st.defaultContainer = cs[i];
                dirty = true;
            }
        }
        y += gfx::S(220);

        // ── Интернет ─────────────────────────────────────────────────────
        RectF net(content.x, y, W, gfx::S(130));
        ui::Card(3500, net, false, S(20));
        R().Text(TR(L"Доступ к закрытым видео"), fonts::H2, 600, theme::text, RectF(net.x + gfx::S(26), net.y + gfx::S(16), gfx::S(420), gfx::S(26)),
                 Align::Left, VAlign::Middle, false);
        R().Text(TR(L"Куки из вашего браузера помогают скачивать видео, требующие входа"), fonts::TINY, 400,
                 theme::textMute, RectF(net.x + gfx::S(26), net.y + gfx::S(44), gfx::S(600), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);
        R().Text(TR(L"Использовать куки браузера"), fonts::BODY, 400, theme::text,
                 RectF(net.x + gfx::S(26), net.y + gfx::S(72), gfx::S(260), gfx::S(30)),
                 Align::Left, VAlign::Middle, true);
        bool cb = st.cookiesFromBrowser;
        if (ui::Toggle(3501, RectF(net.right() - gfx::S(78), net.y + gfx::S(72), gfx::S(52), gfx::S(30)),
                       st.cookiesFromBrowser, Icon::Cookie) && cb != st.cookiesFromBrowser)
            dirty = true;
        y += gfx::S(150);

        // ── Интерфейс ────────────────────────────────────────────────────
        RectF ui2(content.x, y, W, gfx::S(214));
        ui::Card(3600, ui2, false, S(20));
        R().Text(TR(L"Интерфейс"), fonts::H2, 600, theme::text, RectF(ui2.x + gfx::S(26), ui2.y + gfx::S(16), gfx::S(420), gfx::S(26)),
                 Align::Left, VAlign::Middle, false);
        R().Text(TR(L"Цветовая схема и оформление"), fonts::TINY, 400, theme::textMute,
                 RectF(ui2.x + gfx::S(26), ui2.y + gfx::S(44), gfx::S(400), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);

        float ax = ui2.x + gfx::S(26);
        float ay = ui2.y + gfx::S(74);
        for (int i = 0; i < theme::AccentCount(); ++i) {
            const theme::AccentPreset& ap = theme::Accents()[i];
            RectF dot(ax, ay, gfx::S(40), gfx::S(40));
            bool sel = st.accent == ap.name;
            float h = ui::Ani(3700 + i, (ui::Hover(dot) || sel) ? 1.0f : 0.0f, 16.0f);
            for (int k = 0; k < 3; ++k) {
                float t = (float)(k + 1) / 3.0f;
                float alpha = (44.0f + 56.0f * h) * (1.0f - t);
                if (alpha < 2) continue;
                R().Ellipse(dot.cx(), dot.cy(), gfx::S(21) * t, gfx::S(21) * t,
                            Color(ap.a.r, ap.a.g, ap.a.b, (BYTE)alpha));
            }
            R().FillGrad(RectF(dot.x + gfx::S(5), dot.y + gfx::S(5), dot.w - gfx::S(10), dot.h - gfx::S(10)),
                         gfx::S(10), ap.a, ap.b, 45.0f);
            R().Stroke(RectF(dot.x + gfx::S(1.5f), dot.y + gfx::S(1.5f), dot.w - gfx::S(3), dot.h - gfx::S(3)),
                       gfx::S(13), sel ? Color(255, 255, 255, 235) : Color(255, 255, 255, 24), sel ? 2.0f : 1.0f);
            if (ui::in.pressed && dot.Contains(ui::in.x, ui::in.y) && ui::in.released) {
                theme::gAccent = ap;
                st.accent = ap.name;
                dirty = true;
            }
            if (ui::Hover(dot)) ui::SetTooltip(dot, ap.name);
            ax += gfx::S(52);
        }

        // ── Язык интерфейса ────────────────────────────────────────────────
        float ly = ay + gfx::S(76);
        R().Text(TR(L"Язык интерфейса"), fonts::BODY, 400, theme::text,
                 RectF(ui2.x + gfx::S(26), ly, gfx::S(220), gfx::S(32)),
                 Align::Left, VAlign::Middle, false);
        {
            const auto& langs = lang::List();
            float lx = ui2.right() - gfx::S(26) - gfx::S(330);
            for (size_t i = 0; i < langs.size(); ++i) {
                const lang::Entry& e = langs[i];
                float w = R().MeasureText(e.name, fonts::SMALL, 600) + gfx::S(36);
                RectF cr = RectF(lx, ly + gfx::S(1), w, gfx::S(30));
                if (ui::Chip(3620 + (int)i, cr, e.name, st.lang == (int)e.id)) {
                    st.lang = (int)e.id;
                    dirty = true;
                }
                lx += w + gfx::S(8);
            }
        }
        y += gfx::S(234);

        // ── Данные ───────────────────────────────────────────────────────
        RectF data(content.x, y, W, gfx::S(130));
        ui::Card(3700, data, false, S(20));
        R().Text(TR(L"Данные программы"), fonts::H2, 600, theme::text, RectF(data.x + gfx::S(26), data.y + gfx::S(16), gfx::S(420), gfx::S(26)),
                 Align::Left, VAlign::Middle, false);
        R().Text(TR(L"Настройки и история: ") + util::AppDataDir(), fonts::TINY, 400, theme::textMute,
                 RectF(data.x + gfx::S(26), data.y + gfx::S(44), gfx::S(660), gfx::S(18)),
                 Align::Left, VAlign::Middle, true);
        if (ui::Button(3710, RectF(data.x + gfx::S(26), data.y + gfx::S(72), gfx::S(200), gfx::S(40)),
                       TR(L"Открыть папку"), Icon::Folder, ui::Btn::Soft))
            util::RevealInExplorer(util::AppDataDir());
        if (ui::Button(3711, RectF(data.x + gfx::S(236), data.y + gfx::S(72), gfx::S(200), gfx::S(40)),
                       TR(L"Сбросить настройки"), Icon::Refresh, ui::Btn::Ghost)) {
            if (MessageBoxW(a.hwnd, TR(L"Сбросить все настройки к значениям по умолчанию?"), L"LOADUCK",
                            MB_ICONQUESTION | MB_YESNO) == IDYES) {
                Settings def2;
                def2.downloadDir = st.downloadDir;
                st = def2;
                a.SyncFromSettings();
                dirty = true;
                toast::Show(TR(L"Настройки сброшены"), toast::Kind::Info);
            }
        }
        y += gfx::S(150);

        if (dirty) {
            a.store.Save();
            a.SyncFromSettings();
        }
    });
}

// ── Нижняя строка состояния ───────────────────────────────────────────────

void DrawStatusBar(AppState& a, const RectF& r) {
    R().Fill(r, Color(255, 255, 255, 5));
    R().Fill(RectF(r.x, r.y, r.w, gfx::S(1)), Color(255, 255, 255, 12));

    float y = r.cy();
    int running = 0, queued = 0;
    double speed = 0;
    uint64_t doneBytes = 0;
    for (auto& j : a.jobs) {
        JobState s = j->GetState();
        if (s == JobState::Running) { running++; speed += j->GetProgress().speed; }
        if (s == JobState::Queued) queued++;
        if (s == JobState::Done) doneBytes += j->GetResultSize();
    }

    struct Item { Icon ic; std::wstring text; Color c; };
    std::vector<Item> items = {
        { Icon::Download,    running ? (std::to_wstring(running) + TR(L" активных")) : TR(L"Готов"),
          running ? theme::A1() : theme::textMute },
        { Icon::Queue,       TR(L"в очереди ") + std::to_wstring(queued), theme::textMute },
        { Icon::Speed,       speed > 0 ? util::FormatSpeed(speed) : L"—", speed > 0 ? theme::success : theme::textMute },
        { Icon::CheckCircle, doneBytes ? (util::FormatBytes(doneBytes) + TR(L" скачано")) : L"—", theme::textMute },
        { Icon::Equalizer,   TR(L"параллельно ") + std::to_wstring(a.store.Get().maxParallel), theme::textMute },
    };
    float x = r.x + gfx::S(26);
    for (auto& it : items) {
        icons::Draw(R(), it.ic, RectF(x, y - gfx::S(7), gfx::S(14), gfx::S(14)), it.c, 1.8f);
        float tw = R().MeasureText(it.text, fonts::TINY, 400);
        R().Text(it.text, fonts::TINY, 400, it.c, RectF(x + gfx::S(20), y - gfx::S(10), tw + gfx::S(10), gfx::S(20)),
                 Align::Left, VAlign::Middle, false);
        x += tw + gfx::S(50);
    }
    R().Text(TR(L"Ctrl+V — вставить    ·    Ctrl+Enter — скачать    ·    Ctrl+1…4 — разделы"),
             fonts::TINY, 400, theme::textMute, RectF(r.x, y - gfx::S(10), r.w - gfx::S(26), gfx::S(20)),
             Align::Right, VAlign::Middle, false);
}

// ── Контент ───────────────────────────────────────────────────────────────

void DrawContent(AppState& a, const RectF& r) {
    DrawHeader(a, RectF(r.x, r.y, r.w, gfx::S(88)));
    RectF body(r.x, r.y + gfx::S(88), r.w, r.h - gfx::S(88) - gfx::S(46));

    switch (a.page) {
        case Page::Home:      DrawHome(a, body); break;
        case Page::Downloads: DrawDownloads(a, body); break;
        case Page::History:   DrawHistory(a, body); break;
        default:              DrawSettings(a, body); break;
    }

    DrawStatusBar(a, RectF(r.x, r.bottom() - gfx::S(46), r.w, gfx::S(46)));
}

} // namespace pages
