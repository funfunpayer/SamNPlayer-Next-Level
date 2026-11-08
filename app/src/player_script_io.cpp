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
#include "samn/playback.h"
#include "samn/playback_mapping.h"
#include "samn/project.h"
#include "samn/range_edit.h"
#include "samn/samn_file.h"
#include "samn/script_finder.h"
#include "samn/script_quality.h"
#include "samn/sound_vibration.h"

namespace samn::ui {

using player_internal::EditLane;
using player_internal::ParseEditLane;

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

}  // namespace samn::ui
