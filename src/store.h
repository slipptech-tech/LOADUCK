// store.h — настройки и история (сохраняются в %LOCALAPPDATA%\LOADUCK).
#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <mutex>

struct Settings {
    std::wstring downloadDir;      // куда качать
    std::wstring ytdlpPath;
    std::wstring ffmpegPath;
    std::wstring fileTemplate = L"%(title)s.%(ext)s";
    std::wstring defaultQuality = L"best";   // best | 2160 | 1440 | 1080 | 720 | 480 | 360 | audio
    std::wstring defaultContainer = L"mp4";   // mp4 | mkv | webm | auto
    int  maxParallel = 2;
    bool embedMeta = true;
    bool embedThumbnail = true;
    bool embedSubs = false;
    bool writeSubs = false;
    bool notifications = true;
    bool preferH264 = true;        // H.264 + AAC: файл открывает любой системный плеер
    bool closeToTray = false;
    bool confirmExit = true;
    bool cookiesFromBrowser = false;
    std::wstring cookiesBrowser = L"chrome";
    std::wstring accent = L"Утка";
    bool animatedBackground = true;
    bool soundOnFinish = false;
};

struct HistoryItem {
    std::wstring title;
    std::wstring url;
    std::wstring filePath;
    std::wstring quality;
    std::wstring container;
    uint64_t     size = 0;
    time_t       when = 0;
    bool         opened = false;
};

class Store {
public:
    Store();
    void Load();
    void Save();

    Settings& Get() { return s_; }
    const Settings& Get() const { return s_; }

    const std::vector<HistoryItem>& History() const { return history_; }
    void AddHistory(const HistoryItem& item);
    void RemoveHistory(const std::wstring& url);
    void ClearHistory();
    void MarkOpened(const std::wstring& url);
    size_t HistorySize() const { return history_.size(); }
    uint64_t HistoryBytes() const;

private:
    std::mutex mu_;
    Settings s_;
    std::vector<HistoryItem> history_;
};