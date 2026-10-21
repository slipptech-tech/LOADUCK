#pragma once

#include "render.h"
#include <string>

// Шрифты: размер в «логических» пикселях, умножается на масштаб DPI.
namespace fonts {
inline constexpr float H1 = 27.0f;
inline constexpr float BIG = 19.0f;
inline constexpr float H2 = 16.5f;
inline constexpr float H3 = 14.5f;
inline constexpr float BODY = 14.0f;
inline constexpr float SMALL = 12.5f;
inline constexpr float TINY = 11.0f;
inline constexpr float ICON = 16.0f;
} // namespace fonts

namespace theme {

// Палитра (тёмная, «студийная»)
extern Color bg;          // фон окна
extern Color bgDeep;      // боковая панель
extern Color surface;     // карточки
extern Color surfaceAlt;  // вложенные элементы, поля
extern Color surfaceHi;   // наведение
extern Color surfaceTop;  // верхняя подсветка карточки
extern Color stroke;      // границы
extern Color strokeHi;    // активные границы
extern Color text;        // основной текст
extern Color textDim;     // вторичный
extern Color textMute;    // третичный
extern Color success;
extern Color warning;
extern Color danger;

// Акцент (градиент)
struct AccentPreset {
    const wchar_t* name;
    Color a;
    Color b;
};

extern AccentPreset gAccent;
inline Color A1() { return gAccent.a; }
inline Color A2() { return gAccent.b; }

const AccentPreset* Accents();
int  AccentCount();
int  AccentIndexByName(const std::wstring& name);

float Scale();
void  SetScale(float s);

void Init();

} // namespace theme