#include "MainWindow.h"

#include <cmath>

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QTextEdit>
#include <QTextCursor>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QtCore/Qt>
#include <QtCore/qoverload.h>
#include <algorithm>
#include <utility>

namespace {
constexpr int kMaxLogEntries = 500;
const QSet<QString> kAllowedThumbSchemes = {QStringLiteral("http"), QStringLiteral("https")};
}

bool FormatRow::isVideo() const {
    return (vcodec.isEmpty() ? QStringLiteral("none") : vcodec) != QStringLiteral("none") && height.value_or(0) > 0;
}

bool FormatRow::isAudio() const {
    return (acodec.isEmpty() ? QStringLiteral("none") : acodec) != QStringLiteral("none") && height.value_or(0) == 0;
}

bool FormatRow::isProgressive() const {
    const auto v = vcodec.isEmpty() ? QStringLiteral("none") : vcodec;
    const auto a = acodec.isEmpty() ? QStringLiteral("none") : acodec;
    return v != QStringLiteral("none") && a != QStringLiteral("none") && height.value_or(0) > 0;
}

QString FormatRow::videoLabel() const {
    QStringList parts;
    parts << fid;
    if (height) {
        parts << QString::number(height.value()) % QLatin1String("p");
    }
    if (!vcodec.isEmpty() && vcodec != QStringLiteral("none")) {
        parts << vcodec;
    }
    if (fps) {
        parts << QString::number(static_cast<int>(fps.value())) % QLatin1String("fps");
    }
    if (tbr) {
        parts << QLatin1String("~") % QString::number(tbr.value(), 'f', 1) % QLatin1String(" Mb/s");
    }
    if (!ext.isEmpty()) {
        parts << ext;
    }
    if (!formatNote.isEmpty()) {
        parts << formatNote;
    }
    return parts.join(QLatin1String(" | "));
}

