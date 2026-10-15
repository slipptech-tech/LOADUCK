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

} // namespace pages