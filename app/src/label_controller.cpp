#include "label_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "frame_cache.h"
#include "samn/vision/frame_reader.h"

namespace samn::ui {

namespace {
QString LocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
std::filesystem::path Fs(const QString& p) { return std::filesystem::path(p.toStdU16String()); }
constexpr size_t kUndoMax = 200;
}  // namespace

LabelController::LabelController(QObject* parent) : QObject(parent) {}

QString LabelController::videoName() const { return QFileInfo(video_path_).fileName(); }

QUrl LabelController::videoUrl() const { return video_path_.isEmpty() ? QUrl() : QUrl::fromLocalFile(video_path_); }

QStringList LabelController::presetLabels() const {
    // 0.7's body parts as classes (one class, several objects), and the
    // contact point where the stroke touches (0.8 teacher plan), and the
    // exclusion zones the tracker must not follow ("not this").
    return {"glans", "penis", "mouth", "hand", "breast", "nipple", "vagina", "face", "contact",
            QString::fromLatin1(samn::kExcludeClass)};
}

QStringList LabelController::labels() const {
    QStringList out;
    for (const auto& t : project_.tracks) out << QString::fromStdString(t.label());
    return out;
}

void LabelController::setCurrentLabel(const QString& label) {
    if (label == current_) return;
    current_ = label;
    emit currentLabelChanged();
}

bool LabelController::openVideo(const QString& pathOrUrl) {
    const QString path = LocalPath(pathOrUrl);
    if (!QFileInfo(path).isFile()) {
        setStatus(tr("Not found: %1").arg(path));
        return false;
    }
    QImageReader image(path);
    if (image.canRead()) {
        still_ = true;
        const QImage img = image.read();
        if (img.isNull()) {
            setStatus(tr("Cannot read the image."));
            return false;
        }
        frame_ = img.convertToFormat(QImage::Format_RGB888);
        video_size_ = frame_.size();
        duration_ms_ = 0;
        frame_ms_ = 40;
    } else {
        std::string error;
        auto reader = vision::FrameReader::Open(Fs(path), &error);
        if (!reader) {
            setStatus(tr("Cannot open the video: %1").arg(QString::fromStdString(error)));
            return false;
        }
        still_ = false;
        video_size_ = QSize(reader->size().width, reader->size().height);
        const double fps = reader->fps() > 0 ? reader->fps() : 25;
        frame_ms_ = 1000.0 / fps;
        duration_ms_ = reader->frameCount() > 0 ? (reader->frameCount() - 1) * frame_ms_ : 0;
    }
    video_path_ = path;
    project_ = {};
    undo_.clear();
    redo_.clear();
    const auto file = samn::LabelProjectPath(Fs(path));
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        try {
            project_ = samn::LoadLabelProject(file);
        } catch (const std::exception& e) {
            setStatus(tr("The labels file could not be read (%1); starting empty.").arg(QString::fromUtf8(e.what())));
        }
    }
    project_.video = QFileInfo(path).fileName().toStdString();
    project_.duration_ms = int64_t(duration_ms_);
    current_ = project_.tracks.empty() ? QString() : QString::fromStdString(project_.tracks.front().label());
    emit videoChanged();
    emit tracksChanged();
    emit currentLabelChanged();
    preview_ms_ = -1;
    seek(0);
    if (status_.isEmpty() || !status_.startsWith(tr("The labels file")))
        setStatus(project_.tracks.empty() ? tr("Add a label, then drag its box on the frame.") : tr("Labels loaded."));
    return true;
}

void LabelController::seek(double ms) {
    if (video_path_.isEmpty()) return;
    if (still_) {
        preview_ms_ = 0;
        emit previewChanged();
        return;
    }
    ms = std::clamp(ms, 0.0, duration_ms_);
    const FrameCache::Frame f = FrameCache::shared().At(video_path_, ms);
    if (!f.image.isNull()) frame_ = f.image;
    preview_ms_ = f.image.isNull() ? ms : std::min(f.ms, duration_ms_);
    emit previewChanged();
}

void LabelController::step(int frames) { seek(preview_ms_ + frames * frame_ms_); }

bool LabelController::addLabel(const QString& cls) {
    const QString name = cls.trimmed().toLower().replace(' ', '_');
    if (name.isEmpty() || !hasVideo()) return false;
    QString added;
    change(tr("Add %1").arg(name), [&](samn::LabelProject& p) {
        added = QString::fromStdString(p.AddObject(name.toStdString()).label());
        return true;
    });
    setCurrentLabel(added);
    return true;
}

