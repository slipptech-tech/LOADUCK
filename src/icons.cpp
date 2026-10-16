#include "icons.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace icons {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Иконка рисуется в системе координат 24x24; u — размер одной единицы в пикселях.
struct P {
    Renderer* r;
    Color c;
    float u;   // пикселей на единицу
    float x, y;
    float w;   // толщина линии

    void L(float x1, float y1, float x2, float y2) {
        r->StrokeLine(x + x1 * u, y + y1 * u, x + x2 * u, y + y2 * u, c, w);
    }
    void Circ(float cx, float cy, float rad) {
        r->Ring(x + cx * u, y + cy * u, rad * u, c, w);
    }
    void Arc(float cx, float cy, float rad, float startDeg, float sweepDeg) {
        r->RingArc(x + cx * u, y + cy * u, rad * u, c, w, startDeg, sweepDeg);
    }
    void Dot(float cx, float cy, float rad) {
        r->Ellipse(x + cx * u, y + cy * u, rad * u, rad * u, c);
    }
    void Fill(float cx, float cy, float rx, float ry) {
        r->Ellipse(x + cx * u, y + cy * u, rx * u, ry * u, c);
    }
    void Box(float rx, float ry, float rw, float rh, float rad) {
        r->FillRound(RectF(x + rx * u, y + ry * u, rw * u, rh * u), rad * u, c);
    }
    void Tri(float ax, float ay, float bx, float by, float cx2, float cy2) {
        const int N = 12;
        float px = ax, py = ay, qx = bx, qy = by;
        for (int i = 1; i <= N; ++i) {
            float t = (float)i / N;
            float nx = qx + (cx2 - qx) * t;
            float ny = qy + (cy2 - qy) * t;
            L(px, py, nx, ny);
            px = nx;
            py = ny;
        }
        L(px, py, ax, ay);
    }
    void RRect(float rx, float ry, float rw, float rh, float rad) {
        r->Stroke(RectF(x + rx * u, y + ry * u, rw * u, rh * u), rad * u, c, w);
    }
};

} // namespace