QString FormatRow::audioLabel() const {
    QStringList parts;
    parts << fid;
    if (!acodec.isEmpty() && acodec != QStringLiteral("none")) {
        parts << acodec;
    }
    if (tbr) {
        double rate = tbr.value();
        if (rate < 5.0) {
            rate *= 1000.0;
        }
        parts << QLatin1String("~") % QString::number(static_cast<int>(rate)) % QLatin1String(" kb/s");
    }
    if (!ext.isEmpty()) {
        parts << ext;
    }
    if (!formatNote.isEmpty()) {
        parts << formatNote;
    }
    return parts.join(QLatin1String(" | "));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      urlEdit(nullptr),
      btnAnalyze(nullptr),
      btnDownload(nullptr),
      btnStop(nullptr),
      outDirEdit(nullptr),
      btnBrowse(nullptr),
      templateEdit(nullptr),
      videoCombo(nullptr),
      audioCombo(nullptr),
      containerCombo(nullptr),
      audioOnlyCheck(nullptr),
      ariaCheck(nullptr),
      ariaConn(nullptr),
      embedThumbCheck(nullptr),
      thumbLabel(nullptr),
      cookiesCombo(nullptr),
      progress(nullptr),
      logView(nullptr),
      thumbManager(new QNetworkAccessManager(this)),
      thumbReply(nullptr),
      settings(QStringLiteral("falcionx"), QStringLiteral("yt-dlp-gui")),
      proc(nullptr),
      metaProc(nullptr),
      metaTimer(this),
      thumbMaxBytes(5 * 1024 * 1024),
      percentRe(QStringLiteral("(\\d{1,3}(?:\\.\\d+)?)%")),
      ariaProgressRe(QStringLiteral("\\[#(?<id>[^\\s]+)\\s+(?<done>[0-9.]+[A-Za-z]+)/(?:\\s*)?(?<total>[0-9.]+[A-Za-z]+)\\((?<pct>[0-9.]+)%\\)\\s+CN:(?<conn>\\d+)\\s+DL:(?<speed>[0-9.]+[A-Za-z/]+)\\s+ETA:(?<eta>[^\\]]+)\\]")) {
    setupUi();

    ariaAvailable = !QStandardPaths::findExecutable(QStringLiteral("aria2c")).isEmpty();
    if (!ariaAvailable) {
        ariaCheck->setChecked(false);
        ariaCheck->setEnabled(false);
        ariaCheck->setToolTip(QStringLiteral("aria2c not found in PATH"));
    } else {
        ariaCheck->setToolTip(QStringLiteral("Use aria2c external downloader"));
    }

    if (QStandardPaths::findExecutable(QStringLiteral("yt-dlp")).isEmpty()) {
        appendLog(QStringLiteral("Warning: yt-dlp not found in PATH. Downloads will fail."));
    }
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        appendLog(QStringLiteral("Warning: ffmpeg not found in PATH. Remuxing may fail."));
    }

    const QString defaultDir = defaultOutputDir();
    if (!defaultDir.isEmpty()) {
        outDirEdit->setText(defaultDir);
    }

    metaTimer.setSingleShot(true);
    connect(&metaTimer, &QTimer::timeout, this, &MainWindow::onMetaTimeout);

    const QString storedOverride = settings.value(QStringLiteral("cookies/browser")).toString();
    if (!storedOverride.isEmpty()) {
        cookieUserOverride = storedOverride;
    }

    detectedBrowsers = detectInstalledBrowsers();
    refreshCookieChoices();
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("yt-dlp GUI"));
    setFixedSize(QSize(1280, 559));

    auto *central = new QWidget(this);
    setCentralWidget(central);

    urlEdit = new QLineEdit();
    urlEdit->setPlaceholderText(QStringLiteral("https://www.youtube.com/watch?v=… or another URL"));

    btnAnalyze = new QPushButton(QStringLiteral("Analyze"));
    btnDownload = new QPushButton(QStringLiteral("Download"));
    btnStop = new QPushButton(QStringLiteral("Stop"));
    btnStop->setEnabled(false);

    outDirEdit = new QLineEdit();
    outDirEdit->setPlaceholderText(QStringLiteral("Output directory"));
    btnBrowse = new QPushButton(QStringLiteral("Browse…"));
    templateEdit = new QLineEdit(QStringLiteral("%(title)s-%(id)s.%(ext)s"));

    videoCombo = new QComboBox();
    audioCombo = new QComboBox();
    containerCombo = new QComboBox();
    containerCombo->addItems({QStringLiteral("auto"), QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm")});

    audioOnlyCheck = new QCheckBox(QStringLiteral("Audio only"));
    ariaCheck = new QCheckBox(QStringLiteral("aria2c"));
    ariaConn = new QSpinBox();
    ariaConn->setRange(1, 32);
    ariaConn->setValue(16);
    embedThumbCheck = new QCheckBox(QStringLiteral("Embed thumbnail"));

    for (auto combo : {videoCombo, audioCombo, containerCombo}) {
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    thumbLabel = new QLabel(QStringLiteral("No thumbnail"));
    thumbLabel->setFixedSize(QSize(420, 263));
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setStyleSheet(QStringLiteral("border:0px solid #666; background:#111;"));

    cookiesCombo = new QComboBox();
    cookiesCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    progress = new QProgressBar();
    progress->setRange(0, 100);

    logView = new QTextEdit();
    logView->setReadOnly(true);
    logView->setLineWrapMode(QTextEdit::NoWrap);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("URL:")));
    top->addWidget(urlEdit, 1);
    top->addWidget(btnAnalyze);

    auto *out = new QGridLayout();
    out->addWidget(new QLabel(QStringLiteral("Directory:")), 0, 0);
    out->addWidget(outDirEdit, 0, 1);
    out->addWidget(btnBrowse, 0, 2);
    out->addWidget(new QLabel(QStringLiteral("Filename template:")), 1, 0);
    out->addWidget(templateEdit, 1, 1, 1, 2);
    out->setColumnStretch(1, 1);

    auto *sel = new QGridLayout();
    sel->addWidget(new QLabel(QStringLiteral("Video:")), 0, 0);
    sel->addWidget(videoCombo, 0, 1);
    sel->addWidget(new QLabel(QStringLiteral("Audio:")), 1, 0);
    sel->addWidget(audioCombo, 1, 1);
    sel->addWidget(audioOnlyCheck, 0, 2);
    sel->addWidget(new QLabel(QStringLiteral("Container:")), 1, 2);
    sel->addWidget(containerCombo, 1, 3);
    sel->setColumnStretch(1, 1);
    sel->setColumnStretch(3, 1);

    auto *aria = new QHBoxLayout();
    aria->addWidget(ariaCheck);
    aria->addWidget(new QLabel(QStringLiteral("connections:")));
    aria->addWidget(ariaConn);
    aria->addWidget(embedThumbCheck);
    aria->addStretch(1);
    aria->addWidget(new QLabel(QStringLiteral("Cookies:")));
    aria->addWidget(cookiesCombo);

    auto *buttons = new QHBoxLayout();
    buttons->addWidget(btnDownload);
    buttons->addWidget(btnStop);
    buttons->addStretch(1);

    auto *layout = new QVBoxLayout(central);
    layout->addLayout(top);
    layout->addLayout(out);
    layout->addLayout(sel);
    layout->addLayout(aria);
    layout->addWidget(progress);

    auto *mid = new QHBoxLayout();
    mid->addWidget(logView, 1);

    auto *thumbBox = new QVBoxLayout();
    thumbBox->addWidget(thumbLabel, 0, Qt::AlignTop);
    thumbBox->addStretch(1);
    mid->addLayout(thumbBox);

    layout->addLayout(mid);
    layout->addLayout(buttons);

    connect(btnBrowse, &QPushButton::clicked, this, &MainWindow::pickDir);
    connect(btnAnalyze, &QPushButton::clicked, this, &MainWindow::analyzeUrl);
    connect(btnDownload, &QPushButton::clicked, this, &MainWindow::startDownload);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::stopDownload);
    connect(audioOnlyCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        toggleAudioOnly(static_cast<int>(state));
    });
    connect(videoCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onVideoChanged);
    connect(cookiesCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onCookieChoiceChanged);
}

