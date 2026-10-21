// lang.h — выбор языка интерфейса.
#pragma once

#include <string>
#include <vector>

namespace lang {

enum class Id { Russian = 0, English = 1, Ukrainian = 2 };

struct Entry {
    Id id;
    const wchar_t* code;      // "ru", "en", "uk"
    const wchar_t* name;      // название для самого выбора языка
};

const std::vector<Entry>& List();

Id Current();
void Set(Id id);
int  IndexOf(Id id);

// Перевод по русскому оригиналу. Если перевода нет — возвращается сам оригинал,
// поэтому интерфейс никогда не остаётся с пустыми надписями.
const wchar_t* T(const wchar_t* ru);

// Уже переведённая строка (в т.ч. пустая) — без поиска по таблице.
const wchar_t* Empty();

} // namespace lang

#define TR(x) ::lang::T(x)