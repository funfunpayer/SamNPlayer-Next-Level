#include "app_log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <fstream>

namespace samn::ui {

namespace {

QtMessageHandler g_previous = nullptr;
thread_local bool g_in_handler = false;  // a message while logging must not loop

void Handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (!g_in_handler) {
        g_in_handler = true;
        samn::LogSeverity level = samn::LogSeverity::kInfo;
        switch (type) {
            case QtDebugMsg: level = samn::LogSeverity::kDebug; break;
            case QtInfoMsg: level = samn::LogSeverity::kInfo; break;
            case QtWarningMsg: level = samn::LogSeverity::kWarn; break;
            case QtCriticalMsg:
            case QtFatalMsg: level = samn::LogSeverity::kError; break;
        }
        AppLog::instance().add(level, msg);
        g_in_handler = false;
    }
    if (g_previous) g_previous(type, ctx, msg);
}

constexpr std::uintmax_t kMaxFileBytes = 2u * 1024u * 1024u;

}  // namespace

AppLog& AppLog::instance() {
    static AppLog log;
    return log;
}

void AppLog::installMessageHandler() { g_previous = qInstallMessageHandler(Handler); }

void AppLog::add(samn::LogSeverity level, const QString& text) {
    samn::LogEntry e{QDateTime::currentMSecsSinceEpoch(), level, text.toStdString()};
    if (!buffer_.Add(e)) return;
    writeFile(e);
    notify();
}

void AppLog::reportError(const QString& text) {
    if (text.trimmed().isEmpty()) return;
    add(samn::LogSeverity::kError, text);
    QMetaObject::invokeMethod(this, [this, text] { emit errorReported(text); }, Qt::QueuedConnection);
}

void AppLog::enableFile(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> l(file_mu_);
    file_ = path;
    if (file_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
}

std::filesystem::path AppLog::filePath() const {
    std::lock_guard<std::mutex> l(file_mu_);
    return file_;
}

void AppLog::writeFile(const samn::LogEntry& e) {
    std::lock_guard<std::mutex> l(file_mu_);
    if (file_.empty()) return;
    std::error_code ec;
    if (std::filesystem::file_size(file_, ec) > kMaxFileBytes && !ec) {
        std::filesystem::path old = file_;
        old += ".1";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(file_, old, ec);
    }
    std::ofstream out(file_, std::ios::app | std::ios::binary);
    if (out) out << samn::FormatLogLine(e) << '\n';
}

void AppLog::notify() {
    // One changed() per event loop turn, on this object's (the GUI) thread.
    if (pending_.exchange(true)) return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            pending_ = false;
            emit changed();
        },
        Qt::QueuedConnection);
}

}  // namespace samn::ui
