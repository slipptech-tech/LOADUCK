// render.cpp — реализация отрисовки на GDI+.
//
// Рисуем в отдельный ARGB-буфер и один раз копируем на экран через BitBlt.
// Это надёжнее, чем рисовать прямо на HDC окна, и убирает мерцание.
#include "render.h"
#include "util.h"

#include <gdiplus.h>
#include <wincodec.h>


#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mutex>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")

// Короткий алиас: полный `using namespace Gdiplus` конфликтует с нашими RectF/Color.
namespace G = Gdiplus;

namespace {

ULONG_PTR g_gdipToken = 0;
bool g_gdipStarted = false;

IWICImagingFactory* Wic() {
    static IWICImagingFactory* f = nullptr;
    if (!f) {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))))
            f = nullptr;
    }
    return f;
}

// Декодирует изображение в 32-битный BGRA (premultiplied) буфер.
bool DecodeToBgra(IWICBitmapSource* src, std::vector<uint8_t>& out, int& w, int& h) {
    IWICImagingFactory* fac = Wic();
    if (!fac || !src) return false;
    IWICFormatConverter* conv = nullptr;
    if (FAILED(fac->CreateFormatConverter(&conv))) return false;
    bool ok = false;
    if (SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT ww = 0, hh = 0;
        if (SUCCEEDED(conv->GetSize(&ww, &hh)) && ww > 0 && hh > 0 && ww <= 4096 && hh <= 4096) {
            w = (int)ww;
            h = (int)hh;
            out.assign((size_t)w * h * 4, 0);
            if (SUCCEEDED(conv->CopyPixels(nullptr, ww * 4, (UINT)out.size(), out.data())))
                ok = true;
        }
    }
    conv->Release();
    return ok;
}

inline Color ApplyOp(Color c, float op) {
    if (op >= 0.999f) return c;
    float a = c.a * op;
    return Color(c.r, c.g, c.b, (BYTE)(a > 255.f ? 255.f : a));
}

// Строит путь скруглённого прямоугольника без вырожденных дуг.
void RoundPath(G::GraphicsPath& path, const RectF& r, float radius) {
    float rad = std::clamp(radius, 0.f, std::min(r.w, r.h) * 0.5f);
    float d = rad * 2.f;
    if (rad <= 0.5f) {
        path.AddRectangle(G::RectF(r.x, r.y, r.w, r.h));
        return;
    }
    path.AddArc(r.x, r.y, d, d, 180.f, 90.f);                    // левый верхний
    path.AddArc(r.right() - d, r.y, d, d, 270.f, 90.f);          // правый верхний
    path.AddArc(r.right() - d, r.bottom() - d, d, d, 0.f, 90.f); // правый нижний
    path.AddArc(r.x, r.bottom() - d, d, d, 90.f, 90.f);          // левый нижний
    path.CloseFigure();
}

inline G::RectF ToGp(const RectF& r) { return G::RectF(r.x, r.y, r.w, r.h); }

} // namespace

// ── Цвет ──────────────────────────────────────────────────────────────────
Color Color::Hex(const wchar_t* hex, BYTE alpha) {
    unsigned v = (unsigned)wcstoul(hex, nullptr, 16);
    return Color((BYTE)((v >> 16) & 255), (BYTE)((v >> 8) & 255), (BYTE)(v & 255), alpha);
}

// ── GfxImage ──────────────────────────────────────────────────────────────

GfxImage::~GfxImage() = default;

