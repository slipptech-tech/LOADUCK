// pages.h — отрисовка страниц приложения.
#pragma once

#include "app.h"
#include "render.h"

namespace pages {

void DrawSidebar(AppState& a, const RectF& r);
void DrawTitleBar(AppState& a, const RectF& r);
void DrawHeader(AppState& a, const RectF& r);
void DrawContent(AppState& a, const RectF& r);
void DrawStatusBar(AppState& a, const RectF& r);
void DrawBackground(AppState& a, const RectF& r);

// Ссылка, на которую кликнули. Отрисовка не должна запускать внешние процессы:
// ShellExecute внутри WM_PAINT включает вложенный цикл сообщений, кадр
// перерисовывается заново, и тот же клик срабатывает тысячи раз. Поэтому
// отрисовка только запоминает ссылку, а открыть её вызывающий код после кадра.
bool TakePendingUrl(std::wstring& out);

} // namespace pages