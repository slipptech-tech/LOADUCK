#include "theme.h"
#include "util.h"
#include "render.h"

#include <algorithm>

namespace theme {

static float g_scale = 1.0f;

Color bg         = Color::Rgb(11, 14, 21);
Color bgDeep     = Color::Rgb(14, 18, 26);
Color surface    = Color::Rgb(23, 29, 42);
Color surfaceAlt = Color::Rgb(30, 37, 53);
Color surfaceHi  = Color::Rgb(41, 51, 71);
Color surfaceTop = Color::Rgb(29, 37, 53);
Color stroke     = Color::Rgb(45, 56, 78);
Color strokeHi   = Color::Rgb(70, 88, 120);
Color text       = Color::Rgb(237, 242, 251);
Color textDim    = Color::Rgb(158, 172, 196);
Color textMute   = Color::Rgb(106, 120, 145);
Color success    = Color::Rgb(52, 211, 153);
Color warning    = Color::Rgb(251, 191, 36);
Color danger     = Color::Rgb(248, 113, 113);

static const AccentPreset kAccents[] = {
    { L"Утка",     Color::Rgb(56, 189, 248), Color::Rgb(139, 92, 246) },
    { L"Закат",    Color::Rgb(255, 128, 96),  Color::Rgb(255, 196, 84) },
    { L"Неон",     Color::Rgb(34, 211, 170),  Color::Rgb(59, 130, 246) },
    { L"Сапфир",   Color::Rgb(79, 141, 255),  Color::Rgb(150, 108, 255) },
    { L"Маджента", Color::Rgb(232, 92, 255),  Color::Rgb(99, 102, 241) },
    { L"Изумруд",  Color::Rgb(34, 197, 94),   Color::Rgb(20, 184, 166) },
};
static constexpr int kAccentCount = (int)(sizeof(kAccents) / sizeof(kAccents[0]));

AccentPreset gAccent = kAccents[0];

const AccentPreset* Accents() { return kAccents; }
int AccentCount() { return kAccentCount; }

int AccentIndexByName(const std::wstring& name) {
    for (int i = 0; i < kAccentCount; ++i)
        if (name == kAccents[i].name) return i;
    return 0;
}

float Scale() { return g_scale; }
void  SetScale(float s) {
    g_scale = std::clamp(s, 1.0f, 3.0f);
    GR().SetScale(g_scale);
}

void Init() {}

} // namespace theme