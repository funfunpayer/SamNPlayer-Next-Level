#include "player_controller.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

#include "curve_item.h"
#include "heatmap_item.h"
#include "samn/mapper.h"
#include "samn/marks.h"
#include "samn/samn_file.h"
#include "samn/script_finder.h"

namespace samn::ui {

namespace {

bool isVideo(const QString& path) {
    const std::string suffix = "." + QFileInfo(path).suffix().toLower().toStdString();
    return std::find(samn::VIDEO_EXTS.begin(), samn::VIDEO_EXTS.end(), suffix) != samn::VIDEO_EXTS.end();
}

std::string u8(const QString& s) { return s.toUtf8().toStdString(); }
QString q(const std::string& s) { return QString::fromUtf8(s.c_str(), static_cast<int>(s.size())); }

// Frames (0-1) as a 0-100 lane for the timeline.
std::vector<samn::Action> laneFrom(const std::vector<samn::Frame>& frames, bool suction) {
    std::vector<samn::Action> out;
    out.reserve(frames.size());
    for (const auto& f : frames) out.push_back({f.at, static_cast<int>(std::lround((suction ? f.suction : f.vibration) * 100))});
    return out;
}

}  // namespace

PlayerController::PlayerController(QObject* parent) : QObject(parent) {}
PlayerController::~PlayerController() = default;

samn::Settings& PlayerController::settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

QString PlayerController::toLocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QStringList PlayerController::scriptFolders() const {
    QStringList out;
    if (!settings_) const_cast<PlayerController*>(this)->settings();
    const auto v = settings_->get(kScriptFoldersKey, "");
    for (const auto& f : samn::split_folders(v.is_string() ? v.get<std::string>() : std::string()))
        out << q(f);
    return out;
}

bool PlayerController::openPath(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    if (!isVideo(path)) {
        variants_.clear();
        variantIndex_ = -1;
        emit variantsChanged();
        return openScript(path);
    }
    // Video first: beside it, in the script folders, tolerant names, variants.
    std::vector<std::string> folders;
    for (const auto& f : scriptFolders()) folders.push_back(u8(f));
    const auto match = samn::find_script_for_video(u8(path), folders);
    variants_.clear();
    variantIndex_ = -1;
    for (size_t i = 0; i < match.variants.size(); ++i) {
        const auto& v = match.variants[i];
        variants_.append(QVariantMap{{"path", q(v.path)}, {"label", q(v.label)}, {"beside", v.beside}});
        if (v.path == match.script_path) variantIndex_ = static_cast<int>(i);
    }
    emit variantsChanged();
    if (match.found && !match.script_path.empty()) return openScript(q(match.script_path), path);
    clearScript();
    setVideo(path);
    setError({});
    return true;
}

bool PlayerController::chooseVariant(int index) {
    if (index < 0 || index >= variants_.size()) return false;
    const QString video = videoPath_;
    if (!openScript(variants_[index].toMap().value("path").toString(), video)) return false;
    variantIndex_ = index;
    emit variantsChanged();
    return true;
}

void PlayerController::addScriptFolder(const QString& pathOrUrl) {
    const QString dir = toLocalPath(pathOrUrl);
    QStringList folders = scriptFolders();
    if (!folders.contains(dir)) {
        folders << dir;
        try {
            settings().set(kScriptFoldersKey, u8(folders.join('\n')));
        } catch (const std::exception& e) {
            setError(tr("Could not save the script folders: %1").arg(q(e.what())));
            return;
        }
        emit scriptFoldersChanged();
    }
    // Look again for the open video's script.
    if (!hasScript() && !videoPath_.isEmpty()) {
        const QString video = videoPath_;
        openPath(video);
        if (!hasScript()) setError(tr("No script for %1 in that folder either.").arg(QFileInfo(video).fileName()));
    }
}

bool PlayerController::openScript(const QString& pathIn, const QString& videoIn) {
    // A .samn beside a .funscript of the same name wins (as in 0.7).
    const QString path = q(samn::PreferSamnCompanion(u8(toLocalPath(pathIn))));
    samn::Document doc;
    try {
        doc = samn::IsSamnPath(u8(path)) ? samn::LoadDocument(std::filesystem::u8path(u8(path)))
                                         : samn::LoadFunscriptFile(std::filesystem::u8path(u8(path)));
    } catch (const std::exception& e) {
        setError(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), q(e.what())));
        return false;
    }
    if (doc.general.empty()) {
        setError(tr("Could not open %1: the script has no points.").arg(QFileInfo(path).fileName()));
        return false;
    }
    stroke_ = std::move(doc.general);
    // Vibration and suction lanes: from the file when it has them, else as
    // the device would get them (mapper, as in 0.7's GetHeatmap/Play).
    // The lanes use the device tick; the heatmap one bucket per 1/240.
    const auto laneFrames = samn::ToIntensityCurve(stroke_, samn::DefaultMapOptions())
                                .value_or(std::vector<samn::Frame>{});
    vibrationFromScript_ = !doc.vibration.empty();
    suctionFromScript_ = !doc.suction.empty();
    vibration_ = vibrationFromScript_ ? std::move(doc.vibration) : laneFrom(laneFrames, false);
    suction_ = suctionFromScript_ ? std::move(doc.suction) : laneFrom(laneFrames, true);
    auto opts = samn::DefaultMapOptions();
    const int64_t dur = stroke_.back().at;
    opts.tick_ms = std::max<int64_t>(10, dur / kHeatmapBuckets);
    const auto frames = samn::ToIntensityCurve(stroke_, opts).value_or(std::vector<samn::Frame>{});
    heat_.clear();
    heat_.reserve(frames.size());
    for (const auto& f : frames) heat_.push_back(static_cast<float>(std::max(f.vibration, f.suction)));

    scriptPath_ = path;
    loop_.reset();
    emit loopChanged();
    loadMarks(path);
    // Offset saved per script (0.7 key playback.offset.<path>).
    const auto saved = settings().get(samn::offset_key(u8(path)), 0);
    offsetMs_ = static_cast<int>(samn::clamp_offset(saved.is_number() ? saved.get<int64_t>() : 0));
    emit offsetMsChanged();
    setError({});

    QString video = toLocalPath(videoIn);
    if (video.isEmpty()) {
        if (const auto m = samn::find_matching_video(u8(path))) video = q(*m);
    }
    pushToViews();
    emit scriptChanged();
    setVideo(video);
    setView(samn::FullView(timelineMs()));
    setVideoPositionMs(0);
    return true;
}

