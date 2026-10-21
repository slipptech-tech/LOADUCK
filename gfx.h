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

// Фон целиком: вертикальный градиент + мягкие пятна. Рисуется попиксельно
// прямо в буфер кадра и кэшируется, поэтому ни колец, ни артефактов.
void PaintBackground(const RectF& area, Color top, Color bottom,
                     const std::vector<Blob>& blobs);
void ClearGlowCache();          // сброс кэша фона (смена темы/размера)

// Инициализация/завершение
bool Init(HWND hwnd);
void Shutdown();
float Scale();

// Иконка программы (рисуется логотипом, копируется в HICON)
HICON RenderAppIcon(int size);

} // namespace gfx