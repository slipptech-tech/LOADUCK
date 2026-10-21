#include "dlp.h"
#include "util.h"
#include "render.h"
#include "json.h"
#include "lang.h"

#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <deque>
#include <condition_variable>

#pragma comment(lib, "winhttp.lib")

namespace {

// ── Чтение из канала процесса с разбиением на строки ──────────────────────
class PipeReader {
public:
    // maxTail == 0 — не обрезать (нужно для полного JSON от yt-dlp)
    size_t maxTail = 60000;

    void Run(HANDLE h, const std::function<void(const std::string&)>& onLine, std::string* tailAll) {
        if (!h) return;
        char buf[8192];
        std::string pending;
        DWORD read = 0;
        for (;;) {
            if (!ReadFile(h, buf, (DWORD)sizeof buf, &read, nullptr) || read == 0) break;
            pending.append(buf, read);
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                pending.erase(0, pos + 1);
                if (tailAll) {
                    tailAll->append(line);
                    tailAll->push_back('\n');
                    if (maxTail && tailAll->size() > maxTail) tailAll->erase(0, tailAll->size() - maxTail / 2);
                }
                if (onLine) onLine(line);
            }
        }
        if (!pending.empty()) {
            if (tailAll) { tailAll->append(pending); tailAll->push_back('\n'); }
            if (onLine) onLine(pending);
        }
    }
};

// Все дочерние процессы (yt-dlp, ffmpeg) попадают в job object с флагом
// KILL_ON_JOB_CLOSE. Если LOADUCK завершится аварийно (диспетчер задач,
// падение), Windows сама снимет всё дерево. Иначе осиротевший yt-dlp
// продолжил бы качать втихую и испортил .part для следующего запуска.
HANDLE DownloadJob() {
    static HANDLE job = []() -> HANDLE {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (!j) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof(li));
        return j;
    }();
    return job;
}

struct Proc {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE readOut = nullptr;
    HANDLE readErr = nullptr;
    DWORD pid = 0;

    bool Start(const std::wstring& exe, const std::wstring& args) {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE wOut = nullptr, wErr = nullptr;
        if (!CreatePipe(&readOut, &wOut, &sa, 0)) return false;
        if (!CreatePipe(&readErr, &wErr, &sa, 0)) { CloseHandle(readOut); CloseHandle(wOut); return false; }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nullptr;
        si.hStdOutput = wOut;
        si.hStdError = wErr;

        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + exe + L"\" " + args;
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(0);

        SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
        SetEnvironmentVariableW(L"PYTHONUTF8", L"1");

        // CREATE_SUSPENDED — чтобы успеть записать процесс в job object до
        // того, как он успеет породить своих детей.
        BOOL ok = CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
        CloseHandle(wOut);
        CloseHandle(wErr);
        if (!ok) {
            CloseHandle(readOut);
            CloseHandle(readErr);
            readOut = readErr = nullptr;
            return false;
        }
        if (HANDLE job = DownloadJob()) AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);
        process = pi.hProcess;
        thread = pi.hThread;
        pid = pi.dwProcessId;
        return true;
    }

    int Wait() {
        if (!process) return -1;
        WaitForSingleObject(process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        if (thread) CloseHandle(thread);
        process = thread = nullptr;
        CloseHandle(readOut); readOut = nullptr;
        CloseHandle(readErr); readErr = nullptr;
        return (int)code;
    }
};

void KillTree(DWORD pid) {
    if (!pid) return;
    std::wstring cmd = L"taskkill.exe /PID " + std::to_wstring(pid) + L" /T /F";
    std::vector<wchar_t> cb(cmd.begin(), cmd.end());
    cb.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(L"taskkill.exe", cb.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 4000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

int RunProcess(const std::wstring& exe, const std::wstring& args,
               const std::function<void(const std::string&)>& onLine,
               std::string* fullOut, std::string* errTail) {
    Proc p;
    if (!p.Start(exe, args)) return -1;
    PipeReader r1, r2;
    std::thread t1([&] { r1.Run(p.readOut, onLine, fullOut); });
    std::thread t2([&] { r2.Run(p.readErr, nullptr, errTail); });
    t1.join();
    t2.join();
    return p.Wait();
}

std::vector<std::string> SplitStr(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool IsNum(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!isdigit((unsigned char)s[i]) && s[i] != '.' && s[i] != 'e' && s[i] != 'E') return false;
    return true;
}

int ParseEta(const std::string& s) {
    if (!IsNum(s)) return 0;
    if (s.find(':') == std::string::npos) return (int)strtol(s.c_str(), nullptr, 10);
    int total = 0;
    std::string cur;
    for (char c : s + ":") {
        if (c == ':') { total = total * 60 + atoi(cur.c_str()); cur.clear(); }
        else cur += c;
    }
    return total;
}

// Читает оба канала уже запущенного процесса и дожидается его завершения
int PumpAndWait(Proc& proc, const std::function<void(const std::string&)>& onLine, std::string* errTail) {
    PipeReader r1, r2;
    std::thread t1([&] { r1.Run(proc.readOut, onLine, nullptr); });
    std::thread t2([&] { r2.Run(proc.readErr, nullptr, errTail); });
    t1.join();
    t2.join();
    return proc.Wait();
}

} // namespace

// YouTube отдаёт невнятную ошибку «Sign in to confirm you're not a bot», из-за
// которой непонятно, что делать. Превращаем её в конкретную подсказку.
static void AppendCookieHint(std::wstring& err, bool cookiesEnabled) {
    std::string low = util::WideToUtf8(err);
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)tolower(c); });

    const bool botCheck = low.find("not a bot") != std::string::npos ||
                          low.find("confirm you") != std::string::npos;
    const bool cookieProblem = low.find("cookies") != std::string::npos;

    if (!botCheck && !cookieProblem) return;

    err += L"\n\n";
    if (cookiesEnabled)
        err += L"Куки не помогли: закройте браузер и повторите — пока он открыт, его база с куками заблокирована.";
    else
        err += L"Откройте «Настройки» → «Использовать куки браузера» и закройте браузер.";
    err += L" YouTube просит подтверждение входа при слишком частых запросах с одного адреса.";
}

