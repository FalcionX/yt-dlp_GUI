# 🎛  yt-dlp GUI

**Lightweight Qt6 frontend for `yt-dlp`** with **ffmpeg** and optional **aria2c**.
Paste a URL → see formats → download with a progress bar.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

## ✨ Features

```
┌─────────────────────────────────────────────────────────┐
│ 🔍 URL analysis + rich format listing (audio/video)     │
│ 🍪 Auto cookies from browser (`--cookies-from-browser`) │
│ 🖼️ Thumbnail preview                                    │
│ 🎥⬇️ Video+audio or 🔊 audio-only                       │
│ 🧩 Remux → mp4 / mkv / webm (no re-encode)              │
│ 🚀 Optional aria2c with parallel connections            │
│ 📊 Live progress bar + unified log                      │
│ 🗂️ Output folder picker & filename template             │
└─────────────────────────────────────────────────────────┘
```

## 🧰 Requirements

```
• Python 3.8+
• Packages: PySide6, yt-dlp
• Tools: ffmpeg in PATH (required) • aria2c in PATH (optional)
• Browser profile if the site requires sign-in
```

## ⚙️ Installation (venv)

```
python -m venv .venv
# Linux/macOS
source .venv/bin/activate
# Windows (PowerShell)
# .venv\Scripts\Activate.ps1

python -m pip install --upgrade pip
pip install PySide6 yt-dlp
```

*Install `ffmpeg` (and optionally `aria2c`) via your OS package manager and ensure they’re in `PATH`.*

## ▶️ Run

```
python yt_dlp_gui.py
```

## 🪄 Usage — 3 steps

```
① Paste a URL → click “Analyze”
② Pick formats (or “Audio only”) → choose output folder/template
③ (Optional) enable aria2c / “Embed thumbnail” → “Download”
```

### 🏷 Filename template (default)

```
%(title)s-%(id)s.%(ext)s
```

## 🆘 Quick fixes

```
❗ yt-dlp: command not found   → install yt-dlp; ensure script is in PATH
❗ ffmpeg not found / silent   → install ffmpeg; add to PATH
🔐 Site needs sign-in          → log into a supported browser; uses --cookies-from-browser
🚫 No formats listed           → check URL / cookies / login
🐢 Slow downloads              → enable aria2c; increase connections (site-permitting)
```

## 🧱 How it works (short)

```
• yt-dlp via QProcess
• Analysis: -J --ignore-config --no-warnings (+ cookies when available)
• ffmpeg handles mux/remux controlled by yt-dlp
• For progressive formats, audio selector is disabled
```

## 🔐 Security

```
• Cookies aren’t copied; yt-dlp reads them locally via --cookies-from-browser
• Thumbnails come from metadata and are displayed locally
```

![## 🖼 Screenshot](docs/screenshot.png)
