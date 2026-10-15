#include "gfx.h"
#include "icons.h"
#include "ui.h"
#include "util.h"

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gfx {

// ── Кэш изображений ───────────────────────────────────────────────────────

GfxImage* ImageGet(const std::wstring& key) { return ImageCache::Get(key); }

void ImagePut(const std::wstring& key, std::vector<uint8_t>&& bytes) {
    GfxImage img;
    if (!img.Load(bytes)) return;
    ImageCache::Put(key, std::move(img));
}

void ImageTrim() { ImageCache::Trim(12); }

// ── Фоновые блики ─────────────────────────────────────────────────────────

// Блики рисуются в маленький offscreen-буфер и кэшируются: пересчёт ~8 раз в секунду
// вместо 60. Иначе 66 заливок эллипсов на каждый кадр съедают всё время CPU.
namespace {
struct GlowCache {
    Gdiplus::Bitmap* bmp = nullptr;
    int w = 0, h = 0;
    double stamp = -1e9;
    Color color{};
    bool colorSet = false;
};
GlowCache g_glow;
} // namespace

void ClearGlowCache() {
    if (g_glow.bmp) delete g_glow.bmp;
    g_glow.bmp = nullptr;
    g_glow.w = g_glow.h = 0;
    g_glow.stamp = -1e9;
    g_glow.colorSet = false;
}

void DrawGlowLayer(Renderer& r, const std::vector<Blob>& blobs, const RectF& area) {
    if (blobs.empty() || area.w <= 0 || area.h <= 0) return;

    constexpr int GW = 300, GH = 180;
    const double now = ui::time;
    bool stale = (now - g_glow.stamp) > 0.11;

    if (!g_glow.colorSet || g_glow.color.r != blobs[0].color.r || g_glow.color.g != blobs[0].color.g ||
        g_glow.color.b != blobs[0].color.b) {
        stale = true;
        g_glow.color = blobs[0].color;
        g_glow.colorSet = true;
    }

    if (!g_glow.bmp || g_glow.w != GW || g_glow.h != GH) {
        ClearGlowCache();
        g_glow.colorSet = true;
        g_glow.color = blobs[0].color;
        g_glow.bmp = new Gdiplus::Bitmap(GW, GH, PixelFormat32bppPARGB);
        if (!g_glow.bmp || g_glow.bmp->GetLastStatus() != Gdiplus::Ok) {
            ClearGlowCache();
            return;
        }
        g_glow.w = GW;
        g_glow.h = GH;
        stale = true;
    }

    if (stale) {
        Gdiplus::Graphics g(g_glow.bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        const int layers = 22;
        for (const Blob& b : blobs) {
            if (b.intensity <= 0.5f) continue;
            float cx = b.x * GW, cy = b.y * GH;
            float rx = b.radius * GW * 0.72f;
            float ry = rx * b.squash;
            if (rx < 4.f) continue;
            for (int i = layers; i >= 1; --i) {
                float t = (float)i / layers;
                float a = b.intensity * (1.f - t) * (1.f - t) * 0.5f;
                if (a < 0.6f) continue;
                BYTE alpha = (BYTE)std::clamp(a, 0.f, 255.f);
                Gdiplus::SolidBrush br(Gdiplus::Color(b.color.r, b.color.g, b.color.b, alpha));
                float ex = rx * t, ey = ry * t;
                g.FillEllipse(&br, cx - ex, cy - ey, ex * 2, ey * 2);
            }
        }
        g.Flush(Gdiplus::FlushIntentionSync);
        g_glow.stamp = now;
    }

    r.DrawBitmapExternal(g_glow.bmp, area);
}

// ── Инициализация ─────────────────────────────────────────────────────────

bool Init(HWND hwnd) { return GRInit(hwnd); }

void Shutdown() {
    ClearGlowCache();
    GRShutdown();
}

float Scale() { return theme::Scale(); }

// ── Иконка программы ──────────────────────────────────────────────────────

// Рисует логотип в ARGB-буфер и превращает его в HICON.
HICON RenderAppIcon(int size) {
    HICON fallback = LoadIconW(nullptr, IDI_APPLICATION);
    if (size < 8) size = 64;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC memdc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(memdc);
        return fallback;
    }
    memset(bits, 0, (size_t)size * size * 4);

    {
        Gdiplus::Bitmap bmp((INT)size, (INT)size, size * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        if (bmp.GetLastStatus() != Gdiplus::Ok) {
            DeleteObject(dib);
            DeleteDC(memdc);
            return fallback;
        }
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        void* saved = GR().NativeGraphics();
        GR().SetNativeGraphics(&g);
        icons::DrawLogo(GR(), RectF(0, 0, (float)size, (float)size));
        GR().SetNativeGraphics(saved);
    }

    // GDI+ пишет premultiplied BGRA, Windows иконкам нужен straight alpha
    BYTE* px = (BYTE*)bits;
    for (int i = 0, n = size * size; i < n; ++i) {
        BYTE* p = px + (size_t)i * 4;
        BYTE a = p[3];
        if (!a || a == 255) continue;
        float inv = 255.f / (float)a;
        for (int k = 0; k < 3; ++k) {
            float v = p[k] * inv;
            p[k] = (BYTE)std::clamp(v, 0.f, 255.f);
        }
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = dib;
    ii.hbmMask = mask;
    HICON ico = CreateIconIndirect(&ii);
    if (mask) DeleteObject(mask);
    DeleteObject(dib);
    DeleteDC(memdc);
    return ico ? ico : fallback;
}

} // namespace gfx
