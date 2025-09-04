#!/usr/bin/env python3

import json
import os
import re
import shlex
import sys
import platform
from dataclasses import dataclass
from typing import List, Optional
from urllib.request import urlopen, Request

PERCENT_RE = re.compile(r"(\d{1,3}(?:\.\d+)?)%")

from PySide6.QtCore import Qt, QProcess, QSize
from PySide6.QtGui import QAction, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QProgressBar,
    QSpinBox,
    QTextEdit,
    QVBoxLayout,
    QWidget,
    QSizePolicy,
)

@dataclass
class FormatRow:
    fid: str
    ext: str
    vcodec: str
    acodec: str
    height: Optional[int]
    fps: Optional[float]
    tbr: Optional[float]
    format_note: str

    @property
    def is_video(self) -> bool:
        return (self.vcodec or "none") != "none" and (self.height or 0) > 0

    @property
    def is_audio(self) -> bool:
        return (self.acodec or "none") != "none" and (self.height or 0) == 0

    @property
    def is_progressive(self) -> bool:
        return (self.vcodec or "none") != "none" and (self.acodec or "none") != "none" and (self.height or 0) > 0

    def video_label(self) -> str:
        parts = [self.fid]
        if self.height:
            parts.append(f"{self.height}p")
        if self.vcodec and self.vcodec != "none":
            parts.append(self.vcodec)
        if self.fps:
            parts.append(f"{int(self.fps)}fps")
        if self.tbr:
            parts.append(f"~{self.tbr:.1f} Mb/s")
        if self.ext:
            parts.append(self.ext)
        if self.format_note:
            parts.append(self.format_note)
        return " | ".join(parts)

    def audio_label(self) -> str:
        parts = [self.fid]
        if self.acodec and self.acodec != "none":
            parts.append(self.acodec)
        if self.tbr:
            kbps = self.tbr * 1000 if self.tbr < 5 else self.tbr
            parts.append(f"~{int(kbps)} kb/s")
        if self.ext:
            parts.append(self.ext)
        if self.format_note:
            parts.append(self.format_note)
        return " | ".join(parts)