// ── Job ───────────────────────────────────────────────────────────────────

// Сетевые сбои лечатся повтором: частичный файл остаётся на диске, и yt-dlp
// продолжает с того же места. Ошибки «видео удалено» или «нет доступа» сюда
// не попадают — их повторять бессмысленно.
static bool IsTransientFailure(const std::string& tail) {
    std::string low = tail;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    static const char* keys[] = {
        "timed out", "timeout", "connection", "network", "temporary failure",
        "unable to connect", "incompleteread", "eof occurred", "http error 5",
        "getaddrinfo", "transport error", "broken pipe", "remote end closed",
        "read error", "ssl"
    };
    for (const char* k : keys)
        if (low.find(k) != std::string::npos) return true;
    return false;
}

Job::Job(JobOptions opt) : opt_(std::move(opt)) {
    createdAt_ = (double)GetTickCount64();
    prog_.stage = TR(L"В очереди");
}

Job::~Job() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        cancelRequested_ = true;
    }
    DWORD pid = 0;
    { std::lock_guard<std::mutex> lk(mu_); pid = pid_; }
    if (pid) KillTree(pid);
    if (worker_.joinable()) worker_.join();
}

void Job::Start(const std::wstring& exePath) {
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(mu_);
        exePath_ = exePath;
        cancelRequested_ = false;
        paused_ = false;
        state_ = JobState::Running;
        prog_ = JobProgress();
        prog_.stage = TR(L"Подготовка");
        error_.clear();
        pid_ = 0;
        notified_ = false;
    }
    worker_ = std::thread([this] { Run(); });
}

