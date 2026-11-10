#include "player_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "app_log.h"
#include "audio_level.h"
#include "curve_item.h"
#include "heatmap_item.h"
#include "player_internal.h"
#include "qt_paths.h"
#include "samn/audio_autosync.h"
#include "samn/audio_envelope.h"
#include "samn/contact_marks.h"
#include "samn/contact_preview.h"
#include "samn/fileio.h"
#include "samn/heatmap_image.h"
#include "samn/improve.h"
#include "samn/mapper.h"
#include "samn/marks.h"
#include "samn/ozone.h"
#include "samn/playback.h"
#include "samn/playback_mapping.h"
#include "samn/project.h"
#include "samn/range_edit.h"
#include "samn/samn_file.h"
#include "samn/script_finder.h"
#include "samn/script_quality.h"
#include "samn/sound_vibration.h"

namespace samn::ui {

void PlayerController::loadMarks(const QString& path) {
    try {
        marks_ = samn::LoadScriptMarks(std::filesystem::u8path(U8(path)));
    } catch (const std::exception&) {
        marks_ = {};  // broken marks must not block playing
    }
    emit marksChanged();
}

namespace {

QString Secs(int64_t ms) { return QString::number(ms / 1000.0, 'f', 1) + QLatin1Char('s'); }

QVariantMap ZoneMap(const samn::OZoneSuggestion& zone, const QString& error) {
    return {{"ok", zone.ok},
            {"startMs", qint64(zone.start_ms)},
            {"endMs", qint64(zone.end_ms)},
            {"reason", QString::fromStdString(zone.reason)},
            {"mean", zone.mean},
            {"error", error}};
}

QString CleanName(const QString& name, const QString& fallback) {
    const QString n = name.trimmed().left(80);
    return n.isEmpty() ? fallback : n;
}

}  // namespace

QVariantList PlayerController::chapters() const {
    QVariantList out;
    for (const auto& c : marks_.chapters) {
        const QString name = c.name.empty() ? tr("Chapter at %1").arg(formatTime(c.start_ms)) : FromU8(c.name);
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
        const QString name = FromU8(r.name);
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
        out.append(QVariantMap{{"name", FromU8(b.name)},
                               {"timeMs", qint64(b.time_ms)},
                               {"label", FromU8(b.name) + QStringLiteral(" · ") + Secs(b.time_ms)}});
    return out;
}

QVariantList PlayerController::oMarkers() const {
    QVariantList out;
    for (const auto& m : marks_.o_markers) {
        const bool primary = m.kind == samn::OMarkerPrimary;
        QString label = (primary ? tr("Primary") : tr("Secondary")) + QStringLiteral(": ") + Secs(m.start_ms) +
                        QStringLiteral(" - ") + Secs(m.end_ms);
        if (!primary) label += QStringLiteral(" · %1%").arg(std::lround(m.intensity * 100));
        out.append(QVariantMap{{"kind", FromU8(m.kind)},
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
        save(std::filesystem::u8path(U8(scriptPath_)), next);
    } catch (const std::exception& e) {
        setFailure(tr("Could not save: %1").arg(FromU8(e.what())));
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
    next.push_back({U8(CleanName(name, tr("Bookmark"))), std::max<int64_t>(0, samn::script_time(videoMs, offsetMs_))});
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.time_ms < b.time_ms; });
    return commitMarks(marks_.bookmarks, std::move(next), samn::SaveScriptBookmarks);
}

bool PlayerController::renameBookmark(int index, const QString& name) {
    if (index < 0 || index >= int(marks_.bookmarks.size())) return false;
    auto next = marks_.bookmarks;
    next[size_t(index)].name = U8(CleanName(name, tr("Bookmark")));
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
    next.push_back({U8(CleanName(name, tr("Chapter"))), marker_.start_ms, marker_.end_ms});
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
    next[size_t(index)].name = U8(CleanName(name, tr("Chapter")));
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

bool PlayerController::addOMarkerAt(qint64 videoMs) {
    if (scriptPath_.isEmpty()) {
        setError(tr("Open a script first."));
        return false;
    }
    samn::OMarker m;
    m.start_ms = std::max<int64_t>(0, samn::script_time(videoMs, offsetMs_));
    m.end_ms = m.start_ms + 4000;
    m.kind = samn::OMarkerPrimary;
    m.intensity = 1.0;
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

QVariantMap PlayerController::applySuggestedOZone() {
    if (scriptPath_.isEmpty() || stroke_.empty()) return ZoneMap({}, tr("no script loaded"));
    const samn::OZoneSuggestion zone = samn::SuggestOZone(stroke_);
    if (!zone.ok) return ZoneMap(zone, {});  // a suggestion only; nothing is written
    const auto path = std::filesystem::u8path(U8(scriptPath_));
    try {
        samn::SaveMarker(path, {zone.start_ms, zone.end_ms});
    } catch (const std::exception& e) {
        return ZoneMap(zone, FromU8(e.what()));
    }
    const samn::RangeMarker marked{zone.start_ms, zone.end_ms};
    if (!(marker_ == marked)) {
        marker_ = marked;
        emit markerChanged();
        emit loopChanged();
    }
    // Primary at intensity 1, secondaries at mean/primary clamped 0.2–0.8
    // (AutoOZoneMarkers). Appended; existing O-markers stay. Never inverted.
    auto next = marks_.o_markers;
    const auto added = samn::AutoOZoneMarkers(stroke_);
    next.insert(next.end(), added.begin(), added.end());
    std::stable_sort(next.begin(), next.end(), [](const samn::OMarker& a, const samn::OMarker& b) {
        return a.start_ms < b.start_ms;
    });
    if (!commitMarks(marks_.o_markers, std::move(next), samn::SaveScriptOMarkers)) return ZoneMap(zone, lastError_);
    setError({});
    return ZoneMap(zone, {});
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

}  // namespace samn::ui
