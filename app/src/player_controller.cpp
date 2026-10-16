#include "player_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "curve_item.h"

namespace samn::ui {

namespace {
const QStringList kVideoExts = {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v", ".wmv", ".mpg", ".mpeg", ".ts"};

bool isVideo(const QString& path) {
    const QString suffix = "." + QFileInfo(path).suffix().toLower();
    return kVideoExts.contains(suffix);
}
}  // namespace

PlayerController::PlayerController(QObject* parent) : QObject(parent) {}

QString PlayerController::toLocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

bool PlayerController::openPath(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    if (isVideo(path)) {
        // Video first: the script with the same name beside it.
        // (Script folders, tolerant names and variants: samn::FindScriptForVideo, next step.)
        const QFileInfo fi(path);
        const QString script = fi.dir().filePath(fi.completeBaseName() + ".funscript");
        if (QFileInfo(script).isFile()) return openScript(script, path);
        actions_.clear();
        scriptPath_.clear();
        pushToCurve();
        emit scriptChanged();
        setVideo(path);
        return true;
    }
    return openScript(path);
}

bool PlayerController::openScript(const QString& pathIn, const QString& videoIn) {
    const QString path = toLocalPath(pathIn);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), f.errorString()));
        return false;
    }
    std::vector<samn::Action> acts;
    try {
        acts = samn::Parse(f.readAll().toStdString()).actions;
    } catch (const std::exception& e) {
        setError(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return false;
    }
    actions_ = std::move(acts);
    scriptPath_ = path;
    setError({});
    QString video = toLocalPath(videoIn);
    if (video.isEmpty()) {
        const QFileInfo fi(path);
        for (const auto& ext : kVideoExts) {
            const QString cand = fi.dir().filePath(fi.completeBaseName() + ext);
            if (QFileInfo(cand).isFile()) {
                video = cand;
                break;
            }
        }
    }
    pushToCurve();
    emit scriptChanged();
    setVideo(video);
    setVideoPositionMs(0);
    return true;
}

void PlayerController::setVideo(const QString& path) {
    if (path == videoPath_) return;
    videoPath_ = path;
    emit videoChanged();
}

void PlayerController::setError(const QString& e) {
    if (e == lastError_) return;
    lastError_ = e;
    emit lastErrorChanged();
}

void PlayerController::setOffsetMs(int ms) {
    ms = std::clamp(ms, -kOffsetLimitMs, kOffsetLimitMs);
    if (ms == offsetMs_) return;
    offsetMs_ = ms;
    emit offsetMsChanged();
    setVideoPositionMs(videoMs_);
}

void PlayerController::setVideoPositionMs(qint64 videoMs) {
    videoMs_ = videoMs;
    // Script time = video time - offset, never below 0 (as in 0.7).
    const qint64 ph = actions_.empty() ? -1 : std::max<qint64>(0, videoMs - offsetMs_);
    if (ph != playheadMs_) {
        playheadMs_ = ph;
        emit playheadMsChanged();
    }
    if (curve_) curve_->setPlayheadMs(playheadMs_);
}

void PlayerController::attachCurve(CurveItem* curve) {
    curve_ = curve;
    pushToCurve();
}

void PlayerController::pushToCurve() {
    if (!curve_) return;
    curve_->setActions(actions_);
    curve_->setPlayheadMs(playheadMs_);
}

QString PlayerController::scriptName() const { return QFileInfo(scriptPath_).fileName(); }
QUrl PlayerController::videoUrl() const { return videoPath_.isEmpty() ? QUrl() : QUrl::fromLocalFile(videoPath_); }
QString PlayerController::videoName() const { return QFileInfo(videoPath_).fileName(); }

QString PlayerController::formatTime(qint64 ms) const {
    if (ms < 0) ms = 0;
    const qint64 s = ms / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace samn::ui
