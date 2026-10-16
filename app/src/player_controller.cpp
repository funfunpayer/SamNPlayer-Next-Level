#include "player_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <filesystem>

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

PlayerController::PlayerController(QObject* parent) : QObject(parent) {
    sleepTimer_.setInterval(1000);
    connect(&sleepTimer_, &QTimer::timeout, this, [this] {
        if (sleepMinutes_ <= 0 || !playClock_.isValid()) return;
        if (playedMs_ + playClock_.elapsed() >= qint64(sleepMinutes_) * 60000) {
            playedMs_ = 0;
            playClock_.invalidate();
            sleepTimer_.stop();
            emit sleepReached();
        }
    });
}
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
    loadMarks(path);
    {
        const auto v = settings().get("playback.omarker_intensity", 0.5);
        oMarkerIntensity_ = v.is_number() ? std::clamp(v.get<double>(), 0.0, 1.0) : 0.5;
    }
    {
        const auto on = settings().get("playback.speed_highlights", true);
        speedHL_ = on.is_boolean() ? on.get<bool>() : true;
        const auto t = settings().get("playback.speed_highlight_thresh", 400.0);
        const double v = t.is_number() ? t.get<double>() : 400.0;
        speedHLThresh_ = (std::isfinite(v) && v > 0) ? std::clamp(v, 50.0, 2000.0) : 400.0;
        emit speedHighlightChanged();
    }
    pendingStart_.reset();
    marker_ = samn::LoadMarker(std::filesystem::u8path(u8(path))).value_or(samn::RangeMarker{});
    emit markerChanged();
    emit loopChanged();
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
    emit scriptOpened(scriptPath_);
    return true;
}

void PlayerController::loadMarks(const QString& path) {
    try {
        marks_ = samn::LoadScriptMarks(std::filesystem::u8path(u8(path)));
    } catch (const std::exception&) {
        marks_ = {};  // broken marks must not block playing
    }
    emit marksChanged();
}

namespace {

QString Secs(int64_t ms) { return QString::number(ms / 1000.0, 'f', 1) + QLatin1Char('s'); }

QString CleanName(const QString& name, const QString& fallback) {
    const QString n = name.trimmed().left(80);
    return n.isEmpty() ? fallback : n;
}

}  // namespace

QVariantList PlayerController::chapters() const {
    QVariantList out;
    for (const auto& c : marks_.chapters) {
        const QString name = c.name.empty() ? tr("Chapter at %1").arg(formatTime(c.start_ms)) : q(c.name);
        const QString span = c.end_ms > c.start_ms ? Secs(c.start_ms) + QStringLiteral(" - ") + Secs(c.end_ms)
                                                   : Secs(c.start_ms) + QStringLiteral(" → end");
        out.append(QVariantMap{{"name", name},
                               {"startMs", qint64(c.start_ms)},
                               {"endMs", qint64(c.end_ms)},
                               {"label", name + QStringLiteral(" · ") + span}});
    }
    return out;
}

QVariantList PlayerController::chapterBands() const {
    QVariantList out;
    for (const auto& r : samn::ChapterRanges(marks_.chapters, scriptDurationMs())) {
        const QString name = q(r.name);
        out.append(QVariantMap{{"name", name},
                               {"startMs", qint64(r.start_ms)},
                               {"endMs", qint64(r.end_ms)},
                               {"index", r.index},
                               {"tip", tr("%1 · %2–%3 — click to seek").arg(name, formatTime(r.start_ms), formatTime(r.end_ms))}});
    }
    return out;
}

QVariantList PlayerController::bookmarks() const {
    QVariantList out;
    for (const auto& b : marks_.bookmarks)
        out.append(QVariantMap{{"name", q(b.name)},
                               {"timeMs", qint64(b.time_ms)},
                               {"label", q(b.name) + QStringLiteral(" · ") + Secs(b.time_ms)}});
    return out;
}

