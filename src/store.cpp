#include "store.h"
#include "util.h"
#include "json.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <Shlobj.h>

namespace {
std::wstring SettingsPath() { return util::PathJoin(util::AppDataDir(), L"settings.json"); }
std::wstring HistoryPath()  { return util::PathJoin(util::AppDataDir(), L"history.json"); }

bool ReadFile(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void WriteFile(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(data.data(), (std::streamsize)data.size());
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
} // namespace

Store::Store() {
    // значения по умолчанию
    wchar_t dl[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, dl))) {
        s_.downloadDir = util::PathJoin(dl, L"Downloads\\LOADUCK");
    } else {
        s_.downloadDir = util::PathJoin(util::AppDataDir(), L"Downloads");
    }
    util::EnsureDir(s_.downloadDir);
}

void Store::Load() {
    std::lock_guard<std::mutex> lock(mu_);
    std::string text;
    if (ReadFile(SettingsPath(), text)) {
        Json j;
        if (Json::Parse(text, j) && j.isObj()) {
            auto getw = [&](const char* k, const std::wstring& def) {
                const Json& v = j[k];
                return v.isStr() ? util::Utf8ToWide(v.str) : def;
            };
            auto getb = [&](const char* k, bool def) { return j.has(k) ? j[k].asBool(def) : def; };
            auto geti = [&](const char* k, int def) { return j.has(k) ? (int)j[k].asInt(def) : def; };

            std::wstring dir = getw("downloadDir", s_.downloadDir);
            if (!dir.empty()) s_.downloadDir = dir;
            s_.ytdlpPath   = getw("ytdlpPath", L"");
            s_.ffmpegPath  = getw("ffmpegPath", L"");
            s_.fileTemplate= getw("fileTemplate", s_.fileTemplate);
            s_.defaultQuality = getw("defaultQuality", s_.defaultQuality);
            s_.defaultContainer = getw("defaultContainer", s_.defaultContainer);
            s_.maxParallel = std::clamp(geti("maxParallel", 2), 1, 8);
            s_.embedMeta = getb("embedMeta", true);
            s_.embedThumbnail = getb("embedThumbnail", true);
            s_.embedSubs = getb("embedSubs", false);
            s_.writeSubs = getb("writeSubs", false);
            s_.notifications = getb("notifications", true);
            s_.preferH264 = getb("preferH264", true);
            s_.closeToTray = getb("closeToTray", false);
            s_.confirmExit = getb("confirmExit", true);
            s_.cookiesFromBrowser = getb("cookiesFromBrowser", false);
            s_.cookiesBrowser = getw("cookiesBrowser", L"chrome");
            s_.accent = getw("accent", L"Утка");
            s_.lang = std::clamp(geti("lang", 0), 0, 2);
            s_.animatedBackground = getb("animatedBackground", true);
            s_.soundOnFinish = getb("soundOnFinish", false);
        }
    }
    if (ReadFile(HistoryPath(), text)) {
        Json j;
        if (Json::Parse(text, j) && j.isArr()) {
            for (size_t i = 0; i < j.size(); ++i) {
                const Json& e = j[i];
                HistoryItem h;
                h.title = util::Utf8ToWide(e["title"].asStr());
                h.url = util::Utf8ToWide(e["url"].asStr());
                h.filePath = util::Utf8ToWide(e["filePath"].asStr());
                h.quality = util::Utf8ToWide(e["quality"].asStr());
                h.container = util::Utf8ToWide(e["container"].asStr());
                h.size = (uint64_t)e["size"].asInt(0);
                h.when = (time_t)e["when"].asInt(0);
                h.opened = e["opened"].asBool(false);
                if (!h.url.empty()) history_.push_back(h);
            }
        }
    }
    if (history_.size() > 400) history_.resize(400);
}

void Store::Save() {
    std::lock_guard<std::mutex> lock(mu_);
    Json j = Json::object();
    j.set("downloadDir", Json(util::WideToUtf8(s_.downloadDir)));
    j.set("ytdlpPath", Json(util::WideToUtf8(s_.ytdlpPath)));
    j.set("ffmpegPath", Json(util::WideToUtf8(s_.ffmpegPath)));
    j.set("fileTemplate", Json(util::WideToUtf8(s_.fileTemplate)));
    j.set("defaultQuality", Json(util::WideToUtf8(s_.defaultQuality)));
    j.set("defaultContainer", Json(util::WideToUtf8(s_.defaultContainer)));
    j.set("maxParallel", Json(s_.maxParallel));
    j.set("embedMeta", Json(s_.embedMeta));
    j.set("embedThumbnail", Json(s_.embedThumbnail));
    j.set("embedSubs", Json(s_.embedSubs));
    j.set("writeSubs", Json(s_.writeSubs));
    j.set("notifications", Json(s_.notifications));
    j.set("preferH264", Json(s_.preferH264));
    j.set("closeToTray", Json(s_.closeToTray));
    j.set("confirmExit", Json(s_.confirmExit));
    j.set("cookiesFromBrowser", Json(s_.cookiesFromBrowser));
    j.set("cookiesBrowser", Json(util::WideToUtf8(s_.cookiesBrowser)));
    j.set("accent", Json(util::WideToUtf8(s_.accent)));
    j.set("lang", Json(s_.lang));
    j.set("animatedBackground", Json(s_.animatedBackground));
    j.set("soundOnFinish", Json(s_.soundOnFinish));
    WriteFile(SettingsPath(), j.dump(2));

    Json h = Json::array();
    for (const auto& it : history_) {
        Json e = Json::object();
        e.set("title", Json(util::WideToUtf8(it.title)));
        e.set("url", Json(util::WideToUtf8(it.url)));
        e.set("filePath", Json(util::WideToUtf8(it.filePath)));
        e.set("quality", Json(util::WideToUtf8(it.quality)));
        e.set("container", Json(util::WideToUtf8(it.container)));
        e.set("size", Json((long long)it.size));
        e.set("when", Json((long long)it.when));
        e.set("opened", Json(it.opened));
        h.push(e);
    }
    WriteFile(HistoryPath(), h.dump(2));
}

void Store::AddHistory(const HistoryItem& item) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        RemoveHistory(item.url);
        history_.insert(history_.begin(), item);
        if (history_.size() > 400) history_.pop_back();
    }
    Save();
}

void Store::RemoveHistory(const std::wstring& url) {
    history_.erase(std::remove_if(history_.begin(), history_.end(),
                                  [&](const HistoryItem& h) { return h.url == url; }),
                   history_.end());
}

void Store::ClearHistory() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        history_.clear();
    }
    Save();
}

void Store::MarkOpened(const std::wstring& url) {
    for (auto& h : history_)
        if (h.url == url) h.opened = true;
    Save();
}

uint64_t Store::HistoryBytes() const {
    uint64_t total = 0;
    for (const auto& h : history_) total += h.size;
    return total;
}