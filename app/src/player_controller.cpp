#include "player_controller.h"

#include <QCoreApplication>

#include <QMediaPlayer>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

#include "app_log.h"
#include "audio_level.h"
#include "curve_item.h"
#include "device_controller.h"
#include "qt_paths.h"
#include "samn/audio_envelope.h"
#include "samn/contact_preview.h"
#include "samn/fileio.h"
#include "samn/heatmap_image.h"
#include "samn/improve.h"
#include "samn/project.h"
#include "samn/range_edit.h"
#include "samn/script_quality.h"
#include "samn/audio_autosync.h"
#include "samn/sound_vibration.h"
#include "heatmap_item.h"
#include "samn/mapper.h"
#include "samn/marks.h"
#include "samn/playback.h"
#include "samn/playback_mapping.h"
#include "samn/contact_marks.h"
#include "samn/samn_file.h"
#include "samn/script_finder.h"

namespace samn::ui {

namespace {

bool isVideo(const QString& path) {
    const std::string suffix = "." + QFileInfo(path).suffix().toLower().toStdString();
    return std::find(samn::VIDEO_EXTS.begin(), samn::VIDEO_EXTS.end(), suffix) != samn::VIDEO_EXTS.end();
}

bool isScript(const QString& path) {
    const std::string suffix = "." + QFileInfo(path).suffix().toLower().toStdString();
    return std::find(samn::SCRIPT_EXTS.begin(), samn::SCRIPT_EXTS.end(), suffix) != samn::SCRIPT_EXTS.end();
}

// Frames (0-1) as a 0-100 lane for the timeline.
std::vector<samn::Action> laneFrom(const std::vector<samn::Frame>& frames, bool suction) {
    std::vector<samn::Action> out;
    out.reserve(frames.size());
    for (const auto& f : frames) out.push_back({f.at, static_cast<int>(std::lround((suction ? f.suction : f.vibration) * 100))});
    return out;
}

// Median spacing of stroke peaks → BPM in 20–240 (0 when unclear). Peaks are
// local maxima at least 55 — quiet baselines do not count as beats.
double EstimateStrokeBpm(const std::vector<samn::Action>& stroke) {
    if (stroke.size() < 4) return 0;
    std::vector<int64_t> peaks;
    peaks.reserve(stroke.size() / 2);
    for (size_t i = 1; i + 1 < stroke.size(); ++i) {
        const int p = stroke[i].pos;
        if (p < 55) continue;
        if (p >= stroke[i - 1].pos && p > stroke[i + 1].pos) peaks.push_back(stroke[i].at);
    }
    if (peaks.size() < 2) return 0;
    std::vector<int64_t> gaps;
    gaps.reserve(peaks.size() - 1);
    for (size_t i = 1; i < peaks.size(); ++i) {
        const int64_t g = peaks[i] - peaks[i - 1];
        if (g >= 250 && g <= 3000) gaps.push_back(g);  // 20–240 BPM
    }
    if (gaps.empty()) return 0;
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    const double period = double(gaps[gaps.size() / 2]);
    if (period <= 0) return 0;
    return std::clamp(std::round(60000.0 / period), 20.0, 240.0);
}

}  // namespace

PlayerController::PlayerController(QObject* parent)
    : QObject(parent), output_(&settings()), mapping_(&settings(), &output_) {
    connect(&output_, &PlaybackOutput::emergencyStopped, this, &PlayerController::emergencyStopped);
    // The video playhead between the media player's reports: the ticks feed
    // only the output (the curves follow the reports).
    connect(&mediaHead_, &MediaPlayhead::advanced, this, [this](qint64 videoMs) { sendToOutput(videoMs, false); });
    connect(&output_, &PlaybackOutput::outputChanged, this, [this] { mediaHead_.setRate(output_.speed()); });
    connect(&output_, &PlaybackOutput::emergencyStopped, this, [this] { mediaHead_.hold(); });
    connect(this, &PlayerController::loopChanged, this, &PlayerController::updateMediaLimit);
    connect(this, &PlayerController::markerChanged, this, &PlayerController::updateMediaLimit);
    connect(this, &PlayerController::offsetMsChanged, this, &PlayerController::updateMediaLimit);
    // A picked lane that is no longer shown (device changed, script closed)
    // falls back to the stroke.
    connect(this, &PlayerController::lanesChanged, this, [this] {
        if (activeLane_ != QLatin1String("stroke") && !laneChoices().contains(activeLane_)) setActiveLane({});
    });
    connect(&mapping_, &MappingSettings::mappingChanged, this, [this] {
        if (!opening_) remap(false);
    });
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
    // Play list options (0.7 kept Shuffle / Repeat in localStorage).
    auto flag = [this](const char* key, bool def) {
        const auto v = settings().get(key, def);
        return v.is_boolean() ? v.get<bool>() : def;
    };
    playlist_.SetAuto(flag("ui.player.playlist.auto", true));
    playlist_.SetRepeat(flag("ui.player.playlist.repeat", false));
    if (flag("ui.player.playlist.shuffle", false)) playlist_.SetShuffle(true, rng_);
    bpmGrid_ = flag("playback.bpm_grid", false);
    scaleSoftEdges_ = flag("playback.scale_soft_edges", false);
    {
        const auto v = settings().get("playback.cap_intensity", 400.0);
        const double c = v.is_number() ? v.get<double>() : 400.0;
        capIntensity_ = (std::isfinite(c) && c > 0) ? std::clamp(c, 150.0, 800.0) : 400.0;
    }
    {
        const auto v = settings().get("playback.fps_snap", 0.0);
        const double f = v.is_number() ? v.get<double>() : 0.0;
        fpsSnap_ = (std::isfinite(f) && f > 0) ? std::clamp(f, 0.0, 240.0) : 0.0;
    }
}
PlayerController::~PlayerController() = default;

void PlayerController::setDevice(DeviceController* device) {
    if (device == device_) return;
    if (device_) disconnect(device_, nullptr, this, nullptr);
    device_ = device;
    output_.setDevice(device);
    if (device_) connect(device_, &DeviceController::statusChanged, this, &PlayerController::lanesChanged);
    emit deviceChanged();
    emit lanesChanged();
}

// D-132: with a connected device the lanes follow what it can use; without
// one they show what the script carries.
bool PlayerController::showStrokeLane() const {
    if (stroke_.empty()) return false;
    return device_ && device_->connected() ? device_->capLinear() : true;
}
bool PlayerController::showVibrationLane() const {
    if (stroke_.empty()) return false;
    return device_ && device_->connected() ? device_->capVibration() : vibrationFromScript_;
}
bool PlayerController::showSuctionLane() const {
    if (stroke_.empty()) return false;
    return device_ && device_->connected() ? device_->capSuction() : suctionFromScript_;
}

QStringList PlayerController::laneChoices() const {
    if (stroke_.empty()) return {};
    QStringList out{QStringLiteral("stroke")};
    if (showVibrationLane()) out << QStringLiteral("vibration");
    if (showSuctionLane()) out << QStringLiteral("suction");
    out << axisLanes();
    return out;
}

void PlayerController::setActiveLane(const QString& lane) {
    const QString next = laneChoices().contains(lane) ? lane : QStringLiteral("stroke");
    if (next == activeLane_) return;
    activeLane_ = next;
    emit activeLaneChanged();
    pushToViews();
}

void PlayerController::stepActiveLane(int delta) {
    const QStringList choices = laneChoices();
    if (choices.size() < 2) return;
    const int i = std::max<int>(0, int(choices.indexOf(activeLane_)));
    const int n = int(choices.size());
    setActiveLane(choices[((i + delta) % n + n) % n]);
}

QStringList PlayerController::axisLanes() const {
    QStringList out;
    if (stroke_.empty()) return out;
    const bool connected = device_ && device_->connected();
    const QStringList has = connected ? device_->axes() : QStringList{};
    for (const char* axis : samn::kCompanionAxes) {
        const QString name = QString::fromLatin1(axis);
        if (axisActions_.count(axis) && (!connected || has.contains(name))) out << name;
    }
    return out;
}

QStringList PlayerController::missingAxisChoices() const {
    QStringList out;
    if (stroke_.empty()) return out;
    for (const char* axis : samn::kCompanionAxes) {
        if (!axisActions_.count(axis)) out << QString::fromLatin1(axis);
    }
    return out;
}

samn::Settings& PlayerController::settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

QString PlayerController::toLocalPath(const QString& pathOrUrl) { return LocalPath(pathOrUrl); }

QStringList PlayerController::scriptFolders() const {
    QStringList out;
    if (!settings_) const_cast<PlayerController*>(this)->settings();
    const auto v = settings_->get(kScriptFoldersKey, "");
    for (const auto& f : samn::split_folders(v.is_string() ? v.get<std::string>() : std::string()))
        out << FromU8(f);
    return out;
}

bool PlayerController::openPath(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    if (!isVideo(path)) {
        variants_.clear();
        variantIndex_ = -1;
        emit variantsChanged();
        if (!openScript(path)) return false;
        // Open… makes a fresh one-item list (0.7).
        playlist_.Replace({U8(scriptPath_)});
        emit playlistChanged();
        return true;
    }
    // Video first: beside it, in the script folders, tolerant names, variants.
    std::vector<std::string> folders;
    for (const auto& f : scriptFolders()) folders.push_back(U8(f));
    const auto match = samn::find_script_for_video(U8(path), folders);
    variants_.clear();
    variantIndex_ = -1;
    for (size_t i = 0; i < match.variants.size(); ++i) {
        const auto& v = match.variants[i];
        // 0.7 #pb-variant: "Soft", "By someone" or "Main script", and where it lies.
        QString name = FromU8(v.label);
        name = name.isEmpty() ? tr("Main script") : name.left(1).toUpper() + name.mid(1);
        if (!v.beside) name += tr(" · script folder");
        variants_.append(QVariantMap{{"path", FromU8(v.path)}, {"label", FromU8(v.label)}, {"display", name}, {"beside", v.beside}});
        if (v.path == match.script_path) variantIndex_ = static_cast<int>(i);
    }
    emit variantsChanged();
    if (match.found && !match.script_path.empty()) {
        if (!openScript(FromU8(match.script_path), path)) return false;
        playlist_.Replace({U8(scriptPath_)});
        emit playlistChanged();
        return true;
    }
    clearScript();
    setError({});  // before setVideo: the player may report a broken video at once
    setVideo(path);
    return true;
}

bool PlayerController::openPaths(const QStringList& pathsOrUrls) {
    std::vector<std::string> scripts;
    QString video;
    int extraVideos = 0;
    QStringList unsupported;
    for (const QString& p : pathsOrUrls) {
        const QString local = toLocalPath(p);
        if (isVideo(local)) {
            if (video.isEmpty())
                video = local;
            else
                ++extraVideos;
        } else if (isScript(local)) {
            scripts.push_back(U8(local));
        } else {
            unsupported << QFileInfo(local).fileName();
        }
    }
    if (scripts.empty() && video.isEmpty()) {
        // 0.7: a drop never vanishes silently.
        if (!pathsOrUrls.isEmpty()) setError(tr("Drop ignored: drop a video or a script (.funscript, .samn)."));
        return false;
    }
    bool ok;
    if (scripts.empty()) {
        ok = openPath(video);
    } else if (scripts.size() == 1 && video.isEmpty()) {
        ok = openPath(FromU8(scripts.front()));
    } else {
        // A video and a script dropped together open that script with that
        // video; several scripts become the play list.
        playlist_.Replace(scripts);
        emit playlistChanged();
        variants_.clear();
        variantIndex_ = -1;
        emit variantsChanged();
        ok = openScript(FromU8(playlist_.Current()->path), video);
    }
    if (!ok) return false;
    // Say what was left out (0.7 PS-10); the Player opens one video.
    QStringList left;
    if (extraVideos > 0)
        left << (extraVideos == 1 ? tr("1 more video (the Player opens one at a time)")
                                  : tr("%1 more videos (the Player opens one at a time)").arg(extraVideos));
    if (!unsupported.isEmpty())
        left << (unsupported.size() == 1 ? tr("an unsupported file: %1") : tr("unsupported files: %1"))
                    .arg(unsupported.join(QStringLiteral(", ")));
    if (!left.isEmpty()) setError(tr("Not opened: %1").arg(left.join(QStringLiteral("; "))));
    return true;
}

QVariantList PlayerController::playlist() const {
    QVariantList out;
    const auto& items = playlist_.Items();
    for (size_t i = 0; i < items.size(); ++i)
        out.append(QVariantMap{{"name", FromU8(items[i].name)}, {"path", FromU8(items[i].path)}, {"active", i == playlist_.Index()}});
    return out;
}

bool PlayerController::loadPlaylistCurrent() {
    const auto* cur = playlist_.Current();
    emit playlistChanged();
    if (!cur) return false;
    variants_.clear();
    variantIndex_ = -1;
    emit variantsChanged();
    return openScript(FromU8(cur->path));
}

bool PlayerController::addToPlaylist(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    if (!playlist_.Append(U8(path), U8(scriptPath_))) {
        setError(tr("Already in list: %1").arg(QFileInfo(path).completeBaseName()));
        return false;
    }
    emit playlistChanged();
    return true;
}

bool PlayerController::removeFromPlaylist(int index) {
    if (index < 0) return false;
    const size_t before = playlist_.Items().size();
    const bool wasCurrent = playlist_.Remove(size_t(index));
    if (playlist_.Items().size() == before) return false;
    if (wasCurrent && playlist_.Current()) return loadPlaylistCurrent();
    emit playlistChanged();
    return true;
}

bool PlayerController::pickPlaylist(int index) {
    if (index < 0 || !playlist_.Pick(size_t(index))) return false;
    return loadPlaylistCurrent();
}

bool PlayerController::playlistNext() { return playlist_.Advance(rng_) && loadPlaylistCurrent(); }

bool PlayerController::playlistPrevious() { return playlist_.Back() && loadPlaylistCurrent(); }

bool PlayerController::playlistFinished() { return playlist_.AdvanceOnFinish(rng_) && loadPlaylistCurrent(); }

void PlayerController::setPlaylistAuto(bool on) {
    if (on == playlist_.Auto()) return;
    playlist_.SetAuto(on);
    try {
        settings().set("ui.player.playlist.auto", on);
    } catch (const std::exception&) {
    }
    emit playlistChanged();
}

void PlayerController::setPlaylistShuffle(bool on) {
    if (on == playlist_.Shuffle()) return;
    playlist_.SetShuffle(on, rng_);
    try {
        settings().set("ui.player.playlist.shuffle", on);
    } catch (const std::exception&) {
    }
    emit playlistChanged();
}

void PlayerController::setPlaylistRepeat(bool on) {
    if (on == playlist_.Repeat()) return;
    playlist_.SetRepeat(on);
    try {
        settings().set("ui.player.playlist.repeat", on);
    } catch (const std::exception&) {
    }
    emit playlistChanged();
}

bool PlayerController::chooseVariant(int index) {
    if (index < 0 || index >= variants_.size()) return false;
    const QString video = videoPath_;
    if (!openScript(variants_[index].toMap().value("path").toString(), video)) return false;
    variantIndex_ = index;
    emit variantsChanged();
    return true;
}

void PlayerController::removeScriptFolder(const QString& path) {
    QStringList folders = scriptFolders();
    if (!folders.removeAll(toLocalPath(path))) return;
    try {
        settings().set(kScriptFoldersKey, U8(folders.join('\n')));
    } catch (const std::exception& e) {
        setFailure(tr("Could not save the script folders: %1").arg(FromU8(e.what())));
        return;
    }
    emit scriptFoldersChanged();
}

bool PlayerController::isVideoFile(const QString& pathOrUrl) const { return isVideo(toLocalPath(pathOrUrl)); }

bool PlayerController::autoCreate() const {
    try {
        const auto v = const_cast<PlayerController*>(this)->settings().get(kAutoCreateKey);
        return v.is_boolean() && v.get<bool>();
    } catch (const std::exception&) {
        return false;
    }
}

void PlayerController::setAutoCreate(bool on) {
    if (on == autoCreate()) return;
    try {
        settings().set(kAutoCreateKey, on);
    } catch (const std::exception& e) {
        setFailure(tr("Could not save the setting: %1").arg(FromU8(e.what())));
        return;
    }
    emit autoCreateChanged();
}

void PlayerController::addScriptFolder(const QString& pathOrUrl) {
    const QString dir = toLocalPath(pathOrUrl);
    QStringList folders = scriptFolders();
    if (!folders.contains(dir)) {
        folders << dir;
        try {
            settings().set(kScriptFoldersKey, U8(folders.join('\n')));
        } catch (const std::exception& e) {
            setFailure(tr("Could not save the script folders: %1").arg(FromU8(e.what())));
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

namespace {

QVariantMap MarkBox(const samn::ContactMarkBox& b) {
    return {{"x", qint64(b.x)}, {"y", qint64(b.y)}, {"w", qint64(b.w)}, {"h", qint64(b.h)},
            {"cls", QString::fromStdString(b.cls)}, {"fixed", b.fixed}};
}

// 0.7 applyContactMarksInfo: what the marks are, for the hint under the video.
QVariantMap MarksInfo(const samn::ContactMarks& m, const std::optional<samn::TrajectoryData>& path) {
    QVariantMap out;
    out["tipClass"] = QString::fromStdString(m.tip_class);
    out["driveStroke"] = m.drive_stroke;
    int boxes = 0;
    if (m.tip && m.tip->w > 0 && m.tip->h > 0) {
        out["tip"] = MarkBox(*m.tip);
        ++boxes;
    }
    if (m.primary && m.primary->w > 0 && m.primary->h > 0) {
        out["primary"] = MarkBox(*m.primary);
        ++boxes;
    }
    QVariantList extras;
    for (const samn::ContactMarkBox& e : m.extras)
        if (e.w > 0 && e.h > 0) extras << MarkBox(e);
    boxes += int(extras.size());
    out["extras"] = extras;
    out["width"] = path ? qint64(path->width) : 0;
    out["height"] = path ? qint64(path->height) : 0;
    const bool hasPartner = path && !path->partner.empty();
    out["hasPartner"] = hasPartner;
    const bool hasExtras =
        path && std::any_of(path->extras.begin(), path->extras.end(),
                            [](const std::vector<samn::TrajectoryPoint>& p) { return !p.empty(); });
    out["hasExtras"] = hasExtras;
    QStringList hint;
    if (!m.tip_class.empty()) hint << QCoreApplication::translate("PlayerController", "Tip: %1").arg(QString::fromStdString(m.tip_class));
    if (boxes == 1) hint << QCoreApplication::translate("PlayerController", "1 marked box");
    else if (boxes > 1) hint << QCoreApplication::translate("PlayerController", "%1 marked boxes").arg(boxes);
    if (hasPartner) hint << QCoreApplication::translate("PlayerController", "partner path");
    if (hasExtras) hint << QCoreApplication::translate("PlayerController", "extras path");
    hint << (m.drive_stroke ? QCoreApplication::translate("PlayerController", "distance drives the stroke")
                            : QCoreApplication::translate("PlayerController", "feel only (the tip tracker drives the stroke)"));
    out["hint"] = hint.join(QStringLiteral(" · "));
    return out;
}

}  // namespace

bool PlayerController::bakeOffset() {
    if (scriptPath_.isEmpty()) {
        setError(tr("Open a script first."));
        return false;
    }
    const int64_t delta = offsetMs_;
    if (delta == 0) return false;
    int dropped = 0;
    const auto shift = [&](std::vector<samn::Action>& actions) {
        if (actions.empty()) return;
        auto [moved, gone] = samn::ShiftActions(actions, delta);
        if (moved.empty()) throw std::runtime_error(tr("the offset would move every point before the start").toStdString());
        dropped += gone;
        actions = std::move(moved);
    };
    namespace fs = std::filesystem;
    const fs::path path = fs::u8path(U8(scriptPath_));
    const bool samnScript = samn::IsSamnPath(U8(scriptPath_));
    // Everything is read and moved first, the files are written last: one
    // that cannot be read (a broken .samn) leaves every file as it was. A
    // write that fails puts back the files written before it.
    std::vector<fs::path> files;
    std::vector<std::function<void()>> writes;
    try {
        // The .funscript (the .samn's companion, or the script itself).
        fs::path funscript = path;
        if (samnScript) funscript.replace_extension(".funscript");
        std::error_code ec;
        if (fs::exists(funscript, ec)) {
            const samn::FunscriptView view = samn::LoadFunscript(funscript);
            auto actions = view.actions;
            const int before = dropped;
            shift(actions);
            const auto bookmarks = samn::ShiftBookmarks(samn::LoadBookmarks(funscript), delta);
            const auto chapters = samn::ShiftChapters(samn::LoadChapters(funscript), delta);
            const std::vector<samn::OMarker> oldMarkers = samn::LoadOMarkers(funscript);
            const auto markers = samn::ShiftOMarkers(oldMarkers, delta);
            const auto metadata = samn::ShiftedFunscriptMetadata(view, delta);
            dropped += bookmarks.second + chapters.second + markers.second;
            if (samnScript) dropped = before;  // counted once, from the .samn
            files.push_back(funscript);
            writes.push_back([funscript, actions, bookmarks, chapters, markers, hadMarkers = !oldMarkers.empty(), metadata] {
                samn::PatchActions(funscript, actions);
                samn::SaveBookmarks(funscript, bookmarks.first);
                samn::SaveChapters(funscript, chapters.first);
                for (const auto& [key, encoded] : metadata) samn::PatchMetadataKey(funscript, key, encoded);
                if (hadMarkers) samn::SaveOMarkers(funscript, markers.first);
            });
        }
        // 0.7 companion axis files (<stem>.surge.funscript …).
        for (const auto& axisFile : samn::companion_axis_files(U8(scriptPath_))) {
            const fs::path file = fs::u8path(axisFile.second);
            std::vector<samn::Action> actions;
            try {
                actions = samn::LoadFunscript(file).actions;
                shift(actions);
            } catch (const std::exception& e) {
                throw std::runtime_error(file.filename().u8string() + ": " + e.what());
            }
            files.push_back(file);
            writes.push_back([file, actions] { samn::PatchActions(file, actions); });
        }
        // The loop marker beside the script; one that ends before the start goes.
        if (const auto marker = samn::LoadMarker(path)) {
            samn::RangeMarker moved{std::max<int64_t>(0, marker->start_ms + delta), marker->end_ms + delta};
            if (marker->end_ms + delta <= 0) {
                moved = {};
                ++dropped;
            }
            files.push_back(samn::MarkerPath(path));
            writes.push_back([path, moved] { samn::SaveMarker(path, moved); });
        }
        if (samnScript) {
            samn::Document d = samn::LoadDocument(path);
            shift(d.general);
            shift(d.vibration);
            shift(d.suction);
            dropped += samn::ShiftDocumentMarks(d, delta);
            files.push_back(path);
            writes.push_back([path, d]() mutable { samn::SaveDocument(path, d); });
        }
        std::vector<std::pair<fs::path, std::string>> originals;
        for (const fs::path& file : files) {
            std::ifstream in(file, std::ios::binary);
            if (!in) throw std::runtime_error("cannot read " + file.filename().u8string());
            originals.emplace_back(file, std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
        }
        try {
            for (const auto& write : writes) write();
        } catch (const std::exception& failed) {
            std::string unrestored;
            for (const auto& entry : originals) {
                try {
                    samn::WriteFileAtomic(entry.first, entry.second);
                } catch (const std::exception&) {
                    unrestored += (unrestored.empty() ? "" : ", ") + entry.first.filename().u8string();
                }
            }
            // A file left shifted must never be shifted again by a second bake.
            if (!unrestored.empty())
                throw std::runtime_error(std::string(failed.what()) + "; these files are already shifted and could "
                                         "not be put back: " + unrestored + " (check them before baking again)");
            throw;
        }
    } catch (const std::exception& e) {
        setFailure(tr("Offset not baked: %1").arg(FromU8(e.what())));
        return false;
    }
    setOffsetMs(0);
    reloadScript();
    toolNote_ = (dropped > 0 ? tr("Offset of %1 ms written into the script; %2 point(s) or marks before the start dropped.")
                                .arg(delta)
                                .arg(dropped)
                          : tr("Offset of %1 ms written into the script.").arg(delta));
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return true;
}

bool PlayerController::reloadScript() {
    if (scriptPath_.isEmpty()) return false;
    const QString script = scriptPath_, video = videoPath_;
    return openScript(script, video);
}

QString PlayerController::optimizeFor(const QString& target) {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script with at least 2 stroke points first."));
        return {};
    }
    const bool neo2 = target == QLatin1String("neo2");
    if (!neo2 && target != QLatin1String("general")) return {};

    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));

    samn::ImproveOptions o;
    o.fill_gaps = true;
    o.heal_tracking_gaps = true;
    try {
        o.tracking_gaps = samn::LoadFunscript(funscript).metadata.tracking_gaps;
    } catch (const std::exception&) {
    }

    samn::Document doc;
    try {
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
            if (o.tracking_gaps.empty()) o.tracking_gaps = doc.tracking_gaps;
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
    } catch (const std::exception& e) {
        setFailure(tr("Optimize failed: %1").arg(FromU8(e.what())));
        return {};
    }
    if (doc.general.size() < 2) doc.general = stroke_;

    const QString label = neo2 ? tr("Optimize for Sam Neo 2") : tr("Optimize for any device");
    QStringList parts{neo2 ? tr("Neo 2 ready") : tr("Ready for any device")};
    try {
        const samn::ImproveResult res = samn::ImproveScript(doc.general, o);
        const bool strokeChanged = res.points_added > 0 || res.windows_healed > 0 || res.after_count != res.before_count;
        if (strokeChanged) doc.general = res.actions;
        if (res.windows_healed > 0) parts << tr("healed %n tracking gap(s)", "", res.windows_healed);
        if (res.gaps_filled > 0) parts << tr("filled %n gap(s) (+%1 points)", "", res.gaps_filled).arg(res.points_added);
        else if (!neo2) parts << tr("no gaps to fill");
        if (res.clear_tracking_gaps) doc.tracking_gaps.clear();

        if (neo2) {
            doc.ApplyContactRecipe(true, samn::DefaultContactVibrationSpan, "soft");
            doc.BakeNeoAxes();
            parts << tr("contact on") << tr("vibration and suction written");
            if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
            if (doc.kind.empty()) doc.kind = samn::SamnKind;
            if (doc.creator.empty()) doc.creator = "SamNPlayer";
            const char* source = samn::PlaybackAxes;
            doc.playback_source = source;
            doc.recipe.playback_source = source;
            samn::SaveDocument(samnPath, doc);
            if (fs::exists(funscript)) {
                samn::PatchActions(funscript, doc.general);
                if (res.clear_tracking_gaps) samn::PatchMetadataKey(funscript, "tracking_gaps", "null");
            }
        } else {
            // Any device: rewrite the stroke on disk; keep an existing .samn in sync.
            if (fs::exists(funscript)) {
                samn::PatchActions(funscript, doc.general);
                if (res.clear_tracking_gaps) samn::PatchMetadataKey(funscript, "tracking_gaps", "null");
            }
            if (wasSamn || fs::exists(samnPath)) {
                if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
                if (doc.kind.empty()) doc.kind = samn::SamnKind;
                samn::SaveDocument(wasSamn ? opened : samnPath, doc);
            } else if (!fs::exists(funscript)) {
                // Opened a lone .samn without a companion funscript.
                samn::SaveDocument(opened, doc);
            }
        }
    } catch (const std::exception& e) {
        setFailure(tr("%1: %2").arg(label, FromU8(e.what())));
        return {};
    }

    const QString reloadPath = neo2 || wasSamn || fs::exists(samnPath) ? FromU8(samnPath.u8string()) : scriptPath_;
    const QString video = videoPath_;
    if (!openScript(reloadPath, video)) return {};
    const QString msg = parts.join(QStringLiteral(" · "));
    toolNote_ = msg;
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return msg;
}

bool PlayerController::axesFromStroke() {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script with at least 2 stroke points first."));
        return false;
    }
    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));

    try {
        samn::Document doc;
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
        if (doc.general.size() < 2) doc.general = stroke_;
        doc.BakeNeoAxes();
        if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
        if (doc.kind.empty()) doc.kind = samn::SamnKind;
        if (doc.creator.empty()) doc.creator = "SamNPlayer";
        const char* source = samn::PlaybackAxes;
        doc.playback_source = source;
        doc.recipe.playback_source = source;
        samn::SaveDocument(samnPath, doc);
    } catch (const std::exception& e) {
        setFailure(tr("Vibration and suction: %1").arg(FromU8(e.what())));
        return false;
    }

    if (!openScript(FromU8(samnPath.u8string()), videoPath_)) return false;
    toolNote_ = tr("Feel channels baked · vibration and suction from the stroke.");
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return true;
}