class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("yt-dlp GUI")
        self.setFixedSize(QSize(1280, 559))

        central = QWidget(self)
        self.setCentralWidget(central)

        self.url_edit = QLineEdit()
        self.url_edit.setPlaceholderText("https://www.youtube.com/watch?v=… or another URL")

        self.btn_analyze = QPushButton("Analyze")
        self.btn_download = QPushButton("Download")
        self.btn_stop = QPushButton("Stop")
        self.btn_stop.setEnabled(False)

        self.out_dir_edit = QLineEdit()
        self.out_dir_edit.setPlaceholderText("Output directory")
        self.btn_browse = QPushButton("Browse…")
        self.template_edit = QLineEdit("%(title)s-%(id)s.%(ext)s")

        self.video_combo = QComboBox()
        self.audio_combo = QComboBox()
        self.audio_only_chk = QCheckBox("Audio only")
        self.container_combo = QComboBox()
        self.container_combo.addItems(["auto", "mp4", "mkv", "webm"])

        for cb in (self.video_combo, self.audio_combo, self.container_combo):
            cb.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

        self.aria_chk = QCheckBox("aria2c")
        self.aria_conn = QSpinBox()
        self.aria_conn.setRange(1, 32)
        self.aria_conn.setValue(16)

        self.cookies_status = QLabel("Cookies: auto (detecting…)")

        self.embed_thumb_chk = QCheckBox("Embed thumbnail")
        self.thumb_label = QLabel("No thumbnail")
        self.thumb_label.setFixedSize(420, 236)
        self.thumb_label.setAlignment(Qt.AlignCenter)
        self.thumb_label.setStyleSheet("border:0px solid #666; background:#111;")

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.log = QTextEdit()
        self.log.setReadOnly(True)

        top = QHBoxLayout()
        top.addWidget(QLabel("URL:"))
        top.addWidget(self.url_edit, 1)
        top.addWidget(self.btn_analyze)

        out = QGridLayout()
        out.addWidget(QLabel("Directory:"), 0, 0)
        out.addWidget(self.out_dir_edit, 0, 1)
        out.addWidget(self.btn_browse, 0, 2)
        out.addWidget(QLabel("Filename template:"), 1, 0)
        out.addWidget(self.template_edit, 1, 1, 1, 2)
        out.setColumnStretch(1, 1)

        sel = QGridLayout()
        sel.addWidget(QLabel("Video:"), 0, 0)
        sel.addWidget(self.video_combo, 0, 1)
        sel.addWidget(QLabel("Audio:"), 1, 0)
        sel.addWidget(self.audio_combo, 1, 1)
        sel.addWidget(self.audio_only_chk, 0, 2)
        sel.addWidget(QLabel("Container:"), 1, 2)
        sel.addWidget(self.container_combo, 1, 3)
        sel.setColumnStretch(1, 1)
        sel.setColumnStretch(3, 1)

        aria = QHBoxLayout()
        aria.addWidget(self.aria_chk)
        aria.addWidget(QLabel("connections:"))
        aria.addWidget(self.aria_conn)
        aria.addWidget(self.embed_thumb_chk)
        aria.addStretch(1)
        aria.addWidget(self.cookies_status)

        btns = QHBoxLayout()
        btns.addWidget(self.btn_download)
        btns.addWidget(self.btn_stop)
        btns.addStretch(1)

        v = QVBoxLayout(central)
        v.addLayout(top)
        v.addLayout(out)
        v.addLayout(sel)
        v.addLayout(aria)
        v.addWidget(self.progress)

        mid = QHBoxLayout()
        mid.addWidget(self.log, 1)

        thumb_box = QVBoxLayout()
        thumb_box.addWidget(self.thumb_label, 0, Qt.AlignTop)
        thumb_box.addStretch(1)

        mid.addLayout(thumb_box)
        v.addLayout(mid)
        v.addLayout(btns)

        self.formats: List[FormatRow] = []
        self.video_map: List[FormatRow] = []
        self.audio_map: List[FormatRow] = []
        self.rows_by_id = {}
        self.proc: Optional[QProcess] = None
        self.thumbnail_url: str = ""

        self.detected_browsers: List[str] = self._detect_installed_browsers()
        self.active_browser: Optional[str] = None
        self._update_cookies_status()

        self.btn_browse.clicked.connect(self.pick_dir)
        self.btn_analyze.clicked.connect(self.analyze_url)
        self.btn_download.clicked.connect(self.start_download)
        self.btn_stop.clicked.connect(self.stop_download)
        self.audio_only_chk.stateChanged.connect(self.toggle_audio_only)
        self.video_combo.currentIndexChanged.connect(self.on_video_changed)

        m = self.menuBar().addMenu("About")
        a = QAction("About", self)
        a.triggered.connect(lambda: QMessageBox.information(self, "⌬⌁❋⌁⌬", "╔═━── 𝐅𝐚𝐥𝐜𝐢𝐨𝐧𝐗 ──━═╗"))
        m.addAction(a)

    def append_log(self, text: str) -> None:
        self.log.append(text.rstrip())

    def pick_dir(self) -> None:
        d = QFileDialog.getExistingDirectory(self, "Select output directory", self.out_dir_edit.text() or os.getcwd())
        if d:
            self.out_dir_edit.setText(d)

    def toggle_audio_only(self) -> None:
        only = self.audio_only_chk.isChecked()
        self.video_combo.setEnabled(not only)
        self.container_combo.setEnabled(not only)
        self.audio_combo.setEnabled(True)
        if not only:
            self.on_video_changed()

    def on_video_changed(self) -> None:
        if self.audio_only_chk.isChecked():
            return
        idx = self.video_combo.currentIndex()
        progressive = False
        if 0 <= idx < len(self.video_map):
            row = self.video_map[idx]
            progressive = row.is_progressive
        self.audio_combo.setEnabled(not progressive)
        self.audio_combo.setToolTip("This format already contains audio" if progressive else "")

    def update_thumbnail(self) -> None:
        pix = QPixmap()
        if self.thumbnail_url:
            try:
                req = Request(self.thumbnail_url, headers={"User-Agent": "Mozilla/5.0"})
                with urlopen(req, timeout=10) as r:
                    data = r.read()
                pix.loadFromData(data)
            except Exception:
                pass
        if pix.isNull():
            self.thumb_label.setText("No thumbnail")
            self.thumb_label.setPixmap(QPixmap())
        else:
            scaled = pix.scaled(self.thumb_label.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation)
            self.thumb_label.setPixmap(scaled)
            self.thumb_label.setText("")

    def _update_cookies_status(self) -> None:
        if self.active_browser:
            self.cookies_status.setText(f"Cookies: {self.active_browser}")
        elif self.detected_browsers:
            self.cookies_status.setText("Cookies: auto (" + ", ".join(self.detected_browsers) + ")")
        else:
            self.cookies_status.setText("Cookies: none detected")

    def _cookies_args(self) -> List[str]:
        if self.active_browser:
            return ["--cookies-from-browser", self.active_browser]
        return []

    def _detect_installed_browsers(self) -> List[str]:
        sysname = platform.system().lower()

        def exists_any(paths: List[str]) -> bool:
            for p in paths:
                p = os.path.expandvars(os.path.expanduser(p))
                if os.path.exists(p):
                    return True
            return False

        home = os.path.expanduser("~")
        local = os.environ.get("LOCALAPPDATA", "")
        appdata = os.environ.get("APPDATA", "")

        candidates: List[tuple[str, List[str]]] = []

        if "windows" in sysname:
            candidates = [
                ("chrome", [
                    os.path.join(local, r"Google\\Chrome\\User Data\\Default\\Network\\Cookies"),
                    os.path.join(local, r"Google\\Chrome\\User Data\\Default\\Cookies"),
                ]),
                ("edge", [
                    os.path.join(local, r"Microsoft\\Edge\\User Data\\Default\\Network\\Cookies"),
                    os.path.join(local, r"Microsoft\\Edge\\User Data\\Default\\Cookies"),
                ]),
                ("brave", [
                    os.path.join(local, r"BraveSoftware\\Brave-Browser\\User Data\\Default\\Network\\Cookies"),
                    os.path.join(local, r"BraveSoftware\\Brave-Browser\\User Data\\Default\\Cookies"),
                ]),
                ("chromium", [
                    os.path.join(local, r"Chromium\\User Data\\Default\\Network\\Cookies"),
                    os.path.join(local, r"Chromium\\User Data\\Default\\Cookies"),
                ]),
                ("opera", [
                    os.path.join(appdata, r"Opera Software\\Opera Stable\\Network\\Cookies"),
                    os.path.join(appdata, r"Opera Software\\Opera Stable\\Cookies"),
                ]),
                ("firefox", [
                    os.path.join(appdata, r"Mozilla\\Firefox\\Profiles"),
                ]),
            ]
        elif "darwin" in sysname:
            app_sup = os.path.join(home, "Library", "Application Support")
            candidates = [
                ("safari", [os.path.join(home, "Library", "Cookies", "Cookies.binarycookies")]),
                ("chrome", [
                    os.path.join(app_sup, "Google", "Chrome", "Default", "Cookies"),
                ]),
                ("brave", [
                    os.path.join(app_sup, "BraveSoftware", "Brave-Browser", "Default", "Cookies"),
                ]),
                ("edge", [
                    os.path.join(app_sup, "Microsoft Edge", "Default", "Cookies"),
                ]),
                ("firefox", [
                    os.path.join(app_sup, "Firefox", "Profiles"),
                ]),
                ("chromium", [
                    os.path.join(app_sup, "Chromium", "Default", "Cookies"),
                ]),
                ("opera", [
                    os.path.join(app_sup, "com.operasoftware.Opera", "Cookies"),
                ]),
            ]
        else:
            cfg = os.path.join(home, ".config")
            candidates = [
                ("chrome", [os.path.join(cfg, "google-chrome", "Default", "Cookies")]),
                ("chromium", [os.path.join(cfg, "chromium", "Default", "Cookies")]),
                ("brave", [os.path.join(cfg, "BraveSoftware", "Brave-Browser", "Default", "Cookies")]),
                ("edge", [os.path.join(cfg, "microsoft-edge", "Default", "Cookies")]),
                ("firefox", [os.path.join(home, ".mozilla", "firefox")]),
                ("opera", [os.path.join(cfg, "opera", "Cookies"), os.path.join(cfg, "opera-stable", "Cookies")]),
                ("vivaldi", [os.path.join(cfg, "vivaldi", "Default", "Cookies")]),
            ]

        order = [name for name, paths in candidates if exists_any(paths)]
        return order

    def analyze_url(self) -> None:
        url = self.url_edit.text().strip()
        if not url:
            QMessageBox.warning(self, "Error", "Enter a URL.")
            return

        self.append_log(f"Analyzing: {url}")

        base_args = ["-J", "--ignore-config", "--no-warnings"]

        tried: List[Optional[str]] = [None] + self.detected_browsers
        last_raw = ""
        data = None

        for b in tried:
            p = QProcess(self)
            args = list(base_args)
            if b:
                args += ["--cookies-from-browser", b]
            p.setProgram("yt-dlp")
            p.setArguments(args + [url])
            p.setProcessChannelMode(QProcess.MergedChannels)
            p.start()
            if not p.waitForFinished(60000):
                p.kill()
                continue
            raw = bytes(p.readAllStandardOutput()).decode("utf-8", errors="replace").strip()
            last_raw = raw

            ok = (p.exitStatus() == QProcess.NormalExit and p.exitCode() == 0)

            if raw:
                first = raw.find("{")
                last = raw.rfind("}")
                if first != -1 and last != -1 and last > first:
                    candidate = raw[first:last + 1]
                    try:
                        data = json.loads(candidate)
                        ok = True
                    except Exception:
                        data = None

            if ok and data:
                self.active_browser = b
                self._update_cookies_status()
                break
        else:
            self.append_log(last_raw or "No output from yt-dlp.")
            if "cookies" in (last_raw or "").lower():
                QMessageBox.warning(
                    self,
                    "Authentication required",
                    "The site requires sign-in. No usable browser cookies found."
                )
            else:
                QMessageBox.critical(self, "Error", "Failed to fetch metadata. Check the URL.")
            return

        self._populate_formats_from_info(data)

        self.thumbnail_url = data.get("thumbnail") or ""
        self.update_thumbnail()

        title = data.get("title") or ""
        self.append_log(f"{title}")

    def _populate_formats_from_info(self, data: dict) -> None:
        self.formats = []
        self.video_map = []
        self.audio_map = []
        self.video_combo.clear()
        self.audio_combo.clear()

        for f in data.get("formats", []):
            row = FormatRow(
                fid=str(f.get("format_id")),
                ext=f.get("ext") or "",
                vcodec=f.get("vcodec") or "none",
                acodec=f.get("acodec") or "none",
                height=f.get("height"),
                fps=f.get("fps"),
                tbr=f.get("tbr"),
                format_note=f.get("format_note") or "",
            )
            if row.is_video:
                self.video_map.append(row)
            elif row.is_audio:
                self.audio_map.append(row)
            self.formats.append(row)

        self.rows_by_id = {r.fid: r for r in self.formats}

        self.video_map.sort(key=lambda r: ((r.height or 0), (r.tbr or 0)), reverse=True)
        self.audio_map.sort(key=lambda r: (r.tbr or 0), reverse=True)

        for r in self.video_map:
            label = r.video_label()
            if r.is_progressive:
                label += " [with audio]"
            self.video_combo.addItem(label, r.fid)
        for r in self.audio_map:
            self.audio_combo.addItem(r.audio_label(), r.fid)

        if self.video_map:
            self.video_combo.setCurrentIndex(0)
        if self.audio_map:
            self.audio_combo.setCurrentIndex(0)

        self.on_video_changed()

    def start_download(self) -> None:
        if self.proc:
            QMessageBox.warning(self, "In progress", "A download is already in progress.")
            return

        url = self.url_edit.text().strip()
        if not url:
            QMessageBox.warning(self, "Error", "Enter a URL.")
            return

        out_dir = self.out_dir_edit.text().strip() or os.getcwd()
        if not os.path.isdir(out_dir):
            QMessageBox.warning(self, "Error", "Invalid output directory.")
            return

        template = self.template_edit.text().strip() or "%(title)s-%(id)s.%(ext)s"
        out_template = os.path.join(out_dir, template)

        is_audio_only = self.audio_only_chk.isChecked()
        container = self.container_combo.currentText()
        use_aria = self.aria_chk.isChecked()
        conn = self.aria_conn.value()

        args: List[str] = [
            "--newline",
            "--ignore-config",
            "--no-warnings",
            "-o",
            out_template,
        ]

        args += self._cookies_args()

        if is_audio_only:
            if self.audio_combo.count() == 0:
                QMessageBox.warning(self, "Missing", "No audio tracks.")
                return
            a_id = self.audio_combo.currentData()
            args += ["-f", a_id]
        else:
            if self.video_combo.count() == 0:
                QMessageBox.warning(self, "Missing", "Select a video.")
                return
            v_id = self.video_combo.currentData()
            row = self.rows_by_id.get(str(v_id)) or self.rows_by_id.get(v_id)
            if row and row.is_progressive:
                args += ["-f", str(v_id)]
            else:
                if self.audio_combo.count() == 0:
                    QMessageBox.warning(self, "Missing", "No audio tracks for the selected item.")
                    return
                a_id = self.audio_combo.currentData()
                args += ["-f", f"{v_id}+{a_id}"]
            if container != "auto":
                args += ["--remux-video", container]

        if self.embed_thumb_chk.isChecked():
            args += ["--embed-thumbnail"]

        if use_aria:
            args += [
                "--external-downloader",
                "aria2c",
                "--external-downloader-args",
                f"-x{conn} -s{conn} -k1M --summary-interval=1 --console-log-level=warn --show-console-readout=false --enable-color=false",
            ]

        args.append(url)

        self.append_log("yt-dlp " + " ".join(shlex.quote(a) for a in args))

        self.proc = QProcess(self)
        self.proc.setProgram("yt-dlp")
        self.proc.setArguments(args)
        self.proc.setProcessChannelMode(QProcess.MergedChannels)
        self.proc.readyReadStandardOutput.connect(self.on_proc_output)
        self.proc.finished.connect(self.on_proc_finished)
        self.proc.start()

        self.btn_download.setEnabled(False)
        self.btn_stop.setEnabled(True)
        self.progress.setValue(0)

    def stop_download(self) -> None:
        if self.proc:
            self.append_log("Stopping…")
            self.proc.kill()

    def on_proc_output(self) -> None:
        if not self.proc:
            return
        chunk = bytes(self.proc.readAllStandardOutput()).decode("utf-8", errors="replace")
        chunk = chunk.replace(chr(13), chr(10))
        for line in chunk.splitlines():
            if not line.strip():
                continue
            self.append_log(line)
            m = PERCENT_RE.search(line)
            if m:
                try:
                    pct = float(m.group(1))
                    if 0 <= pct <= 100:
                        self.progress.setValue(int(pct))
                except ValueError:
                    pass

    def on_proc_finished(self) -> None:
        code = self.proc.exitCode() if self.proc else -1
        self.append_log(f"Finished. Code: {code}")
        self.proc = None
        self.btn_download.setEnabled(True)
        self.btn_stop.setEnabled(False)
        self.progress.setValue(100 if code == 0 else 0)

def main() -> None:
    app = QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