void Job::Run() {
    std::wstring exe;
    { std::lock_guard<std::mutex> lk(mu_); exe = exePath_.empty() ? engine::ResolveTool(L"", L"yt-dlp.exe") : exePath_; }

    if (exe.empty() || !util::FileExists(exe)) {
        std::lock_guard<std::mutex> lk(mu_);
        state_ = JobState::Failed;
        prog_.stage = TR(L"Ошибка");
        error_ = TR(L"Не найден yt-dlp.exe. Установите компоненты в настройках.");
        return;
    }

    std::wstring outTpl = opt_.outTemplate.empty() ? L"%(title)s.%(ext)s" : opt_.outTemplate;
    // Плейлист складываем в отдельную папку: сотня роликов не должна
    // рассыпаться по корню загрузок. Внутри — номер, чтобы одинаковые
    // названия не перетирали друг друга. Свой шаблон с playlist-полями
    // не трогаем.
    if (opt_.playlist && outTpl.find(L"%(playlist") == std::wstring::npos)
        outTpl = L"%(playlist_title)s/%(playlist_index)03d - " + outTpl;
    std::wstring outPath = util::PathJoin(opt_.outDir, outTpl);

    std::wstring args;
    args = L"--no-warnings --no-color --newline --ignore-config --continue ";
    args += opt_.playlist ? L"--yes-playlist " : L"--no-playlist ";
    args += L"--retries 10 --fragment-retries 10 ";   // сеть может моргнуть
    args += L"--progress ";
    args += L"--progress-template \"download:LUPD@@%(progress.downloaded_bytes)s|%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s\" ";
    args += L"--progress-template \"postprocess:LUPP@@%(progress.status)s\" ";
    args += L"--print \"before_dl:LU@@TITLE@@%(title).180B [%(id)s]\" ";
    args += L"--print \"before_dl:LU@@APX@@%(filesize,filesize_approx)s\" ";
    args += L"--print \"before_dl:LU@@PLI@@%(playlist_index)s|%(n_entries)s\" ";
    args += L"--print \"after_move:LU@@FILE@@%(filepath)s\" ";
    args += L"--windows-filenames ";
    args += L"-o \"" + outPath + L"\" ";

    if (!opt_.formatSel.empty()) args += L"-f \"" + util::Utf8ToWide(opt_.formatSel) + L"\" ";

    if (opt_.audioOnly) {
        args += L"-x --audio-format " + opt_.audioFormat + L" ";
    } else if (opt_.mergeTo && !opt_.container.empty()) {
        args += L"--merge-output-format " + opt_.container + L" ";
    }
    if (opt_.embedMeta) args += L"--embed-metadata ";
    if (opt_.embedThumb) args += L"--embed-thumbnail ";
    if (opt_.writeSubs) args += L"--write-subs --sub-langs \"ru,en\" ";
    if (opt_.embedSubs) args += L"--embed-subs --sub-langs \"ru,en\" ";
    if (opt_.useCookies && !opt_.cookiesBrowser.empty())
        args += L"--cookies-from-browser " + opt_.cookiesBrowser + L" ";
    if (opt_.rateLimit > 0)
        args += L"--limit-rate " + std::to_wstring(opt_.rateLimit) + L"K ";
    // Явно указываем ffmpeg: в системе может лежать древняя сборка, которая не понимает
    // современные кодеки и роняет постобработку.
    if (!opt_.ffmpegPath.empty() && util::FileExists(opt_.ffmpegPath))
        args += L"--ffmpeg-location \"" + util::DirName(opt_.ffmpegPath) + L"\" ";
    args += L"\"" + opt_.url + L"\"";

    // Если профиля выбранного браузера нет, сразу пробуем установленный
    bool withCookies = opt_.useCookies && !opt_.cookiesBrowser.empty();
    if (withCookies && !util::BrowserInstalled(opt_.cookiesBrowser))
        withCookies = false;

    std::string errTail;

    auto onLine = [&](const std::string& line) {
        std::lock_guard<std::mutex> lk(mu_);
        if (cancelRequested_) return;

        if (line.rfind("LUPD@@", 0) == 0) {
            auto parts = SplitStr(line.substr(6), '|');
            if (parts.size() >= 5) {
                uint64_t done = IsNum(parts[0]) ? strtoull(parts[0].c_str(), nullptr, 10) : 0;
                uint64_t total = 0;
                if (IsNum(parts[1])) total = strtoull(parts[1].c_str(), nullptr, 10);
                else if (parts.size() > 2 && IsNum(parts[2])) total = strtoull(parts[2].c_str(), nullptr, 10);
                prog_.downloaded = done;
                if (total > 0) prog_.total = total;
                if (IsNum(parts[3])) prog_.speed = strtod(parts[3].c_str(), nullptr);
                prog_.eta = ParseEta(parts[4]);
                if (prog_.total > 0) prog_.pct = std::min(1.0, (double)prog_.downloaded / (double)prog_.total);
                else prog_.pct = std::min(prog_.pct + 0.0002, 0.999);
                prog_.stage = TR(L"Загрузка");
            }
            return;
        }
        if (line.rfind("LUPP@@", 0) == 0) {
            prog_.stage = TR(L"Обработка");
            return;
        }
        if (line.rfind("LU@@", 0) == 0) {
            size_t p2 = line.find("@@", 4);
            if (p2 == std::string::npos) return;
            std::string key = line.substr(4, p2 - 4);
            std::string val = line.substr(p2 + 2);
            if (key == "FILE") {
                file_ = util::Utf8ToWide(val);
            } else if (key == "TITLE") {
                if (opt_.title.empty()) opt_.title = util::Utf8ToWide(val);
            } else if (key == "APX") {
                // before_dl печатается для каждой дорожки (видео и аудио) —
                // накапливаем, иначе итог показывал размер одной дорожки
                if (IsNum(val)) prog_.approx += strtoull(val.c_str(), nullptr, 10);
                if (prog_.total == 0 && prog_.approx > 0) prog_.total = prog_.approx;
            } else if (key == "PLI") {
                auto parts = SplitStr(val, '|');
                if (parts.size() >= 2) {
                    if (IsNum(parts[0])) prog_.item = atoi(parts[0].c_str());
                    if (IsNum(parts[1])) prog_.items = atoi(parts[1].c_str());
                }
            }
            return;
        }
        if (!line.empty() && line[0] == '[') {
            if (line.find("Merger") != std::string::npos) prog_.stage = TR(L"Объединение дорожек");
            else if (line.find("ExtractAudio") != std::string::npos) prog_.stage = TR(L"Извлечение аудио");
            else if (line.find("Subtitle") != std::string::npos) prog_.stage = TR(L"Субтитры");
            else if (line.find("Writing") != std::string::npos) prog_.stage = TR(L"Сохранение");
        }
    };

    // Один запуск yt-dlp. Возвращает код выхода; частичный файл остаётся,
    // поэтому следующий запуск продолжит с места остановки.
    auto startProc = [&]() -> std::unique_ptr<Proc> {
        auto p = std::make_unique<Proc>();
        if (!p->Start(exe, args)) return nullptr;
        std::lock_guard<std::mutex> lk(mu_);
        pid_ = p->pid;
        return p;
    };
    auto runOnce = [&]() -> int {
        auto p = startProc();
        if (!p) return -1;
        int c = PumpAndWait(*p, onLine, &errTail);
        std::lock_guard<std::mutex> lk(mu_);
        pid_ = 0;
        return c;
    };

    int code = runOnce();

    // Куки могут не сработать (браузер закрыт, профиля нет, БД заблокирована).
    // Вместо ошибки один раз пробуем повторить загрузку без них.
    if (code != 0 && withCookies) {
        std::wstring strip = L"--cookies-from-browser " + opt_.cookiesBrowser + L" ";
        size_t p = args.find(strip);
        if (p != std::wstring::npos) {
            args.erase(p, strip.size());
            errTail.clear();
            {
                std::lock_guard<std::mutex> lk(mu_);
                prog_.speed = 0;
                prog_.eta = 0;
                prog_.stage = TR(L"Повтор без куков");
            }
            code = runOnce();
        }
    }

    // Связь оборвалась посреди загрузки — ждём и продолжаем. Пауза, отмена
    // и «видео больше нет» прерывают цикл сразу.
    static const int kBackoff[] = { 4, 8, 15, 30, 60 };
    for (int attempt = 0; code != 0 && attempt < 5; ++attempt) {
        if (!IsTransientFailure(errTail)) break;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (cancelRequested_) break;
            prog_.speed = 0;
            prog_.eta = 0;
            prog_.stage = TR(L"Переподключение…");
        }
        const int need = kBackoff[attempt] * 1000;
        bool stop = false;
        for (int waited = 0; waited < need; waited += 250) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                stop = cancelRequested_;
            }
            if (stop) break;
            Sleep(250);
        }
        if (stop) break;
        errTail.clear();
        {
            std::lock_guard<std::mutex> lk(mu_);
            prog_.stage = TR(L"Продолжаем с места остановки");
        }
        code = runOnce();
        if (code == -1) break;
    }

    std::lock_guard<std::mutex> lk(mu_);
    pid_ = 0;
    if (cancelRequested_) {
        state_ = paused_ ? JobState::Paused : JobState::Canceled;
        prog_.stage = paused_ ? TR(L"На паузе") : TR(L"Отменено");
        return;
    }
    if (code == 0) {
        state_ = JobState::Done;
        prog_.pct = 1.0;
        prog_.speed = 0;
        prog_.eta = 0;
        prog_.stage = TR(L"Готово");
    } else {
        // Файл мог скачаться, а сломаться постобработка (метаданные, обложка).
        std::wstring warn = util::Utf8ToWide(errTail);
        size_t pos = warn.find(L"ERROR:");
        if (pos != std::wstring::npos) warn = warn.substr(pos);
        if (!warn.empty() && warn.back() == L'\r') warn.pop_back();

        bool haveFile = !file_.empty() && util::FileExists(file_);
        if (haveFile) {
            state_ = JobState::Done;
            prog_.pct = 1.0;
            prog_.speed = 0;
            prog_.eta = 0;
            prog_.stage = TR(L"Готово без метаданных");
            if (warn.size() > 160) warn.resize(160);
            error_ = warn;   // показываем как предупреждение
        } else {
            state_ = JobState::Failed;
            prog_.stage = TR(L"Ошибка");
            error_ = warn;
            if (code == -1) error_ = TR(L"Не удалось запустить yt-dlp");
            AppendCookieHint(error_, withCookies);
            if (error_.size() > 700) error_ = error_.substr(error_.size() - 700);
        }
        if (error_.empty()) error_ = TR(L"yt-dlp завершился с кодом ") + std::to_wstring(code);
    }
}