QString PlayerController::exportFunscript(const QString& destPath) {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Share: open a script with at least 2 stroke points first."));
        return {};
    }
    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string companionU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const QString dest = toLocalPath(destPath.isEmpty() ? FromU8(companionU8) : destPath);
    if (dest.isEmpty()) {
        setError(tr("Share: no destination path."));
        return {};
    }
    const fs::path out = fs::u8path(U8(dest));
    try {
        samn::Document doc;
        if (wasSamn || fs::exists(fs::u8path(samn::CompanionSamnPath(companionU8)))) {
            doc = samn::LoadDocument(wasSamn ? opened : fs::u8path(samn::CompanionSamnPath(companionU8)));
        } else {
            doc = samn::LoadFunscriptFile(opened);
        }
        if (doc.general.size() < 2) doc.general = stroke_;
        doc.ExportFunscript(out);
    } catch (const std::exception& e) {
        setFailure(tr("Share: %1").arg(FromU8(e.what())));
        return {};
    }
    toolNote_ = tr("Shared for other apps · %1").arg(QFileInfo(dest).fileName());
    emit toolNoteChanged();
    return dest;
}

QVariantMap PlayerController::contactFeelProbe() const {
    const samn::ContactVibrationPreview p = samn::PreviewContactVibration(
        mapping_.contactSpan(), mapping_.contactCurve().toStdString(), mapping_.contactIntensity());
    QVariantList stroke, vib, peaks;
    stroke.reserve(int(p.stroke.size()));
    vib.reserve(int(p.vib.size()));
    peaks.reserve(int(p.peaks.size()));
    for (const auto& pt : p.stroke) stroke << QVariantMap{{"x", pt.x}, {"y", pt.y}};
    for (const auto& pt : p.vib) vib << QVariantMap{{"x", pt.x}, {"y", pt.y}};
    for (const auto& pt : p.peaks) peaks << QVariantMap{{"x", pt.x}, {"y", pt.y}};
    return {{"activePct", p.active_pct},
            {"peak", p.peak},
            {"hint", QString::fromStdString(p.hint)},
            {"stroke", stroke},
            {"vib", vib},
            {"peaks", peaks}};
}

