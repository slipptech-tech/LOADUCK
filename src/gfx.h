// gfx.h — слой отрисовки поверх Direct2D + утилиты изображений.
#pragma once

#include "render.h"
#include "theme.h"

#include <string>
#include <vector>
#include <cstdint>

namespace gfx {

inline float S(float v) { return v * theme::Scale(); }

// Изображения
GfxImage* ImageGet(const std::wstring& key);
void ImagePut(const std::wstring& key, std::vector<uint8_t>&& bytes);
void ImageTrim();

// Фоновые блики
struct Blob {
    float x = 0.5f;
    float y = 0.5f;
    float radius = 0.4f;
    float squash = 1.0f;
    Color color;
    float intensity = 30.0f;
};
void DrawGlowLayer(Renderer& r, const std::vector<Blob>& blobs, const RectF& area);
void ClearGlowCache();          // сброс кэша бликов (при смене размера/темы)

// Инициализация/завершение
bool Init(HWND hwnd);
void Shutdown();
float Scale();

// Иконка программы (рисуется логотипом, копируется в HICON)
HICON RenderAppIcon(int size);

} // namespace gfx