bool GfxImage::Load(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 16) return false;

    // Основной путь: GDI+ умеет JPEG/PNG напрямую
    auto viaGdiplus = [&](const BYTE* data, UINT len) -> bool {
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len);
        if (!hg) return false;
        void* p = GlobalLock(hg);
        if (!p) { GlobalFree(hg); return false; }
        memcpy(p, data, len);
        GlobalUnlock(hg);

        IStream* st = nullptr;
        bool created = SUCCEEDED(CreateStreamOnHGlobal(hg, TRUE, &st));
        if (!created) GlobalFree(hg);
        if (!created || !st) return false;

        G::Bitmap bmp(st, FALSE);
        st->Release();
        if (bmp.GetLastStatus() != G::Ok) return false;
        UINT w = bmp.GetWidth(), h = bmp.GetHeight();
        if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;

        G::Rect box(0, 0, (INT)w, (INT)h);
        G::BitmapData bd{};
        if (bmp.LockBits(&box, G::ImageLockModeRead, PixelFormat32bppPARGB, &bd) != G::Ok) return false;
        int stride = (int)bd.Stride;
        std::vector<uint8_t> out((size_t)w * h * 4, 0);
        const BYTE* src = (const BYTE*)bd.Scan0;
        for (UINT y = 0; y < h; ++y) {
            const BYTE* s = src + (size_t)y * stride;
            BYTE* d = out.data() + (size_t)y * w * 4;
            memcpy(d, s, (size_t)w * 4);
        }
        bmp.UnlockBits(&bd);
        pixels_ = std::move(out);
        w_ = (int)w;
        h_ = (int)h;
        stride_ = w_ * 4;
        return true;
    };

    if (viaGdiplus(bytes.data(), (UINT)bytes.size())) return true;

    // Запасной путь: WIC (может знать форматы, которых не знает GDI+)
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    IWICImagingFactory* fac = Wic();
    if (fac) {
        IWICStream* stream = nullptr;
        if (SUCCEEDED(fac->CreateStream(&stream))) {
            bool ok = false;
            do {
                if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), (DWORD)bytes.size())))
                    break;
                IWICBitmapDecoder* dec = nullptr;
                if (FAILED(fac->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec)))
                    break;
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(dec->GetFrame(0, &frame))) {
                    ok = DecodeToBgra(frame, px, w, h);
                    frame->Release();
                }
                dec->Release();
            } while (false);
            stream->Release();
            if (ok) {
                pixels_ = std::move(px);
                w_ = w;
                h_ = h;
                stride_ = w_ * 4;
                return true;
            }
        }
    }
    return false;
}
bool GfxImage::Load(const wchar_t* filePath) {
    if (!filePath || !*filePath) return false;
    IWICImagingFactory* fac = Wic();
    if (!fac) return false;

    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(fac->CreateDecoderFromFilename(filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                              &dec)))
        return false;

    bool ok = false;
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    IWICBitmapFrameDecode* frame = nullptr;
    if (SUCCEEDED(dec->GetFrame(0, &frame))) {
        ok = DecodeToBgra(frame, px, w, h);
        frame->Release();
    }
    dec->Release();
    if (!ok) return false;

    pixels_ = std::move(px);
    w_ = w;
    h_ = h;
    stride_ = w_ * 4;
    return true;
}

// ── Кэш изображений ───────────────────────────────────────────────────────

std::vector<std::pair<std::wstring, ImageCache::Item>> ImageCache::items_;
uint64_t ImageCache::clock_ = 0;

GfxImage* ImageCache::Get(const std::wstring& key) {
    if (key.empty()) return nullptr;
    for (auto& kv : items_) {
        if (kv.first == key) {
            kv.second.used = ++clock_;
            return kv.second.img.get();
        }
    }
    return nullptr;
}

void ImageCache::Put(const std::wstring& key, GfxImage&& img) {
    if (key.empty() || !img.Valid()) return;
    for (auto& kv : items_) {
        if (kv.first == key) {
            kv.second.used = ++clock_;
            *kv.second.img = std::move(img);
            return;
        }
    }
    Item it;
    it.used = ++clock_;
    it.img = std::make_unique<GfxImage>(std::move(img));
    items_.emplace_back(key, std::move(it));
    Trim(12);
}

void ImageCache::Trim(size_t maxItems) {
    while (items_.size() > maxItems) {
        size_t oldest = 0;
        for (size_t i = 1; i < items_.size(); ++i)
            if (items_[i].second.used < items_[oldest].second.used) oldest = i;
        items_.erase(items_.begin() + oldest);
    }
}