bool PlayerController::saveContactToScript() {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        return false;
    }
    if (!contactControls_) {
        setError(tr("This script has no contact vibration to save."));
        return false;
    }
    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));

    try {
        samn::Document doc;
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
        if (doc.general.size() < 2) doc.general = stroke_;
        const bool on = !mapping_.contactOff();
        doc.ApplyContactRecipe(on, mapping_.contactSpan(), mapping_.contactCurve().toStdString());
        if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
        if (doc.kind.empty()) doc.kind = samn::SamnKind;
        if (doc.creator.empty()) doc.creator = "SamNPlayer";
        samn::SaveDocument(wasSamn ? opened : samnPath, doc);
        if (fs::exists(funscript) || !wasSamn) doc.ExportFunscript(funscript);
    } catch (const std::exception& e) {
        setFailure(tr("Save contact: %1").arg(FromU8(e.what())));
        return false;
    }

    const QString reload = wasSamn ? scriptPath_ : FromU8(samnPath.u8string());
    if (!openScript(reload, videoPath_)) return false;
    toolNote_ = tr("Contact settings saved to the script.");
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return true;
}

bool PlayerController::setPlaybackSource(const QString& sourceIn) {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        return false;
    }
    const std::string source = samn::NormalizePlaybackSource(U8(sourceIn));
    if (QString::fromStdString(source) == playbackSource_ && samn::IsSamnPath(U8(scriptPath_))) {
        toolNote_ = tr("Drive already set to %1.").arg(playbackSource_ == QLatin1String("axes") ? tr("Axes")
                                                                                                 : tr("Recipe"));
        emit toolNoteChanged();
        return true;
    }
    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));

    try {
        samn::Document doc;
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
        if (doc.general.size() < 2) doc.general = stroke_;
        if (source == samn::PlaybackAxes && doc.vibration.size() < 2 && doc.suction.size() < 2) {
            doc.BakeNeoAxes();
        }
        doc.playback_source = source;
        doc.recipe.playback_source = source;
        if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
        if (doc.kind.empty()) doc.kind = samn::SamnKind;
        if (doc.creator.empty()) doc.creator = "SamNPlayer";
        samn::SaveDocument(wasSamn ? opened : samnPath, doc);
        if (fs::exists(funscript) || !wasSamn) doc.ExportFunscript(funscript);
    } catch (const std::exception& e) {
        setFailure(tr("Drive: %1").arg(FromU8(e.what())));
        return false;
    }

    const QString reload = wasSamn ? scriptPath_ : FromU8(samnPath.u8string());
    if (!openScript(reload, videoPath_)) return false;
    toolNote_ = tr("Drive · %1").arg(playbackSource_ == QLatin1String("axes") ? tr("Axes") : tr("Recipe"));
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return true;
}

