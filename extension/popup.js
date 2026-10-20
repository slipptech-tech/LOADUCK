// popup.js — интерфейс расширения. Язык берётся из LOADUCK (ping → lang),
// чтобы попап говорил на том же языке, что и программа.

const $ = (id) => document.getElementById(id);
const HOST = "com.loaduck.host";

const DICT = {
  ru: {
    sub: "Загрузка видео",
    checking: "Проверяем страницу…",
    noTab: "Нет активной вкладки",
    notWeb: "Это не веб-страница — откройте видео на сайте",
    download: "Скачать видео",
    sending: "Отправляем…",
    transferring: "Передаём ссылку в LOADUCK…",
    done: "Готово — LOADUCK начал загрузку",
    openApp: "Открыть приложение",
    opening: "Открываем…",
    opened: "LOADUCK открыт",
    openFail: "Не удалось открыть LOADUCK",
    foot: "Качество, кодек и папка — в настройках LOADUCK",
    errDisabled: "Интеграция выключена: включите «Расширение для браузера» в настройках LOADUCK",
    errUnsupported: "Этот сайт не поддерживается",
    errBusy: "LOADUCK занят другой задачей, попробуйте ещё раз",
    errHost: "LOADUCK не найден. Запустите программу и включите интеграцию",
    errDenied: "Браузер не разрешил связь с LOADUCK",
    errGeneric: "Не удалось передать ссылку в LOADUCK",
  },
  uk: {
    sub: "Завантаження відео",
    checking: "Перевіряємо сторінку…",
    noTab: "Немає активної вкладки",
    notWeb: "Це не веб-сторінка — відкрийте відео на сайті",
    download: "Завантажити відео",
    sending: "Надсилаємо…",
    transferring: "Передаємо посилання в LOADUCK…",
    done: "Готово — LOADUCK почав завантаження",
    openApp: "Відкрити програму",
    opening: "Відкриваємо…",
    opened: "LOADUCK відкрито",
    openFail: "Не вдалося відкрити LOADUCK",
    foot: "Якість, кодек і тека — у налаштуваннях LOADUCK",
    errDisabled: "Інтеграцію вимкнено: увімкніть «Розширення для браузера» в налаштуваннях LOADUCK",
    errUnsupported: "Цей сайт не підтримується",
    errBusy: "LOADUCK зайнятий іншим завданням, спробуйте ще раз",
    errHost: "LOADUCK не знайдено. Запустіть програму й увімкніть інтеграцію",
    errDenied: "Браузер не дозволив зв'язок із LOADUCK",
    errGeneric: "Не вдалося передати посилання в LOADUCK",
  },
  en: {
    sub: "Video downloader",
    checking: "Checking the page…",
    noTab: "No active tab",
    notWeb: "This is not a web page — open a video on a website",
    download: "Download video",
    sending: "Sending…",
    transferring: "Sending the link to LOADUCK…",
    done: "Done — LOADUCK started downloading",
    openApp: "Open the app",
    opening: "Opening…",
    opened: "LOADUCK is open",
    openFail: "Could not open LOADUCK",
    foot: "Quality, codec and folder are set in LOADUCK",
    errDisabled: "Integration is off: enable “Browser extension” in LOADUCK settings",
    errUnsupported: "This site is not supported",
    errBusy: "LOADUCK is busy with another task, try again",
    errHost: "LOADUCK was not found. Start the app and enable the integration",
    errDenied: "The browser did not allow the connection to LOADUCK",
    errGeneric: "Could not send the link to LOADUCK",
  },
};

let L = DICT.ru;
let tabUrl = "";
let busy = false;
let openBusy = false;
let statusKey = "";
let statusKind = "";
let hostState = "checking"; // checking | host | noTab
let hostText = "";

function setStatus(key, kind = "") {
  statusKey = key || "";
  statusKind = kind;
  const el = $("status");
  el.textContent = statusKey ? (L[statusKey] || "") : "";
  el.className = "status" + (kind ? " " + kind : "");
}

function renderHost() {
  $("host").textContent = hostState === "host" ? hostText : (L[hostState] || "");
}