void Job::Cancel() {
    DWORD pid = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cancelRequested_ = true;
        paused_ = false;
        if (state_ == JobState::Queued) state_ = JobState::Canceled;
        pid = pid_;
    }
    if (pid) KillTree(pid);
}

void Job::Pause() {
    DWORD pid = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cancelRequested_ = true;
        paused_ = true;
        pid = pid_;
    }
    if (pid) KillTree(pid);
}

void Job::Resume(const std::wstring& exePath) { Start(exePath); }
void Job::Retry(const std::wstring& exePath) { Start(exePath); }

void Job::MarkPaused() {
    std::lock_guard<std::mutex> lk(mu_);
    state_ = JobState::Paused;
    prog_.stage = TR(L"На паузе");
}

JobOptions Job::GetOptions() const {
    std::lock_guard<std::mutex> lk(mu_);
    return opt_;
}

JobState Job::GetState() const { std::lock_guard<std::mutex> lk(mu_); return state_; }
JobProgress Job::GetProgress() const { std::lock_guard<std::mutex> lk(mu_); return prog_; }
std::wstring Job::GetFile() const { std::lock_guard<std::mutex> lk(mu_); return file_; }
std::wstring Job::GetError() const { std::lock_guard<std::mutex> lk(mu_); return error_; }

uint64_t Job::GetResultSize() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_.empty()) return 0;
    return util::FileSize(file_);
}

std::wstring Job::GetStage() const { std::lock_guard<std::mutex> lk(mu_); return prog_.stage; }

bool Job::IsActive() const {
    JobState s = GetState();
    return s == JobState::Running || s == JobState::Queued;
}

// ── engine ────────────────────────────────────────────────────────────────

namespace engine {

std::wstring ResolveTool(const std::wstring& configured, const std::wstring& exeName) {
    if (!configured.empty() && util::FileExists(configured)) return configured;
    std::wstring bin = util::PathJoin(util::BinDir(), exeName);
    if (util::FileExists(bin)) return bin;
    std::wstring inExeDir = util::PathJoin(util::ExeDir(), exeName);
    if (util::FileExists(inExeDir)) return inExeDir;
    std::wstring up = util::PathJoin(util::DirName(util::ExeDir()), exeName);
    if (util::FileExists(up)) return up;
    return util::FindOnPath(exeName);
}

bool HasYtdlp(const std::wstring& path) { return util::FileExists(path); }
bool HasFfmpeg(const std::wstring& path) { return util::FileExists(path); }

std::string RunCapture(const std::wstring& exe, const std::wstring& args, int* exitCode, size_t maxBytes) {
    std::string out, errTail;
    Proc p;
    if (!p.Start(exe, args)) {
        if (exitCode) *exitCode = -1;
        return {};
    }
    // Обе трубы читаются одновременно. Иначе yt-dlp, застряв на записи в
    // stderr (буфер 64 КБ переполнился), никогда не закроет stdout — и вызов
    // повиснет навсегда. Для больших ответов это происходит легко.
    PipeReader r1, r2;
    r1.maxTail = 0;   // ответ целиком: -J может вернуть несколько мегабайт
    std::thread t1([&] { r1.Run(p.readOut, nullptr, &out); });
    std::thread t2([&] { r2.Run(p.readErr, nullptr, &errTail); });
    t1.join();
    t2.join();
    int code = p.Wait();
    if (exitCode) *exitCode = code;
    if (maxBytes && out.size() > maxBytes) out.resize(maxBytes);
    return out;
}

std::wstring YtdlpVersion(const std::wstring& path) {
    if (!util::FileExists(path)) return {};
    int code = 0;
    std::string out = RunCapture(path, L"--version", &code, 4096);
    if (code != 0) return {};
    return util::Trim(util::Utf8ToWide(out));
}

bool Probe(const std::wstring& url, MediaInfo& out, std::wstring& err,
           bool useCookies, const std::wstring& browser) {
    out = MediaInfo();
    std::wstring exe = ResolveTool(L"", L"yt-dlp.exe");
    if (exe.empty()) {
        err = TR(L"Не найден yt-dlp.exe. Откройте «Настройки» и установите компоненты.");
        return false;
    }

    // Куки нужны уже на разборе: видео «18+» и закрытые ролики без них не отдают
    // даже список форматов. Но если профиль заблокирован (браузер открыт) —
    // повторяем запрос без куок, чтобы анализ не падал.
    const bool wantCookies = useCookies && !browser.empty() && util::BrowserInstalled(browser);
    std::wstring cookieArg = wantCookies ? (L"--cookies-from-browser " + browser + L" ") : L"";

    // Плейлист (чистая ссылка /playlist или ролик с параметром list=) разбираем
    // как плейлист, но тянем только первый ролик: нужны его форматы и обложка.
    const bool wantPlaylist = url.find(L"/playlist") != std::wstring::npos ||
                              url.find(L"list=") != std::wstring::npos;
    std::wstring base = L"--no-warnings --no-color --ignore-config ";
    base += wantPlaylist ? L"--yes-playlist --playlist-end 1 " : L"--no-playlist ";
    std::string text;
    int code = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::wstring args = base + cookieArg + L"-J \"" + url + L"\"";
        text = RunCapture(exe, args, &code, 64u * 1024 * 1024);
        if (code == 0 && !text.empty()) break;
        if (cookieArg.empty()) break;      // уже без куок — повтор не поможет
        cookieArg.clear();                 // ещё одна попытка, уже без куок
    }
    if (code != 0 || text.empty()) {
        // повторяем запрос, чтобы забрать текст ошибки из stderr
        std::string errText;
        Proc p;
        if (p.Start(exe, L"--no-color --ignore-config --simulate \"" + url + L"\"")) {
            PumpAndWait(p, nullptr, &errText);
        }
        std::string tail = errText;
        size_t pos = tail.find("ERROR:");
        if (pos != std::string::npos) tail = tail.substr(pos);
        if (tail.empty()) tail = "yt-dlp не смог обработать ссылку (код " + std::to_string(code) + ")";
        err = util::Utf8ToWide(tail.substr(0, 600));
        AppendCookieHint(err, useCookies);
        return false;
    }