QString PlayerController::createCompanionAxis(const QString& axisIn) {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script with at least 2 stroke points first."));
        return {};
    }
    const std::string axis = axisIn.toLower().toStdString();
    if (!samn::is_companion_axis(axis)) {
        setError(tr("Unknown axis: %1").arg(axisIn));
        return {};
    }
    if (axisActions_.count(axis)) {
        setError(tr("Axis file already exists."));
        return {};
    }
    const int64_t duration = stroke_.back().at;
    std::string err;
    const auto created = samn::create_companion_axis(U8(scriptPath_), axis, duration, &err);
    if (!created) {
        setFailure(tr("+ Axis: %1").arg(FromU8(err.empty() ? "could not create" : err)));
        return {};
    }
    try {
        auto actions = samn::LoadFunscript(std::filesystem::u8path(*created)).actions;
        if (actions.empty()) {
            setFailure(tr("+ Axis: created file has no points."));
            return {};
        }
        axisActions_[axis] = std::move(actions);
    } catch (const std::exception& e) {
        setFailure(tr("+ Axis: %1").arg(FromU8(e.what())));
        return {};
    }
    emit lanesChanged();
    pushToViews();
    setActiveLane(QString::fromStdString(axis));
    const QString path = FromU8(*created);
    toolNote_ = tr("+ Axis · %1").arg(QFileInfo(path).fileName());
    emit toolNoteChanged();
    setError({});
    return path;
}

QString PlayerController::saveAsEmotionScript() {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        return {};
    }
    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));
    const QString outPath = FromU8(samnPath.u8string());

    try {
        samn::Document doc;
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
        if (doc.general.size() < 2) doc.general = stroke_;
        if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
        if (doc.kind.empty()) doc.kind = samn::SamnKind;
        if (doc.creator.empty()) doc.creator = "SamNPlayer";
        samn::SaveDocument(samnPath, doc);
        if (fs::exists(funscript) || !wasSamn) doc.ExportFunscript(funscript);
    } catch (const std::exception& e) {
        setFailure(tr("Emotion Script: %1").arg(FromU8(e.what())));
        return {};
    }

    if (!openScript(outPath, videoPath_)) return {};
    toolNote_ = tr("Emotion Script saved · %1").arg(QFileInfo(outPath).fileName());
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return outPath;
}

void PlayerController::setCapIntensity(double v) {
    v = (std::isfinite(v) && v > 0) ? std::clamp(v, 150.0, 800.0) : 400.0;
    v = std::round(v / 25.0) * 25.0;
    if (v == capIntensity_) return;
    capIntensity_ = v;
    try {
        settings().set("playback.cap_intensity", capIntensity_);
    } catch (const std::exception&) {
    }
    emit curveEditChanged();
}

void PlayerController::setScaleSoftEdges(bool on) {
    if (on == scaleSoftEdges_) return;
    scaleSoftEdges_ = on;
    try {
        settings().set("playback.scale_soft_edges", on);
    } catch (const std::exception&) {
    }
    emit curveEditChanged();
}

void PlayerController::setFpsSnap(double fps) {
    fps = std::isfinite(fps) ? std::clamp(fps, 0.0, 240.0) : 0.0;
    if (fps == fpsSnap_) return;
    fpsSnap_ = fps;
    try {
        settings().set("playback.fps_snap", fpsSnap_);
    } catch (const std::exception&) {
    }
    emit curveEditChanged();
}

namespace {

enum class EditLane { Stroke, Vibration, Suction };

std::optional<EditLane> ParseEditLane(const QString& lane, bool vibFromScript, bool sucFromScript) {
    if (lane == QLatin1String("stroke")) return EditLane::Stroke;
    if (lane == QLatin1String("vibration") && vibFromScript) return EditLane::Vibration;
    if (lane == QLatin1String("suction") && sucFromScript) return EditLane::Suction;
    return std::nullopt;
}

}  // namespace

bool PlayerController::capSelection() {
    return applySelectionEdit(tr("Cap speed"), [this](const std::vector<samn::Action>& a) {
        return samn::CapSpeedRange(a, marker_.start_ms, marker_.end_ms, capIntensity_);
    });
}

bool PlayerController::scaleSelection(double factor) {
    return applySelectionEdit(tr("Scale range"), [this, factor](const std::vector<samn::Action>& a) {
        return samn::ScaleRangePosFade(a, marker_.start_ms, marker_.end_ms, factor, scaleSoftEdges_);
    });
}

bool PlayerController::deleteSelection() {
    const bool ok = applySelectionEdit(tr("Delete range"), [this](const std::vector<samn::Action>& a) {
        return samn::DeleteRange(a, marker_.start_ms, marker_.end_ms);
    });
    if (ok) clearMarker();
    return ok;
}

bool PlayerController::invertSelection() {
    return applySelectionEdit(tr("Invert range"), [this](std::vector<samn::Action> a) {
        for (auto& x : a)
            if (x.at >= marker_.start_ms && x.at <= marker_.end_ms) x.pos = 100 - x.pos;
        return a;
    });
}

bool PlayerController::invertAll() {
    return applySelectionEdit(
        tr("Invert curve"),
        [](std::vector<samn::Action> a) {
            for (auto& x : a) x.pos = 100 - x.pos;
            return a;
        },
        false);
}

bool PlayerController::applySelectionEdit(const QString& label,
                                         const std::function<std::vector<samn::Action>(const std::vector<samn::Action>&)>& op,
                                         bool needMarker) {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        return false;
    }
    if (needMarker && !hasMarker()) {
        setError(tr("Mark a range on the heatmap first."));
        return false;
    }
    const auto lane = ParseEditLane(activeLane_, vibrationFromScript_, suctionFromScript_);
    if (!lane) {
        setError(tr("Pick the Stroke lane, or Adapt for Sam Neo 2 so vibration/suction are in the script."));
        return false;
    }

    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));

    try {
        samn::Document doc;
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
        if (doc.general.size() < 2) doc.general = stroke_;

        std::vector<samn::Action>* target = &doc.general;
        if (*lane == EditLane::Vibration) target = &doc.vibration;
        else if (*lane == EditLane::Suction) target = &doc.suction;
        if (target->size() < 2) {
            setError(tr("%1: the lane has too few points.").arg(label));
            return false;
        }
        *target = op(*target);

        if (*lane == EditLane::Stroke) {
            if (fs::exists(funscript)) samn::PatchActions(funscript, doc.general);
            if (wasSamn || fs::exists(samnPath)) {
                if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
                if (doc.kind.empty()) doc.kind = samn::SamnKind;
                samn::SaveDocument(wasSamn ? opened : samnPath, doc);
            } else if (!fs::exists(funscript)) {
                samn::SaveDocument(opened, doc);
            }
        } else {
            if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
            if (doc.kind.empty()) doc.kind = samn::SamnKind;
            const char* source = samn::PlaybackAxes;
            doc.playback_source = source;
            doc.recipe.playback_source = source;
            samn::SaveDocument(samnPath, doc);
        }
    } catch (const std::exception& e) {
        setFailure(tr("%1: %2").arg(label, FromU8(e.what())));
        return false;
    }

    const QString laneTitle = *lane == EditLane::Stroke       ? tr("Stroke")
                              : *lane == EditLane::Vibration ? tr("Vibration")
                                                             : tr("Suction");
    const QString reload = (wasSamn || fs::exists(samnPath) || *lane != EditLane::Stroke)
                               ? FromU8(samnPath.u8string())
                               : scriptPath_;
    if (!openScript(reload, videoPath_)) return false;
    toolNote_ = tr("%1 · %2").arg(label, laneTitle);
    emit toolNoteChanged();
    emit scriptEdited(scriptPath_);
    return true;
}