// ── Внутреннее состояние рендерера ────────────────────────────────────────

struct Renderer::Impl {
    G::Bitmap* bmp = nullptr;
    G::Graphics* g = nullptr;
    int pw = 0, ph = 0;
    // Тот же буфер с точки зрения GDI: DIB-секция + memory DC.
    // Позволяет выводить кадр через BitBlt — на порядок быстрее GDI+ DrawImage.
    HBITMAP dib = nullptr;
    HDC memDC = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* bits = nullptr;

    std::vector<RectF> clips;
    std::vector<float> layers;

    struct FontEntry { int sizeQ; bool bold; G::Font* font; };
    struct FmtEntry { int h; G::StringFormat* fmt; };
    std::vector<FontEntry> fonts;
    std::vector<FmtEntry> fmts;

    bool Ensure(int w, int h);
    void Destroy();
    G::Font* GetFont(float size, bool bold);
    G::StringFormat* GetFormat(Align a, VAlign v, bool ellipsis);
    float Opacity() const { return layers.empty() ? 1.f : layers.back(); }
};

bool Renderer::Impl::Ensure(int w, int h) {
    if (bmp && w == pw && h == ph) return g != nullptr;
    Destroy();

    // 1) DIB-секция 32bpp (top-down) — её можно и рисовать через GDI+, и копировать BitBlt
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;      // отрицательная высота = top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (screen) ReleaseDC(nullptr, screen);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        dib = nullptr;
        bits = nullptr;
    }

    if (dib && bits) {
        bmp = new G::Bitmap(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        if (!bmp || bmp->GetLastStatus() != G::Ok) {
            delete bmp;
            bmp = nullptr;
        }
        memDC = CreateCompatibleDC(nullptr);
        if (memDC) oldBmp = SelectObject(memDC, dib);
    }

    if (!bmp) {
        // Запасной путь — обычный буфер GDI+
        G::Bitmap* b = new G::Bitmap((INT)w, (INT)h, PixelFormat32bppPARGB);
        if (!b || b->GetLastStatus() != G::Ok) {
            if (b) delete b;
            return false;
        }
        bmp = b;
    }

    G::Graphics* gr = new G::Graphics(bmp);
    if (!gr || gr->GetLastStatus() != G::Ok) {
        if (gr) delete gr;
        Destroy();
        return false;
    }
    g = gr;
    pw = w;
    ph = h;
    return true;
}

void Renderer::Impl::Destroy() {
    for (auto& e : fmts) delete e.fmt;
    fmts.clear();
    for (auto& e : fonts) delete e.font;
    fonts.clear();
    clips.clear();
    layers.clear();
    delete g;
    g = nullptr;
    delete bmp;          // GDI+ не владеет битами — DIB остаётся валидным
    bmp = nullptr;
    if (memDC) {
        if (oldBmp) SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        memDC = nullptr;
        oldBmp = nullptr;
    }
    if (dib) {
        DeleteObject(dib);
        dib = nullptr;
    }
    bits = nullptr;
    pw = ph = 0;
}

G::Font* Renderer::Impl::GetFont(float size, bool bold) {
    int sq = (int)(size * 2.f + 0.5f);
    for (auto& e : fonts)
        if (e.sizeQ == sq && e.bold == bold) return e.font;

    if (fonts.size() > 48) {
        for (auto& e : fonts) delete e.font;
        fonts.clear();
    }
    G::FontStyle style = bold ? G::FontStyleBold : G::FontStyleRegular;
    G::Font* f = new G::Font(L"Segoe UI", (float)(sq / 2.f), style, G::UnitPixel);
    if (!f || f->GetLastStatus() != G::Ok) {
        delete f;
        f = new G::Font(L"Segoe UI", (float)(sq / 2.f), G::FontStyleRegular, G::UnitPixel);
        if (!f || f->GetLastStatus() != G::Ok) {
            delete f;
            return nullptr;
        }
    }
    fonts.push_back({ sq, bold, f });
    return f;
}

