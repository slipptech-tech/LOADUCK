
<h1 align="center">LOADUCK</h1>

<p align="center">
  <strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/OPEN%20SOURCE-18181B?style=for-the-badge&logo=github&logoColor=white" alt="Open Source">
  <img src="https://img.shields.io/badge/FREE-16A34A?style=for-the-badge&logo=freebsd&logoColor=white" alt="Free">
  <img src="https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++">
</p>

<p align="center">
  <em>
</p>


A native Windows application (C++ / Win32) for downloading videos from YouTube, TikTok, and 20+ other services.

Features a dark UI, video previews, quality selection, download queue, and history.

![Interface](assets/screenshott.png)

## Features

- **Link Analysis** — thumbnail, title, creator, view count, duration, available quality options, and estimated file sizes.
- **Download Queue** — up to 8 concurrent downloads, pause/resume, cancel, retry, real-time progress, and download speed.
- **History** — downloaded files, timestamps, file sizes, and quick actions to reveal or delete files.
- **Settings** — download directory, filename template, maximum concurrent downloads, exit behavior, system tray toggle, and notifications.
- **Components** — built-in installation and updating of `yt-dlp` and `ffmpeg` directly from the application.
- **Convenience Features** — minimize to system tray, drag and drop links into the window, keyboard shortcuts, and clipboard integration.

**Supported services:** YouTube, TikTok, Instagram, X (Twitter), Facebook, Twitch, SoundCloud, Vimeo, Bilibili, and many others (inherited from yt-dlp).

## Requirements

| | |
|---|---|
| OS | Windows 10/11 version 1809 or later (x64) |
| Build | Visual Studio 2026 (MSVC 14.5x), C++20, Windows SDK 10.0.28000.0 |
| Components | `bin\yt-dlp.exe` (required), `bin\ffmpeg.exe` (for merging media streams and embedding metadata) |

Both components are included in the `bin` directory. If either is missing, open **Settings → Components** and click **Update**.

## Building

```bat
:: x64 / Release
msbuild LOADUCK.vcxproj -p:Configuration=Release -p:Platform=x64

:: Or use Visual Studio: open LOADUCK.sln → select Release|x64 → press F5
```

The compiled executable will be located at:

`build\x64\Release\LOADUCK.exe`

> Close any running instance of `LOADUCK.exe` before rebuilding; otherwise, the linker will be unable to overwrite the executable.

## Running

```bat
build\x64\Release\LOADUCK.exe
```

You can also open a link directly from the command line:

```bat
build\x64\Release\LOADUCK.exe -Url "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
```

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+V` | Paste a link from the clipboard |
| `Ctrl+Enter` | Download or start analyzing the link if it hasn't been analyzed yet |
| `Ctrl+1…4` | Switch between Download, Queue, History, and Settings |
| `Esc` | Close the current panel or remove focus from the input field |

## Architecture

The project uses no third-party UI frameworks — only native Win32 APIs and GDI+.

| File | Description |
|---|---|
| `src/main.cpp` | Window, message loop, input handling, and application state |
| `src/render.h/.cpp` | GDI+ rendering abstraction: primitives, fonts, text, clipping, and image caching |
| `src/ui.h/.cpp` | UI widgets: buttons, chips, toggles, input fields, progress indicators, and tooltips |
| `src/pages.h/.cpp` | Four interface pages, header, sidebar, and status bar |
| `src/icons.h/.cpp` | Vector icons and application logo |
| `src/theme.h/.cpp` | Color palette, accent themes, and DPI scaling |
| `src/dlp.h/.cpp` | yt-dlp process management: analysis, download queue, progress tracking, HTTP downloads of thumbnails and components |
| `src/store.h/.cpp` | Settings and download history stored in `%LOCALAPPDATA%\LOADUCK` |
| `src/util.h/.cpp` | URL handling, formatting, dialogs, and clipboard integration |
| `src/json.h` | Lightweight JSON parser with no external dependencies |
| `src/toast.h/.cpp` | Toast notifications |

Rendering takes place in a 32-bit ARGB offscreen buffer, which is then displayed in a single `DrawImage` call. This enables smooth rendering at up to 60 FPS and supports translucent shadows and gradients without visible aliasing.

## Data Storage

Settings, download history, and logs are stored in `%LOCALAPPDATA%\LOADUCK`. You can access this directory using the **Open Folder** button in Settings.

## Disclaimer

LOADUCK acts as a frontend for `yt-dlp`. Downloading copyrighted material may violate applicable laws. Only download content you have the right to access and save.

---

<p align="center">
  <a href="https://ko-fi.com/SlippTech">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg"
         alt="Support me on Ko-fi"
         height="42">
  </a>
  &nbsp;&nbsp;
  <a href="https://www.buymeacoffee.com/SLIPPTECH">
    <img src="https://img.shields.io/badge/☕_Buy_Me_a_Coffee-Support_SlippTech-FFDD00?style=for-the-badge&logoColor=black&labelColor=FFDD00"
         alt="Buy Me a Coffee"
         height="42">
  </a>
</p>