bool PlayerController::openScript(const QString& pathIn, const QString& videoIn) {
    // A .samn beside a .funscript of the same name wins (as in 0.7).
    const QString path = FromU8(samn::PreferSamnCompanion(U8(toLocalPath(pathIn))));
    clearAutoSyncSuggestion();
    {
        const std::string opened = U8(path);
        const std::string fun = samn::IsSamnPath(opened) ? samn::CompanionFunscriptPath(opened) : opened;
        const QString key = FromU8(samn::CompanionSamnPath(fun));
        if (!soundCurveUndoPath_.isEmpty() && key != soundCurveUndoPath_) {
            soundCurveUndo_.reset();
            soundCurveUndoPath_.clear();
        }
        if (!pendingSoundCurve_.empty()) pendingSoundCurve_.clear();
        emit soundCurveChanged();
    }
    samn::Document doc;
    try {
        doc = samn::IsSamnPath(U8(path)) ? samn::LoadDocument(std::filesystem::u8path(U8(path)))
                                         : samn::LoadFunscriptFile(std::filesystem::u8path(U8(path)));
    } catch (const std::exception& e) {
        setFailure(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), FromU8(e.what())));
        return false;
    }
    // 0.7 "Honor inverted flag" (playback.honor_inverted, on by default): off
    // plays a funscript's "inverted": true script as written.
    if (!samn::IsSamnPath(U8(path))) {
        bool honor = true;
        try {
            const auto v = settings().get("playback.honor_inverted");
            if (v.is_boolean()) honor = v.get<bool>();
        } catch (const std::exception&) {
        }
        if (!honor) {
            try {
                const samn::FunscriptView view = samn::LoadFunscript(std::filesystem::u8path(U8(path)));
                if (view.inverted) doc.general = view.actions;
            } catch (const std::exception&) {
            }
        }
    }
    if (doc.general.empty()) {
        setFailure(tr("Could not open %1: the script has no points.").arg(QFileInfo(path).fileName()));
        return false;
    }
    // The script's own recipe (profile, sync, contact vibration, feel,
    // balance) and the Player's settings over it shape what the device
    // gets, as 0.7's StartPlayback did with MapOptionsFromScript; a plain
    // script gets the defaults.
    scriptMap_ = samn::DefaultMapOptions();
    try {
        samn::Document view = doc;
        scriptMap_ = samn::MapOptionsFromScript(view.ToFunscript());
    } catch (const std::exception&) {
    }
    profile_ = doc.profile;
    playbackSource_ = QString::fromStdString(samn::NormalizePlaybackSource(doc.playback_source));
    recipeHasContact_ = doc.recipe.contact_vibration;
    contactMarks_ = doc.contact_marks && (samn::HasAnyArea(*doc.contact_marks) || !doc.contact_marks->tip_class.empty())
                        ? MarksInfo(*doc.contact_marks, doc.trajectory)
                        : QVariantMap{};
    tipPath_ = doc.trajectory ? samn::SortedTipPath(*doc.trajectory) : std::vector<samn::TrajectoryPoint>{};
    partnerPath_ = doc.trajectory ? samn::SortedPartnerPath(*doc.trajectory) : std::vector<samn::TrajectoryPoint>{};
    extraPaths_.clear();
    if (doc.trajectory) {
        extraPaths_.reserve(doc.trajectory->extras.size());
        for (size_t i = 0; i < doc.trajectory->extras.size(); ++i)
            extraPaths_.push_back(samn::SortedExtraPath(*doc.trajectory, i));
    }
    // 0.7 showContact: contact in the recipe, Neo axes, or tf / tj.
    contactControls_ = recipeHasContact_ || !doc.vibration.empty() || !doc.suction.empty() ||
                       samn::IsDistanceProfile(doc.profile);
    strength_ = samn::IsSamnPath(U8(path)) ? doc.strength_presets : std::vector<samn::StrengthPreset>{};
    activeStrength_ = samn::IsSamnPath(U8(path)) ? doc.active_strength : std::string();
    opening_ = true;  // the knobs follow the new script; remap(true) below maps it
    mapping_.resetContactKnobs(doc.recipe.contact_vibration_span, doc.recipe.contact_vibration_curve);
    opening_ = false;
    stroke_ = std::move(doc.general);
    scriptTempoBpm_ = EstimateStrokeBpm(stroke_);
    emit bpmChanged();  // effectiveBpm / auto grid follow the new stroke tempo
    // D-132: a lane the file carries shows without a device.
    vibrationFromScript_ = !doc.vibration.empty();
    suctionFromScript_ = !doc.suction.empty();
    // A new script starts on the stroke.
    if (activeLane_ != QLatin1String("stroke")) {
        activeLane_ = QStringLiteral("stroke");
        emit activeLaneChanged();
    }
    // 0.7 companion axis files (multi-axis devices); an unreadable one is left out.
    axisActions_.clear();
    for (const auto& [axis, file] : samn::companion_axis_files(U8(path))) {
        try {
            auto actions = samn::LoadFunscript(std::filesystem::u8path(file)).actions;
            if (!actions.empty()) axisActions_[axis] = std::move(actions);
        } catch (const std::exception&) {
        }
    }
    remap(true);
    // The heatmap uses the defaults, one bucket per 1/240 (0.7 GetHeatmap).
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
    marker_ = samn::LoadMarker(std::filesystem::u8path(U8(path))).value_or(samn::RangeMarker{});
    emit markerChanged();
    emit loopChanged();
    // Offset saved per script (0.7 key playback.offset.<path>).
    const auto saved = settings().get(samn::offset_key(U8(path)), 0);
    offsetMs_ = static_cast<int>(samn::clamp_offset(saved.is_number() ? saved.get<int64_t>() : 0));
    emit offsetMsChanged();
    setError({});

    QString video = toLocalPath(videoIn);
    if (video.isEmpty()) {
        if (const auto m = samn::find_matching_video(U8(path))) video = FromU8(*m);
    }
    pushToViews();
    emit scriptChanged();
    setVideo(video);
    setView(samn::FullView(timelineMs()));
    setVideoPositionMs(0);
    emit scriptOpened(scriptPath_);
    return true;
}

void PlayerController::remap(bool fresh) {
    if (stroke_.empty()) return;
    std::vector<samn::Frame> frames;
    samn::PlaybackPlan plan;
    try {
        const samn::StrengthPreset* preset = nullptr;
        for (const auto& p : strength_)
            if (!activeStrength_.empty() && p.name == activeStrength_) preset = &p;
        plan = samn::PlanPlayback(scriptMap_, profile_, mapping_.choices(recipeHasContact_), preset);
        frames = samn::PlaybackFrames(stroke_, plan);
    } catch (const std::exception& e) {
        setFailure(tr("Could not map the script: %1").arg(FromU8(e.what())));
        return;
    }
    // The vibration and suction lanes are what the device gets (0.7 sent
    // these frames), on the device tick.
    vibration_ = laneFrom(frames, false);
    suction_ = laneFrom(frames, true);
    audioVibApplied_ = false;
    if (fresh)
        output_.setScript(stroke_, vibration_, suction_, axisActions_);
    else
        output_.setLevels(vibration_, suction_);
    contactKnobsLive_ = plan.contact_on;
    emit mappedChanged();
    emit lanesChanged();
    if (!fresh) {
        for (const auto& c : curves_)
            if (c && c->lane() != QLatin1String("stroke"))
                c->setActions(c->lane() == QLatin1String("vibration") ? vibration_ : suction_);
        emit playheadMsChanged();  // the live values at the playhead
    }
    // Live audio vibration: re-apply after a mid-play remap when ready.
    if (output_.playing()) applyLiveAudioVibrationIfReady();
}

QStringList PlayerController::strengthPresets() const {
    QStringList out;
    for (const auto& p : strength_) out << FromU8(p.name);
    return out;
}

void PlayerController::setActiveStrength(const QString& nameIn) {
    const std::string name = U8(nameIn.trimmed());
    if (name == activeStrength_ || !samn::IsSamnPath(U8(scriptPath_))) return;
    // 0.7 SetActiveStrength: written into the .samn, so it stays with the script.
    try {
        const auto file = std::filesystem::u8path(U8(scriptPath_));
        samn::Document doc = samn::LoadDocument(file);
        doc.active_strength = name;
        samn::SaveDocument(file, doc);
    } catch (const std::exception& e) {
        setFailure(tr("Could not save the strength: %1").arg(FromU8(e.what())));
        emit mappedChanged();  // the choice shows the one still in use
        return;
    }
    activeStrength_ = name;
    remap(false);
}

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
    try {
        settings().set("playback.bpm_grid", on);
    } catch (const std::exception&) {
    }
    emit bpmChanged();
    pushMarks();
}

double PlayerController::effectiveBpm() const { return bpm_ > 0 ? bpm_ : scriptTempoBpm_; }

double PlayerController::beatPeriodMs() const {
    const double bpm = effectiveBpm();
    return bpmGrid_ && bpm > 0 ? 60000.0 / bpm : 0.0;
}

double PlayerController::guessBpmFromStroke() {
    if (stroke_.size() < 4) {
        setError(tr("Open a script with a clear stroke first."));
        return 0;
    }
    const double bpm = EstimateStrokeBpm(stroke_);
    if (bpm <= 0) {
        toolNote_ = tr("No clear beat in the stroke.");
        emit toolNoteChanged();
        setError(tr("No clear beat in the stroke."));
        return 0;
    }
    scriptTempoBpm_ = bpm;
    setBpm(bpm);
    setBpmGrid(true);
    toolNote_ = tr("%1 BPM from the stroke.").arg(int(bpm));
    emit toolNoteChanged();
    setError({});
    return bpm;
}

