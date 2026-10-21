#include "ui.h"
#include "util.h"
#include "theme.h"
#include "gfx.h"

#include <algorithm>
#include <cmath>

namespace ui {

Input in;
float dt = 0.016f;
double time = 0.0;
int frameAnim = 0;

static Renderer& R() { return GR(); }
static float S(float v) { return gfx::S(v); }

static std::unordered_map<int, float> s_anim;

struct FieldState {
    size_t caret = 0;
    bool focused = false;
    double blinkBase = 0;
};
static std::unordered_map<int, FieldState> s_fields;

struct TipState {
    bool active = false;
    Rect owner;
    std::wstring text;
    double shownAt = 0;
};
static TipState s_tip;

void Begin() {
    s_tip.active = false;
    if (in.pressed)
        for (auto& kv : s_fields) kv.second.focused = false;
}

void End() {}

bool Hover(const Rect& r) { return r.Contains(in.x, in.y); }
bool Down(const Rect& r) { return in.down && r.Contains(in.x, in.y); }

float Ani(int id, float target, float speed) {
    float& cur = s_anim[id];
    if (std::isnan(cur)) cur = target;
    // Ограничиваем шаг: при просадке кадров анимация иначе «прыгает» к цели
    const float step = std::min(dt, 0.05f);
    float k = 1.0f - std::exp(-speed * step);
    if (k > 1.0f) k = 1.0f;
    cur += (target - cur) * k;
    if (std::fabs(target - cur) < 0.0008f) cur = target;
    else ++frameAnim;
    return cur;
}

void ResetAni(int id) { s_anim.erase(id); }

// ── Карточка ──────────────────────────────────────────────────────────────

bool Card(int id, const Rect& r, bool interactive, float radius) {
    if (radius <= 0) radius = S(16);
    bool hov = interactive && Hover(r);
    float a = Ani(id * 3 + 11, hov ? 1.0f : 0.0f, 13.0f);

    R().ShadowRound(r, radius, S(14), Color(0, 0, 0, (BYTE)(70 + 40 * a)));

    Color top = Mix(theme::surfaceTop, theme::surfaceHi, a * 0.8f);
    Color bottom = Mix(theme::surface, theme::bgDeep, 0.25f);
    R().FillGradV(r, radius, top, bottom);

    // верхняя подсветка
    R().Fill(Rect(r.x + radius * 0.7f, r.y + S(1), r.w - radius * 1.4f, S(1)),
             Color(255, 255, 255, (BYTE)(12 + 16 * a)));

    R().Stroke(r, radius, Mix(theme::stroke, theme::strokeHi, a), 1.0f);
    if (a > 0.02f)
        R().Stroke(r.Inflate(0.5f, 0.5f), radius + 0.5f,
                   Color(theme::A1().r, theme::A1().g, theme::A1().b, (BYTE)(90 * a)), 1.0f);
    return hov;
}

void PanelGradV(const Rect& r, float radius, Color top, Color bottom) {
    R().FillGradV(r, radius, top, bottom);
}

void Divider(const Rect& r, Color c) {
    if (c.a == 0) c = theme::stroke;
    R().Fill(Rect(r.x, r.y, r.w, std::max(1.0f, S(1))), c);
}

void PageHeading(const Rect& r, const std::wstring& title, const std::wstring& subtitle) {
    float barW = S(3.5f);
    R().FillGradV(Rect(r.x, r.y + S(3), barW, r.h - S(6)), barW * 0.5f, theme::A1(), theme::A2());
    float x = r.x + barW + S(13);
    R().Text(title, fonts::H1, 700, theme::text, Rect(x, r.y, r.w - (x - r.x), S(32)),
             Align::Left, VAlign::Middle, true);
    if (!subtitle.empty())
        R().Text(subtitle, fonts::SMALL, 400, theme::textMute, Rect(x, r.y + S(33), r.w - (x - r.x), S(20)),
                 Align::Left, VAlign::Middle, true);
}

// ── Кнопки ────────────────────────────────────────────────────────────────

bool Button(int id, const Rect& r, const std::wstring& label, Icon ic, Btn style, bool enabled) {
    float radius = S(12);
    bool hov = enabled && Hover(r);
    bool held    = enabled && in.down && r.Contains(in.x, in.y);
    bool pressed = enabled && in.pressed && in.released && r.Contains(in.x, in.y);

    float h = Ani(id * 7 + 1, hov ? 1.0f : 0.0f, 16.0f);
    float p = Ani(id * 7 + 3, held ? 1.0f : 0.0f, 24.0f);

    // лёгкое «нажатие»
    Rect rr = Rect(r.x, r.y, r.w, r.h - p * S(2.5f));

    Color c1, c2, fg, bd;
    bool elevated = false;
    switch (style) {
        case Btn::Primary:
        case Btn::Success: {
            elevated = true;
            Color base1 = (style == Btn::Success) ? theme::success : theme::A1();
            Color base2 = (style == Btn::Success) ? Mix(theme::success, theme::A1(), 0.6f) : theme::A2();
            c1 = Mix(base1, Color(255, 255, 255, 255), 0.12f + 0.14f * h);
            c2 = Mix(base2, Color(255, 255, 255, 255), 0.02f + 0.12f * h);
            fg = Color(255, 255, 255, 255);
            bd = Color(0, 0, 0, 0);
            break;
        }
        case Btn::Danger: {
            elevated = true;
            c1 = Mix(theme::danger, Color(255, 255, 255, 255), 0.10f + 0.14f * h);
            c2 = Mix(Mix(theme::danger, theme::A2(), 0.35f), Color(255, 255, 255, 255), 0.02f + 0.10f * h);
            fg = Color(255, 255, 255, 255);
            bd = Color(0, 0, 0, 0);
            break;
        }
        case Btn::Ghost:
            c1 = Color(255, 255, 255, (BYTE)(0 + 16 * h));
            c2 = c1;
            fg = enabled ? Mix(theme::textDim, theme::text, h) : theme::textMute;
            bd = Color(0, 0, 0, 0);
            break;
        case Btn::Soft:
            c1 = Mix(theme::surfaceAlt, theme::surfaceHi, h);
            c2 = Mix(theme::surface, theme::surfaceAlt, h);
            fg = enabled ? Mix(theme::textDim, theme::text, h) : theme::textMute;
            bd = Mix(theme::stroke, theme::strokeHi, h);
            break;
        default:
            c1 = Mix(theme::surfaceAlt, theme::surfaceHi, h);
            c2 = Mix(theme::surface, theme::surfaceAlt, h);
            fg = enabled ? theme::text : theme::textMute;
            bd = Mix(theme::stroke, theme::strokeHi, h);
            break;
    }
    if (!enabled) {
        elevated = false;
        c1 = theme::surfaceAlt;
        c2 = theme::surface;
        fg = theme::textMute;
        bd = theme::stroke;
    }

    if (elevated) {
        // мягкое свечение под кнопкой
        Color glow = (style == Btn::Success) ? theme::success
                   : (style == Btn::Danger) ? theme::danger : theme::A1();
        float spread = S(6) + h * S(5);
        for (int i = 0; i < 3; ++i) {
            float t = (float)(i + 1) / 3.0f;
            float grow = spread * t;
            float alpha = (26.0f + 30.0f * h) * (1.0f - t) * (1.0f - t);
            if (alpha < 2) continue;
            R().Fill(Rect(rr.x - grow, rr.y - grow * 0.6f, rr.w + grow * 2, rr.h + grow * 1.2f),
                     Color(glow.r, glow.g, glow.b, (BYTE)alpha));
        }
    } else {
        R().ShadowRound(rr, radius, S(5), Color(0, 0, 0, (BYTE)(22 + 16 * h)));
    }

    R().FillGradV(rr, radius, c1, c2);
    if (elevated)
        R().Fill(Rect(rr.x + radius * 0.6f, rr.y + S(1), rr.w - radius * 1.2f, S(1)),
                 Color(255, 255, 255, 40));
    if (bd.a > 0) R().Stroke(rr, radius, bd, 1.0f);

    float iconSize = std::min(S(18), rr.h * 0.46f);
    float textW = label.empty() ? 0.0f : R().MeasureText(label, fonts::BODY, 600);
    float iconW = (ic != Icon::None) ? iconSize : 0.0f;
    float gap = (label.empty() || ic == Icon::None) ? 0.0f : S(9);
    float total = textW + iconW + gap;
    float x0 = rr.x + (rr.w - total) / 2.0f;
    if (ic != Icon::None) {
        icons::Draw(R(), ic, Rect(x0, rr.y + (rr.h - iconSize) / 2, iconSize, iconSize), fg, 2.0f);
        x0 += iconW + gap;
    }
    if (!label.empty())
        R().Text(label, fonts::BODY, 600, fg, Rect(x0, rr.y, textW + S(4), rr.h),
                 Align::Left, VAlign::Middle, false);

    if (hov && !label.empty()) SetTooltip(r, label);
    return pressed && in.released;
}

bool IconButton(int id, const Rect& r, Icon ic, const std::wstring& tip, Color tint, bool enabled) {
    float radius = S(12);
    bool hov = enabled && Hover(r);
    bool held    = enabled && in.down && r.Contains(in.x, in.y);
    bool pressed = enabled && in.pressed && in.released && r.Contains(in.x, in.y);
    float h = Ani(id * 13 + 5, hov ? 1.0f : 0.0f, 16.0f);
    float p = Ani(id * 13 + 9, held ? 1.0f : 0.0f, 24.0f);

    Rect rr(r.x, r.y, r.w, r.h - p * S(2.0f));

    Color base, fg;
    if (tint.a) {
        base = Color(tint.r, tint.g, tint.b, (BYTE)(38 + 40 * h));
        fg = tint;
    } else {
        // Заметный фон в покое, чтобы кнопка не тонула в карточке
        base = Mix(Mix(theme::surfaceAlt, theme::surfaceHi, 0.45f), theme::surfaceHi, h);
        fg = enabled ? Mix(theme::text, theme::text, h) : theme::textMute;
    }

    if (tint.a && h > 0.02f) {
        float spread = S(5) * h;
        for (int i = 0; i < 3; ++i) {
            float t = (float)(i + 1) / 3.0f;
            float alpha = 46.0f * h * (1.0f - t) * (1.0f - t);
            if (alpha < 2) continue;
            R().Fill(Rect(rr.x - spread * t, rr.y - spread * t * 0.7f,
                          rr.w + spread * 2 * t, rr.h + spread * 1.4f * t),
                     Color(tint.r, tint.g, tint.b, (BYTE)alpha));
        }
    }

    R().FillGradV(rr, radius, base, ScaleColor(base, 0.72f));
    if (!tint.a) R().Stroke(rr, radius, Mix(theme::stroke, theme::strokeHi, h), 1.0f);

    float sz = std::min(rr.w, rr.h) * 0.52f;
    icons::Draw(R(), ic, Rect(rr.x + (rr.w - sz) / 2, rr.y + (rr.h - sz) / 2, sz, sz), fg, 2.0f);
    if (hov && !tip.empty()) SetTooltip(rr, tip);
    return pressed && in.released;
}

bool Chip(int id, const Rect& r, const std::wstring& label, bool selected, Icon ic) {
    bool hov = Hover(r);
    bool pressed = in.pressed && in.released && r.Contains(in.x, in.y);
    float s = Ani(id * 17 + 3, selected ? 1.0f : 0.0f, 15.0f);
    float h = Ani(id * 17 + 4, hov && !selected ? 1.0f : 0.0f, 18.0f);

    float radius = S(11);
    Color fill = Mix(Mix(theme::surface, theme::surfaceHi, h * 0.85f),
                     Mix(theme::A1(), theme::A2(), 0.4f), s * 0.30f);
    Color bd = Mix(Mix(theme::stroke, theme::strokeHi, h), theme::A1(), s);
    Color fg = Mix(Mix(theme::textDim, theme::text, h), theme::A1(), s);

    if (s > 0.01f) {
        float spread = S(4) * s;
        for (int i = 0; i < 2; ++i) {
            float t = (float)(i + 1) / 2.0f;
            float alpha = 30.0f * s * (1.0f - t);
            if (alpha < 2) continue;
            R().Fill(Rect(r.x - spread * t, r.y - spread * t * 0.6f, r.w + spread * 2 * t, r.h + spread * 1.2f * t),
                     Color(theme::A1().r, theme::A1().g, theme::A1().b, (BYTE)alpha));
        }
    }
    R().FillGradV(r, radius, Mix(fill, Color(255, 255, 255, 20), 0.10f), fill);
    R().Stroke(r, radius, bd, s > 0.5f ? 1.5f : 1.0f);

    float textW = R().MeasureText(label, fonts::SMALL, 400);
    float iconW = (ic != Icon::None) ? S(15) : 0;
    float gap = (ic != Icon::None && !label.empty()) ? S(7) : 0;
    float x0 = r.x + (r.w - textW - iconW - gap) / 2.0f;
    if (ic != Icon::None) {
        icons::Draw(R(), ic, Rect(x0, r.y + (r.h - S(15)) / 2, S(15), S(15)), fg, 1.9f);
        x0 += iconW + gap;
    }
    if (!label.empty())
        R().Text(label, fonts::SMALL, 400, fg, Rect(x0, r.y, textW + S(2), r.h),
                 Align::Left, VAlign::Middle, false);
    return pressed && in.released;
}

namespace {
// Единая геометрия чипа с правой подписью. Все отступы одинаковые, поэтому
// соседние кнопки в ряду выровнены по левому краю и по правому полю.
constexpr float kPad = 14.0f;      // отступ слева и справа
constexpr float kIcon = 15.0f;     // сторона иконки
constexpr float kGap = 8.0f;       // промежуток иконка↔подпись и подпись↔размер
} // namespace

float ChipWithSubWidth(const std::wstring& label, const std::wstring& sub, Icon ic) {
    float w = S(kPad) * 2;
    if (ic != Icon::None) w += S(kIcon) + S(kGap);
    w += R().MeasureText(label, fonts::SMALL, 400);
    if (!sub.empty()) w += S(kGap) + R().MeasureText(sub, fonts::TINY, 400);
    return w;
}

bool ChipWithSub(int id, const Rect& r, const std::wstring& label, const std::wstring& sub,
                 bool selected, Icon ic) {
    bool hov = Hover(r);
    bool pressed = in.pressed && in.released && r.Contains(in.x, in.y);
    float s = Ani(id * 17 + 3, selected ? 1.0f : 0.0f, 15.0f);
    float h = Ani(id * 17 + 4, hov && !selected ? 1.0f : 0.0f, 18.0f);

    const float radius = S(11);
    Color fill = Mix(Mix(theme::surface, theme::surfaceHi, h * 0.85f),
                     Mix(theme::A1(), theme::A2(), 0.4f), s * 0.30f);
    Color bd = Mix(Mix(theme::stroke, theme::strokeHi, h), theme::A1(), s);
    Color fg = Mix(Mix(theme::textDim, theme::text, h), theme::A1(), s);

    if (s > 0.01f) {
        float spread = S(4) * s;
        for (int i = 0; i < 2; ++i) {
            float t = (float)(i + 1) / 2.0f;
            float alpha = 30.0f * s * (1.0f - t);
            if (alpha < 2) continue;
            R().Fill(Rect(r.x - spread * t, r.y - spread * t * 0.6f, r.w + spread * 2 * t, r.h + spread * 1.2f * t),
                     Color(theme::A1().r, theme::A1().g, theme::A1().b, (BYTE)alpha));
        }
    }
    R().FillGradV(r, radius, Mix(fill, Color(255, 255, 255, 20), 0.10f), fill);
    R().Stroke(r, radius, bd, s > 0.5f ? 1.5f : 1.0f);

    // Всё содержимое начинается с одного и того же отступа слева
    float x0 = r.x + S(kPad);
    if (ic != Icon::None) {
        icons::Draw(R(), ic, Rect(x0, r.cy() - S(kIcon) * 0.5f, S(kIcon), S(kIcon)), fg, 1.9f);
        x0 += S(kIcon) + S(kGap);
    }
    if (!label.empty())
        R().Text(label, fonts::SMALL, 400, fg, Rect(x0, r.y, r.right() - x0, r.h),
                 Align::Left, VAlign::Middle, false);

    // Размер файла прижат к правому краю на том же отступе, что и слева
    if (!sub.empty()) {
        float subW = R().MeasureText(sub, fonts::TINY, 400);
        R().Text(sub, fonts::TINY, 400,
                 s > 0.5f ? Mix(theme::textDim, theme::A1(), s) : theme::textMute,
                 Rect(r.right() - S(kPad) - subW, r.y, subW + S(2), r.h),
                 Align::Left, VAlign::Middle, false);
    }
    return pressed && in.released;
}

bool Toggle(int id, const Rect& r, bool& on, Icon ic) {
    bool hov = Hover(r);
    float s = Ani(id * 19 + 2, on ? 1.0f : 0.0f, 15.0f);
    float h = Ani(id * 19 + 6, hov ? 1.0f : 0.0f, 16.0f);
    bool pressed = in.pressed && in.released && r.Contains(in.x, in.y);
    bool res = false;
    if (pressed) { on = !on; res = true; }

    float trW = S(44), trH = S(25);
    Rect tr(r.x + r.w - trW, r.y + (r.h - trH) / 2, trW, trH);

    // дорожка
    R().FillGradH(tr, trH / 2, Mix(theme::surfaceAlt, theme::surfaceHi, h),
                  Mix(theme::surfaceAlt, theme::A1(), s));
    R().Stroke(tr, trH / 2, Mix(Mix(theme::stroke, theme::strokeHi, h), theme::A1(), s), 1.0f);

    // ручка
    float kx = tr.x + trH / 2 + (trW - trH) * s;
    Color knob = Mix(Color(226, 233, 246), theme::A1(), s * 0.6f);
    R().Ellipse(kx, tr.y + trH / 2, trH / 2 - S(3), trH / 2 - S(3), knob);

    if (ic != Icon::None) {
        Color fg = Mix(theme::textMute, theme::A1(), s);
        icons::Draw(R(), ic, Rect(r.x, r.y, r.w - trW - S(10), r.h), fg, 1.9f);
    }
    return res;
}

// ── Поле ввода ────────────────────────────────────────────────────────────

bool TextField(int id, const Rect& r, std::wstring& value, const std::wstring& placeholder) {
    FieldState& fs = s_fields[id];
    bool hov = Hover(r);
    if (in.pressed && r.Contains(in.x, in.y)) fs.focused = true;
    float focus = Ani(id * 23 + 1, fs.focused ? 1.0f : (hov ? 0.4f : 0.0f), 14.0f);
    if (in.esc && fs.focused) fs.focused = false;

    if (fs.focused) {
        size_t caret = std::min(fs.caret, value.size());
        for (wchar_t ch : in.text)
            if (ch >= 32) { value.insert(caret, 1, ch); caret++; }
        if (in.vkHome) caret = 0;
        else if (in.vkEnd) caret = value.size();
        else if (in.vkLeft && caret > 0) caret--;
        else if (in.vkRight && caret < value.size()) caret++;
        else if (in.vkBack > 0 && caret > 0) { value.erase(caret - 1, 1); caret--; }
        else if (in.vkDel > 0 && caret < value.size()) value.erase(caret, 1);
        fs.caret = caret;
    }

    float radius = S(12);
    R().FillGradV(r, radius, Mix(theme::surface, theme::surfaceAlt, 0.55f), theme::surface);
    R().Stroke(r, radius, Mix(theme::stroke, theme::A1(), focus), 1.0f);
    if (focus > 0.02f) {
        float spread = S(4) * focus;
        for (int i = 0; i < 2; ++i) {
            float t = (float)(i + 1) / 2.0f;
            float alpha = 34.0f * focus * (1.0f - t);
            if (alpha < 2) continue;
            R().Fill(Rect(r.x - spread * t, r.y - spread * t * 0.5f, r.w + spread * 2 * t, r.h + spread * t),
                     Color(theme::A1().r, theme::A1().g, theme::A1().b, (BYTE)alpha));
        }
    }

    float padX = S(14);
    Rect tr(r.x + padX, r.y, r.w - padX * 2, r.h);
    R().PushClip(r);
    if (value.empty()) {
        R().Text(placeholder, fonts::BODY, 400, theme::textMute, tr, Align::Left, VAlign::Middle, true);
    } else {
        R().Text(value, fonts::BODY, 400, theme::text, tr, Align::Left, VAlign::Middle, true);
    }
    if (fs.focused) {
        float cx = tr.x + R().MeasureText(value.substr(0, fs.caret), fonts::BODY, 400) + 1.0f;
        bool blink = fmod(time - fs.blinkBase, 1.06) < 0.62;
        if (blink)
            R().Fill(Rect(cx, r.y + r.h * 0.28f, std::max(1.5f, S(1.8f)), r.h * 0.44f), theme::A1());
    }
    R().Pop();
    return fs.focused;
}

bool AnyFieldFocused() {
    for (auto& kv : s_fields)
        if (kv.second.focused) return true;
    return false;
}

void BlurAllFields() {
    for (auto& kv : s_fields) kv.second.focused = false;
}

// ── Индикаторы ────────────────────────────────────────────────────────────

void Progress(const Rect& r, float pct, Color c1, Color c2, float shimmer) {
    float radius = r.h / 2.0f;
    pct = std::clamp(pct, 0.0f, 1.0f);
    R().FillRound(r, radius, theme::surfaceAlt);
    R().Stroke(r, radius, Color(255, 255, 255, 12), 1.0f);
    if (pct <= 0.0005f) return;

    Rect fr(r.x, r.y, std::max(r.h, r.w * pct), r.h);
    R().FillGradH(fr, radius, c1, c2);

    if (shimmer > 0.01f && r.w > 80) {
        float head = r.w * pct;
        float sweep = std::min(S(110), r.w * 0.35f);
        float base = head - sweep * 0.6f;
        const int N = 8;
        for (int i = 0; i < N; ++i) {
            float f0 = (float)i / N, f1 = (float)(i + 1) / N;
            float mid = 0.5f * (f0 + f1);
            float a = 64.0f * std::sin(mid * 3.14159265f) * shimmer;
            if (a < 1.5f) continue;
            R().Fill(Rect(base + sweep * f0, r.y + S(1), sweep * (f1 - f0) + 1, r.h - S(2)),
                     Color(255, 255, 255, (BYTE)a));
        }
    }
}

void ProgressRing(const Rect& r, float pct, Color c1, Color c2) {
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    float rad = std::min(r.w, r.h) / 2 - S(2);
    float w = std::max(2.0f, rad * 0.2f);
    R().Ring(cx, cy, rad, theme::surfaceAlt, w);
    pct = std::clamp(pct, 0.0f, 1.0f);
    if (pct <= 0.001f) return;
    const int steps = 20;
    for (int i = 0; i < steps; ++i) {
        float f0 = (float)i / steps, f1 = (float)(i + 1) / steps;
        if (f1 > pct) break;
        R().RingArc(cx, cy, rad, Mix(c1, c2, f0), w, f0 * 360.0f - 90.0f, (f1 - f0) * 360.0f + 1.2f);
    }
}

void Spinner(const Rect& r, Color c, float thickness) {
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    float rad = std::min(r.w, r.h) / 2 - S(2);
    float w = std::max(2.0f, rad * thickness * 0.5f);
    R().Ring(cx, cy, rad, Color(c.r, c.g, c.b, 40), w);
    float start = (float)fmod(time * 230.0, 360.0);
    for (int i = 0; i < 10; ++i) {
        float f = (float)i / 10.0f;
        float a = 210.0f * (1.0f - f) + 20.0f;
        R().RingArc(cx, cy, rad, Color(c.r, c.g, c.b, (BYTE)a), w, start - f * 84.0f, 46.0f);
    }
}

void Skeleton(const Rect& r, float radius) {
    R().FillRound(r, radius, theme::surfaceAlt);
    R().PushClip(r);
    float w = r.w;
    float x = r.x + (float)fmod(time * 0.55, 1.9) * w - w * 0.9f;
    const int N = 9;
    for (int i = 0; i < N; ++i) {
        float f = (float)i / N;
        float a = 26.0f * std::max(0.0f, 1.0f - std::fabs(f - 0.5f) / 0.5f);
        if (a < 1.5f) continue;
        R().Fill(Rect(x + w * 0.11f * i, r.y, w * 0.09f, r.h), Color(255, 255, 255, (BYTE)a));
    }
    R().Pop();
}

bool ScrollArea(int id, const Rect& clip, float& scroll, float contentH, const std::function<void(const Rect&)>& body) {
    (void)id;
    bool hov = Hover(clip);
    if (hov && std::fabs(in.wheel) > 0.001f) scroll -= in.wheel * 120.0f;
    float maxScroll = std::max(0.0f, contentH - clip.h);
    scroll = std::clamp(scroll, 0.0f, maxScroll);

    R().PushClip(clip);
    if (maxScroll > 0) {
        float thumbH = std::max(S(44), clip.h * (clip.h / contentH));
        float t = scroll / maxScroll;
        float ty = clip.y + (clip.h - thumbH) * t;
        R().FillRound(Rect(clip.right() - S(7), ty, S(4), thumbH), S(2),
                      Color(255, 255, 255, (BYTE)(hov ? 150 : 80)));
    }

    Rect content(clip.x, clip.y - scroll, clip.w - (maxScroll > 0 ? S(13) : 0), contentH);
    body(content);
    R().Pop();
    return hov;
}

// ── Тултип ────────────────────────────────────────────────────────────────

void SetTooltip(const Rect& owner, const std::wstring& text) {
    if (text.empty()) return;
    s_tip.active = true;
    s_tip.owner = owner;
    s_tip.text = text;
    s_tip.shownAt = time;
}

void DrawTooltip() {
    if (!s_tip.active || s_tip.text.empty()) return;
    float age = (float)(time - s_tip.shownAt);
    float a = std::clamp((age - 0.45f) / 0.18f, 0.0f, 1.0f);
    if (a <= 0.001f) return;

    float tw = R().MeasureText(s_tip.text, fonts::SMALL, 400);
    float padX = S(12), padY = S(8);
    float w = tw + padX * 2, h = S(20) + padY * 2;
    float x = std::clamp(s_tip.owner.x + (s_tip.owner.w - w) / 2, S(12), (float)GR().Width() - w - S(12));
    float y = s_tip.owner.bottom() + S(10);
    if (y + h > (float)GR().Height() - S(12)) y = s_tip.owner.y - h - S(8);

    Rect r(x + (1 - a) * S(6), y + (1 - a) * S(6), w, h);
    R().ShadowRound(r, S(9), S(12), Color(0, 0, 0, (BYTE)(140 * a)));
    R().FillRound(r, S(9), Color(14, 19, 28, (BYTE)(246 * a)));
    R().Stroke(r, S(9), Color(255, 255, 255, (BYTE)(34 * a)), 1.0f);
    R().Text(s_tip.text, fonts::SMALL, 400, Color(230, 236, 247, (BYTE)(255 * a)), r,
             Align::Center, VAlign::Middle, false);
}

} // namespace ui