    Json j;
    if (!Json::Parse(text, j) || !j.isObj()) {
        err = TR(L"Не удалось разобрать ответ yt-dlp");
        return false;
    }

    // У плейлиста метаданные (название, обложка) берём у самого плейлиста,
    // а форматы/длительность — у первого ролика.
    const bool isPl = j["_type"].asStr() == "playlist";
    const Json& jj = (isPl && j["entries"].isArr() && j["entries"].size() > 0)
                         ? j["entries"][0] : j;

    out.id = jj["id"].asStr();
    out.title = isPl ? j["title"].asStr() : jj["title"].asStr();
    out.uploader = j["uploader"].asStr();
    if (out.uploader.empty()) out.uploader = jj["uploader"].asStr();
    out.extractor = j["extractor"].asStr();
    if (out.extractor.empty()) out.extractor = jj["extractor"].asStr();
    out.extractorKey = j["extractor_key"].asStr();
    out.webpageUrl = j["webpage_url"].asStr();
    out.thumbnail = j["thumbnail"].asStr();
    if (out.thumbnail.empty()) {
        // Некоторые экстракторы отдают только массив thumbnails
        const Json& th = j["thumbnails"];
        std::string best;
        for (size_t i = 0; i < th.size(); ++i) {
            std::string u = th[i]["url"].asStr();
            if (u.empty()) continue;
            if (u.find("maxres") != std::string::npos) { best = u; break; }
            if (u.size() > best.size()) best = u;
        }
        out.thumbnail = best;
    }
    if (out.thumbnail.empty()) out.thumbnail = jj["thumbnail"].asStr();
    if (out.thumbnail.empty() && !out.id.empty())
        out.thumbnail = "https://i.ytimg.com/vi/" + out.id + "/hqdefault.jpg";
    // WIC не понимает WebP/AVIF: приводим путь и расширение к JPEG
    {
        std::string& th = out.thumbnail;
        const char* bad[] = { "/vi_webp/", "/vi_avif/" };
        for (const char* b : bad) {
            size_t k = th.find(b);
            if (k != std::string::npos) th = th.substr(0, k) + "/vi/" + th.substr(k + 9);
        }
        size_t slash = th.rfind('/');
        if (slash != std::string::npos) {
            std::string tail = th.substr(slash + 1);
            size_t dot = tail.rfind('.');
            if (dot != std::string::npos) {
                std::string ext = tail.substr(dot);
                for (auto& c : ext) c = (char)tolower((unsigned char)c);
                if (ext == ".webp" || ext == ".avif")
                    th = th.substr(0, slash + 1) + tail.substr(0, dot) + ".jpg";
            }
        }
    }
    out.duration = jj["duration"].asNum(0);
    out.durationText = jj["duration_string"].asStr();
    out.viewCount = jj["view_count"].asInt(0);
    out.likeCount = jj["like_count"].asInt(0);
    out.isLive = jj["is_live"].asBool(false);
    out.isPlaylist = isPl;
    if (isPl) {
        out.playlistCount = (int)j["playlist_count"].asInt(0);
        if (out.playlistCount <= 0 && j["entries"].isArr())
            out.playlistCount = (int)j["entries"].size();
        if (j["entries"].isArr() && j["entries"].size() > 0) {
            out.firstEntryUrl = jj["webpage_url"].asStr();
            out.firstEntryTitle = jj["title"].asStr();
            out.firstEntryThumbnail = jj["thumbnail"].asStr();
        }
    }

    const Json& fmts = jj["formats"];
    std::set<int> heights, heights264;
    for (size_t i = 0; i < fmts.size(); ++i) {
        const Json& f = fmts[i];
        MediaFormat mf;
        mf.id = f["format_id"].asStr();
        mf.ext = f["ext"].asStr();
        mf.vcodec = f["vcodec"].asStr();
        mf.acodec = f["acodec"].asStr();
        mf.resolution = f["resolution"].asStr();
        mf.note = f["format_note"].asStr();
        mf.height = (int)f["height"].asInt(0);
        mf.width = (int)f["width"].asInt(0);
        mf.fps = f["fps"].asNum(0);
        mf.filesize = (uint64_t)f["filesize"].asInt(0);
        if (mf.filesize == 0) mf.filesize = (uint64_t)f["filesize_approx"].asInt(0);
        mf.tbr = (int)f["tbr"].asInt(0);
        mf.hasVideo = !mf.vcodec.empty() && mf.vcodec != "none";
        mf.hasAudio = !mf.acodec.empty() && mf.acodec != "none";
        mf.combined = mf.hasVideo && mf.hasAudio;
        if (mf.id.empty()) continue;
        if (!mf.hasVideo && !mf.hasAudio) continue;
        if (mf.height > 0 && mf.hasVideo) {
            heights.insert(mf.height);
            std::string vc = mf.vcodec;
            for (auto& ch : vc) ch = (char)tolower((unsigned char)ch);
            if (vc.rfind("avc", 0) == 0) {          // avc1… — H.264
                heights264.insert(mf.height);
                out.hasH264 = true;
            }
            else if (vc.rfind("vp9", 0) == 0 || vc.rfind("vp09", 0) == 0)
                out.hasVP9 = true;                  // vp9 / vp09 — до 4K, контейнер webm
            else if (vc.rfind("av01", 0) == 0 || vc.rfind("av1", 0) == 0)
                out.hasAV1 = true;                  // av01 — до 8K, контейнер mp4
        }
        out.formats.push_back(mf);
    }
    for (auto it = heights.rbegin(); it != heights.rend(); ++it) out.heights.push_back(*it);
    for (auto it = heights264.rbegin(); it != heights264.rend(); ++it) out.heightsH264.push_back(*it);
    std::stable_sort(out.formats.begin(), out.formats.end(),
                     [](const MediaFormat& a, const MediaFormat& b) {
                         if (a.height != b.height) return a.height > b.height;
                         return a.tbr > b.tbr;
                     });

    if (out.title.empty() && out.isPlaylist && j["entries"].isArr())
        out.playlistCount = (int)j["entries"].size();
    if (out.title.empty()) {
        err = TR(L"Ссылка не содержит доступного видео");
        return false;
    }
    return true;
}