bool PlayerController::guessBpmFromAudio() {
    if (videoPath_.isEmpty()) {
        setError(tr("Link a video to measure BPM from its sound."));
        toolNote_ = tr("No audio tempo: link a video first.");
        emit toolNoteChanged();
        return false;
    }
    if (!bpmAudio_) {
        bpmAudio_ = new AudioLevelDecoder(this);
        connect(bpmAudio_, &AudioLevelDecoder::finished, this, [this](const std::vector<double>& env) {
            const double bpm = samn::EstimateBpmFromLoudness(env);
            if (bpm <= 0) {
                toolNote_ = tr("No clear beat in the sound.");
                emit toolNoteChanged();
                setError(tr("No clear beat in the sound."));
                return;
            }
            setBpm(bpm);
            setBpmGrid(true);
            toolNote_ = tr("%1 BPM from audio.").arg(int(bpm));
            emit toolNoteChanged();
            setError({});
        });
        connect(bpmAudio_, &AudioLevelDecoder::failed, this, [this](const QString& error) {
            toolNote_ = tr("No audio tempo: %1").arg(error);
            emit toolNoteChanged();
            setFailure(toolNote_);
        });
    }
    if (bpmAudio_->busy()) {
        setError(tr("Still measuring the sound…"));
        return false;
    }
    toolNote_ = tr("Measuring BPM from the sound…");
    emit toolNoteChanged();
    setError({});
    bpmAudio_->start(videoPath_);
    return true;
}

bool PlayerController::proposeSoundCurve() {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        return false;
    }
    if (videoPath_.isEmpty()) {
        setError(tr("Link a video to write the sound as a Vibration curve."));
        toolNote_ = tr("Sound Curve: link a video first.");
        emit toolNoteChanged();
        return false;
    }
    if (!soundCurveAudio_) {
        soundCurveAudio_ = new AudioLevelDecoder(this);
        connect(soundCurveAudio_, &AudioLevelDecoder::finished, this, [this](const std::vector<double>& env) {
            pendingSoundCurve_ = samn::VibrationCurveFromSound(env, samn::kAudioLevelStepMs, offsetMs_);
            emit soundCurveChanged();
            if (pendingSoundCurve_.size() < 2) {
                toolNote_ = tr("The sound is too short for a curve.");
                emit toolNoteChanged();
                setError(toolNote_);
                return;
            }
            toolNote_ = tr("Sound Curve ready · %1 points.").arg(int(pendingSoundCurve_.size()));
            emit toolNoteChanged();
            setError({});
            emit soundCurveReady(int(pendingSoundCurve_.size()));
        });
        connect(soundCurveAudio_, &AudioLevelDecoder::failed, this, [this](const QString& error) {
            pendingSoundCurve_.clear();
            emit soundCurveChanged();
            toolNote_ = tr("Sound Curve: %1").arg(error);
            emit toolNoteChanged();
            setFailure(toolNote_);
        });
    }
    if (soundCurveAudio_->busy()) {
        setError(tr("Still reading the sound…"));
        return false;
    }
    toolNote_ = tr("Reading the sound…");
    emit toolNoteChanged();
    setError({});
    soundCurveAudio_->start(videoPath_);
    return true;
}

void PlayerController::cancelSoundCurve() {
    pendingSoundCurve_.clear();
    emit soundCurveChanged();
}

bool PlayerController::canUndoSoundCurve() const {
    if (!soundCurveUndo_ || soundCurveUndoPath_.isEmpty() || scriptPath_.isEmpty()) return false;
    const std::string opened = U8(scriptPath_);
    const std::string fun = samn::IsSamnPath(opened) ? samn::CompanionFunscriptPath(opened) : opened;
    return FromU8(samn::CompanionSamnPath(fun)) == soundCurveUndoPath_;
}

bool PlayerController::confirmSoundCurve() {
    if (pendingSoundCurve_.size() < 2) {
        setError(tr("Nothing to write — propose a Sound Curve first."));
        return false;
    }
    auto curve = std::move(pendingSoundCurve_);
    pendingSoundCurve_.clear();
    emit soundCurveChanged();
    return applyVibrationLane(std::move(curve), tr("Sound Curve"), true);
}

bool PlayerController::undoSoundCurve() {
    if (!canUndoSoundCurve()) {
        setError(tr("Nothing to undo for the Sound Curve."));
        return false;
    }
    auto prev = std::move(*soundCurveUndo_);
    soundCurveUndo_.reset();
    soundCurveUndoPath_.clear();
    emit soundCurveChanged();
    return applyVibrationLane(std::move(prev), tr("Undo Sound Curve"), false);
}

bool PlayerController::applyVibrationLane(std::vector<samn::Action> vibration, const QString& label, bool recordUndo) {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        return false;
    }
    // Write needs ≥2 points; undo may restore an empty prior lane.
    if (recordUndo && vibration.size() < 2) {
        setError(tr("The sound is too short for a curve."));
        return false;
    }
    const int nPoints = int(vibration.size());
    namespace fs = std::filesystem;
    const std::string openedU8 = U8(scriptPath_);
    const fs::path opened = fs::u8path(openedU8);
    const bool wasSamn = samn::IsSamnPath(openedU8);
    const std::string funscriptU8 = wasSamn ? samn::CompanionFunscriptPath(openedU8) : openedU8;
    const fs::path funscript = fs::u8path(funscriptU8);
    const fs::path samnPath = fs::u8path(samn::CompanionSamnPath(funscriptU8));
    const QString undoKey = FromU8(samnPath.u8string());
    std::vector<samn::Action> previous;

    try {
        samn::Document doc;
        if (wasSamn || fs::exists(samnPath)) {
            doc = samn::LoadDocument(wasSamn ? opened : samnPath);
        } else {
            doc = samn::LoadFunscriptFile(funscript);
        }
        if (doc.general.size() < 2) doc.general = stroke_;
        if (recordUndo) previous = doc.vibration;
        doc.vibration = std::move(vibration);
        if (doc.version == 0) doc.version = samn::SamnCurrentVersion;
        if (doc.kind.empty()) doc.kind = samn::SamnKind;
        if (doc.creator.empty()) doc.creator = "SamNPlayer";
        const char* source = samn::PlaybackAxes;
        doc.playback_source = source;
        doc.recipe.playback_source = source;
        samn::SaveDocument(samnPath, doc);
    } catch (const std::exception& e) {
        setFailure(tr("%1: %2").arg(label, FromU8(e.what())));
        return false;
    }

    const QString reload = wasSamn ? scriptPath_ : FromU8(samnPath.u8string());
    if (!openScript(reload, videoPath_)) return false;
    if (recordUndo) {
        soundCurveUndo_ = std::move(previous);
        soundCurveUndoPath_ = undoKey;
    }
    toolNote_ = nPoints > 0 ? tr("%1 · %2 points.").arg(label).arg(nPoints) : tr("%1 · vibration cleared.").arg(label);
    emit toolNoteChanged();
    emit soundCurveChanged();
    emit scriptEdited(scriptPath_);
    return true;
}

void PlayerController::clearAutoSyncSuggestion() {
    autoSyncBusy_ = false;
    autoSyncNote_.clear();
    autoSyncConfidence_ = QStringLiteral("none");
    autoSyncOffsetMs_ = 0;
    emit autoSyncChanged();
}

bool PlayerController::autoSyncCanApply() const {
    if (autoSyncConfidence_ == QLatin1String("none") || autoSyncBusy_) return false;
    return autoSyncOffsetMs_ != offsetMs_;
}

bool PlayerController::suggestAudioSync() {
    if (scriptPath_.isEmpty() || stroke_.size() < 2) {
        setError(tr("Open a script first."));
        autoSyncNote_ = tr("Auto sync needs a script.");
        emit autoSyncChanged();
        return false;
    }
    if (videoPath_.isEmpty()) {
        setError(tr("Link a video to compare its sound with the script."));
        autoSyncNote_ = tr("Auto sync: link a video first.");
        emit autoSyncChanged();
        return false;
    }
    if (!autoSyncAudio_) {
        autoSyncAudio_ = new AudioLevelDecoder(this);
        connect(autoSyncAudio_, &AudioLevelDecoder::finished, this, [this](const std::vector<double>& env) {
            autoSyncBusy_ = false;
            const auto sug = samn::EstimateAudioSyncOffset(env, samn::kAudioLevelStepMs, stroke_);
            autoSyncOffsetMs_ = int(sug.offset_ms);
            switch (sug.confidence) {
                case samn::AudioSyncConfidence::High:
                    autoSyncConfidence_ = QStringLiteral("high");
                    break;
                case samn::AudioSyncConfidence::Low:
                    autoSyncConfidence_ = QStringLiteral("low");
                    break;
                default:
                    autoSyncConfidence_ = QStringLiteral("none");
                    break;
            }
            if (sug.confidence != samn::AudioSyncConfidence::None && autoSyncOffsetMs_ == offsetMs_) {
                autoSyncNote_ = tr("That is the offset in use.");
            } else {
                autoSyncNote_ = QString::fromStdString(sug.note);
            }
            toolNote_ = autoSyncNote_;
            emit toolNoteChanged();
            emit autoSyncChanged();
            if (sug.confidence == samn::AudioSyncConfidence::None) setError(autoSyncNote_);
            else setError({});
        });
        connect(autoSyncAudio_, &AudioLevelDecoder::failed, this, [this](const QString& error) {
            autoSyncBusy_ = false;
            autoSyncConfidence_ = QStringLiteral("none");
            autoSyncOffsetMs_ = 0;
            autoSyncNote_ = tr("Auto sync: %1").arg(error);
            toolNote_ = autoSyncNote_;
            emit toolNoteChanged();
            emit autoSyncChanged();
            setFailure(autoSyncNote_);
        });
    }
    if (autoSyncAudio_->busy() || autoSyncBusy_) {
        setError(tr("Still listening…"));
        return false;
    }
    autoSyncBusy_ = true;
    autoSyncNote_ = tr("Listening…");
    emit autoSyncChanged();
    toolNote_ = autoSyncNote_;
    emit toolNoteChanged();
    setError({});
    autoSyncAudio_->start(videoPath_);
    return true;
}

bool PlayerController::applyAudioSyncSuggestion() {
    if (!autoSyncCanApply()) {
        if (autoSyncConfidence_ != QLatin1String("none") && autoSyncOffsetMs_ == offsetMs_) {
            autoSyncNote_ = tr("That is the offset in use.");
            emit autoSyncChanged();
        } else {
            setError(tr("Nothing to apply — run Auto sync first."));
        }
        return false;
    }
    setOffsetMs(autoSyncOffsetMs_);
    autoSyncNote_ = tr("Applied %1 ms — Bake offset keeps it for other players.").arg(autoSyncOffsetMs_);
    toolNote_ = autoSyncNote_;
    emit toolNoteChanged();
    emit autoSyncChanged();
    setError({});
    return true;
}