void MainWindow::appendLog(const QString &text) {
    QString msg = text;
    while (msg.endsWith(QLatin1Char('\n')) || msg.endsWith(QLatin1Char('\r'))) {
        msg.chop(1);
    }
    if (!msg.isEmpty()) {
        logMessages.append(msg);
        if (logMessages.size() > kMaxLogEntries) {
            const int removeCount = logMessages.size() - kMaxLogEntries;
            for (int i = 0; i < removeCount; ++i) {
                logMessages.removeFirst();
            }
        }
    }
    clearDownloadLogLine();
    refreshLogDisplay();
}

void MainWindow::clearDownloadLogLine() {
    downloadProgressLine.clear();
}

void MainWindow::updateDownloadLogLine(const QString &text) {
    QString line = text;
    while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    downloadProgressLine = line;
    refreshLogDisplay();
}

void MainWindow::refreshLogDisplay() {
    QStringList parts = logMessages;
    if (!downloadProgressLine.isEmpty()) {
        parts.append(downloadProgressLine);
    }
    logView->setPlainText(parts.join(QLatin1Char('\n')));
    QTextCursor cursor = logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    logView->setTextCursor(cursor);
}

std::optional<QString> MainWindow::normalizeProgressLine(const QString &text) const {
    const QString stripped = text.trimmed();
    if (stripped.isEmpty()) {
        return std::nullopt;
    }

    bool onlyDecor = true;
    for (const QChar ch : stripped) {
        if (ch != QLatin1Char('-') && ch != QLatin1Char('=') && ch != QLatin1Char('_')) {
            onlyDecor = false;
            break;
        }
    }
    if (onlyDecor) {
        return std::nullopt;
    }

    const QString lowered = stripped.toLower();
    if (lowered.contains(QStringLiteral("[error]"))) {
        return std::nullopt;
    }
    if (lowered.startsWith(QStringLiteral("*** download progress summary"))) {
        return std::nullopt;
    }
    if (lowered.startsWith(QStringLiteral("file:")) || lowered.startsWith(QStringLiteral("destination:"))) {
        return std::nullopt;
    }
    if (lowered.startsWith(QStringLiteral("==="))) {
        return std::nullopt;
    }

    if (lowered.contains(QStringLiteral("[download]"))) {
        const int idx = lowered.indexOf(QStringLiteral("[download]"));
        QString segment = text.mid(idx);
        segment.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        return segment.trimmed();
    }

    const QRegularExpressionMatch ariaMatch = ariaProgressRe.match(text);
    if (ariaMatch.hasMatch()) {
        const QString pct = ariaMatch.captured(QStringLiteral("pct"));
        const QString done = ariaMatch.captured(QStringLiteral("done"));
        const QString total = ariaMatch.captured(QStringLiteral("total"));
        const QString speed = ariaMatch.captured(QStringLiteral("speed"));
        const QString eta = ariaMatch.captured(QStringLiteral("eta")).trimmed();
        const QString conn = ariaMatch.captured(QStringLiteral("conn"));
        QString formatted = QStringLiteral("aria2c %1% — %2/%3 @ %4 ETA %5 (CN:%6)")
                                 .arg(pct, done, total, speed, eta, conn);
        return formatted;
    }

    if (text.startsWith(QStringLiteral("[#")) && percentRe.match(text).hasMatch()) {
        const QRegularExpressionMatch pctMatch = percentRe.match(text);
        const QString pct = pctMatch.hasMatch() ? pctMatch.captured(1) : QStringLiteral("?");
        QString compact = text;
        compact.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        QString formatted = QStringLiteral("aria2c %1% — %2").arg(pct, compact.trimmed());
        return formatted;
    }

    return std::nullopt;
}

bool MainWindow::shouldSkipPlainLine(const QString &text) const {
    const QString stripped = text.trimmed();
    if (stripped.isEmpty()) {
        return true;
    }
    bool onlyDecor = true;
    for (const QChar ch : stripped) {
        if (ch != QLatin1Char('-') && ch != QLatin1Char('=') && ch != QLatin1Char('_')) {
            onlyDecor = false;
            break;
        }
    }
    if (onlyDecor) {
        return true;
    }
    if (stripped.startsWith(QStringLiteral("***")) || stripped.startsWith(QStringLiteral("==="))) {
        return true;
    }
    const QString lowered = stripped.toLower();
    if (lowered.contains(QStringLiteral("[error]"))) {
        return true;
    }
    if (lowered.startsWith(QStringLiteral("file:")) || lowered.startsWith(QStringLiteral("destination:"))) {
        return true;
    }
    if (lowered.startsWith(QStringLiteral("exception:"))) {
        return true;
    }
    const QStringList noisyPrefixes = {QStringLiteral("yt-dlp "), QStringLiteral("aria2c "), QStringLiteral("ffmpeg ")};
    for (const QString &prefix : noisyPrefixes) {
        if (lowered.startsWith(prefix)) {
            return true;
        }
    }
    if (lowered.startsWith(QStringLiteral("[youtube]")) || lowered.startsWith(QStringLiteral("[ffmpeg]"))) {
        return true;
    }
    return false;
}

