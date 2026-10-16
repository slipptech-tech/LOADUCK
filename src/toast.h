// toast.h — всплывающие уведомления.
#pragma once

#include "render.h"
#include <string>
#include <deque>

namespace toast {

enum class Kind { Info, Success, Warn, Error };

void Show(const std::wstring& text, Kind kind = Kind::Info, float life = 4.5f);
void Tick();
void Draw(Renderer& r, const RectF& area);
void Clear();
bool Alive();               // есть ли живые уведомления (нужно для перерисовки)

} // namespace toast