QVariantList PlayerController::oMarkers() const {
    QVariantList out;
    for (const auto& m : marks_.o_markers) {
        const bool primary = m.kind == samn::OMarkerPrimary;
        QString label = (primary ? tr("Primary") : tr("Secondary")) + QStringLiteral(": ") + Secs(m.start_ms) +
                        QStringLiteral(" - ") + Secs(m.end_ms);
        if (!primary) label += QStringLiteral(" · %1%").arg(std::lround(m.intensity * 100));
        out.append(QVariantMap{{"kind", q(m.kind)},
                               {"startMs", qint64(m.start_ms)},
                               {"endMs", qint64(m.end_ms)},
                               {"intensity", m.intensity},
                               {"label", label}});
    }
    return out;
}

template <typename T, typename Save>
bool PlayerController::commitMarks(std::vector<T>& list, std::vector<T> next, Save save) {
    if (scriptPath_.isEmpty()) {
        setError(tr("Open a script first."));
        return false;
    }
    try {
        save(std::filesystem::u8path(u8(scriptPath_)), next);
    } catch (const std::exception& e) {
        setError(tr("Could not save: %1").arg(q(e.what())));
        return false;  // the old list stays (0.7: rollback)
    }
    list = std::move(next);
    emit marksChanged();
    emit scriptEdited(scriptPath_);
    pushMarks();
    return true;
}

bool PlayerController::addBookmark(const QString& name, qint64 videoMs) {
    auto next = marks_.bookmarks;
    next.push_back({u8(CleanName(name, tr("Bookmark"))), std::max<int64_t>(0, samn::script_time(videoMs, offsetMs_))});
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.time_ms < b.time_ms; });
    return commitMarks(marks_.bookmarks, std::move(next), samn::SaveScriptBookmarks);
}

bool PlayerController::renameBookmark(int index, const QString& name) {
    if (index < 0 || index >= int(marks_.bookmarks.size())) return false;
    auto next = marks_.bookmarks;
    next[size_t(index)].name = u8(CleanName(name, tr("Bookmark")));
    return commitMarks(marks_.bookmarks, std::move(next), samn::SaveScriptBookmarks);
}

bool PlayerController::removeBookmark(int index) {
    if (index < 0 || index >= int(marks_.bookmarks.size())) return false;
    auto next = marks_.bookmarks;
    next.erase(next.begin() + index);
    return commitMarks(marks_.bookmarks, std::move(next), samn::SaveScriptBookmarks);
}

qint64 PlayerController::bookmarkVideoMs(int index) const {
    if (index < 0 || index >= int(marks_.bookmarks.size())) return -1;
    return std::max<qint64>(0, marks_.bookmarks[size_t(index)].time_ms + offsetMs_);
}

bool PlayerController::addChapterFromSelection(const QString& name) {
    if (!marker_.Valid()) {
        setError(tr("Mark a range on the heatmap first."));
        return false;
    }
    auto next = marks_.chapters;
    next.push_back({u8(CleanName(name, tr("Chapter"))), marker_.start_ms, marker_.end_ms});
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.start_ms < b.start_ms; });
    return commitMarks(marks_.chapters, std::move(next), samn::SaveScriptChapters);
}

bool PlayerController::addSceneMarker(qint64 videoMs) {
    if (scriptPath_.isEmpty()) {
        setError(tr("Load a script first."));
        return false;
    }
    auto next = samn::AddSceneMarker(marks_.chapters, samn::script_time(videoMs, offsetMs_));
    if (!next) {
        setError(tr("A scene marker is already here."));
        return false;
    }
    return commitMarks(marks_.chapters, std::move(*next), samn::SaveScriptChapters);
}

bool PlayerController::renameChapter(int index, const QString& name) {
    if (index < 0 || index >= int(marks_.chapters.size())) return false;
    auto next = marks_.chapters;
    next[size_t(index)].name = u8(CleanName(name, tr("Chapter")));
    return commitMarks(marks_.chapters, std::move(next), samn::SaveScriptChapters);
}

bool PlayerController::removeChapter(int index) {
    if (index < 0 || index >= int(marks_.chapters.size())) return false;
    auto next = marks_.chapters;
    next.erase(next.begin() + index);
    return commitMarks(marks_.chapters, std::move(next), samn::SaveScriptChapters);
}

