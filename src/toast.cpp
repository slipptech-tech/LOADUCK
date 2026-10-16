#include "toast.h"
#include "icons.h"
#include "ui.h"
#include "util.h"
#include "theme.h"
#include "gfx.h"

#include <algorithm>

namespace toast {

struct Item {
    std::wstring text;
    Kind kind;
    double born = 0;
    float life = 4.5f;
};

static std::deque<Item> s_items;

void Show(const std::wstring& text, Kind kind, float life) {
    Item it;
    it.text = text;
    it.kind = kind;
    it.born = ui::time;
    it.life = life;
    s_items.push_back(it);
    while (s_items.size() > 5) s_items.pop_front();
}

void Clear() { s_items.clear(); }

bool Alive() { return !s_items.empty(); }

void Tick() {
    for (auto it = s_items.begin(); it != s_items.end();) {
        if (ui::time - it->born > it->life) it = s_items.erase(it);
        else ++it;
    }
}

static Color KindColor(Kind k) {
    switch (k) {
        case Kind::Success: return theme::success;
        case Kind::Warn:    return theme::warning;
        case Kind::Error:   return theme::danger;
        default:            return theme::A1();
    }
}

static Icon KindIcon(Kind k) {
    switch (k) {
        case Kind::Success: return Icon::CheckCircle;
        case Kind::Warn:    return Icon::Warn;
        case Kind::Error:   return Icon::Error;
        default:            return Icon::Info;
    }
}

void Draw(Renderer& r, const RectF& area) {
    if (s_items.empty()) return;
    float sc = theme::Scale();
    float y = area.bottom() - gfx::S(20);
    float maxW = std::min(gfx::S(440), area.w * 0.55f);

    for (auto it = s_items.rbegin(); it != s_items.rend(); ++it) {
        float age = (float)(ui::time - it->born);
        float inA = std::clamp(age / 0.22f, 0.0f, 1.0f);
        float outA = std::clamp((it->life - age) / 0.4f, 0.0f, 1.0f);
        float a = std::min(inA, outA);
        if (a <= 0.002f) continue;

        Color accent = KindColor(it->kind);
        float tw = r.MeasureText(it->text, fonts::SMALL, 400);
        float w = std::min(maxW, tw + gfx::S(62));
        float h = gfx::S(50);
        float slide = (1.0f - inA) * gfx::S(46);
        RectF box(area.right() - w - gfx::S(22) + slide, y - h, w, h);

        // фон + акцентная полоса
        r.ShadowRound(box, gfx::S(13), gfx::S(16), Color(0, 0, 0, (BYTE)(130 * a)));
        r.FillRound(box, gfx::S(13), Color(26, 33, 47, (BYTE)(248 * a)));
        r.Stroke(box, gfx::S(13), Color(accent.r, accent.g, accent.b, (BYTE)(95 * a)), 1.0f);
        r.FillGradV(RectF(box.x + gfx::S(10), box.y + gfx::S(12), gfx::S(3), box.h - gfx::S(24)), gfx::S(2),
                    Color(accent.r, accent.g, accent.b, (BYTE)(245 * a)),
                    Color(accent.r, accent.g, accent.b, (BYTE)(160 * a)));

        icons::Draw(r, KindIcon(it->kind),
                    RectF(box.x + gfx::S(22), box.cy() - gfx::S(10), gfx::S(20), gfx::S(20)),
                    Color(accent.r, accent.g, accent.b, (BYTE)(255 * a)), 2.0f);
        r.Text(it->text, fonts::SMALL, 400, Color(228, 235, 247, (BYTE)(255 * a)),
               RectF(box.x + gfx::S(50), box.y, w - gfx::S(62), h), Align::Left, VAlign::Middle, true);

        y -= h + gfx::S(10);
    }
}

} // namespace toast