bool PlayerController::attachVideo(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    if (!QFileInfo(path).isFile()) {
        setFailure(tr("Video not found: %1").arg(path));
        return false;
    }
    if (scriptPath_.isEmpty()) return openPath(path);
    setError({});  // before setVideo: the player may report a broken video at once
    setVideo(path);
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
        setFailure(tr("Snapshot failed: no video frame yet."));
        return false;
    }
    QString path = toLocalPath(pathOrUrl);
    if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
    if (!img.save(path, "PNG")) {
        setFailure(tr("Snapshot failed: cannot write %1").arg(path));
        return false;
    }
    setError({});
    return true;
}

QVariant PlayerController::uiValue(const QString& key, const QVariant& fallback) {
    try {
        const auto v = settings().get("ui." + U8(key), nlohmann::json());
        if (v.is_string()) return FromU8(v.get<std::string>());
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
    default: j = U8(value.toString());
    }
    try {
        settings().set("ui." + U8(key), j);
    } catch (const std::exception&) {
        // A remembered tab is a convenience; never an error for the user.
    }
}

void PlayerController::clearScript() {
    strength_.clear();
    activeStrength_.clear();
    contactMarks_.clear();
    tipPath_.clear();
    partnerPath_.clear();
    extraPaths_.clear();
    contactControls_ = contactKnobsLive_ = false;
    playbackSource_ = QStringLiteral("recipe");
    emit mappedChanged();
    stroke_.clear();
    vibration_.clear();
    suction_.clear();
    axisActions_.clear();
    output_.setScript({}, {}, {}, {});
    emit lanesChanged();
    heat_.clear();
    marks_ = {};
    emit marksChanged();
    scriptPath_.clear();
    if (scriptTempoBpm_ != 0) {
        scriptTempoBpm_ = 0;
        emit bpmChanged();
    }
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
    mediaHead_.clear();
    mediaHead_.setDurationMs(0);
    clearPlayEnvelope();
    emit videoChanged();
    if (!path.isEmpty()) emit videoLinked(path);
    emit scriptChanged();  // noScriptForVideo depends on the video too
    if (audioVibration() && !path.isEmpty()) warmPlayAudio();
}

void PlayerController::setError(const QString& e) {
    mediaError_ = false;
    if (e == lastError_) return;
    lastError_ = e;
    emit lastErrorChanged();
}

void PlayerController::setFailure(const QString& e) {
    setError(e);
    AppLog::instance().reportError(e);
}

void PlayerController::setOffsetMs(int ms) {
    ms = static_cast<int>(samn::clamp_offset(ms));
    if (ms == offsetMs_) return;
    const bool wasZoomed = zoomed();
    offsetMs_ = ms;
    if (ms != 0 && !toolNote_.isEmpty()) {
        toolNote_.clear();  // a new offset: the last bake's note is old news
        emit toolNoteChanged();
    }
    setView(wasZoomed ? view_ : samn::FullView(timelineMs()));
    if (!scriptPath_.isEmpty()) {
        try {
            settings().set(samn::offset_key(U8(scriptPath_)), offsetMs_);
        } catch (const std::exception& e) {
            setFailure(tr("Could not save the offset: %1").arg(FromU8(e.what())));
        }
    }
    emit offsetMsChanged();
    // While the video plays the output stays where the estimate is (no step
    // back to the last report) and moves on from the next report. The
    // change is no report then: it neither feeds the stale watchdog nor
    // wakes the output (the script clock keeps its path).
    mediaHead_.hold();
    const bool video = mediaHead_.playing();
    movePlayhead(videoMs_, video ? mediaHead_.estimate() : videoMs_, !video);
}

void PlayerController::setVideoPositionMs(qint64 videoMs) {
    // 0.7 FPS-Snap: explicit seeks land on frame boundaries of script time.
    if (fpsSnap_ > 0 && !stroke_.empty()) {
        const int64_t script = samn::SnapMs(samn::script_time(videoMs, offsetMs_), fpsSnap_);
        videoMs = script + offsetMs_;
        if (videoMs < 0) videoMs = 0;
    }
    movePlayhead(videoMs, videoMs, true);
}

void PlayerController::movePlayhead(qint64 videoMs, qint64 outputVideoMs, bool fromReport) {
    videoMs_ = videoMs;
    const qint64 ph = stroke_.empty() ? -1 : samn::script_time(videoMs, offsetMs_);
    if (ph != playheadMs_) {
        playheadMs_ = ph;
        emit playheadMsChanged();
    }
    for (const auto& c : curves_)
        if (c) c->setPlayheadMs(playheadMs_);
    if (heatmap_) heatmap_->setPlayheadMs(playheadMs_);
    sendToOutput(outputVideoMs, fromReport);
    checkAutoExtendedO(ph);
}

void PlayerController::reportMediaPosition(qint64 videoMs) {
    const qint64 out = mediaHead_.report(videoMs);
    // At the loop end the page jumps back right after this call: nothing
    // is estimated past the end until the jump's report.
    if (mediaHead_.playing() && loopJumpFor(videoMs) >= 0) mediaHead_.seek();
    movePlayhead(videoMs, out, true);
}

void PlayerController::holdMediaPlayhead() { mediaHead_.seek(); }

void PlayerController::updateMediaLimit() {
    // The ms before the loop end in video time: only a report reaches it.
    mediaHead_.setLimitMs(loopActive() ? std::max<qint64>(0, marker_.end_ms + offsetMs_ - 1) : -1);
}

void PlayerController::sendToOutput(qint64 videoMs, bool fromReport) {
    // One offset, applied to the (estimated) video time (PS-01).
    if (!stroke_.empty()) output_.update(samn::script_time(videoMs, offsetMs_), fromReport);
}

void PlayerController::checkAutoExtendedO(qint64 ph) {
    if (ph < 0 || !marker_.Valid()) return;
    if (ph < marker_.start_ms) {
        autoEoFired_ = false;  // before the range: armed again
        return;
    }
    if (!autoEo_ || autoEoFired_ || !output_.playing() || ph > marker_.end_ms) return;
    autoEoFired_ = true;  // once per pass, also when Extended-O is off (0.7)
    output_.triggerExtendedO();
}

void PlayerController::setAutoExtendedO(bool on) {
    if (on == autoEo_) return;
    autoEo_ = on;
    emit loopChanged();
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
            samn::SaveMarker(std::filesystem::u8path(U8(scriptPath_)), marker_);
        } catch (const std::exception& e) {
            setFailure(tr("Could not save the marked range: %1").arg(FromU8(e.what())));
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

bool PlayerController::showScenes() const {
    return const_cast<PlayerController*>(this)->uiValue(QStringLiteral("player.scenes"), false).toBool();
}

void PlayerController::setShowScenes(bool on) {
    if (on == showScenes()) return;
    setUiValue(QStringLiteral("player.scenes"), on);
    emit viewOptionsChanged();
}

bool PlayerController::showContactMarks() const {
    return const_cast<PlayerController*>(this)->uiValue(QStringLiteral("player.showContactMarks"), true).toBool();
}

void PlayerController::setShowContactMarks(bool on) {
    if (on == showContactMarks()) return;
    setUiValue(QStringLiteral("player.showContactMarks"), on);
    emit viewOptionsChanged();
}

bool PlayerController::trajectoryOverlay() const {
    return const_cast<PlayerController*>(this)->uiValue(QStringLiteral("player.trajectoryOverlay"), false).toBool();
}

void PlayerController::setTrajectoryOverlay(bool on) {
    if (on == trajectoryOverlay()) return;
    setUiValue(QStringLiteral("player.trajectoryOverlay"), on);
    emit viewOptionsChanged();
}

namespace {

QVariantList PathPoints(const std::vector<samn::TrajectoryPoint>& path) {
    QVariantList out;
    out.reserve(int(path.size()));
    for (const auto& p : path) out.append(QVariantMap{{"x", p.x}, {"y", p.y}});
    return out;
}

}  // namespace

QVariantMap PlayerController::tipAt(double scriptMs) const {
    const auto p = samn::PathAt(tipPath_, int64_t(std::llround(scriptMs)));
    if (!p) return {};
    return {{"x", p->x}, {"y", p->y}};
}

QVariantMap PlayerController::partnerAt(double scriptMs) const {
    const auto p = samn::PathAt(partnerPath_, int64_t(std::llround(scriptMs)));
    if (!p) return {};
    return {{"x", p->x}, {"y", p->y}};
}

QVariantMap PlayerController::extraAt(int index, double scriptMs) const {
    if (index < 0 || size_t(index) >= extraPaths_.size()) return {};
    const auto p = samn::PathAt(extraPaths_[size_t(index)], int64_t(std::llround(scriptMs)));
    if (!p) return {};
    return {{"x", p->x}, {"y", p->y}};
}

QVariantList PlayerController::tipPathPoints() const { return PathPoints(tipPath_); }

QVariantList PlayerController::partnerPathPoints() const { return PathPoints(partnerPath_); }


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
    return FromU8(samn::FormatTooltipTime(int64_t(ms))) + QStringLiteral(" · %1%").arg(std::lround(heat_[size_t(i)] * 100));
}

void PlayerController::setSleepMinutes(int minutes) {
    minutes = std::clamp(minutes, 0, 24 * 60);
    if (minutes == sleepMinutes_) return;
    sleepMinutes_ = minutes;
    playedMs_ = 0;  // a new choice counts from now (0.7 re-arms on change)
    if (playClock_.isValid()) playClock_.restart();
    emit sleepChanged();
}

void PlayerController::reportMediaError(int error, const QString& detail) {
    QString kind;
    switch (error) {
    case QMediaPlayer::NoError: return;
    case QMediaPlayer::ResourceError: kind = tr("file missing or unreadable"); break;
    case QMediaPlayer::FormatError: kind = tr("format not supported"); break;
    case QMediaPlayer::NetworkError: kind = tr("network error"); break;
    case QMediaPlayer::AccessDeniedError: kind = tr("access denied"); break;
    default: kind = tr("playback error"); break;
    }
    QString msg = tr("Video cannot be played (%1)").arg(kind);
    if (!videoName().isEmpty()) msg += QStringLiteral(": ") + videoName();
    if (!detail.isEmpty()) msg += QStringLiteral(" ") + QChar(0x2014) + QStringLiteral(" ") + detail;
    msg += tr(". “Make playable” creates an H.264 copy.");
    setFailure(msg);
    mediaError_ = true;
}