G::StringFormat* Renderer::Impl::GetFormat(Align a, VAlign v, bool ellipsis) {
    int h = (int)a * 100 + (int)v * 10 + (ellipsis ? 1 : 0);
    for (auto& e : fmts)
        if (e.h == h) return e.fmt;

    // GenericTypographic() возвращает общий read-only объект — настройки не применяются,
    // поэтому делаем собственную копию
    G::StringFormat* f = new G::StringFormat(G::StringFormat::GenericTypographic());
    if (!f) return nullptr;
    G::StringAlignment ha = G::StringAlignmentNear;
    if (a == Align::Center) ha = G::StringAlignmentCenter;
    else if (a == Align::Right) ha = G::StringAlignmentFar;
    G::StringAlignment va = G::StringAlignmentNear;
    if (v == VAlign::Middle) va = G::StringAlignmentCenter;
    else if (v == VAlign::Bottom) va = G::StringAlignmentFar;
    f->SetAlignment(ha);
    f->SetLineAlignment(va);
    f->SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoFitBlackBox |
                      G::StringFormatFlagsMeasureTrailingSpaces);
    fmts.push_back({ h, f });
    return f;
}

// ── Renderer ──────────────────────────────────────────────────────────────

bool Renderer::Init(HWND hwnd) {
    hwnd_ = hwnd;
    if (!g_gdipStarted) {
        G::GdiplusStartupInput input;
        if (G::GdiplusStartup(&g_gdipToken, &input, nullptr) != G::Ok) return false;
        g_gdipStarted = true;
    }
    impl_ = std::make_unique<Impl>();
    RECT rc{};
    GetClientRect(hwnd, &rc);
    w_ = rc.right - rc.left;
    h_ = rc.bottom - rc.top;
    if (w_ <= 0) w_ = 1280;
    if (h_ <= 0) h_ = 800;
    if (!impl_->Ensure(w_, h_)) {
        impl_.reset();
        return false;
    }
    ready_ = true;
    return true;
}