QString MainWindow::defaultOutputDir() const {
    const QString home = QDir::homePath();
    const QStringList candidates = {
        QDir(home).filePath(QStringLiteral("Desktop")),
        QDir(home).filePath(QStringLiteral("Pulpit")),
        QDir(home).filePath(QString::fromUtf8("Рабочий стол"))};

    for (const QString &path : candidates) {
        QFileInfo info(path);
        if (info.exists() && info.isDir()) {
            return path;
        }
    }
    return home;
}

void MainWindow::refreshCookieChoices() {
    const QString detectedText = detectedBrowsers.isEmpty()
                                     ? QStringLiteral("none")
                                     : detectedBrowsers.join(QStringLiteral(", "));
    QString autoLabel = QStringLiteral("Auto (%1)").arg(detectedText);
    if (activeBrowser && !activeBrowser->isEmpty()) {
        autoLabel += QStringLiteral(" • last: %1").arg(activeBrowser.value());
    }

    cookiesCombo->blockSignals(true);
    cookiesCombo->clear();
    cookiesCombo->addItem(autoLabel, QVariant());
    for (const QString &browser : detectedBrowsers) {
        cookiesCombo->addItem(browser, browser);
    }

    if (cookieUserOverride && !cookieUserOverride->isEmpty()) {
        const int idx = cookiesCombo->findData(cookieUserOverride.value());
        if (idx != -1) {
            cookiesCombo->setCurrentIndex(idx);
        } else {
            cookiesCombo->setCurrentIndex(0);
        }
    } else {
        cookiesCombo->setCurrentIndex(0);
    }
    cookiesCombo->blockSignals(false);

    if (cookieUserOverride && cookiesCombo->currentIndex() == 0) {
        if (!detectedBrowsers.contains(cookieUserOverride.value())) {
            cookieUserOverride.reset();
            settings.remove(QStringLiteral("cookies/browser"));
        }
    }
}

QStringList MainWindow::cookiesArgs() const {
    const QString browser = cookieUserOverride.has_value() && !cookieUserOverride->isEmpty()
                                ? cookieUserOverride.value()
                                : (activeBrowser.has_value() ? activeBrowser.value() : QString());
    if (!browser.isEmpty()) {
        return {QStringLiteral("--cookies-from-browser"), browser};
    }
    return {};
}

QStringList MainWindow::detectInstalledBrowsers() const {
    const QString sysname = QSysInfo::productType().toLower();
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString home = QDir::homePath();
    const QString local = env.value(QStringLiteral("LOCALAPPDATA"));
    const QString appdata = env.value(QStringLiteral("APPDATA"));

    struct Candidate {
        QString name;
        QStringList paths;
    };

    auto resolvePath = [](const QString &base, const QString &suffix) -> QString {
        if (base.isEmpty()) {
            return QString();
        }
        return QDir(base).filePath(suffix);
    };

    auto existsAny = [](const QStringList &paths) -> bool {
        for (const QString &path : paths) {
            if (path.isEmpty()) {
                continue;
            }
            QFileInfo info(path);
            if (info.exists()) {
                return true;
            }
        }
        return false;
    };

    QList<Candidate> candidates;

    if (sysname.contains(QStringLiteral("windows"))) {
        candidates = {
            {QStringLiteral("chrome"),
             {resolvePath(local, QStringLiteral("Google/Chrome/User Data/Default/Network/Cookies")),
              resolvePath(local, QStringLiteral("Google/Chrome/User Data/Default/Cookies"))}},
            {QStringLiteral("edge"),
             {resolvePath(local, QStringLiteral("Microsoft/Edge/User Data/Default/Network/Cookies")),
              resolvePath(local, QStringLiteral("Microsoft/Edge/User Data/Default/Cookies"))}},
            {QStringLiteral("brave"),
             {resolvePath(local, QStringLiteral("BraveSoftware/Brave-Browser/User Data/Default/Network/Cookies")),
              resolvePath(local, QStringLiteral("BraveSoftware/Brave-Browser/User Data/Default/Cookies"))}},
            {QStringLiteral("chromium"),
             {resolvePath(local, QStringLiteral("Chromium/User Data/Default/Network/Cookies")),
              resolvePath(local, QStringLiteral("Chromium/User Data/Default/Cookies"))}},
            {QStringLiteral("opera"),
             {resolvePath(appdata, QStringLiteral("Opera Software/Opera Stable/Network/Cookies")),
              resolvePath(appdata, QStringLiteral("Opera Software/Opera Stable/Cookies"))}},
            {QStringLiteral("firefox"),
             {resolvePath(appdata, QStringLiteral("Mozilla/Firefox/Profiles"))}},
        };
    } else if (sysname.contains(QStringLiteral("osx")) || sysname.contains(QStringLiteral("macos"))) {
        const QString appSup = QDir(home).filePath(QStringLiteral("Library/Application Support"));
        candidates = {
            {QStringLiteral("safari"), {QDir(home).filePath(QStringLiteral("Library/Cookies/Cookies.binarycookies"))}},
            {QStringLiteral("chrome"), {resolvePath(appSup, QStringLiteral("Google/Chrome/Default/Cookies"))}},
            {QStringLiteral("brave"), {resolvePath(appSup, QStringLiteral("BraveSoftware/Brave-Browser/Default/Cookies"))}},
            {QStringLiteral("edge"), {resolvePath(appSup, QStringLiteral("Microsoft Edge/Default/Cookies"))}},
            {QStringLiteral("firefox"), {resolvePath(appSup, QStringLiteral("Firefox/Profiles"))}},
            {QStringLiteral("chromium"), {resolvePath(appSup, QStringLiteral("Chromium/Default/Cookies"))}},
            {QStringLiteral("opera"), {resolvePath(appSup, QStringLiteral("com.operasoftware.Opera/Cookies"))}},
        };
    } else {
        const QString cfg = QDir(home).filePath(QStringLiteral(".config"));
        candidates = {
            {QStringLiteral("chrome"), {resolvePath(cfg, QStringLiteral("google-chrome/Default/Cookies"))}},
            {QStringLiteral("chromium"), {resolvePath(cfg, QStringLiteral("chromium/Default/Cookies"))}},
            {QStringLiteral("brave"), {resolvePath(cfg, QStringLiteral("BraveSoftware/Brave-Browser/Default/Cookies"))}},
            {QStringLiteral("edge"), {resolvePath(cfg, QStringLiteral("microsoft-edge/Default/Cookies"))}},
            {QStringLiteral("firefox"), {QDir(home).filePath(QStringLiteral(".mozilla/firefox"))}},
            {QStringLiteral("opera"), {resolvePath(cfg, QStringLiteral("opera/Cookies")), resolvePath(cfg, QStringLiteral("opera-stable/Cookies"))}},
            {QStringLiteral("vivaldi"), {resolvePath(cfg, QStringLiteral("vivaldi/Default/Cookies"))}},
        };
    }

    QStringList order;
    for (const Candidate &candidate : candidates) {
        if (existsAny(candidate.paths)) {
            order.append(candidate.name);
        }
    }
    return order;
}

