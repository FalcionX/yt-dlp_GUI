#pragma once

#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;

struct FormatRow {
    QString fid;
    QString ext;
    QString vcodec;
    QString acodec;
    std::optional<int> height;
    std::optional<double> fps;
    std::optional<double> tbr;
    QString formatNote;

    bool isVideo() const;
    bool isAudio() const;
    bool isProgressive() const;
    QString videoLabel() const;
    QString audioLabel() const;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void pickDir();
    void analyzeUrl();
    void startDownload();
    void stopDownload();
    void onProcOutput();
    void onProcFinished(int exitCode, QProcess::ExitStatus status);
    void toggleAudioOnly(int state);
    void onVideoChanged(int index);
    void onCookieChoiceChanged(int index);
    void updateThumbnail();
    void onThumbFinished();
    void onMetaReady();
    void onMetaFinished(int exitCode, QProcess::ExitStatus status);
    void onMetaTimeout();

private:
    void setupUi();
    void appendLog(const QString &text);
    void clearDownloadLogLine();
    void updateDownloadLogLine(const QString &text);
    void refreshLogDisplay();
    std::optional<QString> normalizeProgressLine(const QString &text) const;
    bool shouldSkipPlainLine(const QString &text) const;
    QString defaultOutputDir() const;
    void refreshCookieChoices();
    QStringList cookiesArgs() const;
    QStringList detectInstalledBrowsers() const;
    void startNextAnalysisAttempt();
    void cleanupMetaProcess();
    void resetAnalysisState();
    void handleAnalysisSuccess(const QJsonObject &object);
    void populateFormatsFromInfo(const QJsonObject &object);
    QList<std::optional<QString>> buildCookieAttempts() const;
    void logMetaFailureOutput(const QString &raw);
    void processDownloadLine(const QString &line);

    QLineEdit *urlEdit;
    QPushButton *btnAnalyze;
    QPushButton *btnDownload;
    QPushButton *btnStop;
    QLineEdit *outDirEdit;
    QPushButton *btnBrowse;
    QLineEdit *templateEdit;
    QComboBox *videoCombo;
    QComboBox *audioCombo;
    QComboBox *containerCombo;
    QCheckBox *audioOnlyCheck;
    QCheckBox *ariaCheck;
    QSpinBox *ariaConn;
    QCheckBox *embedThumbCheck;
    QLabel *thumbLabel;
    QComboBox *cookiesCombo;
    QProgressBar *progress;
    QTextEdit *logView;
    QNetworkAccessManager *thumbManager;
    QNetworkReply *thumbReply;
    QSettings settings;
    QProcess *proc;
    QProcess *metaProc;
    QTimer metaTimer;

    QStringList logMessages;
    QString downloadProgressLine;

    QList<FormatRow> formats;
    QList<FormatRow> videoMap;
    QList<FormatRow> audioMap;
    QHash<QString, FormatRow> rowsById;

    QString thumbnailUrl;

    QList<std::optional<QString>> metaAttempts;
    std::optional<QString> metaCurrentBrowser;
    QString metaRaw;
    QString metaUrl;

    QStringList detectedBrowsers;
    std::optional<QString> activeBrowser;
    std::optional<QString> cookieUserOverride;

    bool ariaAvailable;
    const int thumbMaxBytes;

    QRegularExpression percentRe;
    QRegularExpression ariaProgressRe;
};
