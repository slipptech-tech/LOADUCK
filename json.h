// json.h — минимальный JSON-парсер/сериализатор (только то, что нужно программе).
#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>

struct Json {
    enum class Type { Null, Bool, Num, Str, Arr, Obj };

    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type(Type::Bool), b(v) {}
    Json(double v) : type(Type::Num), num(v) {}
    Json(int v) : type(Type::Num), num((double)v) {}
    Json(long long v) : type(Type::Num), num((double)v) {}
    Json(const char* v) : type(Type::Str), str(v ? v : "") {}
    Json(std::string v) : type(Type::Str), str(std::move(v)) {}

    static Json object() { Json j; j.type = Type::Obj; return j; }
    static Json array()  { Json j; j.type = Type::Arr; return j; }

    bool isNull() const { return type == Type::Null; }
    bool isObj()  const { return type == Type::Obj; }
    bool isArr()  const { return type == Type::Arr; }
    bool isStr()  const { return type == Type::Str; }
    bool isNum()  const { return type == Type::Num; }

    const Json& operator[](const std::string& key) const {
        static const Json nullv;
        auto it = obj.find(key);
        return it == obj.end() ? nullv : it->second;
    }
    const Json& operator[](size_t i) const {
        static const Json nullv;
        return i < arr.size() ? arr[i] : nullv;
    }
    size_t size() const { return type == Type::Arr ? arr.size() : obj.size(); }

    bool has(const std::string& key) const { return type == Type::Obj && obj.find(key) != obj.end(); }

    std::string asStr(const std::string& def = "") const {
        if (type == Type::Str) return str;
        if (type == Type::Num) { char buf[64]; snprintf(buf, sizeof buf, "%g", num); return buf; }
        if (type == Type::Bool) return b ? "true" : "false";
        return def;
    }
    double asNum(double def = 0) const { return type == Type::Num ? num : def; }
    long long asInt(long long def = 0) const { return type == Type::Num ? (long long)llround(num) : def; }
    bool asBool(bool def = false) const { return type == Type::Bool ? b : def; }

    void set(const std::string& key, Json v) { type = Type::Obj; obj[key] = std::move(v); }
    void push(Json v) { type = Type::Arr; arr.push_back(std::move(v)); }

    std::string asStrOrNum() const { return type == Type::Str ? str : asStr(); }

    // ── Сериализация ───────────────────────────────────────────────────────
    static void escape(const std::string& s, std::string& out) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    } else out += (char)c;
            }
        }
        out += '"';
    }

    std::string dump(int indent = -1) const {
        std::string out;
        dumpTo(*this, out, indent, 0);
        return out;
    }

    static void dumpTo(const Json& j, std::string& out, int indent, int depth) {
        auto nl = [&](int d) {
            if (indent < 0) return;
            out += '\n';
            out.append((size_t)(indent * d), ' ');
        };
        switch (j.type) {
            case Type::Null: out += "null"; break;
            case Type::Bool: out += j.b ? "true" : "false"; break;
            case Type::Num: {
                char buf[40];
                if (j.num == (double)(long long)j.num && std::fabs(j.num) < 9e15)
                    snprintf(buf, sizeof buf, "%lld", (long long)j.num);
                else snprintf(buf, sizeof buf, "%.10g", j.num);
                out += buf;
                break;
            }
            case Type::Str: escape(j.str, out); break;
            case Type::Arr: {
                if (j.arr.empty()) { out += "[]"; break; }
                out += '[';
                bool first = true;
                for (const auto& e : j.arr) {
                    if (!first) out += ',';
                    first = false;
                    nl(depth + 1);
                    dumpTo(e, out, indent, depth + 1);
                }
                nl(depth);
                out += ']';
                break;
            }
            case Type::Obj: {
                if (j.obj.empty()) { out += "{}"; break; }
                out += '{';
                bool first = true;
                for (const auto& [k, v] : j.obj) {
                    if (!first) out += ',';
                    first = false;
                    nl(depth + 1);
                    escape(k, out);
                    out += ':';
                    if (indent >= 0) out += ' ';
                    dumpTo(v, out, indent, depth + 1);
                }
                nl(depth);
                out += '}';
                break;
            }
        }
    }

    // ── Парсинг ────────────────────────────────────────────────────────────
    struct Parser {
        const char* p;
        const char* end;
        bool ok = true;

        void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
        bool lit(const char* s) {
            size_t n = strlen(s);
            if ((size_t)(end - p) < n || memcmp(p, s, n) != 0) return false;
            p += n; return true;
        }

        Json parse() {
            ws();
            if (p >= end) { ok = false; return {}; }
            char c = *p;
            if (c == '{') return parseObj();
            if (c == '[') return parseArr();
            if (c == '"') { Json j; j.type = Json::Type::Str; j.str = parseStr(); return j; }
            if (lit("true"))  return Json(true);
            if (lit("false")) return Json(false);
            if (lit("null"))  return Json();
            return parseNum();
        }

        Json parseNum() {
            char* e = nullptr;
            double v = strtod(p, &e);
            if (e == p) { ok = false; return {}; }
            p = e;
            return Json(v);
        }

        std::string parseStr() {
            std::string s;
            if (p < end && *p == '"') ++p;
            while (p < end) {
                unsigned char c = (unsigned char)*p++;
                if (c == '"') return s;
                if (c != '\\') { s += (char)c; continue; }
                if (p >= end) break;
                char e = *p++;
                switch (e) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case '/': s += '/';  break;
                    case '"': s += '"';  break;
                    case '\\': s += '\\'; break;
                    case 'u': {
                        if (end - p < 4) { ok = false; return s; }
                        unsigned cp = (unsigned)strtoul(std::string(p, 4).c_str(), nullptr, 16);
                        p += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            unsigned lo = (unsigned)strtoul(std::string(p + 2, 4).c_str(), nullptr, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                p += 6;
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                        }
                        // UTF-8 кодирование
                        if (cp < 0x80) s += (char)cp;
                        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) {
                            s += (char)(0xE0 | (cp >> 12));
                            s += (char)(0x80 | ((cp >> 6) & 0x3F));
                            s += (char)(0x80 | (cp & 0x3F));
                        } else {
                            s += (char)(0xF0 | (cp >> 18));
                            s += (char)(0x80 | ((cp >> 12) & 0x3F));
                            s += (char)(0x80 | ((cp >> 6) & 0x3F));
                            s += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: s += e;
                }
            }
            ok = false;
            return s;
        }

        Json parseArr() {
            Json j = Json::array();
            ++p; // [
            ws();
            if (p < end && *p == ']') { ++p; return j; }
            while (p < end) {
                j.arr.push_back(parse());
                if (!ok) return j;
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; return j; }
                break;
            }
            ok = false;
            return j;
        }

        Json parseObj() {
            Json j = Json::object();
            ++p; // {
            ws();
            if (p < end && *p == '}') { ++p; return j; }
            while (p < end) {
                ws();
                if (p >= end || *p != '"') break;
                std::string key = parseStr();
                ws();
                if (p >= end || *p != ':') break;
                ++p;
                j.obj[key] = parse();
                if (!ok) return j;
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; return j; }
                break;
            }
            ok = false;
            return j;
        }
    };

    static bool Parse(const std::string& text, Json& out) {
        Parser ps{ text.c_str(), text.c_str() + text.size() };
        out = ps.parse();
        return ps.ok;
    }
};