// ── HTTP ──────────────────────────────────────────────────────────────────

    static bool HttpGet(const std::wstring& url, std::vector<uint8_t>* out, uint64_t* total,
                    const std::function<void(uint64_t)>& progress, std::wstring* errOut) {
    HINTERNET session = WinHttpOpen(L"LOADUCK/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (errOut) *errOut = L"Не удалось создать сетевое соединение";
        return false;
    }
    WinHttpSetTimeouts(session, 30000, 30000, 60000, 120000);

    URL_COMPONENTS uc{};
    wchar_t host[256]{};
    wchar_t path[2048]{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        if (errOut) *errOut = TR(L"Некорректный адрес");
        WinHttpCloseHandle(session);
        return false;
    }
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) {
        if (errOut) *errOut = TR(L"Не удалось подключиться к серверу");
        WinHttpCloseHandle(session);
        return false;
    }
    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (secure) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        if (errOut) *errOut = L"Не удалось создать запрос";
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
    DWORD dec = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &dec, sizeof(dec));


// Некоторые CDN (i.ytimg.com) отвечают 403 без User-Agent
    wchar_t ua[256];
    swprintf_s(ua, L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                   L"(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 LOADUCK/1.0");
    DWORD uaLen = (DWORD)((wcslen(ua) + 1) * sizeof(wchar_t));
    WinHttpSetOption(req, WINHTTP_OPTION_USER_AGENT, ua, uaLen);

    BOOL okSend = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    BOOL okRecv = WinHttpReceiveResponse(req, nullptr);
    bool ok = okSend && okRecv;
    if (!ok) {
        if (errOut) *errOut = TR(L"Сервер не ответил");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return false;
    }
    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        if (errOut) *errOut = TR(L"Сервер ответил кодом ") + std::to_wstring(status);
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return false;
    }
    DWORD len = 0, lenSize = sizeof(len);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &len, &lenSize, WINHTTP_NO_HEADER_INDEX);
    if (total) *total = len;

    std::vector<uint8_t> buf;
    uint64_t received = 0;
    const DWORD CHUNK = 1 << 16;
    std::vector<uint8_t> chunk(CHUNK);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), CHUNK, &read)) {
            if (errOut) *errOut = TR(L"Ошибка при загрузке данных");
            WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
            return false;
        }
        if (read == 0) break;
        buf.insert(buf.end(), chunk.begin(), chunk.begin() + read);
        received += read;
        if (progress) progress(received);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    if (out) out->swap(buf);
    return true;
}

void StartComponentDownload(const std::wstring& url, const std::wstring& dest,
                            std::shared_ptr<ComponentDownload> state) {
    state->dest = dest;
    state->running = true;
    state->done = false;
    state->ok = false;
    state->received = 0;
    state->total = 0;
    state->message = TR(L"Подключение…");

    std::thread([url, dest, state] {
        util::EnsureDir(util::DirName(dest));
        std::wstring tmp = dest + L".part";
        uint64_t total = 0;
        std::wstring err;
        std::vector<uint8_t> data;
        bool ok = HttpGet(url, &data, &total,
                          [&](uint64_t got) {
                              state->received = got;
                              state->total = total;
                          },
                          &err);
        if (!ok || data.empty()) {
            state->message = err.empty() ? TR(L"Не удалось скачать файл") : err;
            state->done = true;
            state->ok = false;
            state->running = false;
            return;
        }
        HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            state->message = TR(L"Не удалось создать файл");
            state->done = true;
            state->ok = false;
            state->running = false;
            return;
        }
        DWORD written = 0;
        WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr);
        CloseHandle(f);
        MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING);
        state->received = data.size();
        state->total = data.size();
        state->message = TR(L"Готово");
        state->ok = true;
        state->done = true;
        state->running = false;
    }).detach();
}

static std::set<std::wstring> g_fetching;
static std::mutex g_fetchMu;
static std::deque<std::wstring> g_imageQueue;
static std::condition_variable g_imageCv;
static std::vector<std::thread> g_imageWorkers;
static bool g_imagePoolStarted = false;

// Очередь с ограничением параллелизма. Раньше на каждое изображение создавался
// свой поток: при списке из сотни роликов это десятки одновременных TLS-соединений,
// и картинки приходили медленнее, чем при последовательной загрузке.
static void EnsureImagePool() {
    if (g_imagePoolStarted) return;
    g_imagePoolStarted = true;
    for (int i = 0; i < 4; ++i) {
        g_imageWorkers.emplace_back([] {
            for (;;) {
                std::wstring url;
                {
                    std::unique_lock<std::mutex> lk(g_fetchMu);
                    g_imageCv.wait(lk, [] { return !g_imageQueue.empty(); });
                    url = g_imageQueue.front();
                    g_imageQueue.pop_front();
                }
                std::vector<uint8_t> data;
                uint64_t total = 0;
                std::wstring err;
                if (HttpGet(url, &data, &total, nullptr, &err) && !data.empty() &&
                    data.size() < 12u * 1024 * 1024) {
                    GfxImage img;
                    if (img.Load(data)) ImageCache::Put(url, std::move(img));
                }
                std::lock_guard<std::mutex> lk(g_fetchMu);
                g_fetching.erase(url);
            }
        });
    }
}

void FetchImage(const std::wstring& url, bool urgent) {
    if (url.empty()) return;
    {
        std::lock_guard<std::mutex> lk(g_fetchMu);
        if (ImageCache::Get(url)) return;
        if (g_fetching.count(url)) return;
        g_fetching.insert(url);
        EnsureImagePool();
        // То, что сейчас на экране, идёт вперёд — при прокрутке ждать
        // предзагруженный хвост незачем. Остальное копится в конце очереди.
        if (urgent) g_imageQueue.push_front(url);
        else        g_imageQueue.push_back(url);
    }
    g_imageCv.notify_one();
}

std::string NormalizeExt(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)tolower(c); });
    return e;
}

// ── Каналы ────────────────────────────────────────────────────────────────

