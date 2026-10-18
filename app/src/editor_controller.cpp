#include "editor_controller.h"

#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "audio_level.h"
#include "curve_item.h"
#include "samn/funscript.h"
#include "samn/marks.h"
#include "samn/range_edit.h"
#include "samn/samn_file.h"
#include "samn/simplify.h"
#include "samn/sound_vibration.h"

namespace samn::ui {
namespace {

std::filesystem::path FsPath(const QString& p) { return std::filesystem::path(p.toStdU16String()); }

QString LaneTitle(const std::string& lane) {
    if (lane == "vibration") return QObject::tr("Vibration");
    if (lane == "suction") return QObject::tr("Suction");
    return QObject::tr("Stroke");
}

}  // namespace

EditorController::EditorController(QObject* parent) : QObject(parent), sound_(new AudioLevelDecoder(this)) {
    connect(sound_, &AudioLevelDecoder::finished, this, &EditorController::onSoundEnvelope);
    connect(sound_, &AudioLevelDecoder::failed, this, [this](const QString& error) {
        setBusy(false);
        setStatus(tr("Vibration from the sound: %1").arg(error));
    });
}

void EditorController::setVideoPath(const QString& path) {
    if (path == video_path_) return;
    video_path_ = path;
    emit videoPathChanged();
}

void EditorController::setBusy(bool busy) {
    if (busy == busy_) return;
    busy_ = busy;
    emit busyChanged();
}

QString EditorController::name() const { return QFileInfo(path_).fileName(); }

QStringList EditorController::lanes() const {
    QStringList out;
    for (const char* lane : {"stroke", "vibration", "suction"})
        if (!session_.Axis(lane).empty()) out << QString::fromLatin1(lane);
    return out;
}

int EditorController::pointCount() const { return int(session_.Axis(active_lane_.toStdString()).size()); }

void EditorController::setDurationMs(double ms) {
    ms = std::max(0.0, ms);
    if (ms == duration_ms_) return;
    duration_ms_ = ms;
    emit durationChanged();
    setView(samn::FullView(timelineMs()));
}

void EditorController::setActiveLane(const QString& lane) {
    if (lane == active_lane_) return;
    active_lane_ = lane;
    emit activeLaneChanged();
    emit curvesChanged();
}

QString EditorController::undoText() const {
    return canUndo() ? tr("Undo: %1").arg(QString::fromStdString(session_.UndoLabel())) : tr("Nothing to undo");
}

QString EditorController::redoText() const {
    return canRedo() ? tr("Redo: %1").arg(QString::fromStdString(session_.RedoLabel())) : tr("Nothing to redo");
}

bool EditorController::zoomed() const { return samn::IsZoomed(view_, timelineMs()); }

QString EditorController::markerText() const {
    if (!hasMarker()) return tr("No range marked");
    return tr("%1 s – %2 s").arg(marker_start_ / 1000.0, 0, 'f', 1).arg(marker_end_ / 1000.0, 0, 'f', 1);
}

void EditorController::setFpsSnap(double fps) {
    fps = std::clamp(fps, 0.0, 240.0);
    if (fps == fps_snap_) return;
    fps_snap_ = fps;
    emit fpsSnapChanged();
}

void EditorController::setStatus(const QString& s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged();
}

int64_t EditorController::timelineMs() const {
    int64_t last = 0;
    for (const auto& [lane, actions] : session_.axes())
        if (!actions.empty()) last = std::max(last, actions.back().at);
    return std::max<int64_t>(int64_t(duration_ms_), last);
}

int64_t EditorController::snap(double ms) const {
    const auto t = int64_t(std::llround(std::max(0.0, ms)));
    return fps_snap_ > 0 ? samn::SnapMs(t, fps_snap_) : t;
}

bool EditorController::open(const QString& pathIn) {
    QString path = pathIn;
    if (path.startsWith(QStringLiteral("file:"))) path = QUrl(path).toLocalFile();
    QString funscript = path;
    if (path.endsWith(QStringLiteral(".samn"), Qt::CaseInsensitive)) {
        funscript = path.left(path.size() - 5) + QStringLiteral(".funscript");
    }
    samn::EditSession::Axes axes;
    try {
        std::ifstream in(FsPath(funscript), std::ios::binary);
        if (!in) throw std::runtime_error("cannot read the file");
        std::stringstream ss;
        ss << in.rdbuf();
        axes["stroke"] = samn::Parse(ss.str()).actions;
    } catch (const std::exception& e) {
        setStatus(tr("Cannot open %1: %2").arg(QFileInfo(funscript).fileName(), QString::fromUtf8(e.what())));
        return false;
    }
    QString samn_path = QString::fromStdString(samn::CompanionSamnPath(funscript.toStdString()));
    QString warning;
    std::error_code ec;
    if (std::filesystem::exists(FsPath(samn_path), ec)) {
        try {
            const samn::Document doc = samn::LoadDocument(FsPath(samn_path));
            if (!doc.vibration.empty()) axes["vibration"] = doc.vibration;
            if (!doc.suction.empty()) axes["suction"] = doc.suction;
        } catch (const std::exception& e) {
            // The stroke still opens; the companion is left alone.
            warning = tr("The .samn companion could not be read (%1); editing the stroke only.")
                          .arg(QString::fromUtf8(e.what()));
            samn_path.clear();
        }
    } else {
        samn_path.clear();
    }
    session_ = samn::EditSession(std::move(axes));
    path_ = funscript;
    samn_path_ = samn_path;
    active_lane_ = QStringLiteral("stroke");
    marker_start_ = marker_end_ = 0;
    drag_index_ = -1;
    pushCurves();
    setView(samn::FullView(timelineMs()));
    emit scriptChanged();
    emit activeLaneChanged();
    emit markerChanged();
    emit historyChanged();
    setStatus(!warning.isEmpty()
                  ? warning
                  : tr("%1 points · drag a point, click to add, double-click to remove").arg(pointCount()));
    return true;
}

void EditorController::close() {
    if (drag_index_ >= 0) release();
    session_ = samn::EditSession();
    path_.clear();
    samn_path_.clear();
    marker_start_ = marker_end_ = 0;
    pushCurves();
    emit scriptChanged();
    emit markerChanged();
    emit historyChanged();
    setStatus({});
}

void EditorController::attachCurve(CurveItem* curve) {
    if (!curve) return;
    curves_.erase(std::remove_if(curves_.begin(), curves_.end(), [](const QPointer<CurveItem>& c) { return c.isNull(); }),
                  curves_.end());
    curves_.emplace_back(curve);
    curve->setActions(session_.Axis(laneKey(curve)));
}

std::string EditorController::laneKey(const CurveItem* curve) const {
    return curve ? curve->lane().toStdString() : active_lane_.toStdString();
}

void EditorController::pushCurves() {
    for (const auto& c : curves_)
        if (c) c->setActions(session_.Axis(laneKey(c)));
    emit curvesChanged();
    // Only a new or removed lane rebuilds the lane list (not every drag move).
    if (const QStringList now = lanes(); now != lanes_) {
        lanes_ = now;
        emit lanesChanged();
    }
}

double EditorController::press(CurveItem* curve, double x, double y) {
    if (!curve || !hasScript()) return -1;
    if (drag_index_ >= 0) release();
    drag_lane_ = laneKey(curve);
    drag_curve_ = curve;
    setActiveLane(curve->lane());
    const int hit = curve->hitPoint(x, y);
    if (hit >= 0) {
        session_.Begin("move point");
        drag_index_ = hit;
    } else {
        session_.Begin("add point");
        drag_index_ = int(session_.AddPoint(drag_lane_, snap(curve->msAtX(x)), int(std::lround(curve->posAtY(y)))));
        pushCurves();
    }
    curve->setActiveIndex(drag_index_);
    emit draggingChanged();
    return double(session_.Axis(drag_lane_)[size_t(drag_index_)].at);
}

double EditorController::dragTo(double x, double y) {
    if (drag_index_ < 0 || !drag_curve_) return -1;
    session_.MovePoint(drag_lane_, size_t(drag_index_), snap(drag_curve_->msAtX(x)),
                       int(std::lround(drag_curve_->posAtY(y))), int64_t(duration_ms_));
    pushCurves();
    return double(session_.Axis(drag_lane_)[size_t(drag_index_)].at);
}

void EditorController::release() {
    if (drag_index_ < 0) return;
    drag_index_ = -1;
    if (drag_curve_) drag_curve_->setActiveIndex(-1);
    drag_curve_.clear();
    emit draggingChanged();
    // A click that moved nothing records nothing (0.7).
    if (session_.Commit()) stepDone();
}

bool EditorController::deleteAt(CurveItem* curve, double x, double y) {
    if (!curve || !hasScript()) return false;
    const int hit = curve->hitPoint(x, y);
    if (hit < 0) return false;
    setActiveLane(curve->lane());
    if (!session_.DeletePoint(laneKey(curve), size_t(hit))) {
        setStatus(tr("At least 2 points must remain."));
        return false;
    }
    pushCurves();
    stepDone();
    return true;
}

void EditorController::setMarker(double startMs, double endMs) {
    auto a = int64_t(std::llround(std::max(0.0, std::min(startMs, endMs))));
    auto b = int64_t(std::llround(std::max(0.0, std::max(startMs, endMs))));
    if (a == marker_start_ && b == marker_end_) return;
    marker_start_ = a;
    marker_end_ = b;
    emit markerChanged();
}

void EditorController::clearMarker() { setMarker(0, 0); }

bool EditorController::applyRange(const QString& label,
                                  const std::function<std::vector<samn::Action>(const std::vector<samn::Action>&)>& op,
                                  bool needsMarker) {
    if (!hasScript() || drag_index_ >= 0) return false;
    if (needsMarker && !hasMarker()) {
        setStatus(tr("Mark a range first (drag on the ruler)."));
        return false;
    }
    const std::string lane = active_lane_.toStdString();
    const std::vector<samn::Action>& current = session_.Axis(lane);
    try {
        std::vector<samn::Action> next = op(current);
        session_.Begin(label.toStdString());
        session_.Replace(lane, std::move(next), label.toStdString());
        if (!session_.Commit()) {
            setStatus(tr("%1: nothing to change here.").arg(label));
            return false;
        }
    } catch (const std::exception& e) {
        setStatus(tr("%1: %2").arg(label, QString::fromUtf8(e.what())));
        return false;
    }
    pushCurves();
    if (stepDone()) setStatus(tr("%1 · %2").arg(label, LaneTitle(lane)));
    return true;
}

bool EditorController::deleteRange() {
    return applyRange(tr("Delete range"), [this](const auto& a) { return samn::DeleteRange(a, marker_start_, marker_end_); },
                      true);
}

bool EditorController::capSpeed(double maxIntensity) {
    return applyRange(tr("Cap speed"),
                      [this, maxIntensity](const auto& a) {
                          return samn::CapSpeedRange(a, marker_start_, marker_end_, maxIntensity);
                      },
                      true);
}

bool EditorController::scaleRange(double factor, bool softEdges) {
    return applyRange(tr("Scale range"),
                      [this, factor, softEdges](const auto& a) {
                          return samn::ScaleRangePosFade(a, marker_start_, marker_end_, factor, softEdges);
                      },
                      true);
}

bool EditorController::simplifyRange(double maxError) {
    return applyRange(tr("Simplify range"),
                      [this, maxError](const std::vector<samn::Action>& a) {
                          std::vector<samn::Action> out;
                          out.reserve(a.size());
                          size_t i = 0;
                          while (i < a.size() && a[i].at < marker_start_) out.push_back(a[i++]);
                          const size_t first = i;
                          while (i < a.size() && a[i].at <= marker_end_) ++i;
                          std::vector<samn::SimplifyPoint> pts;
                          pts.reserve(i - first);
                          for (size_t k = first; k < i; ++k) pts.push_back({double(a[k].at), double(a[k].pos)});
                          if (pts.size() > 2) {
                              for (int keep : samn::SimplifyIndices(pts, maxError)) out.push_back(a[first + size_t(keep)]);
                          } else {
                              out.insert(out.end(), a.begin() + std::ptrdiff_t(first), a.begin() + std::ptrdiff_t(i));
                          }
                          out.insert(out.end(), a.begin() + std::ptrdiff_t(i), a.end());
                          return out;
                      },
                      true);
}

bool EditorController::invert() {
    const bool whole = !hasMarker();
    return applyRange(whole ? tr("Invert curve") : tr("Invert range"),
                      [this, whole](std::vector<samn::Action> a) {
                          for (auto& x : a)
                              if (whole || (x.at >= marker_start_ && x.at <= marker_end_)) x.pos = 100 - x.pos;
                          return a;
                      },
                      false);
}

bool EditorController::axesFromStroke() {
    if (!hasScript() || drag_index_ >= 0 || busy_) return false;
    if (session_.Axis("stroke").size() < 2) {
        setStatus(tr("The stroke needs at least 2 points."));
        return false;
    }
    try {
        samn::Document doc = companion();
        doc.general = session_.Axis("stroke");
        doc.BakeNeoAxes();
        if (samn_path_.isEmpty()) {
            samn_path_ = QString::fromStdString(samn::CompanionSamnPath(path_.toStdString()));
            emit scriptChanged();
        }
        session_.Begin("vibration and suction from the stroke");
        session_.Replace("vibration", doc.vibration, {});
        session_.Replace("suction", doc.suction, {});
        if (!session_.Commit()) {
            setStatus(tr("Vibration and suction already follow the stroke."));
            return false;
        }
    } catch (const std::exception& e) {
        session_.Rollback();
        setStatus(tr("Vibration and suction: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    pushCurves();
    if (stepDone())
        setStatus(tr("Vibration and suction written from the stroke (%1 and %2 points).")
                      .arg(session_.Axis("vibration").size())
                      .arg(session_.Axis("suction").size()));
    return true;
}

bool EditorController::vibrationFromSound(bool withContact) {
    if (!hasScript() || drag_index_ >= 0 || busy_) return false;
    if (video_path_.isEmpty()) {
        setStatus(tr("No video: the sound comes from the script's video."));
        return false;
    }
    if (session_.Axis("stroke").size() < 2) {
        setStatus(tr("The stroke needs at least 2 points."));
        return false;
    }
    sound_with_contact_ = withContact;
    setBusy(true);
    setStatus(tr("Reading the sound…"));
    sound_->start(video_path_, int(samn::kSoundVibStepMs));
    return true;
}

void EditorController::onSoundEnvelope(const std::vector<double>& envelope) {
    setBusy(false);
    if (!hasScript()) return;
    if (drag_index_ >= 0) release();
    try {
        samn::Document doc = companion();
        doc.general = session_.Axis("stroke");
        doc.Normalize();
        const auto mapped = samn::ToIntensityCurve(session_.Axis("stroke"), doc.BakeOptions());
        if (!mapped || envelope.empty()) {
            setStatus(tr("Vibration from the sound: the video has no sound to follow."));
            return;
        }
        const auto mode = sound_with_contact_ ? samn::SoundVibrationMode::kSoundContact : samn::SoundVibrationMode::kSound;
        std::vector<samn::Action> vibration = samn::SoundVibrationLane(*mapped, envelope, 0, mode);
        if (samn_path_.isEmpty()) {
            samn_path_ = QString::fromStdString(samn::CompanionSamnPath(path_.toStdString()));
            emit scriptChanged();
        }
        session_.Begin(sound_with_contact_ ? "vibration from the sound and contact" : "vibration from the sound");
        // Suction stays as it plays now: written first when there is no lane yet.
        if (session_.Axis("suction").empty()) session_.Replace("suction", samn::FramesToLane(*mapped, false), {});
        session_.Replace("vibration", std::move(vibration), {});
        if (!session_.Commit()) {
            setStatus(tr("The vibration already follows the sound."));
            return;
        }
    } catch (const std::exception& e) {
        session_.Rollback();
        setStatus(tr("Vibration from the sound: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    pushCurves();
    if (stepDone())
        setStatus(tr("Vibration from the sound written (%1 points).").arg(session_.Axis("vibration").size()));
}

bool EditorController::undo() {
    if (!session_.Undo()) return false;
    pushCurves();
    stepDone();
    return true;
}

bool EditorController::redo() {
    if (!session_.Redo()) return false;
    pushCurves();
    stepDone();
    return true;
}

bool EditorController::stepDone() {
    const bool ok = save();
    emit historyChanged();
    return ok;
}

samn::Document EditorController::companion() const {
    std::error_code ec;
    if (!samn_path_.isEmpty() && std::filesystem::exists(FsPath(samn_path_), ec))
        return samn::LoadDocument(FsPath(samn_path_));
    samn::Document doc;
    doc.version = samn::SamnCurrentVersion;
    doc.kind = samn::SamnKind;
    doc.creator = "SamNPlayer";
    doc.general = session_.Axis("stroke");
    return doc;
}

bool EditorController::save() {
    try {
        samn::PatchActions(FsPath(path_), session_.Axis("stroke"));
        if (!samn_path_.isEmpty()) {
            samn::Document doc = companion();
            doc.general = session_.Axis("stroke");  // the .samn carries the stroke too
            doc.vibration = session_.Axis("vibration");
            doc.suction = session_.Axis("suction");
            // Written lanes play as they are (0.7: axes mode after a bake).
            // Without lanes (a tool undone) the recipe plays again.
            const char* source = doc.vibration.empty() && doc.suction.empty() ? samn::PlaybackRecipe : samn::PlaybackAxes;
            doc.playback_source = source;
            doc.recipe.playback_source = source;
            samn::SaveDocument(FsPath(samn_path_), doc);
        }
    } catch (const std::exception& e) {
        setStatus(tr("Not saved: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    session_.MarkSaved();
    return true;
}

void EditorController::zoomAt(double factor, double fraction) {
    const double span = double(view_.end_ms - view_.start_ms);
    const double anchor = double(view_.start_ms) + std::clamp(fraction, 0.0, 1.0) * span;
    setView(samn::ZoomView(view_, timelineMs(), factor, anchor));
}

void EditorController::zoomToMarker() {
    if (hasMarker()) setView(samn::ZoomToRange(marker_start_, marker_end_, timelineMs()));
}

void EditorController::showAll() { setView(samn::FullView(timelineMs())); }

void EditorController::setView(samn::ViewWindow v) {
    v = samn::ClampView(v, timelineMs());
    if (v == view_) return;
    view_ = v;
    emit viewChanged();
}

}  // namespace samn::ui
