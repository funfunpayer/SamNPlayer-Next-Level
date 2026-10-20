#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <filesystem>
#include <mutex>

#include "samn/log_buffer.h"

namespace samn::ui {

// The process log (0.7 logging/ + the Log tab): Qt's messages, the device log
// and app notes in one buffer, and with a file once enabled. Thread-safe;
// changed() comes on the GUI thread, at most once per event loop turn.
class AppLog : public QObject {
    Q_OBJECT
public:
    static AppLog& instance();
    // Routes qDebug/qInfo/qWarning/qCritical here; earlier handlers still get
    // them (the console). Call once at startup.
    static void installMessageHandler();

    void add(samn::LogSeverity level, const QString& text);
    // A failure the user must see (0.7 uiError): logged as an error and,
    // on the GUI thread, errorReported() for the toast (Notices).
    void reportError(const QString& text);
    samn::LogBuffer& buffer() { return buffer_; }
    // Appends every kept entry to path (logs/samnplayer2.log); a file over
    // 2 MB is moved to .1 first. An empty path stops writing.
    void enableFile(const std::filesystem::path& path);
    std::filesystem::path filePath() const;

signals:
    void changed();
    void errorReported(const QString& text);

private:
    AppLog() = default;
    void writeFile(const samn::LogEntry& e);
    void notify();

    samn::LogBuffer buffer_;
    mutable std::mutex file_mu_;
    std::filesystem::path file_;
    std::atomic<bool> pending_{false};
};

}  // namespace samn::ui