std::wstring NormalizeChannelUrl(const std::wstring& input) {
    std::wstring s = util::Trim(input);
    if (s.empty()) return {};
    // Голое имя вида «@something» или «something»
    if (s[0] == L'@') return L"https://www.youtube.com/" + s + L"/videos";
    if (s.find(L"://") == std::wstring::npos && s.find(L'.') == std::wstring::npos)
        return L"https://www.youtube.com/@" + s + L"/videos";
    // Ссылка на конкретный ролик — каналом её считать бессмысленно
    if (util::CleanUrl(s).find(L"/watch") != std::wstring::npos) return {};
    return s;
}

bool ProbeChannel(const std::wstring& url, ChannelInfo& out, std::wstring& err,
                  bool useCookies, const std::wstring& browser, int maxVideos) {
    out = ChannelInfo();
    std::wstring exe = ResolveTool(L"", L"yt-dlp.exe");
    if (exe.empty()) {
        err = TR(L"Не найден yt-dlp.exe. Откройте «Настройки» и установите компоненты.");
        return false;
    }

    const bool wantCookies = useCookies && !browser.empty() && util::BrowserInstalled(browser);
    std::wstring cookieArg = wantCookies ? (L"--cookies-from-browser " + browser + L" ") : L"";

    // Плоский разбор: список роликов приходит сразу, без скачивания каждого
    // ролика целиком — иначе канал из тысячи видео не откроется никогда.
    // Пробел в конце обязателен: куки подставляются между блоками, и без него
    // флаги склеились бы в один несуществующий аргумент.
    const std::wstring base =
        L"--flat-playlist --no-warnings --no-color --ignore-config --dump-single-json ";
    std::wstring limit = L"--playlist-end " + std::to_wstring(maxVideos) + L" ";

    std::string text;
    int code = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        text = RunCapture(exe, base + cookieArg + limit + L"\"" + url + L"\"", &code,
                          64u * 1024 * 1024);
        if (code == 0 && !text.empty()) break;
        if (cookieArg.empty()) break;
        // Куки не сработали (браузер открыт) — повторяем без них
        cookieArg.clear();
    }

    if (code != 0 || text.empty() || text.compare(0, 4, "null") == 0) {
        // Повторный запрос — только ради текста ошибки. Он обязан быть быстрым:
        // без --flat-playlist yt-dlp начал бы выкачивать весь канал целиком,
        // и на крупном канале это висело бы минутами.
        std::string errText;
        Proc p;
        const std::wstring errArgs = L"--no-color --ignore-config --flat-playlist "
                                     L"--playlist-end 1 --simulate \"" + url + L"\"";
        if (p.Start(exe, errArgs)) {
            PumpAndWait(p, nullptr, &errText);
        }
        std::string tail = errText;
        size_t pos = tail.find("ERROR:");
        if (pos != std::string::npos) tail = tail.substr(pos);
        if (tail.empty()) tail = "yt-dlp не смог открыть канал (код " + std::to_string(code) + ")";
        err = util::Utf8ToWide(tail.substr(0, 600));
        AppendCookieHint(err, useCookies);
        return false;
    }

    Json j;
    if (!Json::Parse(text, j) || !j.isObj()) {
        err = TR(L"Не удалось разобрать ответ yt-dlp");
        return false;
    }

    // Название: у вкладки канала оно «Имя - Videos», убираем хвост.
    std::string title = j["channel"].asStr();
    if (title.empty()) title = j["title"].asStr();
    if (title.empty()) title = j["uploader"].asStr();
    if (!title.empty()) {
        static const char* kTails[] = { " - Videos", " - видео", " - Shorts", " - shorts" };
        for (const char* t : kTails) {
            size_t tl = strlen(t);
            if (title.size() > tl && title.compare(title.size() - tl, tl, t) == 0) {
                title.resize(title.size() - tl);
                break;
            }
        }
    }

    out.title = title;
    out.handle = j["uploader_id"].asStr();
    if (out.handle.empty()) out.handle = j["channel_id"].asStr();
    out.url = j["channel_url"].asStr();
    if (out.url.empty()) out.url = j["webpage_url"].asStr();
    out.description = j["description"].asStr();

    const Json& fol = j["channel_follower_count"];
    if (fol.isNum()) {
        out.followers = fol.asInt(0);
        out.followersKnown = out.followers > 0;
    }

    // Аватар: берём самый крупный кадр из превью канала.
    const Json& th = j["thumbnails"];
    std::string best;
    for (size_t i = 0; i < th.size(); ++i) {
        std::string u = th[i]["url"].asStr();
        long long w = th[i]["width"].asInt(0);
        if (u.empty()) continue;
        if (best.empty() || w > 0) best = u;   // yt3 отдаёт по убыванию размера
    }
    out.avatar = best;
    // yt3 отдаёт ссылку на исходник (=s0 или =w1060-…), это мегабайты ради
    // кружка в 116 px. Просим готовый квадрат 240×240: и качается, и
    // декодируется в разы быстрее. Ищем именно спецификатор размера: в самом
    // идентификаторе тоже встречается «=» (base64-добивка).
    if (out.avatar.find("googleusercontent.com") != std::string::npos ||
        out.avatar.find("ggpht.com") != std::string::npos) {
        size_t cut = std::string::npos;
        for (size_t p = out.avatar.size(); p-- > 0; ) {
            if (out.avatar[p] != '=' || p + 2 >= out.avatar.size()) continue;
            char kind = out.avatar[p + 1];
            if ((kind == 's' || kind == 'w' || kind == 'h') &&
                std::isdigit((unsigned char)out.avatar[p + 2])) {
                cut = p;
                break;
            }
        }
        if (cut != std::string::npos)
            out.avatar = out.avatar.substr(0, cut) + "=s240-c-k-c0x00ffffff-no-rj";
    }

    const Json& entries = j["entries"];
    for (size_t i = 0; i < entries.size(); ++i) {
        const Json& e = entries[i];
        ChannelVideo v;
        v.id = e["id"].asStr();
        v.url = e["url"].asStr();
        v.title = e["title"].asStr();
        v.duration = e["duration"].asNum(0);
        v.views = e["view_count"].asInt(0);
        if (v.title.empty() || v.url.empty()) continue;
        // Превью. Подписанные ссылки с параметром sqp CDN отдаёт в WebP, а GDI+
        // его не декодирует — картинка молча пропадала. Поэтому для YouTube
        // всегда собираем неподписанный mqdefault.jpg: это гарантированный JPEG,
        // уже 16:9 (без чёрных полей) и всего ~15 КБ.
        if (e["ie_key"].asStr() == "Youtube" && v.id.size() >= 11) {
            v.thumbnail = "https://i.ytimg.com/vi/" + v.id + "/mqdefault.jpg";
        } else {
            const Json& eth = e["thumbnails"];
            for (size_t k = 0; k < eth.size(); ++k) {
                std::string u = eth[k]["url"].asStr();
                if (u.empty()) continue;
                if (v.thumbnail.empty() || eth[k]["width"].asInt(0) > 320) v.thumbnail = u;
            }
        }
        if (v.views == 0) v.views = e["view_count"].asInt(0);
        out.videos.push_back(std::move(v));
    }

    if (out.title.empty() && out.videos.empty()) {
        err = TR(L"Канал не найден или он пуст");
        return false;
    }
    if (out.title.empty()) out.title = out.handle;
    return true;
}