bool PlayerController::addOMarkerFromSelection(const QString& kind) {
    if (!marker_.Valid()) {
        setError(tr("Mark a range on the heatmap first."));
        return false;
    }
    samn::OMarker m;
    m.start_ms = marker_.start_ms;
    m.end_ms = marker_.end_ms;
    const bool primary = kind != QLatin1String("secondary");
    m.kind = primary ? samn::OMarkerPrimary : samn::OMarkerSecondary;
    m.intensity = primary ? 1.0 : oMarkerIntensity_;
    auto next = marks_.o_markers;
    next.push_back(m);
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.start_ms < b.start_ms; });
    return commitMarks(marks_.o_markers, std::move(next), samn::SaveScriptOMarkers);
}

bool PlayerController::removeOMarker(int index) {
    if (index < 0 || index >= int(marks_.o_markers.size())) return false;
    auto next = marks_.o_markers;
    next.erase(next.begin() + index);
    return commitMarks(marks_.o_markers, std::move(next), samn::SaveScriptOMarkers);
}

void PlayerController::setOMarkerIntensity(double v) {
    v = std::clamp(v, 0.0, 1.0);
    if (v == oMarkerIntensity_) return;
    oMarkerIntensity_ = v;
    try {
        settings().set("playback.omarker_intensity", v);
    } catch (const std::exception&) {
    }
    emit marksChanged();
}

void PlayerController::setSpeedHighlights(bool on) {
    if (on == speedHL_) return;
    speedHL_ = on;
    try {
        settings().set("playback.speed_highlights", on);
    } catch (const std::exception&) {
    }
    emit speedHighlightChanged();
    pushMarks();
}

void PlayerController::setSpeedHighlightThresh(double v) {
    // 0.7 speedHighlightThresh(): not a number or <= 0 -> 400, else 50-2000.
    v = (std::isfinite(v) && v > 0) ? std::clamp(v, 50.0, 2000.0) : 400.0;
    if (v == speedHLThresh_) return;
    speedHLThresh_ = v;
    try {
        settings().set("playback.speed_highlight_thresh", v);
    } catch (const std::exception&) {
    }
    emit speedHighlightChanged();
    pushMarks();
}

void PlayerController::setBpm(double v) {
    v = (v >= 20 && v <= 240) ? v : 0;  // 0.7: 20-240, else none
    if (v == bpm_) return;
    bpm_ = v;
    emit bpmChanged();
    pushMarks();
}

void PlayerController::setBpmGrid(bool on) {
    if (on == bpmGrid_) return;
    bpmGrid_ = on;
    emit bpmChanged();
    pushMarks();
}

bool PlayerController::attachVideo(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    if (!QFileInfo(path).isFile()) {
        setError(tr("Video not found: %1").arg(path));
        return false;
    }
    if (scriptPath_.isEmpty()) return openPath(path);
    setVideo(path);
    setError({});
    return true;
}