void PlayerController::loadMarks(const QString& path) {
    bookmarks_.clear();
    chapterRanges_.clear();
    chapters_.clear();
    try {
        const auto p = std::filesystem::u8path(u8(path));
        for (const auto& b : samn::LoadBookmarks(p)) bookmarks_.push_back(b.time_ms);
        for (const auto& c : samn::LoadChapters(p)) {
            chapterRanges_.emplace_back(c.start_ms, c.end_ms);
            chapters_.append(QVariantMap{{"name", c.name.empty() ? tr("Chapter at %1").arg(formatTime(c.start_ms)) : q(c.name)},
                                         {"startMs", qint64(c.start_ms)},
                                         {"endMs", qint64(c.end_ms)}});
        }
    } catch (const std::exception&) {
        // Broken chapter metadata must not block playing.
        bookmarks_.clear();
        chapterRanges_.clear();
        chapters_.clear();
    }
}

void PlayerController::clearScript() {
    stroke_.clear();
    vibration_.clear();
    suction_.clear();
    heat_.clear();
    bookmarks_.clear();
    chapterRanges_.clear();
    chapters_.clear();
    scriptPath_.clear();
    playheadMs_ = -1;
    loop_.reset();
    emit loopChanged();
    pushToViews();
    emit scriptChanged();
    emit playheadMsChanged();
    setView(samn::FullView(timelineMs()));
}

void PlayerController::setVideo(const QString& path) {
    if (path == videoPath_) return;
    videoPath_ = path;
    videoDurationMs_ = 0;  // the player reports the new one
    emit videoChanged();
    emit scriptChanged();  // noScriptForVideo depends on the video too
}

void PlayerController::setError(const QString& e) {
    if (e == lastError_) return;
    lastError_ = e;
    emit lastErrorChanged();
}

void PlayerController::setOffsetMs(int ms) {
    ms = static_cast<int>(samn::clamp_offset(ms));
    if (ms == offsetMs_) return;
    const bool wasZoomed = zoomed();
    offsetMs_ = ms;
    setView(wasZoomed ? view_ : samn::FullView(timelineMs()));
    if (!scriptPath_.isEmpty()) {
        try {
            settings().set(samn::offset_key(u8(scriptPath_)), offsetMs_);
        } catch (const std::exception& e) {
            setError(tr("Could not save the offset: %1").arg(q(e.what())));
        }
    }
    emit offsetMsChanged();
    setVideoPositionMs(videoMs_);
}