// Лайки и просмотры одного ролика. Плоский разбор канала их не возвращает —
// они живут в полном -J, поэтому запрашиваются отдельно и только для видимых строк.
VideoStats ProbeVideoStats(const std::wstring& url, bool useCookies, const std::wstring& browser) {
    VideoStats st;
    std::wstring exe = ResolveTool(L"", L"yt-dlp.exe");
    if (exe.empty()) return st;
    const bool wantCookies = useCookies && !browser.empty() && util::BrowserInstalled(browser);
    const std::wstring cookieArg = wantCookies ? (L" --cookies-from-browser " + browser) : L"";

    // Только нужные поля: -J тянет полный список форматов и тормозит в разы.
    const std::wstring args = L"--no-playlist --no-warnings --no-color --ignore-config " +
                              cookieArg + L"--skip-download --print %(view_count)s --print %(like_count)s "
                              L"\"" + url + L"\"";
    int code = 0;
    std::string out = RunCapture(exe, args, &code, 4096);
    if (code != 0 || out.empty()) return st;

    std::vector<std::string> nums;
    size_t pos = 0;
    while (pos < out.size() && nums.size() < 2) {
        size_t nl = out.find('\n', pos);
        if (nl == std::string::npos) nl = out.size();
        std::string line = out.substr(pos, nl - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\n'))
            line.pop_back();
        if (!line.empty() && line != "NA" && line != "None") nums.push_back(line);
        pos = nl + 1;
    }
    if (nums.size() > 0) st.views = strtoll(nums[0].c_str(), nullptr, 10);
    if (nums.size() > 1) st.likes = strtoll(nums[1].c_str(), nullptr, 10);
    st.ok = st.views > 0 || st.likes > 0;
    return st;
}

std::string FormatSelector(const std::wstring& quality, const std::wstring& container,
                           const std::wstring& exactFormatId, const std::wstring& codec) {
    if (!exactFormatId.empty()) return util::WideToUtf8(exactFormatId) + "+bestaudio/best";

    std::string q = util::WideToUtf8(quality);
    std::string c = util::WideToUtf8(container);
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return (char)tolower(ch); });

    if (q == "audio") return "bestaudio[ext=m4a]/bestaudio/best";

    std::string h;
    if (q != "best" && !q.empty()) h = "[height<=" + q + "]";
    const int wantH = (q == "best" || q.empty()) ? 0 : atoi(q.c_str());

    // Выбор кодека. H.264 открывает любой системный плеер, но выше 1080p его
    // на YouTube не бывает. 2K/4K существуют только в VP9 (webm) и AV1 (mp4),
    // поэтому их приходится предлагать явно.
    std::string cf;                      // фильтр по кодеку видеодорожки
    if (codec == L"h264")        cf = "[vcodec~='^(avc|h264)']";
    else if (codec == L"vp9")    cf = "[vcodec~='^vp0?9']";
    else if (codec == L"av1")    cf = "[vcodec~='^(av01|av1)']";
    else if (wantH > 1080)       cf = "[vcodec~='^(av01|av1)']";   // авто: 2K/4K
    else                         cf = "[vcodec~='^(avc|h264)']";   // авто: совместимость

    // VP9 в контейнере mp4 не встречается — YouTube отдаёт его только в webm.
    // Молча менять контейнер нельзя, но и присылать заведомо пустой запрос тоже:
    // оставляем как есть, тогда сработает запасная цепочка внизу.
    const bool vp9InMp4 = (codec == L"vp9") && (c == "mp4");

    if (!cf.empty() && !vp9InMp4) {
        const std::string v = "bestvideo" + h + cf;
        if (c == "webm")
            return v + "+bestaudio[ext=webm]/" + v + "+bestaudio/best" + h + "/best" + h;
        if (c == "mp4" || c == "auto" || c.empty())
            return v + "[ext=mp4]+bestaudio[ext=m4a]/" + v + "+bestaudio/best" + h +
                   "/best" + h + "[ext=mp4]/best" + h;
        return v + "+bestaudio/best" + h;
    }

    if (c == "auto" || c.empty())
        return h.empty() ? std::string("bestvideo*+bestaudio/best")
                         : "bestvideo*" + h + "+bestaudio/best" + h + "/best" + h + "/best";
    if (c == "mp4") {
        // Выше 1080p в MP4 YouTube отдаёт только AV1: VP9 лежит в webm, поэтому
        // обычный запрос [ext=mp4] просто не нашёл бы 4K и молча упал бы в 1080p.
        if (wantH > 1080) {
            std::string av = "bestvideo" + h + "[ext=mp4][vcodec~='^(av01|av1)']";
            return av + "+bestaudio[ext=m4a]/" + av + "+bestaudio/best" + h + "/best" + h;
        }
        return "bestvideo" + h + "[ext=mp4]+bestaudio[ext=m4a]/bestvideo" + h + "[ext=mp4]+bestaudio/best" + h + "/best" + h;
    }
    if (c == "webm")
        return "bestvideo" + h + "[ext=webm]+bestaudio[ext=webm]/bestvideo" + h + "[ext=webm]+bestaudio/best" + h + "/best" + h;
    // mkv и прочие контейнеры
    return "bestvideo" + h + "+bestaudio/best" + h + "/best" + h;
}

} // namespace engine
