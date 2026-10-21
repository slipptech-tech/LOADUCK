// ui.h — мини-фреймворк отрисовки (immediate mode) поверх Direct2D.
#pragma once

#include "render.h"
#include "icons.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace ui {

using Rect = RectF;

struct Input {
    float x = -1000, y = -1000;
    bool down = false;        // ЛКМ зажата
    bool pressed = false;     // нажата в этом кадре
    bool released = false;    // отпущена в этом кадре
    float wheel = 0;
    bool ctrl = false, shift = false;
    std::vector<wchar_t> text;
    int vkBack = 0, vkDel = 0, vkLeft = 0, vkRight = 0, vkHome = 0, vkEnd = 0;
    bool enter = false, esc = false, tab = false;
};

extern Input in;
extern float dt;          // секунды с прошлого кадра
extern double time;       // общий таймер в секундах
extern int   frameAnim;   // сколько анимаций «в пути» в текущем кадре

void Begin();
void End();

bool Hover(const Rect& r);
bool Down(const Rect& r);

// Плавная анимация значения
float Ani(int id, float target, float speed = 14.0f);
void  ResetAni(int id);

enum class Btn { Primary, Secondary, Ghost, Danger, Soft, Success };

bool Button(int id, const Rect& r, const std::wstring& label = L"", Icon ic = Icon::None,
            Btn style = Btn::Secondary, bool enabled = true);
bool IconButton(int id, const Rect& r, Icon ic, const std::wstring& tip = L"", Color tint = Color(), bool enabled = true);
bool Chip(int id, const Rect& r, const std::wstring& label, bool selected = false, Icon ic = Icon::None);

// Чип с подписью справа (например, «2160p  ~1.28 ГБ»). В отличие от Chip
// содержимое выровнено по левому краю с одинаковыми отступами слева и
// справа, поэтому ряд таких кнопок выглядит ровным, а не «плавающим».
bool ChipWithSub(int id, const Rect& r, const std::wstring& label, const std::wstring& sub,
                 bool selected = false, Icon ic = Icon::None);
// Ширина, которую займёт такой чип при заданных подписях
float ChipWithSubWidth(const std::wstring& label, const std::wstring& sub, Icon ic = Icon::None);
bool Toggle(int id, const Rect& r, bool& on, Icon ic = Icon::None);
bool TextField(int id, const Rect& r, std::wstring& value, const std::wstring& placeholder);
bool AnyFieldFocused();
void BlurAllFields();

bool Card(int id, const Rect& r, bool interactive = true, float radius = -1.0f);
void PanelGradV(const Rect& r, float radius, Color top, Color bottom);
void Divider(const Rect& r, Color c = Color());
void PageHeading(const Rect& r, const std::wstring& title, const std::wstring& subtitle = L"");

void Progress(const Rect& r, float pct, Color c1, Color c2, float shimmer = 1.0f);
void ProgressRing(const Rect& r, float pct, Color c1, Color c2);
void Spinner(const Rect& r, Color c, float thickness = 2.4f);
void Skeleton(const Rect& r, float radius = 6.0f);

bool ScrollArea(int id, const Rect& clip, float& scroll, float contentH, const std::function<void(const Rect&)>& body);

void SetTooltip(const Rect& owner, const std::wstring& text);
void DrawTooltip();

} // namespace ui