bool LabelController::setBox(const QRectF& box) {
    if (current_.isEmpty() || !hasVideo()) return false;
    const QRectF r = box.normalized().intersected(QRectF(QPointF(0, 0), QSizeF(video_size_)));
    if (r.width() < 4 || r.height() < 4) return false;
    bool done = false;
    change(tr("Box for %1").arg(current_), [&](samn::LabelProject& p) {
        samn::LabelKey k;
        k.ms = int64_t(std::llround(preview_ms_));
        k.box = {r.x(), r.y(), r.width(), r.height()};
        k.confirmed = true;
        k.source = "user";
        p.AddTrack(current_.toStdString()).SetKey(k, tolerance());
        return done = true;
    });
    return done;
}

bool LabelController::markAbsent(bool covered) {
    if (current_.isEmpty() || !hasVideo()) return false;
    bool done = false;
    change(covered ? tr("%1 covered").arg(current_) : tr("%1 not in the picture").arg(current_), [&](samn::LabelProject& p) {
        samn::LabelKey k;
        k.ms = int64_t(std::llround(preview_ms_));
        k.absent = true;
        k.covered = covered;
        k.confirmed = true;
        p.AddTrack(current_.toStdString()).SetKey(k, tolerance());
        return done = true;
    });
    return done;
}

bool LabelController::removeKey() {
    if (current_.isEmpty()) return false;
    bool done = false;
    change(tr("Remove the key of %1").arg(current_), [&](samn::LabelProject& p) {
        auto* t = p.Track(current_.toStdString());
        return done = t && t->RemoveKey(int64_t(std::llround(preview_ms_)), tolerance());
    });
    return done;
}

int LabelController::confirmAll() {
    int n = 0;
    change(tr("Confirm %1").arg(current_), [&](samn::LabelProject& p) {
        auto* t = p.Track(current_.toStdString());
        n = t ? t->Confirm(0, std::numeric_limits<int64_t>::max()) : 0;
        return n > 0;
    });
    return n;
}

QVariantList LabelController::phases() const {
    QVariantList out;
    for (const auto& p : project_.phases)
        out << QVariantMap{{"startMs", double(p.start_ms)},
                           {"endMs", double(p.end_ms)},
                           {"phase", QString::fromStdString(p.name)},
                           {"confirmed", p.confirmed},
                           {"source", QString::fromStdString(p.source)}};
    return out;
}

QStringList LabelController::phaseNames() const {
    QStringList out;
    for (const auto& n : samn::PhaseNames()) out << QString::fromStdString(n);
    return out;
}

QString LabelController::phaseAtPreview() const {
    const auto* p = project_.PhaseAt(int64_t(std::llround(preview_ms_)));
    return p ? QString::fromStdString(p->name) : QString();
}

bool LabelController::setPhase(double startMs, double endMs, const QString& phase) {
    if (!hasVideo()) return false;
    const double end = still_ ? std::max(endMs, startMs + 1) : std::min(endMs, duration_ms_);
    const int64_t a = int64_t(std::llround(std::max(0.0, startMs))), b = int64_t(std::llround(end));
    if (b <= a) return false;
    change(phase.isEmpty() ? tr("Phase cleared") : tr("Phase: %1").arg(phase), [&](samn::LabelProject& p) {
        p.SetPhase(a, b, phase.toStdString());
        return true;
    });
    return true;
}

int LabelController::confirmPhases() {
    int n = 0;
    change(tr("Phases confirmed"), [&](samn::LabelProject& p) {
        for (auto& ph : p.phases)
            if (!ph.confirmed) {
                ph.confirmed = true;
                ++n;
            }
        if (n == 0) return false;
        const auto copy = p.phases;
        p.phases.clear();
        for (const auto& ph : copy) p.SetPhase(ph.start_ms, ph.end_ms, ph.name, true, ph.source);
        return true;
    });
    return n;
}

void LabelController::change(const QString& what, const std::function<bool(samn::LabelProject&)>& op) {
    samn::LabelProject next = project_;
    if (!op(next)) return;
    undo_.push_back(std::move(project_));
    if (undo_.size() > kUndoMax) undo_.erase(undo_.begin());
    redo_.clear();
    project_ = std::move(next);
    save();
    setStatus(what);
    emit tracksChanged();
    emit previewChanged();
}

bool LabelController::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(project_));
    project_ = std::move(undo_.back());
    undo_.pop_back();
    save();
    if (!labels().contains(current_)) setCurrentLabel(labels().value(0));
    emit tracksChanged();
    emit previewChanged();
    return true;
}

bool LabelController::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(project_));
    project_ = std::move(redo_.back());
    redo_.pop_back();
    save();
    emit tracksChanged();
    emit previewChanged();
    return true;
}

void LabelController::save() {
    if (video_path_.isEmpty()) return;
    try {
        samn::SaveLabelProject(samn::LabelProjectPath(Fs(video_path_)), project_);
    } catch (const std::exception& e) {
        setStatus(tr("Not saved: %1").arg(QString::fromUtf8(e.what())));
    }
}