function applyLang(code) {
  if (!DICT[code]) code = "ru";
  L = DICT[code];
  document.documentElement.lang = code;
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const k = el.getAttribute("data-i18n");
    if (L[k]) el.textContent = L[k];
  });
  if (busy) $("goLabel").textContent = L.sending;
  if (openBusy) $("openLabel").textContent = L.opening;
  renderHost();
  if (statusKey) setStatus(statusKey, statusKind);
}

function detectLang() {
  const n = (navigator.language || "").toLowerCase();
  if (n.indexOf("uk") === 0) return "uk";
  if (n.indexOf("en") === 0) return "en";
  return "ru";
}

function pingHost() {
  return new Promise((resolve) => {
    let done = false;
    const finish = (v) => {
      if (!done) { done = true; resolve(v); }
    };
    setTimeout(() => finish(null), 800);
    try {
      chrome.runtime.sendNativeMessage(HOST, { action: "ping" }, (resp) => {
        if (chrome.runtime.lastError) { finish(null); return; }
        finish(resp || null);
      });
    } catch {
      finish(null);
    }
  });
}

function shortPath(u) {
  if (!u) return "";
  try {
    const url = new URL(u);
    let p = url.pathname + url.search;
    if (p === "/") p = "";
    return p.length > 64 ? p.slice(0, 64) + "…" : p;
  } catch {
    return u;
  }
}

async function initTab() {
  let tab = null;
  try {
    [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  } catch {
    /* ignore */
  }
  tabUrl = (tab && tab.url) || "";

  if (!tabUrl) {
    hostState = "noTab";
    renderHost();
    setStatus("notWeb", "warn");
    return;
  }

  let host = tabUrl;
  try {
    host = new URL(tabUrl).hostname.replace(/^www\./, "");
  } catch {
    /* ignore */
  }
  hostState = "host";
  hostText = host;
  renderHost();
  $("url").textContent = shortPath(tabUrl);

  if (!/^https?:/i.test(tabUrl)) {
    setStatus("notWeb", "warn");
    return;
  }
  $("go").disabled = false;
  setStatus("");
}

function setBusy(v) {
  busy = v;
  $("go").classList.toggle("busy", v);
  $("go").disabled = v || !/^https?:/i.test(tabUrl);
  $("goLabel").textContent = v ? L.sending : L.download;
}

function setOpenBusy(v) {
  openBusy = v;
  $("open").disabled = v;
  $("openLabel").textContent = v ? L.opening : L.openApp;
}

$("go").addEventListener("click", () => {
  if (busy || !tabUrl) return;
  setBusy(true);
  setStatus("transferring");

  chrome.runtime.sendMessage({ type: "download", url: tabUrl }, (resp) => {
    setBusy(false);
    if (chrome.runtime.lastError) { setStatus("errHost", "err"); return; }
    if (resp && resp.ok) { setStatus("done", "ok"); return; }

    const code = resp && resp.error;
    if (code === "disabled") setStatus("errDisabled", "warn");
    else if (code === "unsupported") setStatus("errUnsupported", "warn");
    else if (code === "busy") setStatus("errBusy", "warn");
    else if (code === "host") setStatus("errHost", "err");
    else if (code === "denied") setStatus("errDenied", "err");
    else setStatus("errGeneric", "err");
  });
});

$("open").addEventListener("click", () => {
  if (openBusy) return;
  setOpenBusy(true);
  setStatus("opening");

  chrome.runtime.sendMessage({ type: "open" }, (resp) => {
    setOpenBusy(false);
    if (chrome.runtime.lastError) { setStatus("openFail", "err"); return; }
    if (resp && resp.ok) setStatus("opened", "ok");
    else setStatus("openFail", "err");
  });
});

// Пока язык не пришёл от LOADUCK, показываем хотя бы язык браузера.
applyLang(detectLang());
initTab();
setTimeout(() => document.body.classList.add("ready"), 1000);
pingHost().then((resp) => {
  if (resp && resp.lang) applyLang(resp.lang);
  document.body.classList.add("ready");
});
