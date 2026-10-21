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

// Заставка проверки версии при запуске. Рисуется поверх рабочей области;
// заголовок окна остаётся доступным.
void DrawSplash(AppState& a, const RectF& r);

// Карточка «Обновить приложение?» в правом нижнем углу. Прямоугольник нужен
// вызывающему коду, чтобы страница под карточкой не принимала клики.
RectF UpdatePromptRect(AppState& a, const RectF& area);
void DrawUpdatePrompt(AppState& a, const RectF& area);

// Экран загрузки/установки обновления: затемняет страницу и показывает
// прогресс, а после загрузки — кнопку перезапуска.
void DrawUpdateOverlay(AppState& a, const RectF& area);

// Вопрос при повторной загрузке уже существующего файла. Рисуется поверх
// страницы; вызывающий код на время вопроса блокирует ввод страницы.
void DrawDuplicatePrompt(AppState& a, const RectF& client);

// Ссылка, на которую кликнули. Отрисовка не должна запускать внешние процессы:
// ShellExecute внутри WM_PAINT включает вложенный цикл сообщений, кадр
// перерисовывается заново, и тот же клик срабатывает тысячи раз. Поэтому
// отрисовка только запоминает ссылку, а открыть её вызывающий код после кадра.
bool TakePendingUrl(std::wstring& out);

} // namespace pages