void Draw(Renderer& r, Icon id, const RectF& dst, Color c, float weight) {
    if (c.a == 0) return;
    float side = std::min(dst.w, dst.h);
    if (side <= 1.f) return;

    float iconSize = side * 0.78f;          // запас вокруг рисунка
    float u = iconSize / 24.f;              // пикселей на единицу системы координат
    float x = dst.x + (side - iconSize) * 0.5f;
    float y = dst.y + (side - iconSize) * 0.5f;
    P p{ &r, c, u, x, y, std::clamp(weight * u, 1.f, side * 0.16f) };

    switch (id) {
        case Icon::Download:
            p.L(12, 3.5f, 12, 14.5f);
            p.L(6.5f, 10.0f, 12, 15.5f);
            p.L(17.5f, 10.0f, 12, 15.5f);
            p.Arc(12, 15.5f, 7.5f, 0, 180);
            break;
        case Icon::Home:
            p.L(3.5f, 11.5f, 12, 4.0f);
            p.L(12, 4.0f, 20.5f, 11.5f);
            p.L(6.5f, 10.5f, 6.5f, 20);
            p.L(6.5f, 20, 17.5f, 20);
            p.L(17.5f, 20, 17.5f, 10.5f);
            break;
        case Icon::List:
            for (int i = 0; i < 3; ++i) {
                float yy = 6.5f + i * 5.2f;
                p.Dot(4.2f, yy, 1.0f);
                p.L(8.4f, yy, 19.5f, yy);
            }
            break;
        case Icon::History:
            p.Arc(12, 12, 8.5f, -55, 310);
            p.L(12, 6.5f, 12, 12.2f);
            p.L(12, 12.2f, 15.8f, 14.4f);
            break;
        case Icon::Settings: {
            p.Circ(12, 12, 3.4f);
            for (int i = 0; i < 8; ++i) {
                double a = i * kPi / 4.0;
                float cxx = 12 + (float)std::cos(a) * 9.6f;
                float cyy = 12 + (float)std::sin(a) * 9.6f;
                p.L(cxx, cyy, cxx + 2.8f * (float)std::cos(a), cyy + 2.8f * (float)std::sin(a));
            }
            break;
        }
        case Icon::Play:
            p.Tri(8.5f, 5.5f, 19, 12, 8.5f, 18.5f);
            break;
        case Icon::Link:
            p.Arc(9.5f, 12, 4.6f, 155, 235);
            p.Arc(14.5f, 12, 4.6f, -25, 235);
            p.L(10.7f, 13.3f, 13.3f, 10.7f);
            break;
        case Icon::Clipboard:
            p.L(9, 4.2f, 9, 2.8f);
            p.L(9, 2.8f, 15, 2.8f);
            p.L(15, 2.8f, 15, 4.2f);
            p.L(8, 4.8f, 8, 7.2f);
            p.L(16, 4.8f, 16, 7.2f);
            p.RRect(6, 7.2f, 12, 12.8f, 2.2f);
            break;
        case Icon::Check:
            p.L(4.5f, 12.5f, 9.5f, 17.5f);
            p.L(9.5f, 17.5f, 19.5f, 6.5f);
            break;
        case Icon::Close:
            p.L(6, 6, 18, 18);
            p.L(18, 6, 6, 18);
            break;
        case Icon::ChevronDown:
            p.L(6.5f, 9.5f, 12, 15);
            p.L(12, 15, 17.5f, 9.5f);
            break;
        case Icon::ChevronRight:
            p.L(9.5f, 6.5f, 15, 12);
            p.L(15, 12, 9.5f, 17.5f);
            break;
        case Icon::ChevronLeft:
            p.L(14.5f, 6.5f, 9, 12);
            p.L(9, 12, 14.5f, 17.5f);
            break;
        case Icon::Music:
            p.L(10, 17.5f, 10, 5.5f);
            p.L(10, 5.5f, 19, 4.2f);
            p.L(19, 4.2f, 19, 16.2f);
            p.Dot(7.4f, 17.6f, 2.6f);
            p.Dot(16.4f, 16.4f, 2.6f);
            break;
        case Icon::Video:
            p.RRect(3, 6, 13, 12, 3.5f);
            p.L(17.5f, 9.5f, 21, 7);
            p.L(21, 7, 21, 17);
            p.L(21, 17, 17.5f, 14.5f);
            break;
        case Icon::Refresh:
            p.Arc(12, 12, 8, 40, 285);
            p.L(16.5f, 3.2f, 20.2f, 7.4f);
            p.L(20.2f, 7.4f, 15.2f, 9.2f);
            break;
        case Icon::Pause:
            p.Circ(12, 12, 8.5f);
            p.L(9.4f, 8.6f, 9.4f, 15.4f);
            p.L(14.6f, 8.6f, 14.6f, 15.4f);
            break;
        case Icon::Stop:
            p.RRect(3.5f, 3.5f, 17, 17, 8.5f);
            p.Box(8.6f, 8.6f, 6.8f, 6.8f, 1.4f);
            break;
        case Icon::Folder:
            p.L(3, 19.5f, 3, 6.0f);
            p.L(3, 6.0f, 9.4f, 6.0f);
            p.L(9.4f, 6.0f, 11.4f, 8.4f);
            p.L(11.4f, 8.4f, 21, 8.4f);
            p.L(21, 8.4f, 21, 19.5f);
            p.L(21, 19.5f, 3, 19.5f);
            break;
        case Icon::External:
            p.L(13.5f, 4.5f, 20, 4.5f);
            p.L(20, 4.5f, 20, 11);
            p.L(20, 4.5f, 13.5f, 11);
            p.Arc(10.4f, 14.6f, 7.2f, 180, 90);
            p.L(3.2f, 14.6f, 3.2f, 20.6f);
            p.L(3.2f, 20.6f, 10.4f, 20.6f);
            break;
        case Icon::Trash:
            p.L(4.5f, 6.5f, 19.5f, 6.5f);
            p.L(9.5f, 6.5f, 9.5f, 4.4f);
            p.L(9.5f, 4.4f, 14.5f, 4.4f);
            p.L(14.5f, 4.4f, 14.5f, 6.5f);
            p.L(6.2f, 8, 7.2f, 19.5f);
            p.L(7.2f, 19.5f, 16.8f, 19.5f);
            p.L(16.8f, 19.5f, 17.8f, 8);
            p.L(10, 9.5f, 10.4f, 17.5f);
            p.L(14, 9.5f, 13.6f, 17.5f);
            break;
        case Icon::Search:
            p.Circ(10.5f, 10.5f, 6.7f);
            p.L(15.4f, 15.4f, 20.2f, 20.2f);
            break;
        case Icon::Sparkles:
            p.L(10.5f, 3.5f, 10.5f, 13.5f);
            p.L(3.5f, 8.5f, 17.5f, 8.5f);
            p.L(5.6f, 3.6f, 15.4f, 13.4f);
            p.L(15.4f, 3.6f, 5.6f, 13.4f);
            p.Dot(18.6f, 17.4f, 1.5f);
            p.Dot(4.6f, 18.6f, 1.1f);
            break;
        case Icon::Queue:
            p.L(4, 7, 20, 7);
            p.L(4, 12, 20, 12);
            p.L(4, 17, 14, 17);
            break;
        case Icon::Shield:
            p.L(12, 3, 12, 4.2f);
            p.L(12, 3, 20, 6.6f);
            p.L(20, 6.6f, 20, 12);
            p.L(4, 12, 4, 6.6f);
            p.L(4, 6.6f, 12, 3);
            p.Arc(12, 12, 8, 0, 180);
            break;
        case Icon::Info:
            p.Circ(12, 12, 8.5f);
            p.L(12, 11, 12, 16.5f);
            p.Dot(12, 7.6f, 1.0f);
            break;
        case Icon::Warn:
            p.L(12, 4, 12, 13.5f);
            p.L(12, 13.5f, 4.4f, 19.6f);
            p.L(4.4f, 19.6f, 19.6f, 19.6f);
            p.L(19.6f, 19.6f, 12, 4);
            p.Dot(12, 17, 0.9f);
            break;
        case Icon::Error:
            p.Circ(12, 12, 8.5f);
            p.L(8.4f, 8.4f, 15.6f, 15.6f);
            p.L(15.6f, 8.4f, 8.4f, 15.6f);
            break;
        case Icon::CheckCircle:
            p.Circ(12, 12, 8.5f);
            p.L(7.6f, 12.2f, 10.6f, 15.4f);
            p.L(10.6f, 15.4f, 16.4f, 8.8f);
            break;
        case Icon::Copy:
            p.RRect(8.5f, 3.5f, 12, 12, 2.6f);
            p.L(15.5f, 19, 4, 19);
            p.L(4, 19, 4, 8);
            break;
        case Icon::Cookie:
            p.Circ(12, 12, 8.5f);
            p.Dot(9.2f, 9.6f, 1.3f);
            p.Dot(14.6f, 12.2f, 1.0f);
            p.Dot(11.4f, 15.4f, 1.1f);
            break;
        case Icon::Subtitles:
            p.RRect(2.8f, 5, 18.4f, 14, 4);
            p.L(6.2f, 13, 10.5f, 13);
            p.L(13, 13, 17.8f, 13);
            p.L(6.2f, 10, 11.5f, 10);
            break;
        case Icon::Thumbnail:
            p.RRect(3, 5, 18, 14, 3.2f);
            p.L(6, 15.5f, 9.5f, 10);
            p.L(9.5f, 10, 13.5f, 15.5f);
            p.Dot(16.2f, 9.2f, 1.3f);
            break;
        case Icon::Speed:
            p.Arc(12, 12, 8.5f, 150, 240);
            p.L(12, 12, 17, 8);
            p.Dot(12, 12, 1.2f);
            break;
        case Icon::Plus:
            p.L(12, 5, 12, 19);
            p.L(5, 12, 19, 12);
            break;
        case Icon::Minus:
            p.L(5, 12, 19, 12);
            break;
        case Icon::Image:
            p.RRect(3, 5, 18, 14, 3.2f);
            p.L(6, 15.5f, 11, 9.6f);
            p.L(11, 9.6f, 15, 15.5f);
            p.Dot(16.6f, 9, 1.3f);
            break;
        case Icon::ArrowLeft:
            p.L(19, 12, 5.5f, 12);
            p.L(11.5f, 5.5f, 5, 12);
            p.L(5, 12, 11.5f, 18.5f);
            break;
        case Icon::Drag:
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 2; ++j) p.Dot(9.f + j * 6.f, 7.f + i * 5.f, 1.3f);
            break;
        case Icon::Equalizer:
            p.L(6, 4.5f, 6, 19.5f);
            p.L(12, 4.5f, 12, 19.5f);
            p.L(18, 4.5f, 18, 19.5f);
            p.Box(3.4f, 8, 5.2f, 3.4f, 1.4f);
            p.Box(9.4f, 13, 5.2f, 3.4f, 1.4f);
            p.Box(15.4f, 6.5f, 5.2f, 3.4f, 1.4f);
            break;
        case Icon::Clock:
            p.Circ(12, 12, 8.5f);
            p.L(12, 7.2f, 12, 12.2f);
            p.L(12, 12.2f, 15.6f, 14.2f);
            break;
        case Icon::Flame:
            p.L(12, 3, 12, 20.5f);
            p.Arc(12, 12.5f, 8, 270, 180);
            p.L(4, 12.5f, 12, 3);
            break;
        case Icon::Hd:
            p.L(4, 8, 4, 16);
            p.L(4, 12, 8, 12);
            p.L(8, 8, 8, 16);
            p.L(11.5f, 8, 11.5f, 16);
            p.Arc(15, 12, 4, -90, 180);
            p.L(19, 8, 19, 16);
            break;
        case Icon::Ticket:
            p.RRect(3, 7, 18, 10, 2.5f);
            p.L(9, 7, 9, 9);
            p.L(9, 15, 9, 17);
            p.Dot(15, 12, 1.4f);
            break;
        case Icon::Maximize:
            p.RRect(4.5f, 4.5f, 15, 15, 2.2f);
            break;
        case Icon::Restore:
            p.RRect(4, 8.5f, 12, 11, 2.2f);
            p.L(8, 8.5f, 8, 5.5f);
            p.L(8, 5.5f, 19.5f, 5.5f);
            p.L(19.5f, 5.5f, 19.5f, 14);
            p.L(16, 14, 19.5f, 14);
            p.L(19.5f, 11.5f, 19.5f, 14);
            break;
        case Icon::Telegram: {
            // Фирменный знак: бумажный самолётик носом вправо-вверх со сгибом
            // и килем. Обводка идёт по контуру, поэтому заливки не нужно.
            p.L(21.2f, 3.2f, 2.6f, 10.4f);        // верхняя кромка крыла
            p.L(2.6f, 10.4f, 10.2f, 13.2f);      // к переднему углу киля
            p.L(10.2f, 13.2f, 8.4f, 20.6f);      // киль вниз
            p.L(8.4f, 20.6f, 12.9f, 14.1f);
            p.L(12.9f, 14.1f, 18.3f, 13.9f);
            p.L(18.3f, 13.9f, 21.2f, 3.2f);      // нижняя кромка крыла
            p.L(10.2f, 13.2f, 18.3f, 13.9f);     // линия сгиба
            break;
        }
        case Icon::Github: {
            // Знак Octocat в упрощении: круглая голова, треугольные уши
            // и хвост, уходящий вправо по дуге.
            p.Arc(12, 12.6f, 7.4f, -18, 336);     // голова со срезом под хвост
            p.L(6.5f, 7.0f, 5.6f, 2.6f);         // левое ухо
            p.L(5.6f, 2.6f, 10.0f, 5.4f);
            p.L(14.0f, 5.4f, 18.4f, 2.6f);       // правое ухо
            p.L(18.4f, 2.6f, 17.5f, 7.0f);
            p.Arc(20.4f, 15.4f, 4.2f, -50, 190); // хвост
            break;
        }
        case Icon::None:
        default:
            break;
    }
}

