// lang.cpp — таблица переводов интерфейса.
//
// Ключом служит русский оригинал строки: так ничего не теряется — любой текст,
// для которого нет перевода, просто остаётся на русском, а не пропадает.

#include "lang.h"

#include <algorithm>
#include <unordered_map>

namespace lang {

namespace {

struct Row {
    const wchar_t* ru;
    const wchar_t* en;
    const wchar_t* uk;
};

const Row kRows[] = {
    // ── Общее ──────────────────────────────────────────────────────────────
    { L"Каналы",                   L"Channels",                  L"Канали" },
    { L"Аватар, подписчики и все ролики канала", L"Avatar, subscribers and every video", L"Аватар, підписники та всі ролики каналу" },
    { L"Введите адрес канала, ссылку на его страницу или имя — @handle. Покажем аватар, подписчиков и список роликов", L"Enter a channel URL, a link to its page or a name — @handle. We will show the avatar, subscribers and the video list", L"Введiть адресу каналу, посилання на його сторiнку або ім''я \u2014 @handle. Покажемо аватар, пiдписникiв i список роликiв" },
    { L"Открыть",                   L"Open",                      L"Відкрити" },
    { L"Загрузка…",                 L"Loading…",                  L"Завантаження\u2026" },
    { L"Ютуб, TikTok, VK… или @handle", L"YouTube, TikTok, VK\u2026 or @handle", L"YouTube, TikTok, VK\u2026 або @handle" },
    { L"Загружаем канал…",          L"Loading the channel…",      L"Завантажуємо канал\u2026" },
    { L"Получаем список роликов",    L"Fetching the video list",   L"Отримуємо список роликiв" },
    { L"Канал не открыт",           L"Channel not found",         L"Канал не знайдено" },
    { L"Это не похоже на канал",    L"This does not look like a channel", L"Це не схоже на канал" },
    { L"Введите адрес или имя канала", L"Enter a channel URL or name", L"Введiть адресу або ім''я каналу" },
    { L"Дождитесь окончания загрузки канала", L"Wait for the channel to finish loading", L"Дочекайтеся завершення завантаження каналу" },
    { L"Непредвиденная ошибка при разборе канала", L"Unexpected error while reading the channel", L"Непередбачена помилка пiд час розбору каналу" },
    { L"Канал не найден или он пуст", L"Channel not found or it is empty", L"Канал не знайдено або він порожнiй" },
    { L"подписчиков",               L"subscribers",               L"пiдписникiв" },
    { L"роликов загружено",         L"videos loaded",             L"роликiв завантажено" },
    { L"Открыть канал в браузере",  L"Open the channel in the browser", L"Вiдкрити канал у браузерi" },
    { L"В канале нет доступных роликов", L"The channel has no videos", L"У каналi немає доступних роликiв" },
    { L"Ролики",                    L"Videos",                    L"Ролики" },
    { L"Кодек",                     L"Codec",                     L"Кодек" },
    { L"Авто",                      L"Auto",                      L"Авто" },
    { L"H.264 до 1080p, выше — AV1", L"H.264 up to 1080p, above — AV1", L"H.264 до 1080p, вище \u2014 AV1" },
    { L"Любой плеер, только до 1080p", L"Any player, up to 1080p only", L"Будь-який плеєр, лише до 1080p" },
    { L"2K/4K, контейнер webm",     L"2K/4K, webm container",     L"2K/4K, контейнер webm" },
    { L"2K/4K/8K, контейнер mp4",   L"2K/4K/8K, mp4 container",   L"2K/4K/8K, контейнер mp4" },    { L"Загрузка видео",              L"Video downloader",          L"Завантажувач відео" },
    { L"Очередь загрузок",            L"Download queue",            L"Черга завантажень" },
    { L"История",                     L"History",                   L"Історія" },
    { L"Настройки",                   L"Settings",                  L"Налаштування" },
    { L"Загрузка",                    L"Download",                  L"Завантаження" },

    { L"Компоненты, папки и поведение программы", L"Components, folders and behaviour", L"Компоненти, теки та поведінка" },
    { L"Активные, завершённые и ошибочные загрузки", L"Active, finished and failed downloads", L"Активні, завершені та помилкові завантаження" },
    { L"Все файлы, скачанные ранее",  L"All files downloaded earlier", L"Усі файли, завантажені раніше" },
    { L"YouTube, TikTok, Instagram, X, Twitch и ещё 20+ сервисов",
                                      L"YouTube, TikTok, Instagram, X, Twitch and 20+ more services",
                                      L"YouTube, TikTok, Instagram, X, Twitch та ще 20+ сервісів" },

    { L"Очередь",                   L"Queue",                     L"Черга" },
    { L"Компоненты",                 L"Components",                L"Компоненти" },
    { L"Видео",                      L"Video",                     L"Відео" },
    { L"не установлен",              L"not installed",             L"не встановлено" },
    { L"Папка",                      L"Folder",                    L"Тека" },
    { L"   ·   файл не найден",      L"   ·   file not found",     L"   ·   файл не знайдено" },
    { L" активных",                  L" active",                   L" активних" },
    { L" скачано",                   L" downloaded",               L" завантажено" },
    { L"Исполняемые файлы (*.exe)\0*.exe\0\0", L"Executable files (*.exe)\0*.exe\0\0", L"Виконувані файли (*.exe)\0*.exe\0\0" },
    { L"Не удалось инициализировать графический движок (Direct2D).", L"Could not initialise the graphics engine", L"Не вдалося ініціалізувати графічний рушій" },
    { L"Повтор без куков",            L"Retrying without cookies",  L"Повтор без куків" },
    { L"Telegram — написать автору", L"Telegram — contact the author", L"Telegram \u2014 написати авторові" },
    { L"Исходный код на GitHub",  L"Source code on GitHub",      L"Вихідний код на GitHub" },
    { L"H.264 — до 1080p; выше нужен VP9/AV1", L"H.264 — up to 1080p; above that VP9/AV1 is required", L"H.264 \u2014 до 1080p; вище потрібен VP9/AV1" },
    { L"H.264 бывает только до 1080p — для этого качества нужен VP9/AV1", L"H.264 only goes up to 1080p — this quality needs VP9/AV1", L"H.264 буває лише до 1080p \u2014 для цієї якості потрібен VP9/AV1" },
    { L"4K доступно только в VP9/AV1 — штатный плеер Windows может его не открыть", L"4K exists only in VP9/AV1 \u2014 the built-in Windows player may not open it", L"4K існує лише у VP9/AV1 \u2014 вбудований плеєр Windows може його не відкрити" },
    // ── Главная ────────────────────────────────────────────────────────────
    { L"Вставьте ссылку на видео",    L"Paste a video link",         L"Вставте посилання на відео" },
    { L"Вставить",                    L"Paste",                     L"Вставити" },
    { L"Найти",                       L"Find",                      L"Знайти" },
    { L"Анализ…",                     L"Analysing…",                L"Аналіз…" },
    { L"Новая ссылка",                L"New link",                  L"Нове посилання" },
    { L"Как пользоваться",            L"How to use",                L"Як користуватися" },
    { L"Скопируйте ссылку на видео из YouTube, TikTok или другого сервиса",
                                      L"Copy a video link from YouTube, TikTok or another service",
                                      L"Скопіюйте посилання на відео з YouTube, TikTok чи іншого сервісу" },
    { L"Вставьте её в поле выше (Ctrl+V) и нажмите «Найти»",
                                      L"Paste it in the field above (Ctrl+V) and press “Find”",
                                      L"Вставте її в поле вище (Ctrl+V) і натисніть «Знайти»" },
    { L"Выберите качество и формат, затем нажмите «Скачать»",
                                      L"Choose quality and format, then press “Download”",
                                      L"Виберіть якість і формат, потім натисніть «Завантажити»" },
    { L"Отпустите ссылку",            L"Drop the link",             L"Відпустіть посилання" },

    { L"Качество",                    L"Quality",                   L"Якість" },
    { L"Максимум",                    L"Maximum",                   L"Максимум" },
    { L"Только аудио",                L"Audio only",                L"Тільки аудіо" },
    { L"Совместимый кодек H.264",     L"Compatible codec H.264",    L"Сумісний кодек H.264" },
    { L"Формат контейнера",           L"Container format",          L"Формат контейнера" },
    { L"Формат аудио",                L"Audio format",              L"Формат аудіо" },
    { L"Исходный",                    L"Original",                  L"Оригінальний" },

    { L"Куда сохранять",              L"Save to",                   L"Куди зберігати" },
    { L"Папка загрузок",              L"Download folder",           L"Тека завантажень" },
    { L"Имя файла",                   L"File name",                 L"Ім'я файлу" },
    { L"Шаблон имени",                L"File name template",        L"Шаблон імені" },
    { L"Куда сохранять и как называть файлы", L"Where to save and how to name files", L"Куди зберігати та як називати файли" },
    { L"Поддерживаются: %(title)s  %(id)s  %(ext)s  %(upload_date)s  %(duration)s",
                                      L"Supported: %(title)s  %(id)s  %(ext)s  %(upload_date)s  %(duration)s",
                                      L"Підтримуються: %(title)s  %(id)s  %(ext)s  %(upload_date)s  %(duration)s" },
    { L"Встроить обложку",            L"Embed cover",               L"Вбудувати обкладинку" },
    { L"Метаданные в файл",           L"Metadata into file",        L"Метадані у файл" },
    { L"Скачать субтитры",            L"Download subtitles",        L"Завантажити субтитри" },
    { L"Исходный контейнер",          L"Original container",        L"Оригінальний контейнер" },

    { L"Будет сохранено как",         L"Will be saved as",          L"Буде збережено як" },
    { L"Лучшее доступное качество",   L"Best available quality",    L"Найкраща доступна якість" },
    { L"Только аудио · ",             L"Audio only · ",             L"Тільки аудіо · " },
    { L" · H.264",                    L" · H.264",                  L" · H.264" },
    { L"Скачать",                     L"Download",                  L"Завантажити" },
    { L"Дополнительно",               L"More options",              L"Додатково" },
    { L"Скрыть дополнительно",        L"Hide extra options",        L"Сховати додаткові" },
    { L"Формат: ",                    L"Format: ",                  L"Формат: " },
    { L"Скопировать ссылку",          L"Copy link",                 L"Скопіювати посилання" },

    // ── Дополнительно ──────────────────────────────────────────────────────
    { L"Ограничение скорости",        L"Speed limit",               L"Обмеження швидкості" },
    { L"Без ограничений",             L"Unlimited",                 L"Без обмежень" },
    { L" КБ/с",                       L" KB/s",                     L" КБ/с" },
    { L"Браузер не найден — куки отключены", L"No browser found — cookies disabled", L"Браузер не знайдено — куки вимкнено" },
    { L"Браузер запущен — закройте его, иначе куки не прочитаются", L"Browser is running — close it, otherwise cookies cannot be read", L"Браузер запущено \u2014 закрийте його, \u0456\u043d\u0430\u043a\u0448\u0435 \u043a\u0443\u043a\u0438 \u043d\u0435 \u043f\u0440\u043e\u0447\u0438\u0442\u0430\u044e\u0442\u044c\u0441\u044f" },
    { L"Браузер запущен — закройте его", L"Browser is running — close it", L"Браузер запущено \u2014 закрийте його" },    { L"Куки из браузера",            L"Browser cookies",           L"Куки з браузера" },
    { L"Все форматы (точный выбор)",  L"All formats (exact pick)",  L"Усі формати (точний вибір)" },
    { L"Куки будут взяты из выбранного браузера. Закройте его перед скачиванием",
                                      L"Cookies will be taken from the selected browser. Close it before downloading",
                                      L"Куки буде взято з вибраного браузера. Закрийте його перед завантаженням" },

    // ── Очередь ────────────────────────────────────────────────────────────
    { L"Очистить завершённые",       L"Clear finished",            L"Очистити завершені" },
    { L"Здесь пока пусто",            L"Nothing here yet",          L"Тут поки що порожньо" },
    { L"Запустите загрузку с главной страницы", L"Start a download from the home page", L"Запустіть завантаження з головної сторінки" },
    { L"Загрузка…",                   L"Downloading…",              L"Завантаження…" },
    { L"Готов",                       L"Ready",                     L"Готово" },
    { L"Готово",                      L"Done",                      L"Готово" },
    { L"Готово без метаданных",       L"Done without metadata",     L"Готово без метаданих" },
    { L"Ошибка",                      L"Error",                     L"Помилка" },
    { L"Ошибка загрузки",             L"Download error",            L"Помилка завантаження" },
    { L"Подготовка",                  L"Preparing",                 L"Підготовка" },
    { L"Загрузка ",                   L"Downloading ",              L"Завантаження " },
    { L"Обработка",                   L"Processing",                L"Обробка" },
    { L"Объединение дорожек",        L"Merging tracks",            L"Об'єднання доріжок" },
    { L"Извлечение аудио",            L"Extracting audio",          L"Витягування аудіо" },
    { L"Субтитры",                    L"Subtitles",                 L"Субтитри" },
    { L"Сохранение",                  L"Saving",                    L"Збереження" },
    { L"На паузе",                    L"Paused",                    L"На паузі" },
    { L"Отменено",                    L"Canceled",                  L"Скасовано" },
    { L"Пауза (файл сохранится)",    L"Pause (file is kept)",      L"Пауза (файл збережеться)" },
    { L"Продолжить",                  L"Resume",                    L"Продовжити" },
    { L"Отменить",                    L"Cancel",                    L"Скасувати" },
    { L"Повторить",                   L"Retry",                     L"Повторити" },
    { L"Показать файл",               L"Show file",                 L"Показати файл" },
    { L"Убрать из списка",            L"Remove from list",          L"Прибрати зі списку" },
    { L" из ",                        L" of ",                      L" з " },
    { L" лайков",                   L" likes",                     L" лайкiв" },    { L" просмотров",                 L" views",                    L" переглядів" },
    { L" · файл не найден",          L" · file not found",         L" · файл не знайдено" },
    { L"Ошибка загрузки: ",           L"Download error: ",          L"Помилка завантаження: " },
    { L"Готово: ",                    L"Done: ",                    L"Готово: " },
    { L"Добавлено в очередь: ",       L"Added to queue: ",          L"Додано в чергу: " },
    { L"Дождитесь окончания анализа", L"Wait for the analysis to finish", L"Дочекайтеся завершення аналізу" },

    // ── История ────────────────────────────────────────────────────────────
    { L"файлов в истории",            L"files in history",          L"файлів в історії" },
    { L"суммарный объём",             L"total size",                L"сумарний обсяг" },
    { L"папка загрузок",              L"download folder",           L"тека завантажень" },
    { L"Поиск по названию…",          L"Search by title…",          L"Пошук за назвою…" },
    { L"Очистить историю",            L"Clear history",             L"Очистити історію" },
    { L"История пуста",               L"History is empty",          L"Історія порожня" },
    { L"Скачанные файлы появятся здесь", L"Downloaded files will appear here", L"Завантажені файли з’являться тут" },
    { L"Убрать из истории",           L"Remove from history",       L"Прибрати з історії" },

    // ── Настройки ──────────────────────────────────────────────────────────
    { L"yt-dlp скачивает видео, ffmpeg склеивает дорожки и извлекает звук",
                                      L"yt-dlp downloads the video, ffmpeg merges tracks and extracts audio",
                                      L"yt-dlp завантажує відео, ffmpeg об’єднує доріжки та витягує звук" },
    { L"Обновить",                    L"Update",                    L"Оновити" },
    { L"Обзор",                       L"Browse",                    L"Огляд" },
    { L"Установить",                  L"Install",                   L"Встановити" },
    { L"Папка компонентов: ",         L"Components folder: ",       L"Тека компонентів: " },
    { L"Одновременных загрузок",      L"Parallel downloads",        L"Одночасних завантажень" },
    { L"Можно поставить в очередь сразу несколько ссылок",
                                      L"You can queue several links at once",
                                      L"Можна поставити в чергу кілька посилань одразу" },
    { L"Меньше",                      L"Less",                      L"Менше" },
    { L"Больше",                      L"More",                      L"Більше" },
    { L"Уведомления о завершении",   L"Notify on completion",      L"Сповіщати про завершення" },
    { L"Спрашивать перед выходом",    L"Ask before exiting",        L"Питати перед виходом" },
    { L"Сворачивать в трей вместо выхода", L"Minimize to tray instead of exiting", L"Згортати в трей замість виходу" },

    { L"Формат по умолчанию",         L"Default format",            L"Формат за замовчуванням" },
    { L"Контейнер",                   L"Container",                 L"Контейнер" },
    { L"Аудио",                       L"Audio",                     L"Аудіо" },

    { L"Доступ к закрытым видео",     L"Access to private videos",  L"Доступ до закритих відео" },
    { L"Куки из вашего браузера помогают скачивать видео, требующие входа",
                                      L"Cookies from your browser help download videos that require sign-in",
                                      L"Куки з вашого браузера допомагають завантажувати відео, що вимагають входу" },
    { L"Использовать куки браузера",  L"Use browser cookies",       L"Використовувати куки браузера" },

    { L"Интерфейс",                   L"Interface",                 L"Інтерфейс" },
    { L"Цветовая схема и оформление", L"Colours and style",         L"Кольорова схема та оформлення" },
    { L"Язык интерфейса",             L"Interface language",        L"Мова інтерфейсу" },
    { L"Данные программы",            L"Application data",          L"Дані програми" },
    { L"Настройки и история: ",       L"Settings and history: ",    L"Налаштування та історія: " },
    { L"Открыть папку",               L"Open folder",               L"Відкрити теку" },
    { L"Сбросить настройки",          L"Reset settings",            L"Скинути налаштування" },
    { L"Сбросить все настройки к значениям по умолчанию?",
                                      L"Reset all settings to defaults?", L"Скинути всі налаштування до типових?" },
    { L"Настройки сброшены",          L"Settings reset",            L"Налаштування скинуто" },

    // ── Расширение для браузера ────────────────────────────────────────────
    { L"Расширение для браузера",     L"Browser extension",         L"Розширення для браузера" },
    { L"Кнопка LOADUCK в Chrome и Edge: одно нажатие — и видео уже скачивается",
                                      L"The LOADUCK button in Chrome and Edge: one click and the video is downloading",
                                      L"Кнопка LOADUCK у Chrome і Edge: одне натискання — і відео вже завантажується" },
    { L"Выключено",                   L"Off",                       L"Вимкнено" },
    { L"Подключено",                  L"Connected",                 L"Підключено" },
    { L"Не подключено",               L"Not connected",             L"Не підключено" },
    { L"Принимать ссылки из браузера", L"Accept links from the browser", L"Приймати посилання з браузера" },
    { L"Подключить браузер",          L"Connect the browser",       L"Підключити браузер" },
    { L"Папка расширения",            L"Extension folder",          L"Тека розширення" },
    { L"Связь с браузером установлена", L"Browser connection is set up", L"Зв'язок із браузером встановлено" },
    { L"Не удалось записать настройки связи", L"Could not write the connection settings", L"Не вдалося записати налаштування зв'язку" },
    { L"Готово: загрузите папку extension в chrome://extensions",
                                      L"Done: load the extension folder at chrome://extensions",
                                      L"Готово: завантажте теку extension у chrome://extensions" },
    { L"Папка extension не найдена рядом с программой",
                                      L"The extension folder was not found next to the app",
                                      L"Теку extension не знайдено поруч із програмою" },
    { L"Запускать свёрнутым в трей",  L"Start minimised to tray",   L"Запускати згорнутим у трей" },
    { L"Закрывать после загрузки",    L"Close after downloading",   L"Закривати після завантаження" },
    { L"Качество для загрузок из браузера", L"Quality for browser downloads", L"Якість для завантажень із браузера" },
    { L"Как в настройках",            L"As in settings",            L"Як у налаштуваннях" },
    { L"После подключения загрузите папку extension в chrome://extensions",
                                      L"After connecting, load the extension folder at chrome://extensions",
                                      L"Після підключення завантажте теку extension у chrome://extensions" },
    { L"Ссылка из браузера — начинаем анализ", L"Link from the browser — analysing", L"Посилання з браузера — починаємо аналіз" },
    { L"Ссылка из браузера — начинаем загрузку", L"Link from the browser — starting the download", L"Посилання з браузера — починаємо завантаження" },

    // ── Окно, трей, уведомления ────────────────────────────────────────────
    { L"Свернуть",                    L"Minimise",                  L"Згорнути" },
    { L"Развернуть",                  L"Maximise",                  L"Розгорнути" },
    { L"Закрыть",                     L"Close",                     L"Закрити" },
    { L"Выход",                       L"Exit",                      L"Вихід" },
    { L"Открыть LOADUCK",             L"Open LOADUCK",              L"Відкрити LOADUCK" },
    { L"Открыть настройки",           L"Open settings",             L"Відкрити налаштування" },
    { L"Открыть папку загрузок",      L"Open download folder",      L"Відкрити теку завантажень" },
    { L"LOADUCK свернулся",           L"LOADUCK minimised",         L"LOADUCK згорнуто" },
    { L"Загрузка завершена",          L"Download finished",         L"Завантаження завершено" },
    { L"Загрузки продолжаются в фоне", L"Downloads keep running in the background", L"Завантаження тривають у фоні" },
    { L"Загрузки ещё не завершены. Выйти из программы?",
                                      L"Some downloads are not finished. Exit anyway?",
                                      L"Деякі завантаження не завершено. Вийти з програми?" },

    // ── Сообщения ──────────────────────────────────────────────────────────
    { L"Сначала вставьте ссылку",    L"Paste a link first",        L"Спершу вставте посилання" },
    { L"Сначала проанализируйте ссылку", L"Analyse the link first",  L"Спершу проаналізуйте посилання" },
    { L"Это не похоже на ссылку на видео", L"This does not look like a video link", L"Це не схоже на посилання на відео" },
    { L"Ссылка из командной строки не поддерживается", L"Command-line link is not supported", L"Посилання з командного рядка не підтримується" },
    { L"В буфере обмена нет ссылки",  L"No link in the clipboard",  L"У буфері обміну немає посилання" },
    { L"Ссылка вставлена",            L"Link pasted",               L"Посилання вставлено" },
    { L"Ссылка скопирована",          L"Link copied",               L"Посилання скопійовано" },
    { L"Ссылка проанализирована",     L"Link analysed",             L"Посилання проаналізовано" },
    { L"Не удалось получить видео",   L"Could not fetch the video", L"Не вдалося отримати відео" },
    { L"Неизвестная ошибка",          L"Unknown error",             L"Невідома помилка" },
    { L"Эта ссылка уже в очереди",    L"This link is already queued", L"Це посилання вже в черзі" },
    { L"Папка загрузок изменена",     L"Download folder changed",   L"Теку завантажень змінено" },
    { L"Выбрать папку",               L"Choose folder",             L"Вибрати теку" },
    { L"Папка для загрузок",          L"Download folder",           L"Тека для завантажень" },
    { L"Не удалось скачать файл",     L"Could not download the file", L"Не вдалося завантажити файл" },

    { L"Не установлен yt-dlp — откройте Настройки", L"yt-dlp is not installed — open Settings", L"yt-dlp не встановлено — відкрийте Налаштування" },
    { L"Установите yt-dlp в настройках, чтобы начать", L"Install yt-dlp in settings to start", L"Встановіть yt-dlp у налаштуваннях, щоб почати" },
    { L"yt-dlp не найден",            L"yt-dlp not found",          L"yt-dlp не знайдено" },
    { L"ffmpeg не найден",            L"ffmpeg not found",          L"ffmpeg не знайдено" },
    { L"yt-dlp установлен",           L"yt-dlp installed",          L"yt-dlp встановлено" },
    { L"ffmpeg найден",               L"ffmpeg found",              L"ffmpeg знайдено" },
    { L"Не найден yt-dlp.exe. Установите компоненты в настройках.",
                                      L"yt-dlp.exe not found. Install the components in settings.",
                                      L"yt-dlp.exe не знайдено. Встановіть компоненти в налаштуваннях." },
    { L"Не найден yt-dlp.exe. Откройте «Настройки» и установите компоненты.",
                                      L"yt-dlp.exe not found. Open “Settings” and install the components.",
                                      L"yt-dlp.exe не знайдено. Відкрийте «Налаштування» і встановіть компоненти." },
    { L"Без ffmpeg склейка дорожек недоступна", L"Merging tracks needs ffmpeg", L"Для об’єднання доріжок потрібен ffmpeg" },
    { L"ffmpeg не найден: склейка дорожек недоступна", L"ffmpeg not found: merging tracks is unavailable", L"ffmpeg не знайдено: об’єднання доріжок недоступне" },
    { L"Готово к работе. Вставьте ссылку на видео", L"Ready. Paste a video link", L"Готово. Вставте посилання на відео" },

    { L"Получаем информацию о видео…", L"Fetching video details…",   L"Отримуємо інформацію про відео…" },
    { L"Подключение…",                L"Connecting…",               L"Підключення…" },

    { L"Не удалось запустить yt-dlp", L"Could not start yt-dlp",    L"Не вдалося запустити yt-dlp" },
    { L"yt-dlp завершился с кодом ",  L"yt-dlp exited with code ",  L"yt-dlp завершився з кодом " },
    { L"Не удалось разобрать ответ yt-dlp", L"Could not parse yt-dlp output", L"Не вдалося розібрати відповідь yt-dlp" },
    { L"Непредвиденная ошибка при анализе ссылки", L"Unexpected error while analysing the link", L"Непередбачена помилка під час аналізу посилання" },
    { L"Ссылка не содержит доступного видео", L"The link contains no available video", L"Посилання не містить доступного відео" },

    { L"Не удалось подключиться к серверу", L"Could not connect to the server", L"Не вдалося підключитися до сервера" },
    { L"Сервер не ответил",            L"The server did not respond", L"Сервер не відповів" },
    { L"Сервер ответил кодом ",       L"Server responded with code ", L"Сервер відповів кодом " },
    { L"Некорректный адрес",          L"Invalid address",           L"Некоректна адреса" },
    { L"Ошибка при загрузке данных",  L"Data download error",       L"Помилка під час завантаження даних" },
    { L"Не удалось создать файл",     L"Could not create the file", L"Не вдалося створити файл" },

    { L"Ctrl+V — вставить    ·    Ctrl+Enter — скачать    ·    Ctrl+1…4 — разделы",
                                      L"Ctrl+V — paste    ·    Ctrl+Enter — download    ·    Ctrl+1…4 — sections",
                                      L"Ctrl+V — вставити    ·    Ctrl+Enter — завантажити    ·    Ctrl+1…4 — розділи" },

    // ── Статус-бар ─────────────────────────────────────────────────────────
    { L"Готов",                       L"Ready",                     L"Готово" },
    { L"В очереди",                   L"In queue",                  L"У черзі" },
    { L"в очереди ",                  L" queued",                   L" у черзі " },
    { L"скачано",                     L"downloaded",                L"завантажено" },
    { L"активных",                    L" active",                   L" активних" },
    { L"параллельно ",                L"parallel ",                 L"паралельно " },
    { L"Активные",                    L"Active",                    L"Активні" },
    { L"Завершённые",                 L"Finished",                  L"Завершені" },
    { L"Ошибки",                      L"Failed",                    L"Помилки" },
    { L"Все",                         L"All",                       L"Усі" },
};

using Table = std::unordered_map<std::wstring, const wchar_t*>;

Table Build(Id id) {
    Table m;
    m.reserve(sizeof(kRows) / sizeof(kRows[0]) * 2);
    for (const Row& r : kRows) {
        const wchar_t* v = (id == Id::English) ? r.en : r.uk;
        if (v && *v) m.emplace(r.ru, v);
    }
    return m;
}

Id g_current = Id::Russian;
Table* g_table = nullptr;

} // namespace

const std::vector<Entry>& List() {
    static const std::vector<Entry> v = {
        { Id::Russian,   L"ru", L"Русский" },
        { Id::Ukrainian, L"uk", L"Українська" },
        { Id::English,   L"en", L"English" },
    };
    return v;
}

Id Current() { return g_current; }

void Set(Id id) {
    if (id == g_current) return;
    g_current = id;
    delete g_table;
    g_table = new Table(Build(id));
}

int IndexOf(Id id) {
    const auto& v = List();
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id) return (int)i;
    return 0;
}

const wchar_t* T(const wchar_t* ru) {
    if (!ru || !*ru) return ru;
    if (g_current == Id::Russian) return ru;
    if (!g_table) g_table = new Table(Build(g_current));
    auto it = g_table->find(ru);
    return (it != g_table->end()) ? it->second : ru;
}

const wchar_t* Empty() { return L""; }

} // namespace lang