void MainWindow::pickDir() {
    const QString startDir = outDirEdit->text().isEmpty() ? QDir::currentPath() : outDirEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select output directory"), startDir);
    if (!dir.isEmpty()) {
        outDirEdit->setText(dir);
    }
}

void MainWindow::toggleAudioOnly(int state) {
    const bool only = state == Qt::Checked;
    videoCombo->setEnabled(!only);
    containerCombo->setEnabled(!only);
    audioCombo->setEnabled(true);
    if (!only) {
        onVideoChanged(videoCombo->currentIndex());
    }
}

void MainWindow::onVideoChanged(int index) {
    if (audioOnlyCheck->isChecked()) {
        return;
    }
    bool progressive = false;
    if (index >= 0 && index < videoCombo->count()) {
        const QString fid = videoCombo->itemData(index).toString();
        const auto it = rowsById.constFind(fid);
        if (it != rowsById.constEnd()) {
            progressive = it.value().isProgressive();
        }
    }
    audioCombo->setEnabled(!progressive);
    audioCombo->setToolTip(progressive ? QStringLiteral("This format already contains audio") : QString());
}

void MainWindow::onCookieChoiceChanged(int index) {
    const QVariant data = cookiesCombo->itemData(index);
    if (data.isValid() && !data.toString().isEmpty()) {
        cookieUserOverride = data.toString();
        settings.setValue(QStringLiteral("cookies/browser"), cookieUserOverride.value());
    } else {
        cookieUserOverride.reset();
        settings.remove(QStringLiteral("cookies/browser"));
    }
    refreshCookieChoices();
}

void MainWindow::updateThumbnail() {
    if (thumbReply) {
        disconnect(thumbReply, &QNetworkReply::finished, this, &MainWindow::onThumbFinished);
        thumbReply->abort();
        thumbReply->deleteLater();
        thumbReply = nullptr;
    }

    thumbLabel->setPixmap(QPixmap());

    if (thumbnailUrl.isEmpty()) {
        thumbLabel->setText(QStringLiteral("No thumbnail"));
        return;
    }

    const QUrl url(thumbnailUrl);
    if (!url.isValid() || !kAllowedThumbSchemes.contains(url.scheme().toLower())) {
        thumbLabel->setText(QStringLiteral("No thumbnail"));
        return;
    }

    thumbLabel->setText(QStringLiteral("Loading thumbnail…"));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    thumbReply = thumbManager->get(request);
    connect(thumbReply, &QNetworkReply::finished, this, &MainWindow::onThumbFinished);
}

void MainWindow::onThumbFinished() {
    if (!thumbReply) {
        return;
    }

    QNetworkReply *reply = thumbReply;
    thumbReply = nullptr;

    disconnect(reply, &QNetworkReply::finished, this, &MainWindow::onThumbFinished);

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        thumbLabel->setText(QStringLiteral("No thumbnail"));
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.size() > thumbMaxBytes) {
        thumbLabel->setText(QStringLiteral("No thumbnail"));
        return;
    }

    QPixmap pix;
    if (!pix.loadFromData(data)) {
        thumbLabel->setText(QStringLiteral("No thumbnail"));
        return;
    }

    const QPixmap scaled = pix.scaled(thumbLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    thumbLabel->setPixmap(scaled);
    thumbLabel->setText(QString());
}