void DrawLogo(Renderer& r, const RectF& dst) {
    float s = std::min(dst.w, dst.h);
    if (s <= 2.f) return;
    RectF box(dst.x + (dst.w - s) * 0.5f, dst.y + (dst.h - s) * 0.5f, s, s);
    r.FillGrad(box, s * 0.3f, theme::A1(), theme::A2(), 45.0f);
    r.Stroke(box, s * 0.3f, Color(255, 255, 255, 40), 1.0f);

    float cx = box.x + s * 0.5f;
    float cy = box.y + s * 0.52f;

    // «уточка»: тело, голова, клюв, глаз
    r.Ellipse(cx - s * 0.06f, cy, s * 0.24f, s * 0.17f, Color(255, 255, 255, 245));
    r.Ellipse(cx + s * 0.14f, cy - s * 0.16f, s * 0.13f, s * 0.13f, Color(255, 255, 255, 245));
    r.FillGrad(RectF(cx + s * 0.21f, cy - s * 0.21f, s * 0.18f, s * 0.075f), s * 0.035f,
               Color(255, 214, 102), Color(255, 160, 60));
    r.Ellipse(cx + s * 0.19f, cy - s * 0.19f, s * 0.022f, s * 0.022f, theme::A2());

    // стрелка вниз — смысл загрузки
    Color arrow(255, 255, 255, 240);
    float aw = s * 0.055f;
    r.StrokeLine(cx - s * 0.06f, cy - s * 0.10f, cx - s * 0.06f, cy + s * 0.13f, arrow, aw);
    r.StrokeLine(cx - s * 0.14f, cy + s * 0.04f, cx - s * 0.06f, cy + s * 0.13f, arrow, aw);
    r.StrokeLine(cx - s * 0.06f, cy + s * 0.13f, cx + s * 0.02f, cy + s * 0.04f, arrow, aw);
}

} // namespace icons