void PlayerController::reportFirstPicture(const QString& codec) {
    qInfo().noquote() << QStringLiteral("Video: first picture%1%2")
                             .arg(codec.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(codec))
                             .arg(videoName().isEmpty() ? QString() : QStringLiteral(": ") + videoName());
}

void PlayerController::reportNoPicture(const QString& codec) {
    QString msg = codec.isEmpty() ? tr("No picture: this PC cannot decode the video")
                                  : tr("No picture: this PC cannot decode %1 video").arg(codec);
    if (!videoName().isEmpty()) msg += QStringLiteral(": ") + videoName();
    msg += tr(". “Make playable” creates an H.264 copy that plays everywhere.");
    setFailure(msg);
    mediaError_ = true;
}

bool PlayerController::videoPlayAutostart() const {
    const auto v = const_cast<PlayerController*>(this)->settings().get("playback.video_play_autostart", true);
    return !v.is_boolean() || v.get<bool>();
}

void PlayerController::setVideoPlayAutostart(bool on) {
    if (on == videoPlayAutostart()) return;
    settings().set("playback.video_play_autostart", on);
    emit viewOptionsChanged();
}

bool PlayerController::audioVibration() const {
    const auto v = const_cast<PlayerController*>(this)->settings().get("playback.audio_vibration", false);
    return v.is_boolean() && v.get<bool>();
}

void PlayerController::setAudioVibration(bool on) {
    if (on == audioVibration()) return;
    settings().set("playback.audio_vibration", on);
    emit viewOptionsChanged();
    if (on) {
        warmPlayAudio();
    } else {
        restoreScriptVibrationIfNeeded();
    }
}

void PlayerController::setPlayAudioEnvelopeForTest(std::vector<double> envelope) {
    const bool had = !playEnvelope_.empty();
    playEnvelope_ = std::move(envelope);
    if (had != !playEnvelope_.empty()) emit audioVibrationReadyChanged();
}

void PlayerController::ensurePlayAudioDecoder() {
    if (playAudio_) return;
    playAudio_ = new AudioLevelDecoder(this);
    connect(playAudio_, &AudioLevelDecoder::finished, this, [this](const std::vector<double>& env) {
        playEnvelope_ = env;
        emit audioVibrationReadyChanged();
        if (output_.playing()) applyLiveAudioVibrationIfReady();
        if (playEnvelope_.empty())
            qInfo().noquote() << QStringLiteral("Audio vibration: empty envelope — keeping script vibration");
        else
            qInfo().noquote() << QStringLiteral("Audio vibration: envelope ready (%1 steps)").arg(int(playEnvelope_.size()));
    });
    connect(playAudio_, &AudioLevelDecoder::failed, this, [this](const QString& error) {
        clearPlayEnvelope();
        qInfo().noquote() << QStringLiteral("Audio vibration: %1 — keeping script vibration").arg(error);
    });
}

void PlayerController::warmPlayAudio() {
    if (videoPath_.isEmpty()) return;
    ensurePlayAudioDecoder();
    if (playAudio_->busy()) return;
    playAudio_->start(videoPath_);
}

void PlayerController::clearPlayEnvelope() {
    const bool had = !playEnvelope_.empty();
    playEnvelope_.clear();
    if (had) emit audioVibrationReadyChanged();
}

double PlayerController::activeVibrationScale() const {
    for (const auto& p : strength_) {
        if (!activeStrength_.empty() && p.name == activeStrength_)
            return p.vibration_scale > 0 ? p.vibration_scale : 1.0;
    }
    return 1.0;
}

void PlayerController::applyLiveAudioVibrationIfReady() {
    if (!audioVibration()) return;
    if (stroke_.empty()) return;
    if (videoPath_.isEmpty()) {
        qInfo().noquote() << QStringLiteral("Audio vibration: no video — keeping script vibration");
        return;
    }
    if (playEnvelope_.empty()) {
        warmPlayAudio();
        qInfo().noquote() << QStringLiteral("Audio vibration: envelope not ready — keeping script vibration");
        return;
    }
    try {
        const samn::StrengthPreset* preset = nullptr;
        for (const auto& p : strength_)
            if (!activeStrength_.empty() && p.name == activeStrength_) preset = &p;
        const auto plan = samn::PlanPlayback(scriptMap_, profile_, mapping_.choices(recipeHasContact_), preset);
        auto frames = samn::PlaybackFrames(stroke_, plan);
        frames = samn::ApplyAudioVibration(frames, playEnvelope_, samn::kAudioLevelStepMs, offsetMs_,
                                           activeVibrationScale());
        vibration_ = laneFrom(frames, false);
        suction_ = laneFrom(frames, true);
        output_.setLevels(vibration_, suction_);
        audioVibApplied_ = true;
        for (const auto& c : curves_)
            if (c && c->lane() != QLatin1String("stroke"))
                c->setActions(c->lane() == QLatin1String("vibration") ? vibration_ : suction_);
        emit playheadMsChanged();
        qInfo().noquote() << QStringLiteral("Audio vibration: live from video sound");
    } catch (const std::exception& e) {
        qInfo().noquote() << QStringLiteral("Audio vibration: %1 — keeping script vibration").arg(FromU8(e.what()));
    }
}

void PlayerController::restoreScriptVibrationIfNeeded() {
    if (!audioVibApplied_) return;
    remap(false);
}

bool PlayerController::playbackMock() const {
    const auto v = const_cast<PlayerController*>(this)->settings().get("playback.mock", true);
    return !v.is_boolean() || v.get<bool>();
}

void PlayerController::setPlaybackMock(bool on) {
    if (on == playbackMock()) return;
    settings().set("playback.mock", on);
    emit viewOptionsChanged();
}

void PlayerController::setMediaPlaying(bool playing) {
    // A pause stops the ticks before the output stops the device.
    if (!playing) {
        mediaHead_.setPlaying(false);
        setPlaying(false);
        return;
    }
    // Video is playing: the media playhead ticks either way. The device only
    // starts when "Video play starts device" is on (0.7 default).
    mediaHead_.setPlaying(true);
    if (videoPlayAutostart()) setPlaying(true);
}

void PlayerController::setPlaying(bool playing) {
    // 0.7 playback.mock: with nothing connected, Play starts the Mock so the
    // session has a device without opening Devices first. Output waits until
    // the worker reports connected (same soft-start path as a late link).
    if (playing && playbackMock() && device_ && !device_->connected() && !device_->connecting()) {
        device_->connectMock();
        qInfo().noquote() << QStringLiteral("Playback: connecting Mock device");
    }
    // Apply live audio vibration before the device session starts; restore
    // script lanes only after output_.playing is false so remap does not
    // re-apply mid-stop.
    if (playing) applyLiveAudioVibrationIfReady();
    output_.setPlaying(playing);
    if (!playing) restoreScriptVibrationIfNeeded();
    if (playing) autoEoFired_ = false;
    if (playing && mediaError_) setError({});
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
    mediaHead_.setDurationMs(videoDurationMs_);
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
        if (const auto it = axisActions_.find(lane.toStdString()); it != axisActions_.end())
            c->setActions(it->second);
        else
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

QVariantMap PlayerController::scriptQuality() {
    if (scriptPath_.isEmpty()) return {{"error", tr("no script loaded")}};
    try {
        const auto r = samn::ScriptQualityOfFile(std::filesystem::u8path(U8(scriptPath_)));
        QStringList warnings;
        for (const auto& w : r.warnings) warnings << FromU8(w);
        return {{"score", r.score}, {"passed", r.passed}, {"warnings", warnings}, {"error", QString()}};
    } catch (const std::exception& e) {
        return {{"error", FromU8(e.what())}};
    }
}

QString PlayerController::exportHeatmapPng() {
    if (scriptPath_.isEmpty() || stroke_.empty()) {
        setError(tr("Heatmap: no script loaded"));
        return {};
    }
    samn::HeatmapImageOptions o;
    o.width = 960;
    o.height = 56;
    o.chapters = marks_.chapters;
    try {
        const samn::HeatmapImage img = samn::RenderHeatmapImage(stroke_, o);
        QImage out(img.rgba.data(), img.width, img.height, img.width * 4, QImage::Format_RGBA8888);
        const QFileInfo fi(scriptPath_);
        const QString path = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".heatmap.png"));
        if (!out.save(path, "PNG")) {
            setFailure(tr("Heatmap: could not write %1").arg(path));
            return {};
        }
        return path;
    } catch (const std::exception& e) {
        setFailure(tr("Heatmap: %1").arg(FromU8(e.what())));
        return {};
    }
}

QString PlayerController::saveProject(qint64 videoMs) {
    samn::Project p;
    p.video_path = U8(videoPath_);
    p.script_path = U8(scriptPath_);
    const QString media = !videoPath_.isEmpty() ? videoPath_ : scriptPath_;
    if (media.isEmpty()) {
        setError(tr("Project: no video/script for project"));
        return {};
    }
    p.offset_ms = offsetMs_;
    p.seek_ms = std::max<qint64>(0, videoMs);
    if (marker_.Valid()) p.loop_marker = marker_;
    p.last_opened_ms = QDateTime::currentMSecsSinceEpoch();
    const auto path = samn::ProjectPathFor(std::filesystem::u8path(U8(media)));
    try {
        samn::SaveProject(path, p);
    } catch (const std::exception& e) {
        setFailure(tr("Project: %1").arg(FromU8(e.what())));
        return {};
    }
    return FromU8(path.u8string());
}

qint64 PlayerController::loadProject(const QString& pathOrUrl) {
    samn::Project p;
    try {
        p = samn::LoadProject(std::filesystem::u8path(U8(toLocalPath(pathOrUrl))));
    } catch (const std::exception& e) {
        setFailure(tr("Load project: %1").arg(FromU8(e.what())));
        return -1;
    }
    if (p.script_path.empty()) {
        setError(tr("Project has no script path"));
        return -1;
    }
    if (!openScript(FromU8(p.script_path), FromU8(p.video_path))) return -1;
    if (!p.video_path.empty() && videoPath_ != FromU8(p.video_path)) attachVideo(FromU8(p.video_path));
    setOffsetMs(int(p.offset_ms));
    if (p.loop_marker && p.loop_marker->Valid()) setMarker(double(p.loop_marker->start_ms), double(p.loop_marker->end_ms));
    return std::max<qint64>(0, p.seek_ms);
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