QList<std::optional<QString>> MainWindow::buildCookieAttempts() const {
    QList<std::optional<QString>> attempts;

    auto containsValue = [&](const std::optional<QString> &value) {
        for (const auto &item : attempts) {
            if (item.has_value() == value.has_value()) {
                if (!item.has_value() || item.value() == value.value()) {
                    return true;
                }
            }
        }
        return false;
    };

    auto addAttempt = [&](const std::optional<QString> &value) {
        if (!containsValue(value)) {
            attempts.append(value);
        }
    };

    if (cookieUserOverride && !cookieUserOverride->isEmpty()) {
        addAttempt(cookieUserOverride);
    }

    if (activeBrowser && !activeBrowser->isEmpty()) {
        addAttempt(activeBrowser);
    }

    addAttempt(std::nullopt);

    for (const QString &browser : detectedBrowsers) {
        addAttempt(std::optional<QString>(browser));
    }

    return attempts;
}

void MainWindow::analyzeUrl() {
    if (metaProc && metaProc->state() != QProcess::NotRunning) {
        QMessageBox::information(this, QStringLiteral("In progress"), QStringLiteral("Metadata analysis is already running."));
        return;
    }

    const QString url = urlEdit->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Enter a URL."));
        return;
    }

    appendLog(QStringLiteral("Analyzing: %1").arg(url));

    metaUrl = url;
    metaAttempts = buildCookieAttempts();
    metaCurrentBrowser.reset();
    metaRaw.clear();
    btnAnalyze->setEnabled(false);

    startNextAnalysisAttempt();
}

void MainWindow::startNextAnalysisAttempt() {
    cleanupMetaProcess();

    if (metaAttempts.isEmpty()) {
        logMetaFailureOutput(metaRaw);
        if (metaRaw.toLower().contains(QStringLiteral("cookies"))) {
            QMessageBox::warning(this,
                                 QStringLiteral("Authentication required"),
                                 QStringLiteral("The site requires sign-in. No usable browser cookies found."));
        } else {
            QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Failed to fetch metadata. Check the URL."));
        }
        resetAnalysisState();
        return;
    }

    const std::optional<QString> browser = metaAttempts.takeFirst();
    metaCurrentBrowser = browser;
    metaRaw.clear();

    QStringList args{QStringLiteral("-J"), QStringLiteral("--ignore-config"), QStringLiteral("--no-warnings")};
    if (browser && !browser->isEmpty()) {
        args << QStringLiteral("--cookies-from-browser") << browser.value();
        appendLog(QStringLiteral("Trying cookies from %1…").arg(browser.value()));
    } else if (cookieUserOverride && !cookieUserOverride->isEmpty()) {
        appendLog(QStringLiteral("Cookie override failed, retrying without cookies…"));
    }

    metaProc = new QProcess(this);
    metaProc->setProgram(QStringLiteral("yt-dlp"));
    QStringList fullArgs = args;
    fullArgs << metaUrl;
    metaProc->setArguments(fullArgs);
    metaProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(metaProc, &QProcess::readyReadStandardOutput, this, &MainWindow::onMetaReady);
    connect(metaProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::onMetaFinished);
    metaProc->start();
    metaTimer.start(60000);
}

void MainWindow::onMetaReady() {
    if (!metaProc) {
        return;
    }

    const QByteArray chunk = metaProc->readAllStandardOutput();
    QString text = QString::fromUtf8(chunk);
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    metaRaw += text;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QString upper = line.toUpper();
        if (upper.contains(QStringLiteral("ERROR")) || upper.contains(QStringLiteral("WARNING"))) {
            appendLog(line);
        }
    }
}

void MainWindow::logMetaFailureOutput(const QString &raw) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) {
        appendLog(QStringLiteral("No output from yt-dlp."));
        return;
    }
    if (text.startsWith(QLatin1Char('{')) && text.endsWith(QLatin1Char('}'))) {
        appendLog(QStringLiteral("Received metadata payload but parsing failed."));
        return;
    }
    appendLog(text);
}

void MainWindow::onMetaFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    metaTimer.stop();
    cleanupMetaProcess();

    const QString raw = metaRaw.trimmed();
    const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0;

    QJsonObject data;
    bool parsed = false;
    if (!raw.isEmpty()) {
        const int first = raw.indexOf(QLatin1Char('{'));
        const int last = raw.lastIndexOf(QLatin1Char('}'));
        if (first != -1 && last != -1 && last > first) {
            const QString candidate = raw.mid(first, last - first + 1);
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                data = doc.object();
                parsed = true;
            }
        }
    }

    if (ok && parsed) {
        handleAnalysisSuccess(data);
        return;
    }

    if (!metaAttempts.isEmpty()) {
        startNextAnalysisAttempt();
        return;
    }

    logMetaFailureOutput(raw);
    if (raw.toLower().contains(QStringLiteral("cookies"))) {
        QMessageBox::warning(this,
                             QStringLiteral("Authentication required"),
                             QStringLiteral("The site requires sign-in. No usable browser cookies found."));
    } else {
        QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Failed to fetch metadata. Check the URL."));
    }
    resetAnalysisState();
}

