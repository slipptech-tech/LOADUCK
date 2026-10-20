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
    bool hasVP9 = false;           // VP9 / VP09 — до 4K, но системные плееры не тянут
    bool hasAV1 = false;           // AV1 — до 8K, лежит в mp4

    std::wstring error;
    bool ok() const { return error.empty() && !title.empty(); }
};

enum class JobState { Queued, Running, Paused, Done, Failed, Canceled };

// ── Канал ─────────────────────────────────────────────────────────────────
// Ролики канала. Заполняется плоским разбором плейлиста: это быстро, но
// часть полей (просмотры, превью у части площадок) недоступна — превью
// для YouTube восстанавливаем по идентификатору.
struct ChannelVideo {
    std::string id;
    std::string title;
    std::string url;
    std::string thumbnail;
    double duration = 0;
    long long views = 0;
    long long likes = 0;
};

// Статистика ролика: лайки и просмотры. Плоский разбор канала их не отдаёт,
// поэтому они дозапрашиваются отдельно — но уже только для видимых строк.
struct VideoStats {
    long long likes = 0;
    long long views = 0;
    bool ok = false;
};

struct ChannelInfo {
    std::string title;
    std::string handle;        // @Something
    std::string url;           // каноническая ссылка
    std::string avatar;
    std::string description;
    long long followers = 0;
    bool followersKnown = false;
    std::vector<ChannelVideo> videos;

    std::wstring error;
    bool ok() const { return error.empty() && !title.empty(); }
};

struct JobProgress {
    double pct = 0.0;
    double speed = 0.0;
    int    eta = 0;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    uint64_t approx = 0;     // сумма размеров дорожек (для оценки до конца)
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
bool Probe(const std::wstring& url, MediaInfo& out, std::wstring& err,
           bool useCookies = false, const std::wstring& browser = L"");

// Приводит ввод пользователя к ссылке на канал: «@handle», «youtube.com/@x»
// и голое имя превращаются в рабочий адрес. Пустая строка — вход не похож на канал.
std::wstring NormalizeChannelUrl(const std::wstring& input);

// Разбор канала: аватар, название, подписчики и список роликов (блокирующий).
bool ProbeChannel(const std::wstring& url, ChannelInfo& out, std::wstring& err,
                  bool useCookies = false, const std::wstring& browser = L"",
                  int maxVideos = 80);

// Лайки и просмотры одного ролика (блокирующий, из фонового потока).
VideoStats ProbeVideoStats(const std::wstring& url, bool useCookies = false,
                           const std::wstring& browser = L"");

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
// Загрузка превью в фоновую очередь. urgent — то, что видно на экране прямо
// сейчас: такие картинки идут в начало очереди и не ждут предзагрузки.
void FetchImage(const std::wstring& url, bool urgent = true);

// Прочее
// codec: auto | h264 | vp9 | av1
std::string FormatSelector(const std::wstring& quality, const std::wstring& container,
                           const std::wstring& exactFormatId, const std::wstring& codec = L"auto");
std::string NormalizeExt(const std::string& ext);
std::string RunCapture(const std::wstring& exe, const std::wstring& args, int* exitCode, size_t maxBytes = 4u * 1024 * 1024);

} // namespace engine