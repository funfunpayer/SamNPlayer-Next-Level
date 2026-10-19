#include "update_checker.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "app_log.h"
#include "samn/settings.h"
#include "samn/update_check.h"

namespace samn::ui {

namespace {

constexpr const char* kKey = "update.check_on_startup";
constexpr int kTimeoutMs = 10000;  // 0.7: 10 s

QUrl ApiUrl() {
    const QByteArray over = qgetenv("SAMN_UPDATE_URL");
    return QUrl(over.isEmpty() ? QString::fromStdString(samn::LatestReleaseApiUrl()) : QString::fromLocal8Bit(over));
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {
    try {
        const auto v = samn::Settings().get(kKey);
        if (v.is_boolean()) check_on_startup_ = v.get<bool>();
    } catch (const std::exception&) {
        // Unreadable settings: the default stays.
    }
}

UpdateChecker::~UpdateChecker() {
    if (reply_) reply_->abort();
}

void UpdateChecker::setCheckOnStartup(bool on) {
    if (on == check_on_startup_) return;
    check_on_startup_ = on;
    try {
        samn::Settings().set(kKey, on);
    } catch (const std::exception& e) {
        AppLog::instance().add(samn::LogSeverity::kWarn, tr("Setting %1 not saved: %2").arg(kKey, e.what()));
    }
    emit checkOnStartupChanged();
}

QString UpdateChecker::currentVersion() const {
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral("dev") : v;
}

QString UpdateChecker::statusText() const {
    if (state_ == QLatin1String("checking")) return tr("Checking…");
    if (state_ == QLatin1String("available"))
        return tr("Version %1 available (current: %2).").arg(latest_tag_, currentVersion());
    if (state_ == QLatin1String("current")) return tr("No update available (current: %1).").arg(currentVersion());
    if (state_ == QLatin1String("error")) return tr("Update check failed: %1").arg(error_);
    return {};
}

void UpdateChecker::check(bool onStartup) {
    if (reply_) return;  // one at a time
    quiet_ = onStartup;
    QNetworkRequest req(ApiUrl());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "SamNPlayer2-update-check");
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = net_->get(req);
    setResult(QStringLiteral("checking"));
    QNetworkReply* r = reply_;
    connect(r, &QNetworkReply::finished, this, [this, r] { finish(r); });
}

void UpdateChecker::finish(QNetworkReply* reply) {
    reply->deleteLater();
    reply_ = nullptr;
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // 404: no release published yet (or the repository is not public) —
    // "nothing newer", as 0.7 ErrNoRelease.
    if (http == 404) {
        latest_tag_.clear();
        setResult(QStringLiteral("current"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError || (http != 0 && http != 200)) {
        QString why = http != 0 && http != 200 ? tr("GitHub responded with HTTP %1").arg(http) : reply->errorString().trimmed();
        if (why.isEmpty()) why = tr("unknown error");
        setResult(QStringLiteral("error"), why);
        return;
    }
    const auto rel = samn::ParseLatestRelease(reply->readAll().toStdString());
    if (!rel) {
        // Not readable or not a plain release: nothing to offer (0.7 refuses
        // a patch tag published as a release).
        latest_tag_.clear();
        setResult(QStringLiteral("current"));
        return;
    }
    latest_tag_ = QString::fromStdString(rel->tag);
    notes_preview_ = QString::fromStdString(samn::ReleaseNotesPreview(rel->notes));
    page_url_ = QString::fromStdString(rel->page_url);
    const bool newer = samn::IsNewerRelease(currentVersion().toStdString(), rel->tag);
    if (newer && quiet_) banner_ = true;
    setResult(newer ? QStringLiteral("available") : QStringLiteral("current"));
}

void UpdateChecker::setResult(const QString& state, const QString& error) {
    state_ = state;
    error_ = error;
    if (state == QLatin1String("error"))
        AppLog::instance().add(quiet_ ? samn::LogSeverity::kWarn : samn::LogSeverity::kError,
                               (quiet_ ? tr("Update check on startup: %1") : tr("Update check: %1")).arg(error));
    else if (state == QLatin1String("available"))
        AppLog::instance().add(samn::LogSeverity::kInfo, tr("Update available: %1").arg(latest_tag_));
    emit changed();
}

bool UpdateChecker::openReleasePage() {
    const QString url = page_url_.isEmpty() ? QString::fromStdString(samn::ReleasesPageUrl()) : page_url_;
    return QDesktopServices::openUrl(QUrl(url));
}

void UpdateChecker::dismissBanner() {
    if (!banner_) return;
    banner_ = false;
    emit changed();
}

}  // namespace samn::ui