void MainWindow::onMetaTimeout() {
    if (!metaProc) {
        return;
    }
    appendLog(QStringLiteral("Metadata fetch timed out; trying next option…"));
    metaProc->kill();
}

void MainWindow::cleanupMetaProcess() {
    if (metaProc) {
        metaProc->disconnect(this);
        if (metaProc->state() != QProcess::NotRunning) {
            metaProc->kill();
        }
        metaProc->deleteLater();
        metaProc = nullptr;
    }
}

void MainWindow::resetAnalysisState() {
    metaTimer.stop();
    cleanupMetaProcess();
    metaAttempts.clear();
    metaCurrentBrowser.reset();
    metaRaw.clear();
    metaUrl.clear();
    btnAnalyze->setEnabled(true);
}

void MainWindow::handleAnalysisSuccess(const QJsonObject &object) {
    const std::optional<QString> browser = metaCurrentBrowser;
    activeBrowser = browser;
    if (browser && !browser->isEmpty()) {
        appendLog(QStringLiteral("Using cookies from %1.").arg(browser.value()));
        const int idx = cookiesCombo->findData(browser.value());
        if (idx != -1) {
            cookiesCombo->blockSignals(true);
            cookiesCombo->setCurrentIndex(idx);
            cookiesCombo->blockSignals(false);
        }
    } else {
        appendLog(QStringLiteral("No browser cookies used."));
    }

    refreshCookieChoices();

    resetAnalysisState();

    populateFormatsFromInfo(object);

    thumbnailUrl = object.value(QStringLiteral("thumbnail")).toString();
    updateThumbnail();

    const QString title = object.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) {
        appendLog(title);
    }
}

void MainWindow::populateFormatsFromInfo(const QJsonObject &object) {
    formats.clear();
    videoMap.clear();
    audioMap.clear();
    rowsById.clear();
    videoCombo->clear();
    audioCombo->clear();

    const QJsonArray formatArray = object.value(QStringLiteral("formats")).toArray();
    for (const QJsonValue &value : formatArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject f = value.toObject();

        FormatRow row;
        row.fid = f.value(QStringLiteral("format_id")).toVariant().toString();
        row.ext = f.value(QStringLiteral("ext")).toString();
        row.vcodec = f.value(QStringLiteral("vcodec")).toString();
        if (row.vcodec.isEmpty()) {
            row.vcodec = QStringLiteral("none");
        }
        row.acodec = f.value(QStringLiteral("acodec")).toString();
        if (row.acodec.isEmpty()) {
            row.acodec = QStringLiteral("none");
        }
        if (!f.value(QStringLiteral("height")).isNull()) {
            row.height = f.value(QStringLiteral("height")).toInt();
        }
        if (!f.value(QStringLiteral("fps")).isNull()) {
            row.fps = f.value(QStringLiteral("fps")).toDouble();
        }
        if (!f.value(QStringLiteral("tbr")).isNull()) {
            row.tbr = f.value(QStringLiteral("tbr")).toDouble();
        }
        row.formatNote = f.value(QStringLiteral("format_note")).toString();

        if (row.fid.isEmpty()) {
            continue;
        }

        if (row.isVideo()) {
            videoMap.append(row);
        } else if (row.isAudio()) {
            audioMap.append(row);
        }
        formats.append(row);
        rowsById.insert(row.fid, row);
    }

    std::sort(videoMap.begin(), videoMap.end(), [](const FormatRow &a, const FormatRow &b) {
        const int ah = a.height.value_or(0);
        const int bh = b.height.value_or(0);
        if (ah == bh) {
            return a.tbr.value_or(0.0) > b.tbr.value_or(0.0);
        }
        return ah > bh;
    });

    std::sort(audioMap.begin(), audioMap.end(), [](const FormatRow &a, const FormatRow &b) {
        return a.tbr.value_or(0.0) > b.tbr.value_or(0.0);
    });

    videoCombo->blockSignals(true);
    for (const FormatRow &row : std::as_const(videoMap)) {
        QString label = row.videoLabel();
        if (row.isProgressive()) {
            label += QStringLiteral(" [with audio]");
        }
        videoCombo->addItem(label, row.fid);
    }
    if (videoCombo->count() > 0) {
        videoCombo->setCurrentIndex(0);
    }
    videoCombo->blockSignals(false);

    audioCombo->blockSignals(true);
    for (const FormatRow &row : std::as_const(audioMap)) {
        audioCombo->addItem(row.audioLabel(), row.fid);
    }
    if (audioCombo->count() > 0) {
        audioCombo->setCurrentIndex(0);
    }
    audioCombo->blockSignals(false);

    onVideoChanged(videoCombo->currentIndex());
}

