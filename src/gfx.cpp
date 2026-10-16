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

// ── Фон ────────────────────────────────────────────────────────────────────

// Фон рисуем сами, попиксельно, прямо в буфер кадра. GDI+ здесь давал артефакты
// (пятна выходили зелёными, а слои давали концентрические кольца), а своя
// математика даёт идеально гладкий градиент и стоит копейки: считается один раз
// на тему/размер окна и дальше просто копируется.
namespace {
struct BgCache {
    int w = 0, h = 0;
    Color top{}, bottom{};
    Color blobs[3]{};
    bool valid = false;
    std::vector<BYTE> buf;      // готовый фон, каждый кадр копируется одним memcpy
};
BgCache g_bg;

inline BYTE Blend(BYTE dst, BYTE src, int a) {   // a в диапазоне 0..255
    return (BYTE)((dst * (255 - a) + src * a + 127) / 255);
}
} // namespace

void ClearGlowCache() { g_bg.valid = false; g_bg.buf.clear(); }

void PaintBackground(const RectF& area, Color top, Color bottom,
                     const std::vector<Blob>& blobs) {
    void* bits = GR().BackbufferBits();
    int W = GR().Width(), H = GR().Height();
    if (!bits || W <= 0 || H <= 0 || area.w <= 0 || area.h <= 0) return;

    bool same = g_bg.valid && g_bg.w == W && g_bg.h == H &&
                g_bg.top.r == top.r && g_bg.top.g == top.g && g_bg.top.b == top.b &&
                g_bg.bottom.r == bottom.r && g_bg.bottom.g == bottom.g && g_bg.bottom.b == bottom.b;
    for (int i = 0; same && i < 3; ++i) {
        Color c = (i < (int)blobs.size()) ? blobs[i].color : Color(0, 0, 0, 0);
        same = g_bg.blobs[i].r == c.r && g_bg.blobs[i].g == c.g && g_bg.blobs[i].b == c.b;
    }

    if (!same) {
        g_bg.buf.assign((size_t)W * H * 4, 0);
        g_bg.w = W;
        g_bg.h = H;
        g_bg.top = top;
        g_bg.bottom = bottom;
        for (int i = 0; i < 3; ++i)
            g_bg.blobs[i] = (i < (int)blobs.size()) ? blobs[i].color : Color(0, 0, 0, 0);
        g_bg.valid = true;

        BYTE* px = g_bg.buf.data();
        const int stride = W * 4;

        // 1) вертикальный градиент
        for (int y = 0; y < H; ++y) {
            int t = (H > 1) ? (y * 255) / (H - 1) : 0;
            BYTE rr = (BYTE)((top.r * (255 - t) + bottom.r * t) / 255);
            BYTE gg = (BYTE)((top.g * (255 - t) + bottom.g * t) / 255);
            BYTE bb = (BYTE)((top.b * (255 - t) + bottom.b * t) / 255);
            BYTE* row = px + (size_t)y * stride;
            for (int x = 0; x < W; ++x) {
                row[x * 4 + 0] = bb;
                row[x * 4 + 1] = gg;
                row[x * 4 + 2] = rr;
                row[x * 4 + 3] = 255;
            }
        }

        // 2) мягкие пятна: вклад (1 - d)^2, конверт — только по своему прямоугольнику
        for (const Blob& b : blobs) {
            if (b.intensity <= 0.1f) continue;
            float cx = b.x * W, cy = b.y * H;
            float rx = b.radius * W * 0.80f;
            float ry = rx * b.squash;
            if (rx < 2.f || ry < 2.f) continue;

            int x0 = std::max(0, (int)(cx - rx)), x1 = std::min(W - 1, (int)(cx + rx));
            int y0 = std::max(0, (int)(cy - ry)), y1 = std::min(H - 1, (int)(cy + ry));
            float invRx = 1.f / rx, invRy = 1.f / ry;

            for (int y = y0; y <= y1; ++y) {
                float dy = ((float)y + 0.5f - cy) * invRy;
                BYTE* row = px + (size_t)y * stride;
                for (int x = x0; x <= x1; ++x) {
                    float dx = ((float)x + 0.5f - cx) * invRx;
                    float d2 = dx * dx + dy * dy;
                    if (d2 >= 1.0f) continue;
                    float k = 1.0f - std::sqrt(d2);
                    k *= k;                       // плавное затухание к краю
                    int a = (int)(k * b.intensity + 0.5f);
                    if (a <= 0) continue;
                    if (a > 255) a = 255;
                    row[x * 4 + 0] = Blend(row[x * 4 + 0], b.color.b, a);
                    row[x * 4 + 1] = Blend(row[x * 4 + 1], b.color.g, a);
                    row[x * 4 + 2] = Blend(row[x * 4 + 2], b.color.r, a);
                }
            }
        }
    }

    memcpy(bits, g_bg.buf.data(), (size_t)W * H * 4);
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
