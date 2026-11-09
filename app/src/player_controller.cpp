// PlayerController core: open/remap/playhead/output. Script tools live in
// player_script_io.cpp, audio jobs in player_audio_jobs.cpp, marks in
// player_marks.cpp (same class; no QML API change).
#include "player_controller.h"

#include "player_internal.h"

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

using player_internal::LaneFrom;

namespace {

bool isVideo(const QString& path) {
    const std::string suffix = "." + QFileInfo(path).suffix().toLower().toStdString();
    return std::find(samn::VIDEO_EXTS.begin(), samn::VIDEO_EXTS.end(), suffix) != samn::VIDEO_EXTS.end();
}

bool isScript(const QString& path) {
    const std::string suffix = "." + QFileInfo(path).suffix().toLower().toStdString();
    return std::find(samn::SCRIPT_EXTS.begin(), samn::SCRIPT_EXTS.end(), suffix) != samn::SCRIPT_EXTS.end();
}

// 0.7 #pb-variant: "Soft", "By someone" or "Main script", and where it lies.
// index is the row whose path equals preferredPath, or -1.
struct VariantRows {
    QVariantList list;
    int index = -1;
};

VariantRows variantRowsFor(const samn::VideoScriptMatch& match, const std::string& preferredPath) {
    VariantRows rows;
    for (size_t i = 0; i < match.variants.size(); ++i) {
        const auto& v = match.variants[i];
        QString name = FromU8(v.label);
        name = name.isEmpty() ? PlayerController::tr("Main script") : name.left(1).toUpper() + name.mid(1);
        if (!v.beside) name += PlayerController::tr(" · script folder");
        rows.list.append(QVariantMap{{"path", FromU8(v.path)}, {"label", FromU8(v.label)}, {"display", name}, {"beside", v.beside}});
        if (v.path == preferredPath) rows.index = static_cast<int>(i);
    }
    return rows;
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
    const auto rows = variantRowsFor(match, match.script_path);
    variants_ = rows.list;
    variantIndex_ = rows.index;
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
    if (!videoPath_.isEmpty()) refreshVariantsForOpenVideo();
}

void PlayerController::refreshVariantsForOpenVideo() {
    if (videoPath_.isEmpty()) {
        variants_.clear();
        variantIndex_ = -1;
        emit variantsChanged();
        return;
    }
    std::vector<std::string> folders;
    for (const auto& f : scriptFolders()) folders.push_back(U8(f));
    const auto match = samn::find_script_for_video(U8(videoPath_), folders);
    const auto rows = variantRowsFor(match, U8(scriptPath_));
    variants_ = rows.list;
    variantIndex_ = rows.index;
    emit variantsChanged();
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
    // No script yet: search again (that may load one). A script already open
    // only refreshes the variant list, so playback stays put.
    if (!hasScript() && !videoPath_.isEmpty()) {
        const QString video = videoPath_;
        openPath(video);
        if (!hasScript()) setError(tr("No script for %1 in that folder either.").arg(QFileInfo(video).fileName()));
    } else if (!videoPath_.isEmpty()) {
        refreshVariantsForOpenVideo();
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
    // Curve-edit undo is per script, like the Generator's EditSession on open.
    // The reload after a range edit keeps the step pushed before the write.
    if (!preservingEditHistory_) resetCurveHistory(doc);
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
    scriptTempoBpm_ = player_internal::EstimateStrokeBpm(stroke_);
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
    vibration_ = LaneFrom(frames, false);
    suction_ = LaneFrom(frames, true);
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
    resetCurveHistory({});
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
    refreshVideoLinkWarning(path);
    emit videoChanged();
    if (!path.isEmpty()) emit videoLinked(path);
    emit scriptChanged();  // noScriptForVideo depends on the video too
    if (audioVibration() && !path.isEmpty()) warmPlayAudio();
}

void PlayerController::refreshVideoLinkWarning(const QString& path) {
    // Lightweight 0.7 ProbePlaybackVideo stand-in: container + name tags.
    // Qt Multimedia plays more codecs than Chromium did; we still warn on
    // mkv/avi and obvious HEVC/AV1 name tags before play fails.
    QString warn;
    bool suggest = false;
    if (!path.isEmpty()) {
        const QFileInfo fi(path);
        const QString suf = fi.suffix().toLower();
        const QString name = fi.fileName().toLower();
        if (suf == QLatin1String("mkv") || suf == QLatin1String("avi")) {
            warn = tr("Container often problematic in the player — H.264 MP4 copy recommended.");
            suggest = true;
        } else if (name.contains(QLatin1String("hevc")) || name.contains(QLatin1String("x265")) ||
                   name.contains(QLatin1String("h265")) || name.contains(QLatin1String("av1"))) {
            warn = tr("Codec hint in the file name — playback may fail.");
            suggest = true;
        }
    }
    if (warn == videoLinkWarning_ && suggest == videoLinkSuggestPlayable_) return;
    videoLinkWarning_ = warn;
    videoLinkSuggestPlayable_ = suggest;
    emit videoLinkWarningChanged();
}

void PlayerController::clearVideoLinkWarning() {
    if (videoLinkWarning_.isEmpty() && !videoLinkSuggestPlayable_) return;
    videoLinkWarning_.clear();
    videoLinkSuggestPlayable_ = false;
    emit videoLinkWarningChanged();
}

void PlayerController::dismissVideoLinkWarning() { clearVideoLinkWarning(); }

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
    clearVideoLinkWarning();  // it played — drop the soft probe hint
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