void MainWindow::startDownload() {
    if (proc && proc->state() != QProcess::NotRunning) {
        QMessageBox::warning(this, QStringLiteral("In progress"), QStringLiteral("A download is already in progress."));
        return;
    }

    const QString url = urlEdit->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Enter a URL."));
        return;
    }

    QString outDir = outDirEdit->text().trimmed();
    if (outDir.isEmpty()) {
        outDir = QDir::currentPath();
    }
    QFileInfo outInfo(outDir);
    if (!outInfo.exists() || !outInfo.isDir()) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Invalid output directory."));
        return;
    }

    QString tpl = templateEdit->text().trimmed();
    if (tpl.isEmpty()) {
        tpl = QStringLiteral("%(title)s-%(id)s.%(ext)s");
    }
    const QString outTemplate = QDir(outDir).filePath(tpl);

    const bool isAudioOnly = audioOnlyCheck->isChecked();
    const QString container = containerCombo->currentText();
    const bool useAria = ariaCheck->isChecked();
    const int conn = ariaConn->value();

    QStringList args{QStringLiteral("--newline"),
                     QStringLiteral("--ignore-config"),
                     QStringLiteral("--no-warnings"),
                     QStringLiteral("-o"),
                     outTemplate};

    args << cookiesArgs();

    if (isAudioOnly) {
        if (audioCombo->count() == 0) {
            QMessageBox::warning(this, QStringLiteral("Missing"), QStringLiteral("No audio tracks."));
            return;
        }
        const QString aId = audioCombo->currentData().toString();
        args << QStringLiteral("-f") << aId;
    } else {
        if (videoCombo->count() == 0) {
            QMessageBox::warning(this, QStringLiteral("Missing"), QStringLiteral("Select a video."));
            return;
        }
        const QString vId = videoCombo->currentData().toString();
        bool progressive = false;
        const auto it = rowsById.constFind(vId);
        if (it != rowsById.constEnd()) {
            progressive = it.value().isProgressive();
        }
        if (progressive) {
            args << QStringLiteral("-f") << vId;
        } else {
            if (audioCombo->count() == 0) {
                QMessageBox::warning(this, QStringLiteral("Missing"), QStringLiteral("No audio tracks for the selected item."));
                return;
            }
            const QString aId = audioCombo->currentData().toString();
            args << QStringLiteral("-f") << (vId + QStringLiteral("+") + aId);
        }
        if (container != QStringLiteral("auto")) {
            args << QStringLiteral("--remux-video") << container;
        }
    }

    if (embedThumbCheck->isChecked()) {
        args << QStringLiteral("--embed-thumbnail");
    }

    if (useAria) {
        const QString ariaArgs = QStringLiteral("-x%1 -s%1 -k1M --summary-interval=1 --console-log-level=warn --show-console-readout=false --enable-color=false")
                                     .arg(conn);
        args << QStringLiteral("--external-downloader") << QStringLiteral("aria2c")
             << QStringLiteral("--external-downloader-args") << ariaArgs;
    }

    QStringList finalArgs = args;
    finalArgs << url;

    QString summary = QStringLiteral("Starting download");
    if (useAria) {
        summary += QStringLiteral(" (aria2c external downloader)");
    }
    appendLog(summary + QStringLiteral("…"));

    proc = new QProcess(this);
    proc->setProgram(QStringLiteral("yt-dlp"));
    proc->setArguments(finalArgs);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcOutput);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::onProcFinished);
    proc->start();

    btnDownload->setEnabled(false);
    btnStop->setEnabled(true);
    progress->setValue(0);
}

void MainWindow::stopDownload() {
    if (proc) {
        appendLog(QStringLiteral("Stopping…"));
        proc->kill();
    }
}

void MainWindow::onProcOutput() {
    if (!proc) {
        return;
    }

    const QByteArray chunk = proc->readAllStandardOutput();
    QString text = QString::fromUtf8(chunk);
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        processDownloadLine(line);
    }
}

void MainWindow::processDownloadLine(const QString &line) {
    const QString stripped = line.trimmed();
    if (stripped.isEmpty()) {
        return;
    }

    const std::optional<QString> normalized = normalizeProgressLine(stripped);
    if (normalized.has_value()) {
        QRegularExpressionMatch match = percentRe.match(normalized.value());
        if (match.hasMatch()) {
            updateDownloadLogLine(normalized.value());
        } else {
            appendLog(normalized.value());
            match = percentRe.match(normalized.value());
        }
        if (match.hasMatch()) {
            bool ok = false;
            const double pct = match.captured(1).toDouble(&ok);
            if (ok && pct >= 0.0 && pct <= 100.0) {
                progress->setValue(static_cast<int>(pct));
            }
        }
        return;
    }

    if (shouldSkipPlainLine(stripped)) {
        return;
    }

    appendLog(stripped);
    const QRegularExpressionMatch match = percentRe.match(stripped);
    if (match.hasMatch()) {
        bool ok = false;
        const double pct = match.captured(1).toDouble(&ok);
        if (ok && pct >= 0.0 && pct <= 100.0) {
            progress->setValue(static_cast<int>(pct));
        }
    }
}

void MainWindow::onProcFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    appendLog(QStringLiteral("Finished. Code: %1").arg(exitCode));
    if (proc) {
        proc->disconnect(this);
        proc->deleteLater();
        proc = nullptr;
    }
    btnDownload->setEnabled(true);
    btnStop->setEnabled(false);
    progress->setValue(exitCode == 0 ? 100 : 0);
}
