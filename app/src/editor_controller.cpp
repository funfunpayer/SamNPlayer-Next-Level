#include "editor_controller.h"

#include <QCoreApplication>
#include <QDir>
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
#include "samn/improve.h"
#include "samn/marks.h"
#include "samn/range_edit.h"
#include "samn/samn_file.h"
#include "samn/scenes.h"
#include "samn/simplify.h"
#include "samn/sound_vibration.h"
#include "samn/beat_strokes.h"
#include "samn/engine/beatbar_video.h"
#include "samn/fileio.h"

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

EditorController::~EditorController() {
    cancelTool();
    if (tool_.joinable()) tool_.join();
}

void EditorController::setProgress(int percent) {
    if (percent == progress_) return;
    progress_ = percent;
    emit progressChanged();
}

void EditorController::cancelTool() {
    if (tool_cancel_) *tool_cancel_ = true;
    if (sound_->busy()) {
        sound_->cancel();
        setBusy(false);
    }
}

void EditorController::setVideoPath(const QString& path) {
    if (path == video_path_) return;
    video_path_ = path;
    emit videoPathChanged();
    loadSceneProposals();
    emit scenesChanged();
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
    std::vector<samn::Chapter> chapters;
    std::vector<samn::SceneFeelRange> scene_ranges;
    std::optional<samn::ContactMarks> contact_marks;
    try {
        std::ifstream in(FsPath(funscript), std::ios::binary);
        if (!in) throw std::runtime_error("cannot read the file");
        std::stringstream ss;
        ss << in.rdbuf();
        axes["stroke"] = samn::Parse(ss.str()).actions;
        try {
            // Scene feels and the contact mark, for the scenes' suggestions.
            const samn::FunscriptView view = samn::ParseFunscript(ss.str());
            contact_marks = view.metadata.contact_marks;
            if (view.metadata.device_recipe) scene_ranges = view.metadata.device_recipe->scene_ranges;
        } catch (const std::exception&) {
        }
        try {
            chapters = samn::LoadChapters(FsPath(funscript));
        } catch (const std::exception&) {
            // Unreadable chapters: the curves still open, with no scenes.
        }
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
            // The Player plays the .samn, so its scenes count.
            if (!doc.chapters.empty()) chapters = doc.chapters;
            scene_ranges = doc.recipe.scene_ranges;
            if (doc.contact_marks) contact_marks = doc.contact_marks;
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
    std::stable_sort(chapters.begin(), chapters.end(),
                     [](const samn::Chapter& a, const samn::Chapter& b) { return a.start_ms < b.start_ms; });
    chapters_ = std::move(chapters);
    scene_ranges_ = std::move(scene_ranges);
    contact_marks_ = std::move(contact_marks);
    loadSceneProposals();
    emit scenesChanged();
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
    chapters_.clear();
    scene_ranges_.clear();
    contact_marks_.reset();
    pushCurves();
    emit scriptChanged();
    emit scenesChanged();
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

QString EditorController::optimizeFor(const QString& target) {
    if (!hasScript() || drag_index_ >= 0 || busy_) return {};
    const bool neo2 = target == QLatin1String("neo2");
    if (!neo2 && target != QLatin1String("general")) return {};
    const std::vector<samn::Action>& stroke = session_.Axis("stroke");
    if (stroke.size() < 2) {
        setStatus(tr("The stroke needs at least 2 points."));
        return {};
    }
    samn::ImproveOptions o;
    o.fill_gaps = true;
    o.heal_tracking_gaps = true;
    try {
        o.tracking_gaps = samn::LoadFunscript(FsPath(path_)).metadata.tracking_gaps;
    } catch (const std::exception&) {
        // No metadata to read: fill only.
    }
    const QString label = neo2 ? tr("Optimize for Sam Neo 2") : tr("Optimize for any device");
    QStringList parts{neo2 ? tr("Neo 2 ready") : tr("Ready for any device")};
    try {
        const samn::ImproveResult res = samn::ImproveScript(stroke, o);
        const bool strokeChanged = res.points_added > 0 || res.windows_healed > 0 || res.after_count != res.before_count;
        session_.Begin(label.toStdString());
        if (strokeChanged) session_.Replace("stroke", res.actions, {});
        if (res.windows_healed > 0) parts << tr("healed %n tracking gap(s)", "", res.windows_healed);
        if (res.gaps_filled > 0) parts << tr("filled %n gap(s) (+%1 points)", "", res.gaps_filled).arg(res.points_added);
        else if (!neo2) parts << tr("no gaps to fill");
        if (neo2) {
            samn::Document doc = companion();
            doc.general = strokeChanged ? res.actions : session_.Axis("stroke");
            doc.ApplyContactRecipe(true, samn::DefaultContactVibrationSpan, "soft");
            doc.BakeNeoAxes();
            if (samn_path_.isEmpty()) {
                samn_path_ = QString::fromStdString(samn::CompanionSamnPath(path_.toStdString()));
                emit scriptChanged();
            }
            session_.Replace("vibration", doc.vibration, {});
            session_.Replace("suction", doc.suction, {});
            parts << tr("contact on") << tr("vibration and suction written");
        }
        if (!session_.Commit()) {
            setStatus(parts.join(QStringLiteral(" · ")));
            return parts.join(QStringLiteral(" · "));
        }
    } catch (const std::exception& e) {
        session_.Rollback();
        setStatus(tr("%1: %2").arg(label, QString::fromUtf8(e.what())));
        return {};
    }
    pushCurves();
    const QString msg = parts.join(QStringLiteral(" · "));
    if (stepDone()) setStatus(msg);
    return msg;
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

bool EditorController::strokeFromBeatBar() {
    if (busy_ || drag_index_ >= 0) return false;
    if (video_path_.isEmpty()) {
        setStatus(tr("No video: the beat bar is read from the video."));
        return false;
    }
    if (tool_.joinable()) tool_.join();  // the last run has ended (busy_ is false)
    tool_cancel_ = std::make_shared<std::atomic<bool>>(false);
    setBusy(true);
    setProgress(0);
    setStatus(tr("Looking for a beat bar…"));
    QPointer<EditorController> self(this);
    tool_ = std::thread([self, cancel = tool_cancel_, video = FsPath(video_path_)] {
        samn::engine::BeatBarOptions o;
        o.cancel = [cancel] { return cancel->load(); };
        o.progress = [self](int p) {
            QMetaObject::invokeMethod(qApp, [self, p] { if (self) self->setProgress(p); }, Qt::QueuedConnection);
        };
        std::string error;
        std::optional<samn::engine::BeatBarResult> r;
        try {
            r = samn::engine::DetectBeatBarBeats(video, o, &error);
        } catch (const std::exception& e) {
            error = e.what();
        }
        const bool found = r && r->found;
        std::vector<double> beats = r ? r->beats_ms : std::vector<double>{};
        const double bpm = r ? r->bpm : 0;
        const QString err = r ? QString() : QString::fromStdString(error);
        QMetaObject::invokeMethod(qApp, [self, found, beats = std::move(beats), bpm, err] {
            if (self) self->onBeats(found, beats, bpm, err);
        }, Qt::QueuedConnection);
    });
    return true;
}

void EditorController::onBeats(bool found, const std::vector<double>& beats, double bpm, const QString& error) {
    setBusy(false);
    setProgress(found ? 100 : 0);
    if (!error.isEmpty()) {
        setStatus(error == QLatin1String("canceled") ? tr("Beat bar: canceled.") : tr("Beat bar: %1").arg(error));
        return;
    }
    if (!found || beats.size() < 2) {
        setStatus(tr("No beat bar found in this video."));
        return;
    }
    std::vector<samn::Action> stroke = samn::BeatStrokes(beats, {});
    if (stroke.size() < 2) {
        setStatus(tr("Beat bar: too few beats for a stroke."));
        return;
    }
    if (!hasScript()) {
        // Open the video's script, or write a new one, beside the video.
        const QFileInfo v(video_path_);
        const QString script = v.dir().filePath(v.completeBaseName() + QStringLiteral(".funscript"));
        if (!QFileInfo::exists(script)) {
            samn::Script s;
            s.actions = stroke;
            try {
                samn::WriteFileAtomic(FsPath(script), samn::Write(s));
            } catch (const std::exception& e) {
                setStatus(tr("Beat bar: cannot write %1: %2").arg(QFileInfo(script).fileName(), QString::fromUtf8(e.what())));
                return;
            }
            open(script);
            setStatus(tr("Beat bar: %1 beats (about %2 BPM), new script %3.")
                          .arg(beats.size()).arg(bpm, 0, 'f', 0).arg(QFileInfo(script).fileName()));
            return;
        }
        if (!open(script)) return;
    }
    if (drag_index_ >= 0) release();
    session_.Replace("stroke", std::move(stroke), "stroke from the beat bar");
    pushCurves();
    setView(samn::FullView(timelineMs()));
    if (stepDone())
        setStatus(tr("Beat bar: %1 beats (about %2 BPM) written as the stroke.").arg(beats.size()).arg(bpm, 0, 'f', 0));
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

// ---- Scenes (D-145) ----

void EditorController::loadSceneProposals() {
    proposals_.clear();
    if (video_path_.isEmpty()) return;
    const std::filesystem::path p = samn::SceneProposalsPath(FsPath(video_path_));
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return;
    if (auto loaded = samn::LoadSceneProposals(p)) proposals_ = std::move(*loaded);
}

std::vector<samn::ChapterScene> EditorController::chapterScenes() const {
    return samn::ChapterScenes(chapters_, timelineMs(), scene_ranges_, proposals_, contact_marks_);
}

QVariantList EditorController::scenes() const {
    QVariantList out;
    if (!hasScript()) return out;
    for (const samn::ChapterScene& s : chapterScenes())
        out << QVariantMap{{"index", s.index},
                           {"name", QString::fromStdString(s.name)},
                           {"startMs", double(s.start_ms)},
                           {"endMs", double(s.end_ms)},
                           {"feel", QString::fromStdString(s.feel)},
                           {"hasFeel", s.has_feel},
                           {"suggested", QString::fromStdString(s.suggested)},
                           {"source", QString::fromStdString(s.source)},
                           {"confidence", s.confidence}};
    return out;
}

QStringList EditorController::sceneFeels() const {
    QStringList out;
    for (const std::string& f : samn::SceneFeelNames()) out << QString::fromStdString(f);
    return out;
}

bool EditorController::hasSceneSuggestions() const {
    if (!hasScript()) return false;
    for (const samn::ChapterScene& s : chapterScenes())
        if (!s.suggested.empty() && (!s.has_feel || s.feel != s.suggested)) return true;
    return false;
}

bool EditorController::saveScenes(std::vector<samn::Chapter> chapters, std::vector<samn::SceneFeelRange> ranges) {
    std::stable_sort(chapters.begin(), chapters.end(),
                     [](const samn::Chapter& a, const samn::Chapter& b) { return a.start_ms < b.start_ms; });
    // Feels belong to a scene's exact range; one whose scene moved or went
    // is dropped (0.7 kept only the ranges of the current scenes).
    std::vector<samn::SceneFeelRange> kept;
    for (const samn::ChapterRange& r : samn::ChapterRanges(chapters, timelineMs()))
        for (const samn::SceneFeelRange& f : ranges)
            if (f.start_ms == r.start_ms && f.end_ms == r.end_ms) {
                kept.push_back(f);
                break;
            }
    try {
        samn::SaveChapters(FsPath(path_), chapters);
        const bool needs_samn = !kept.empty() || !scene_ranges_.empty();
        if (!samn_path_.isEmpty() || needs_samn) {
            samn::Document doc = companion();
            doc.general = session_.Axis("stroke");
            doc.chapters = chapters;
            doc.recipe.scene_ranges = kept;
            if (samn_path_.isEmpty()) {
                samn_path_ = QString::fromStdString(samn::CompanionSamnPath(path_.toStdString()));
                emit scriptChanged();
            }
            samn::SaveDocument(FsPath(samn_path_), doc);
        }
    } catch (const std::exception& e) {
        setStatus(tr("Scenes not saved: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    chapters_ = std::move(chapters);
    scene_ranges_ = std::move(kept);
    emit scenesChanged();
    return true;
}

bool EditorController::addScene(double ms) {
    if (!hasScript()) return false;
    const auto next = samn::AddSceneMarker(chapters_, std::llround(std::max(0.0, ms)));
    if (!next) {
        setStatus(tr("A scene already starts here."));
        return false;
    }
    if (!saveScenes(*next, scene_ranges_)) return false;
    setStatus(tr("Scene added at %1.").arg(QString::number(ms / 1000.0, 'f', 1) + QStringLiteral(" s")));
    return true;
}

bool EditorController::renameScene(int index, const QString& name) {
    if (index < 0 || index >= int(chapters_.size())) return false;
    auto next = chapters_;
    next[size_t(index)].name = name.trimmed().toStdString();
    return saveScenes(std::move(next), scene_ranges_);
}

bool EditorController::deleteScene(int index) {
    if (index < 0 || index >= int(chapters_.size())) return false;
    auto next = chapters_;
    next.erase(next.begin() + index);
    return saveScenes(std::move(next), scene_ranges_);
}

bool EditorController::setSceneFeel(int index, const QString& feel) {
    const std::string f = feel.toStdString();
    if (!f.empty() && samn::NormalizeSceneFeel(f).empty()) return false;
    const auto scenes = chapterScenes();
    if (index < 0 || index >= int(scenes.size())) return false;
    auto ranges = samn::SceneRangesOf(scenes);
    const samn::ChapterScene& s = scenes[size_t(index)];
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                                [&](const samn::SceneFeelRange& r) { return r.start_ms == s.start_ms && r.end_ms == s.end_ms; }),
                 ranges.end());
    ranges.push_back({s.start_ms, s.end_ms, samn::NormalizeSceneFeel(f)});
    if (!saveScenes(chapters_, std::move(ranges))) return false;
    // A script that plays its written lanes keeps them: the feel shapes the
    // recipe; "Vibration and suction from the stroke" writes it into the lanes.
    if (!session_.Axis("vibration").empty() || !session_.Axis("suction").empty())
        setStatus(tr("Scene feel saved. The script plays its written lanes; rebuild them with "
                     "\"Vibration and suction from the stroke\" to feel it."));
    return true;
}

bool EditorController::clearSceneFeel(int index) {
    const auto scenes = chapterScenes();
    if (index < 0 || index >= int(scenes.size()) || !scenes[size_t(index)].has_feel) return false;
    auto copy = scenes;
    copy[size_t(index)].has_feel = false;
    return saveScenes(chapters_, samn::SceneRangesOf(copy));
}

int EditorController::applySceneSuggestions() {
    auto scenes = chapterScenes();
    int changed = 0;
    for (samn::ChapterScene& s : scenes)
        if (!s.suggested.empty() && (!s.has_feel || s.feel != s.suggested)) {
            s.feel = s.suggested;
            s.has_feel = true;
            ++changed;
        }
    if (changed == 0 || !saveScenes(chapters_, samn::SceneRangesOf(scenes))) return 0;
    setStatus(tr("%1 scene feel(s) applied.").arg(changed));
    return changed;
}

}  // namespace samn::ui

