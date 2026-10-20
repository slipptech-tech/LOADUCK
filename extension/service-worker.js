// service-worker.js — единственное место, где расширение разговаривает с
// LOADUCK.exe. Native Messaging работает только по HTTPS-страницам: ссылки
// file://, chrome:// и javascript: сюда не попадают.

const HOST = "com.loaduck.host";

function validUrl(u) {
  if (typeof u !== "string" || u.length < 10 || u.length > 2048) return false;
  if (!/^https?:\/\//i.test(u)) return false;
  // управляющие символы, пробелы и кавычки в аргументах командной строки недопустимы
  return !/[\u0000-\u0020"<>]/.test(u);
}

function sendToHost(payload) {
  return new Promise((resolve) => {
    chrome.runtime.sendNativeMessage(HOST, payload, (resp) => {
      if (chrome.runtime.lastError) {
        resolve({ ok: false, error: "host", detail: chrome.runtime.lastError.message });
        return;
      }
      if (!resp || typeof resp !== "object") {
        resolve({ ok: false, error: "host" });
        return;
      }
      resolve(resp);
    });
  });
}

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg) return false;

  // Открыть (или показать) программу — кнопка под «Скачать видео».
  if (msg.type === "open") {
    sendToHost({ action: "open" }).then(sendResponse);
    return true;
  }

  if (msg.type !== "download") return false;

  if (!validUrl(msg.url)) {
    sendResponse({ ok: false, error: "unsupported" });
    return false;
  }

  sendToHost({ action: "download", url: msg.url }).then(sendResponse);
  return true;   // ответ будет асинхронным
});
