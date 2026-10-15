// icons.h — векторные иконки (рисуются геометрией GDI+).
#pragma once

#include "render.h"

enum class Icon {
    None,
    Download, Home, List, History, Settings, Play, Link, Clipboard, Check, Close,
    ChevronDown, ChevronRight, ChevronLeft, Music, Video, Refresh, Pause, Stop, Folder, External,
    Trash, Search, Sparkles, Queue, Shield, Info, Warn, Error, CheckCircle, Copy,
    Cookie, Subtitles, Thumbnail, Speed, Plus, Minus, Image, ArrowLeft, Drag,
    Equalizer, Clock, Flame, Hd, Ticket, Maximize, Restore
};

namespace icons {

// Иконка вписывается в квадрат dst (центрируется, ~78% от стороны)
void Draw(Renderer& r, Icon id, const RectF& dst, Color c, float weight = 1.8f);

// Логотип программы
void DrawLogo(Renderer& r, const RectF& dst);

} // namespace icons