void LabelController::setStatus(const QString& s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged();
}

QVariantList LabelController::boxes() const {
    QVariantList out;
    const auto classes = project_.Classes();
    const auto classIndex = [&](const std::string& c) { return int(std::find(classes.begin(), classes.end(), c) - classes.begin()); };
    const auto ms = int64_t(std::llround(preview_ms_));
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& t = project_.tracks[i];
        const samn::LabelState s = t.At(ms);
        const samn::LabelKey* key = t.KeyAt(ms, tolerance());
        out << QVariantMap{{"label", QString::fromStdString(t.label())},
                           {"cls", QString::fromStdString(t.cls())},
                           {"classIndex", classIndex(t.cls())},
                           {"index", int(i)},
                           {"x", s.box.x},
                           {"y", s.box.y},
                           {"w", s.box.w},
                           {"h", s.box.h},
                           {"present", s.present},
                           {"keyed", key != nullptr},
                           {"confirmed", key ? key->confirmed : s.reviewed},
                           {"reviewed", s.reviewed}};
    }
    return out;
}

QVariantList LabelController::lanes() const {
    QVariantList out;
    const auto classes = project_.Classes();
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& t = project_.tracks[i];
        QVariantList keys, spans;
        const auto& k = t.keys();
        for (const auto& key : k)
            keys << QVariantMap{{"ms", double(key.ms)}, {"absent", key.absent}, {"covered", key.covered}, {"confirmed", key.confirmed},
                                {"source", QString::fromStdString(key.source)}};
        // One span per stretch between keys (the last one runs to the end).
        for (size_t j = 0; j < k.size(); ++j) {
            const double end = j + 1 < k.size() ? double(k[j + 1].ms) : std::max(duration_ms_, double(k[j].ms));
            const bool reviewed = k[j].confirmed && (j + 1 == k.size() || k[j + 1].confirmed);
            spans << QVariantMap{{"startMs", double(k[j].ms)}, {"endMs", end}, {"present", !k[j].absent}, {"covered", k[j].covered},
                                 {"reviewed", reviewed}};
        }
        out << QVariantMap{{"label", QString::fromStdString(t.label())},
                           {"cls", QString::fromStdString(t.cls())},
                           {"classIndex", int(std::find(classes.begin(), classes.end(), t.cls()) - classes.begin())},
                           {"index", int(i)},
                           {"keys", keys},
                           {"spans", spans}};
    }
    return out;
}

int LabelController::exportDataset(const QString& dirOrUrl, double everyMs) {
    if (!hasVideo()) return -1;
    const QString dir = LocalPath(dirOrUrl);
    if (dir.isEmpty() || !QDir().mkpath(dir + "/images") || !QDir().mkpath(dir + "/labels")) {
        setStatus(tr("Cannot write to %1").arg(dir));
        return -1;
    }
    const std::vector<std::string> classes = project_.Classes();
    const auto samples = samn::ExportSamples(project_, classes, std::max<int64_t>(1, int64_t(everyMs)));
    const QString stem = QFileInfo(video_path_).completeBaseName();
    int written = 0;
    for (const auto& s : samples) {
        QImage img;
        if (still_) img = frame_;
        else img = FrameCache::shared().At(video_path_, double(s.ms)).image;
        if (img.isNull()) continue;
        const QString name = QStringLiteral("%1_%2").arg(stem).arg(s.ms, 8, 10, QLatin1Char('0'));
        if (!img.save(dir + "/images/" + name + ".jpg", "JPG", 92)) continue;
        std::ofstream(Fs(dir + "/labels/" + name + ".txt"), std::ios::binary)
            << samn::YoloLines(s, img.width(), img.height());
        ++written;
    }
    std::ofstream classesOut(Fs(dir + "/classes.txt"), std::ios::binary);
    for (const auto& c : classes) classesOut << c << '\n';
    // The confirmed phases for the scene model (D-157); clips exported into
    // the same folder add their lines. Binary: "\n" on every system, as
    // the labels and classes.txt.
    QFile phasesOut(dir + "/phases.csv");
    const bool fresh = !phasesOut.exists();
    if (phasesOut.open(QIODevice::Append)) {
        if (fresh) phasesOut.write("clip,start_ms,end_ms,phase\n");
        for (const auto& p : project_.phases)
            if (p.confirmed)
                phasesOut.write(QStringLiteral("%1,%2,%3,%4\n")
                                    .arg(QFileInfo(video_path_).fileName().replace(',', '_'))
                                    .arg(p.start_ms)
                                    .arg(p.end_ms)
                                    .arg(QString::fromStdString(p.name))
                                    .toUtf8());
    }
    setStatus(tr("%n sample(s) written to %1", "", written).arg(dir));
    return written;
}

}  // namespace samn::ui