void Renderer::Resize(int, int) {
    if (!hwnd_ || !impl_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int nw = rc.right - rc.left;
    int nh = rc.bottom - rc.top;
    if (nw <= 0 || nh <= 0) return;
    if (nw == w_ && nh == h_) return;
    w_ = nw;
    h_ = nh;
    impl_->Ensure(w_, h_);
}

void Renderer::Shutdown() {
    if (impl_) impl_->Destroy();
    impl_.reset();
    ready_ = false;
}

void* Renderer::NativeGraphics() const { return impl_ ? (void*)impl_->g : nullptr; }
void Renderer::SetNativeGraphics(void* g) { if (impl_) impl_->g = (G::Graphics*)g; }
void* Renderer::BackbufferBits() const { return impl_ ? impl_->bits : nullptr; }

bool Renderer::DrawBitmapExternal(void* bmp, const RectF& dst) {
    if (!impl_ || !impl_->g || !bmp || dst.w <= 0 || dst.h <= 0) return false;
    impl_->g->DrawImage((G::Bitmap*)bmp, dst.x, dst.y, dst.w, dst.h);
    return true;
}

void Renderer::Begin() {
    if (!impl_ || !impl_->g) return;
    impl_->Ensure(w_, h_);
    G::Graphics* g = impl_->g;
    // закрываем всё, что осталось с прошлого кадра
    impl_->clips.clear();
    impl_->layers.clear();
    g->ResetClip();
    g->SetCompositingMode(G::CompositingModeSourceOver);
    g->SetSmoothingMode(G::SmoothingModeAntiAlias);
    g->SetInterpolationMode(G::InterpolationModeDefault);
    g->SetPixelOffsetMode(G::PixelOffsetModeDefault);
    g->SetCompositingQuality(G::CompositingQualityHighSpeed);
    g->SetTextRenderingHint(G::TextRenderingHintAntiAlias);
}

void Renderer::End() {
    if (!impl_ || !impl_->g) return;
    impl_->clips.clear();
    impl_->layers.clear();
    impl_->g->ResetClip();
    impl_->g->Flush(G::FlushIntentionSync);
}

void Renderer::PresentTo(HDC hdc) {
    if (!hdc || !impl_ || !impl_->bmp || w_ <= 0 || h_ <= 0) return;
    // Основной путь — BitBlt из DIB-секции: простое копирование пикселей.
    if (impl_->memDC && impl_->dib) {
        if (BitBlt(hdc, 0, 0, w_, h_, impl_->memDC, 0, 0, SRCCOPY)) return;
    }
    // Запасной путь
    G::Graphics dst(hdc);
    if (dst.GetLastStatus() != G::Ok) return;
    dst.SetCompositingMode(G::CompositingModeSourceCopy);
    dst.SetCompositingQuality(G::CompositingQualityHighSpeed);
    dst.SetInterpolationMode(G::InterpolationModeDefault);
    dst.DrawImage(impl_->bmp, 0, 0, w_, h_);
}

void Renderer::Clear(Color c) {
    if (!impl_ || !impl_->g) return;
    impl_->g->SetCompositingMode(G::CompositingModeSourceCopy);
    impl_->g->Clear(G::Color((INT)c.a, (INT)c.r, (INT)c.g, (INT)c.b));
    impl_->g->SetCompositingMode(G::CompositingModeSourceOver);
}

// ── Примитивы ─────────────────────────────────────────────────────────────

void Renderer::Fill(const RectF& r, Color c) {
    if (!impl_ || !impl_->g || r.w <= 0 || r.h <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    G::SolidBrush br(G::Color(cc.ToArgb()));
    impl_->g->FillRectangle(&br, ToGp(r));
}

void Renderer::FillRound(const RectF& r, float radius, Color c) {
    if (!impl_ || !impl_->g || r.w <= 0 || r.h <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    if (radius < 0.5f) { Fill(r, c); return; }
    G::SolidBrush br(G::Color(cc.ToArgb()));
    G::GraphicsPath path;
    RoundPath(path, r, radius);
    impl_->g->FillPath(&br, &path);
}

void Renderer::FillGrad(const RectF& r, float radius, Color c1, Color c2, float angleDeg) {
    if (!impl_ || !impl_->g || r.w <= 0 || r.h <= 0) return;
    Color a1 = ApplyOp(c1, impl_->Opacity());
    Color a2 = ApplyOp(c2, impl_->Opacity());
    if (a1.a == 0 && a2.a == 0) return;

    if (radius < 0.5f) {
        G::LinearGradientBrush br(ToGp(r), G::Color(a1.ToArgb()), G::Color(a2.ToArgb()), angleDeg);
        br.SetWrapMode(G::WrapModeTileFlipXY);
        impl_->g->FillRectangle(&br, ToGp(r));
        return;
    }
    // Расширяем область кисти, чтобы в углах не было светлыхarteфактов
    float rad = std::clamp(radius, 0.f, std::min(r.w, r.h) * 0.5f);
    G::RectF brRect(r.x - rad, r.y - rad, r.w + rad * 2, r.h + rad * 2);
    G::LinearGradientBrush br(brRect, G::Color(a1.ToArgb()), G::Color(a2.ToArgb()), angleDeg);
    br.SetWrapMode(G::WrapModeTileFlipXY);
    G::GraphicsPath path;
    RoundPath(path, r, radius);
    impl_->g->FillPath(&br, &path);
}

void Renderer::FillGradV(const RectF& r, float radius, Color top, Color bottom) {
    FillGrad(r, radius, top, bottom, 90.f);
}

void Renderer::FillGradH(const RectF& r, float radius, Color left, Color right) {
    FillGrad(r, radius, left, right, 0.f);
}

void Renderer::Stroke(const RectF& r, float radius, Color c, float width) {
    if (!impl_ || !impl_->g || r.w <= 0 || r.h <= 0 || width <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    float w = std::min(width, std::min(r.w, r.h));
    if (w <= 0.f) return;
    G::Pen pen(G::Color(cc.ToArgb()), w);
    pen.SetAlignment(G::PenAlignmentCenter);
    pen.SetStartCap(G::LineCapRound);
    pen.SetEndCap(G::LineCapRound);

    if (radius < 0.5f) {
        G::RectF box(r.x + w * 0.5f, r.y + w * 0.5f, r.w - w, r.h - w);
        if (box.Width <= 0 || box.Height <= 0) return;
        impl_->g->DrawRectangle(&pen, box);
        return;
    }
    G::GraphicsPath path;
    RoundPath(path, RectF(r.x + w * 0.5f, r.y + w * 0.5f, r.w - w, r.h - w), radius);
    impl_->g->DrawPath(&pen, &path);
}

void Renderer::StrokeLine(float x1, float y1, float x2, float y2, Color c, float width) {
    if (!impl_ || !impl_->g || width <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    G::Pen pen(G::Color(cc.ToArgb()), width);
    pen.SetStartCap(G::LineCapRound);
    pen.SetEndCap(G::LineCapRound);
    impl_->g->DrawLine(&pen, x1, y1, x2, y2);
}

void Renderer::ShadowRound(const RectF& r, float radius, float blur, Color shadow) {
    if (!impl_ || !impl_->g || r.w <= 0 || r.h <= 0 || shadow.a == 0 || blur <= 0) return;
    const int layers = 4;
    for (int i = layers; i >= 1; --i) {
        float t = (float)i / layers;
        float grow = blur * t * 0.5f;
        float a = (float)shadow.a * (1.f - t) * (1.f - t) * 0.55f;
        if (a < 2.f) continue;
        FillRound(r.Inflate(grow, grow), radius + grow, Color(shadow.r, shadow.g, shadow.b, (BYTE)a));
    }
}

void Renderer::Ellipse(float cx, float cy, float rx, float ry, Color c) {
    if (!impl_ || !impl_->g || rx <= 0 || ry <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    G::SolidBrush br(G::Color(cc.ToArgb()));
    impl_->g->FillEllipse(&br, cx - rx, cy - ry, rx * 2, ry * 2);
}

void Renderer::Ring(float cx, float cy, float radius, Color c, float width) {
    if (!impl_ || !impl_->g || radius <= 0 || width <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    G::Pen pen(G::Color(cc.ToArgb()), width);
    impl_->g->DrawEllipse(&pen, cx - radius, cy - radius, radius * 2, radius * 2);
}

void Renderer::RingArc(float cx, float cy, float radius, Color c, float width, float startDeg, float sweepDeg) {
    if (!impl_ || !impl_->g || radius <= 0 || width <= 0 || sweepDeg <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;
    G::Pen pen(G::Color(cc.ToArgb()), width);
    pen.SetStartCap(G::LineCapRound);
    pen.SetEndCap(G::LineCapRound);
    float sweep = sweepDeg > 360.f ? 360.f : sweepDeg;
    impl_->g->DrawArc(&pen, cx - radius, cy - radius, radius * 2, radius * 2, -90.f + startDeg, sweep);
}

// ── Текст ─────────────────────────────────────────────────────────────────

float Renderer::MeasureText(const std::wstring& s, float size, float weight) {
    if (s.empty() || !impl_ || !impl_->g) return 0;
    G::Font* f = impl_->GetFont(size * scale_, weight >= 600.f);
    if (!f) return 0;
    // Формат по умолчанию (nullptr) — безопаснее всего в этой сборке GDI+
    G::RectF out;
    G::RectF layout(-1e5f, -1e5f, 2e5f, 2e5f);
    if (impl_->g->MeasureString(s.c_str(), -1, f, layout, nullptr, &out) != G::Ok) return 0;
    return out.Width;
}

void Renderer::Text(const std::wstring& s, float size, float weight, Color c, const RectF& bounds,
                    Align a, VAlign v, bool ellipsis) {
    if (!impl_ || !impl_->g || s.empty() || bounds.w <= 0 || bounds.h <= 0) return;
    Color cc = ApplyOp(c, impl_->Opacity());
    if (cc.a == 0) return;

    G::Font* f = impl_->GetFont(size * scale_, weight >= 600.f);
    if (!f) return;
    G::StringFormat* fmt = impl_->GetFormat(a, v, ellipsis);

    std::wstring str = s;
    if (ellipsis) {
        float w = MeasureText(str, size, weight);
        if (w > bounds.w + 0.5f) {
            int lo = 0, hi = (int)str.size();
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                std::wstring t = str.substr(0, mid) + L"\u2026";
                if (MeasureText(t, size, weight) <= bounds.w + 0.5f) lo = mid; else hi = mid - 1;
            }
            str = str.substr(0, lo) + L"\u2026";
        }
    }

    G::SolidBrush br(G::Color(cc.ToArgb()));
    impl_->g->DrawString(str.c_str(), -1, f, ToGp(bounds), fmt, &br);
}

// ── Слои и обрезка ────────────────────────────────────────────────────────

void Renderer::PushClip(const RectF& r) {
    if (!impl_ || !impl_->g) return;
    impl_->g->SetClip(ToGp(r), G::CombineModeIntersect);
    impl_->clips.push_back(r);
}

void Renderer::PushLayer(float opacity) {
    if (!impl_) return;
    float op = std::clamp(opacity, 0.f, 1.f);
    impl_->layers.push_back(impl_->layers.empty() ? op : impl_->layers.back() * op);
}

void Renderer::Pop() {
    if (!impl_) return;
    if (!impl_->clips.empty()) {
        impl_->clips.pop_back();
        // Восстанавливаем ПОЛНОЙ ЗАМЕНОЙ: Intersect накапливал бы ошибки округления
        if (impl_->clips.empty()) impl_->g->ResetClip();
        else impl_->g->SetClip(ToGp(impl_->clips.back()), G::CombineModeReplace);
        return;
    }
    if (!impl_->layers.empty()) impl_->layers.pop_back();
}

// ── Изображения ───────────────────────────────────────────────────────────

bool Renderer::DrawImage(GfxImage* img, const RectF& dst) {
    if (!impl_ || !impl_->g || !img || !img->Valid() || dst.w <= 0 || dst.h <= 0) return false;

    G::Bitmap bmp(img->Width(), img->Height(), img->Stride(), PixelFormat32bppPARGB,
                  const_cast<BYTE*>(img->Pixels().data()));
    if (bmp.GetLastStatus() != G::Ok) return false;

    // Вписываем с сохранением пропорций
    float sw = (float)img->Width(), sh = (float)img->Height();
    if (sw <= 0 || sh <= 0) return false;
    float k = std::min(dst.w / sw, dst.h / sh);
    float dw = sw * k, dh = sh * k;
    float dx = dst.x + (dst.w - dw) * 0.5f;
    float dy = dst.y + (dst.h - dh) * 0.5f;

    impl_->g->SetInterpolationMode(G::InterpolationModeHighQualityBilinear);
    impl_->g->DrawImage(&bmp, dx, dy, dw, dh);
    impl_->g->SetInterpolationMode(G::InterpolationModeDefault);
    return true;
}

// ── Глобальный рендерер ───────────────────────────────────────────────────

static Renderer g_renderer;

Renderer& GR() { return g_renderer; }

bool GRInit(HWND hwnd) { return g_renderer.Init(hwnd); }
void GRResize(int w, int h) { g_renderer.Resize(w, h); }

void GRShutdown() {
    g_renderer.Shutdown();
    if (g_gdipStarted) {
        G::GdiplusShutdown(g_gdipToken);
        g_gdipStarted = false;
    }
}