void PlayerController::setVideoPositionMs(qint64 videoMs) {
    videoMs_ = videoMs;
    const qint64 ph = stroke_.empty() ? -1 : samn::script_time(videoMs, offsetMs_);
    if (ph != playheadMs_) {
        playheadMs_ = ph;
        emit playheadMsChanged();
    }
    for (const auto& c : curves_)
        if (c) c->setPlayheadMs(playheadMs_);
    if (heatmap_) heatmap_->setPlayheadMs(playheadMs_);
}

qint64 PlayerController::loopJumpFor(qint64 videoMs) const {
    if (!loop_) return -1;
    const auto j = loop_->jump(videoMs);
    return j ? *j : -1;
}

void PlayerController::setLoopStart(qint64 videoMs) {
    samn::LoopRange r = loop_.value_or(samn::LoopRange{0, 0});
    r.start_ms = videoMs;
    loop_ = r;
    emit loopChanged();
}

void PlayerController::setLoopEnd(qint64 videoMs) {
    samn::LoopRange r = loop_.value_or(samn::LoopRange{0, 0});
    r.end_ms = videoMs;
    loop_ = r;
    emit loopChanged();
}

void PlayerController::clearLoop() {
    loop_.reset();
    emit loopChanged();
}

qint64 PlayerController::chapterVideoMs(int index) const {
    if (index < 0 || index >= static_cast<int>(chapterRanges_.size())) return -1;
    // Script time → video time: the offset moves the script later.
    return chapterRanges_[index].first + offsetMs_;
}

int64_t PlayerController::timelineMs() const {
    // Script time: the video ends at its duration minus the offset.
    return std::max<int64_t>({1, scriptDurationMs(), videoDurationMs_ - offsetMs_});
}

void PlayerController::setView(samn::ViewWindow v) {
    v = samn::ClampView(v, timelineMs());
    if (v == view_) return;
    view_ = v;
    emit viewChanged();
}

void PlayerController::setVideoDurationMs(qint64 ms) {
    const bool wasZoomed = zoomed();
    videoDurationMs_ = std::max<qint64>(0, ms);
    setView(wasZoomed ? view_ : samn::FullView(timelineMs()));
}

void PlayerController::zoomAt(double factor, double fraction) {
    const double span = double(view_.end_ms - view_.start_ms);
    const double anchor = double(view_.start_ms) + std::clamp(fraction, 0.0, 1.0) * span;
    setView(samn::ZoomView(view_, timelineMs(), factor, anchor));
}

void PlayerController::zoomBy(double factor) {
    setView(samn::ZoomView(view_, timelineMs(), factor, double(std::max<qint64>(0, playheadMs_))));
}

void PlayerController::resetZoom() { setView(samn::FullView(timelineMs())); }

QString PlayerController::zoomLabel() const {
    if (!zoomed()) return tr("Full");
    return QStringLiteral("%1s–%2s").arg(view_.start_ms / 1000.0, 0, 'f', 1).arg(view_.end_ms / 1000.0, 0, 'f', 1);
}

void PlayerController::attachCurve(CurveItem* curve) {
    if (!curve) return;
    curves_.erase(std::remove_if(curves_.begin(), curves_.end(), [](const QPointer<CurveItem>& c) { return c.isNull(); }),
                  curves_.end());
    curves_.emplace_back(curve);
    pushToViews();
}

void PlayerController::attachHeatmap(HeatmapItem* heatmap) {
    heatmap_ = heatmap;
    pushToViews();
}

void PlayerController::pushToViews() {
    for (const auto& c : curves_) {
        if (!c) continue;
        const QString lane = c->lane();
        c->setActions(lane == QLatin1String("vibration") ? vibration_ : lane == QLatin1String("suction") ? suction_ : stroke_);
        c->setMarks(lane == QLatin1String("stroke") ? bookmarks_ : std::vector<int64_t>{},
                    lane == QLatin1String("stroke") ? chapterRanges_ : std::vector<std::pair<int64_t, int64_t>>{});
        c->setPlayheadMs(playheadMs_);
    }
    if (heatmap_) {
        heatmap_->setValues(heat_, stroke_.empty() ? 0 : stroke_.front().at, scriptDurationMs());
        heatmap_->setPlayheadMs(playheadMs_);
    }
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
