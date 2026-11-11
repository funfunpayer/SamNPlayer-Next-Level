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
#include "samn/audio_tempo.h"
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

#include <QMediaPlayer>

namespace samn::ui {

using player_internal::EstimateStrokeBpm;
using player_internal::LaneFrom;

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
            const auto rel = samn::AssessTempo(bpmAudio_->onsetBlocks(), bpmAudio_->onsetTempo());
            const auto guess = samn::PickPlaybackAudioBpm(bpmAudio_->onsetTempo(), rel, env);
            if (guess.bpm <= 0) {
                toolNote_ = tr("No clear beat in the sound.");
                emit toolNoteChanged();
                setError(tr("No clear beat in the sound."));
                return;
            }
            setBpm(guess.bpm);
            setBpmGrid(true);
            toolNote_ = guess.source == samn::PlaybackBpmSource::kOnsets
                            ? tr("%1 BPM from audio.").arg(int(guess.bpm))
                            : tr("%1 BPM from audio (loudness).").arg(int(guess.bpm));
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
    // First 10 minutes — EstimateTempo / EstimateBpmFromLoudness ignore the rest.
    bpmAudio_->start(videoPath_, samn::kAudioLevelStepMs, 600000, true);
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
    // First 10 minutes — EstimateAudioSyncOffset ignores the rest.
    autoSyncAudio_->start(videoPath_, samn::kAudioLevelStepMs, 600000);
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
        vibration_ = LaneFrom(frames, false);
        suction_ = LaneFrom(frames, true);
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

}  // namespace samn::ui
