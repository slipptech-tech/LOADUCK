// dlp.h — работа с yt-dlp: анализ ссылки, загрузка, установка компонентов.
#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <ctime>

struct MediaFormat {
    std::string id;
    std::string ext;
    std::string vcodec;
    std::string acodec;
    std::string resolution;
    std::string note;
    int  height = 0;
    int  width = 0;
    double fps = 0;
    uint64_t filesize = 0;
    int  tbr = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool combined = false;
};

struct MediaInfo {
    std::string id;
    std::string title;
    std::string uploader;
    std::string extractor;
    std::string extractorKey;
    std::string webpageUrl;
    std::string thumbnail;
    std::string description;
    std::string durationText;
    double duration = 0;
    long long viewCount = 0;
    long long likeCount = 0;
    bool isLive = false;
    bool isPlaylist = false;
    int  playlistCount = 0;

    std::vector<MediaFormat> formats;
    std::vector<int> heights;      // доступные разрешения по убыванию
    std::vector<int> heightsH264;  // разрешения, доступные в H.264 (совместимые с любым плеером)
    bool hasH264 = false;          // есть ли в ролике хотя бы одна H.264-дорожка

    std::wstring error;
    bool ok() const { return error.empty() && !title.empty(); }
};

enum class JobState { Queued, Running, Paused, Done, Failed, Canceled };

struct JobProgress {
    double pct = 0.0;
    double speed = 0.0;
    int    eta = 0;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    std::wstring stage;
};

struct JobOptions {
    std::wstring url;
    std::wstring title;
    std::wstring qualityLabel;    // «1080p · MP4» — для истории
    std::string  formatSel;     // готовое выражение -f
    std::wstring outTemplate;   // шаблон имени файла (с %(title)s и т.п.)
    std::wstring outDir;
    bool  audioOnly = false;
    std::wstring audioFormat = L"mp3";
    bool  mergeTo = true;
    std::wstring container = L"mp4";
    bool  embedMeta = true;
    bool  embedThumb = false;
    bool  writeSubs = false;
    bool  embedSubs = false;
    bool  useCookies = false;
    std::wstring cookiesBrowser = L"chrome";
    int   rateLimit = 0;        // КБ/с, 0 = без ограничения
    std::wstring ffmpegPath;   // ffmpeg.exe для склейки/метаданных (пусто — искать в PATH)
};

class Job {
public:
    Job(JobOptions opt);
    ~Job();

    void Start(const std::wstring& exePath);
    void Cancel();                 // остановить (частичный файл сохранится)
    void Pause();
    void Resume(const std::wstring& exePath);
    void Retry(const std::wstring& exePath);

    // чтение состояния (потокобезопасно)
    JobState  GetState() const;
    JobProgress GetProgress() const;
    std::wstring GetTitle() const { return opt_.title; }
    std::wstring GetQualityLabel() const { return opt_.qualityLabel; }
    void SetNotified() { notified_ = true; }
    bool WasNotified() const { return notified_; }
    std::wstring GetUrl() const { return opt_.url; }
    std::wstring GetFile() const;
    std::wstring GetError() const;
    double GetCreatedAt() const { return createdAt_; }
    uint64_t GetResultSize() const;
    std::wstring GetStage() const;

    bool IsActive() const;

private:
    void Run();
    void SetState(JobState s);

    JobOptions opt_;
    JobState state_ = JobState::Queued;
    mutable std::mutex mu_;
    JobProgress prog_;
    std::wstring file_;
    std::wstring error_;
    std::wstring exePath_;
    DWORD pid_ = 0;
    HANDLE process_ = nullptr;
    std::thread worker_;
    double createdAt_ = 0;
    bool paused_ = false;
    bool cancelRequested_ = false;
    bool notified_ = false;
};

namespace engine {

// Путь к yt-dlp.exe / ffmpeg.exe (настройка → bin → PATH)
std::wstring ResolveTool(const std::wstring& configured, const std::wstring& exeName);
bool HasYtdlp(const std::wstring& path);
bool HasFfmpeg(const std::wstring& path);
std::wstring YtdlpVersion(const std::wstring& path);

// Анализ ссылки (блокирующий, вызывать из потока)
bool Probe(const std::wstring& url, MediaInfo& out, std::wstring& err);

// Загрузка компонента с прогрессом
struct ComponentDownload {
    std::atomic<bool> running{ false };
    std::atomic<bool> done{ false };
    std::atomic<bool> ok{ false };
    std::atomic<uint64_t> received{ 0 };
    std::atomic<uint64_t> total{ 0 };
    std::wstring dest;
    std::wstring message;
};
void StartComponentDownload(const std::wstring& url, const std::wstring& dest,
                            std::shared_ptr<ComponentDownload> state);

// Загрузка картинки (превью) в кэш
void FetchImage(const std::wstring& url);

// Прочее
std::string FormatSelector(const std::wstring& quality, const std::wstring& container,
                           const std::wstring& exactFormatId, bool preferH264 = false);
std::string NormalizeExt(const std::string& ext);
std::string RunCapture(const std::wstring& exe, const std::wstring& args, int* exitCode, size_t maxBytes = 4u * 1024 * 1024);

} // namespace engine