QString PlayerController::snapshotName(qint64 videoMs) const {
    const qint64 sec = std::max<qint64>(0, videoMs) / 1000;
    return QStringLiteral("%1_%2-%3.png")
        .arg(QFileInfo(videoPath_).completeBaseName())
        .arg(sec / 60, 2, 10, QLatin1Char('0'))
        .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

QString PlayerController::snapshotFolder() const {
    return videoPath_.isEmpty() ? QString() : QUrl::fromLocalFile(QFileInfo(videoPath_).absolutePath()).toString();
}

bool PlayerController::saveSnapshot(QObject* videoSink, const QString& pathOrUrl) {
    auto* sink = qobject_cast<QVideoSink*>(videoSink);
    const QImage img = sink ? sink->videoFrame().toImage() : QImage();
    if (img.isNull()) {
        setError(tr("Snapshot failed: no video frame yet."));
        return false;
    }
    QString path = toLocalPath(pathOrUrl);
    if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
    if (!img.save(path, "PNG")) {
        setError(tr("Snapshot failed: cannot write %1").arg(path));
        return false;
    }
    setError({});
    return true;
}

QVariant PlayerController::uiValue(const QString& key, const QVariant& fallback) {
    try {
        const auto v = settings().get("ui." + u8(key), nlohmann::json());
        if (v.is_string()) return q(v.get<std::string>());
        if (v.is_boolean()) return v.get<bool>();
        if (v.is_number_integer()) return qint64(v.get<int64_t>());
        if (v.is_number()) return v.get<double>();
    } catch (const std::exception&) {
    }
    return fallback;
}

void PlayerController::setUiValue(const QString& key, const QVariant& value) {
    nlohmann::json j;
    switch (value.typeId()) {
    case QMetaType::Bool: j = value.toBool(); break;
    case QMetaType::Int:
    case QMetaType::LongLong: j = value.toLongLong(); break;
    case QMetaType::Double: j = value.toDouble(); break;
    default: j = u8(value.toString());
    }
    try {
        settings().set("ui." + u8(key), j);
    } catch (const std::exception&) {
        // A remembered tab is a convenience; never an error for the user.
    }
}

double PlayerController::beatPeriodMs() const { return bpmGrid_ && bpm_ > 0 ? 60000.0 / bpm_ : 0.0; }

void PlayerController::clearScript() {
    stroke_.clear();
    vibration_.clear();
    suction_.clear();
    heat_.clear();
    marks_ = {};
    emit marksChanged();
    scriptPath_.clear();
    playheadMs_ = -1;
    marker_ = {};
    emit markerChanged();
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
    if (!path.isEmpty()) emit videoLinked(path);
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
    if (!loopActive()) return -1;
    if (samn::script_time(videoMs, offsetMs_) < marker_.end_ms) return -1;
    return std::max<qint64>(0, marker_.start_ms + offsetMs_);
}

void PlayerController::setLoopStart(qint64 videoMs) {
    const int64_t t = std::max<int64_t>(0, samn::script_time(videoMs, offsetMs_));
    if (marker_.Valid() && t < marker_.end_ms) {
        pendingStart_.reset();
        setMarker(double(t), double(marker_.end_ms));
    } else {
        pendingStart_ = t;  // waits for B
    }
}

void PlayerController::setLoopEnd(qint64 videoMs) {
    const int64_t t = samn::script_time(videoMs, offsetMs_);
    const int64_t start = pendingStart_ ? *pendingStart_ : marker_.Valid() ? marker_.start_ms : 0;
    if (t <= start) return;
    pendingStart_.reset();
    setMarker(double(start), double(t));
}

void PlayerController::setLoopEnabled(bool on) {
    if (on == loopEnabled_) return;
    loopEnabled_ = on;
    emit loopChanged();
}

void PlayerController::setMarker(double startMs, double endMs) {
    samn::RangeMarker m{int64_t(std::llround(std::min(startMs, endMs))), int64_t(std::llround(std::max(startMs, endMs)))};
    m.start_ms = std::max<int64_t>(0, m.start_ms);
    if (!m.Valid()) m = {};
    if (m == marker_) return;
    marker_ = m;
    if (!scriptPath_.isEmpty()) {
        try {
            samn::SaveMarker(std::filesystem::u8path(u8(scriptPath_)), marker_);
        } catch (const std::exception& e) {
            setError(tr("Could not save the marked range: %1").arg(q(e.what())));
        }
    }
    emit markerChanged();
    emit loopChanged();
}

void PlayerController::clearMarker() { setMarker(0, 0); }

void PlayerController::zoomToSelection() {
    if (!marker_.Valid()) return;
    setView(samn::ZoomToRange(marker_.start_ms, marker_.end_ms, timelineMs()));
}

QString PlayerController::markerLabel() const {
    if (!marker_.Valid()) return tr("(no selection)");
    return tr("Marked: %1s - %2s").arg(marker_.start_ms / 1000.0, 0, 'f', 1).arg(marker_.end_ms / 1000.0, 0, 'f', 1);
}

double PlayerController::msAtFraction(double fraction) const {
    return double(view_.start_ms) + std::clamp(fraction, 0.0, 1.0) * double(view_.end_ms - view_.start_ms);
}

qint64 PlayerController::finishHeatmapDrag(double startMs, double endMs) {
    if (std::abs(endMs - startMs) > double(samn::kMarkDragMinMs)) {
        setMarker(startMs, endMs);
        return -1;
    }
    return std::max<qint64>(0, qint64(std::llround(startMs)) + offsetMs_);
}

qint64 PlayerController::jumpTenthVideoMs(int n) const {
    const int64_t total = timelineMs();
    return std::max<qint64>(0, qint64(total * std::clamp(n, 0, 10) / 10) + offsetMs_);
}

int PlayerController::strokeNow() const {
    if (playheadMs_ < 0) return 0;
    return int(std::lround(samn::PositionAt(stroke_, playheadMs_).value_or(0)));
}

bool PlayerController::audioLane() const {
    return const_cast<PlayerController*>(this)->uiValue(QStringLiteral("player.audioLane"), true).toBool();
}

void PlayerController::setAudioLane(bool on) {
    if (on == audioLane()) return;
    setUiValue(QStringLiteral("player.audioLane"), on);
    emit viewOptionsChanged();
}

bool PlayerController::posOverlay() const {
    return const_cast<PlayerController*>(this)->uiValue(QStringLiteral("player.posOverlay"), true).toBool();
}

void PlayerController::setPosOverlay(bool on) {
    if (on == posOverlay()) return;
    setUiValue(QStringLiteral("player.posOverlay"), on);
    emit viewOptionsChanged();
}

int PlayerController::vibrationNow() const {
    if (playheadMs_ < 0) return 0;
    return int(std::lround(samn::PositionAt(vibration_, playheadMs_).value_or(0)));
}

int PlayerController::suctionNow() const {
    if (playheadMs_ < 0) return 0;
    return int(std::lround(samn::PositionAt(suction_, playheadMs_).value_or(0)));
}

QString PlayerController::heatTooltip(double ms) const {
    if (heat_.empty() || stroke_.empty()) return {};
    const double span = std::max<double>(1, double(scriptDurationMs()));
    const auto i = std::clamp<long long>(std::llround(ms / span * double(heat_.size() - 1)), 0,
                                         (long long)heat_.size() - 1);
    return q(samn::FormatTooltipTime(int64_t(ms))) + QStringLiteral(" · %1%").arg(std::lround(heat_[size_t(i)] * 100));
}

void PlayerController::setSleepMinutes(int minutes) {
    minutes = std::clamp(minutes, 0, 24 * 60);
    if (minutes == sleepMinutes_) return;
    sleepMinutes_ = minutes;
    playedMs_ = 0;  // a new choice counts from now (0.7 re-arms on change)
    if (playClock_.isValid()) playClock_.restart();
    emit sleepChanged();
}

void PlayerController::setPlaying(bool playing) {
    if (playing) {
        if (!playClock_.isValid()) playClock_.start();
        if (!sleepTimer_.isActive()) sleepTimer_.start();
    } else {
        if (playClock_.isValid()) playedMs_ += playClock_.elapsed();
        playClock_.invalidate();
        sleepTimer_.stop();
    }
}


qint64 PlayerController::chapterVideoMs(int index) const {
    if (index < 0 || index >= static_cast<int>(marks_.chapters.size())) return -1;
    // Script time → video time: the offset moves the script later.
    return marks_.chapters[size_t(index)].start_ms + offsetMs_;
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
        c->setPlayheadMs(playheadMs_);
    }
    if (heatmap_) {
        heatmap_->setValues(heat_, 0, scriptDurationMs());
        heatmap_->setPlayheadMs(playheadMs_);
    }
    pushMarks();
}

void PlayerController::pushMarks() {
    std::vector<int64_t> bookmarks;
    for (const auto& b : marks_.bookmarks) bookmarks.push_back(b.time_ms);
    std::vector<std::pair<int64_t, int64_t>> chapters;
    for (const auto& c : marks_.chapters) chapters.emplace_back(c.start_ms, c.end_ms);
    std::vector<CurveItem::Band> bands;
    for (const auto& m : marks_.o_markers) bands.push_back({m.start_ms, m.end_ms, m.kind == samn::OMarkerPrimary, m.intensity});
    for (const auto& c : curves_) {
        if (!c) continue;
        const bool stroke = c->lane() == QLatin1String("stroke");
        c->setMarks(stroke ? bookmarks : std::vector<int64_t>{},
                    stroke ? chapters : std::vector<std::pair<int64_t, int64_t>>{});
        c->setOverlays(stroke ? bands : std::vector<CurveItem::Band>{}, stroke ? beatPeriodMs() : 0.0);
        c->setSpeedLimit(stroke && speedHL_ ? speedHLThresh_ : 0.0);
    }
    if (heatmap_) heatmap_->setBands(bands);
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
