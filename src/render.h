// render.h — абстракция отрисовки (реализация на GDI+).
//
// Единый интерфейс примитивов, которым пользуется весь UI-код.
// Реализация живёт в render.cpp и не зависит от конкретного графического API.
#pragma once

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// ── Цвет ──────────────────────────────────────────────────────────────────

struct Color {
    BYTE r = 0, g = 0, b = 0, a = 255;
    constexpr Color() = default;
    constexpr Color(BYTE R, BYTE G, BYTE B, BYTE A = 255) : r(R), g(G), b(B), a(A) {}
    static Color Rgb(BYTE R, BYTE G, BYTE B) { return Color(R, G, B, 255); }
    static Color Hex(const wchar_t* hex, BYTE alpha = 255);
    Color WithAlpha(BYTE na) const { return Color(r, g, b, na); }
    DWORD ToArgb() const { return (DWORD)(((UINT32)a << 24) | ((UINT32)r << 16) | ((UINT32)g << 8) | b); }
};

inline Color Mix(Color a, Color b, float t) {
    t = t < 0 ? 0.f : (t > 1 ? 1.f : t);
    auto L = [t](BYTE x, BYTE y) { return (BYTE)(x + (y - x) * t); };
    return Color(L(a.r, b.r), L(a.g, b.g), L(a.b, b.b), L(a.a, b.a));
}

inline Color ScaleColor(Color c, float k) {
    auto L = [k](BYTE v) {
        float f = (float)v * k;
        return (BYTE)(f > 255.f ? 255.f : (f < 0.f ? 0.f : f));
    };
    return Color(L(c.r), L(c.g), L(c.b), c.a);
}

// ── Прямоугольник ─────────────────────────────────────────────────────────

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    constexpr RectF() = default;
    constexpr RectF(float X, float Y, float W, float H) : x(X), y(Y), w(W), h(H) {}
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool Contains(float px, float py) const {
        return px >= x && px <= right() && py >= y && py <= bottom();
    }
    RectF Inflate(float dx, float dy) const { return RectF(x - dx, y - dy, w + dx * 2, h + dy * 2); }
    RectF Offset(float dx, float dy) const { return RectF(x + dx, y + dy, w, h); }
    RectF Offset(float dx) const { return RectF(x + dx, y, w, h); }
};

inline RectF IntersectRect(const RectF& a, const RectF& b) {
    float x1 = a.x > b.x ? a.x : b.x;
    float y1 = a.y > b.y ? a.y : b.y;
    float x2 = a.right() < b.right() ? a.right() : b.right();
    float y2 = a.bottom() < b.bottom() ? a.bottom() : b.bottom();
    if (x2 <= x1 || y2 <= y1) return RectF();
    return RectF(x1, y1, x2 - x1, y2 - y1);
}

// ── Выравнивание текста ───────────────────────────────────────────────────

enum class Align { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

// ── Изображения ───────────────────────────────────────────────────────────

class GfxImage {
public:
    ~GfxImage();
    GfxImage() = default;
    GfxImage(GfxImage&&) = default;
    GfxImage& operator=(GfxImage&&) = default;
    GfxImage(const GfxImage&) = delete;
    GfxImage& operator=(const GfxImage&) = delete;

    bool Load(const std::vector<uint8_t>& bytes);
    bool Load(const wchar_t* filePath);

    bool Valid() const { return w_ > 0 && h_ > 0 && pixels_.size() > 0; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    int Stride() const { return stride_; }
    const std::vector<uint8_t>& Pixels() const { return pixels_; }

private:
    std::vector<uint8_t> pixels_;   // BGRA, premultiplied
    int w_ = 0, h_ = 0, stride_ = 0;
};

class ImageCache {
public:
    static GfxImage* Get(const std::wstring& key);
    static void Put(const std::wstring& key, GfxImage&& img);
    static void Trim(size_t maxItems);

private:
    struct Item {
        std::unique_ptr<GfxImage> img;
        uint64_t used = 0;
    };
    static std::vector<std::pair<std::wstring, Item>> items_;
    static uint64_t clock_;
    static std::mutex mu_;
};

// ── Рендерер ──────────────────────────────────────────────────────────────

class Renderer {
public:
    bool Init(HWND hwnd);
    void Resize(int w, int h);
    void Shutdown();
    bool Ready() const { return ready_; }

    void Begin();
    void End();
    void PresentTo(HDC hdc);

    void Clear(Color c);

    // Формы
    void Fill(const RectF& r, Color c);
    void FillRound(const RectF& r, float radius, Color c);
    void FillGrad(const RectF& r, float radius, Color c1, Color c2, float angleDeg = 45.0f);
    void FillGradV(const RectF& r, float radius, Color top, Color bottom);
    void FillGradH(const RectF& r, float radius, Color left, Color right);
    void Stroke(const RectF& r, float radius, Color c, float width = 1.0f);
    void StrokeLine(float x1, float y1, float x2, float y2, Color c, float width = 1.0f);
    void ShadowRound(const RectF& r, float radius, float blur, Color shadow);
    void Ellipse(float cx, float cy, float rx, float ry, Color c);
    void Ring(float cx, float cy, float radius, Color c, float width);
    void RingArc(float cx, float cy, float radius, Color c, float width, float startDeg, float sweepDeg);
    void RoundedOutline(const RectF& r, float radius, Color c, float width) { Stroke(r, radius, c, width); }

    // Текст
    void Text(const std::wstring& s, float size, float weight, Color c, const RectF& bounds,
              Align a = Align::Left, VAlign v = VAlign::Top, bool ellipsis = true);
    float MeasureText(const std::wstring& s, float size, float weight);

    // Обрезка / прозрачность
    void PushClip(const RectF& r);
    void PushLayer(float opacity);
    void Pop();

    // Изображения
    bool DrawImage(GfxImage* img, const RectF& dst);

    // Временное перенаправление вывода в посторонний GDI+ Graphics (иконка, скриншот)
    void* NativeGraphics() const;
    void SetNativeGraphics(void* gdiplusGraphics);
    void* BackbufferBits() const;     // прямой доступ к пикселям кадра (BGRA)
    void InvalidateBackgroundCache();

    // Рисует произвольный GDI+ Bitmap в указанный прямоугольник
    bool DrawBitmapExternal(void* gdiplusBitmap, const RectF& dst);

    HWND Hwnd() const { return hwnd_; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    float Scale() const { return scale_; }
    void SetScale(float s) { scale_ = s; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    HWND hwnd_ = nullptr;
    float scale_ = 1.0f;
    int w_ = 0, h_ = 0;
    bool ready_ = false;
};

// Единственный экземпляр на приложение
Renderer& GR();
bool GRInit(HWND hwnd);
void GRResize(int w, int h);
void GRShutdown();