"""audio_check.py - Plausibilitätsprüfung des Skript-Tempos gegen die
Audiospur des Videos.

Kein KI-Baustein (gehört deshalb nicht zu docs/AI_ADAPTER.md), sondern eine
klassische, regelbasierte Prüfung im Stil von quality_doctor.py: beide
messen eine Zahl aus dem Signal und vergleichen sie gegen eine Erwartung,
ohne trainiertes Modell.

Idee: die dominante Frequenz der Audio-Energiehüllkurve (wie laut/aktiv ist
die Tonspur gerade) korreliert oft mit dem tatsächlichen Bewegungstempo -
nicht 1:1 phasengleich wie eine Positionskurve, aber im TEMPO vergleichbar.
Weicht das aus dem Video getrackte Skript-Tempo stark vom Audio-Tempo ab
(auch nicht bei einem plausiblen Vielfachen wie x2/x0.5), ist das ein
Hinweis - kein Beweis -, dass das Tracking danebenliegt oder eine
untypische Szene vorliegt. Genau wie ai_quality.py's Zweitmeinung: die
Prüfung liefert eine WARNUNG, sie überschreibt nie quality["passed"]/
quality["score"] und korrigiert nichts automatisch.

Zusätzlich (Go-Parity): Speech-Hold + Segment-Labels holding/gentle/intense/
climax als Review-/Kapitel-Hinweise — multi-Band-Energie, keine Stroke-Kurve.
Die vollständige Segment-Taxonomie lebt im Go-Pfad (generator/audio_segments.go);
Python liefert dieselben Metadata-Felder, wenn die Analyse greift.

Ausführen der Tests: python3 generator/audio_check_test.py
"""

import subprocess
import sys

import numpy as np

# Plausibler Bereich für ein Bewegungstempo - siehe device_profile.py
# (SLOWEST_FULL_STROKE_MS entspricht ~1.1Hz als langsamste sinnvolle Grenze;
# 5Hz ist bereits deutlich schneller als jede reale Vollhub-Bewegung).
MIN_TEMPO_HZ = 0.2
MAX_TEMPO_HZ = 5.0

# Erlaubte Verhältnisse zwischen Skript- und Audio-Tempo. Ein Ton pro Hub UND
# Rückzug (statt nur pro Hub) macht das Audio-Tempo doppelt so hoch wie das
# Skript-Tempo - das ist kein Fehler, sondern eine andere, ebenso plausible
# Zählweise.
DEFAULT_HARMONICS = (0.5, 1.0, 2.0)


class AudioUnavailable(Exception):
    """ffmpeg fehlt oder das Video hat keine (lesbare) Audiospur."""


def _dominant_frequency_hz(values, sample_rate_hz, min_hz=MIN_TEMPO_HZ, max_hz=MAX_TEMPO_HZ):
    """FFT-Spitzenfrequenz innerhalb eines plausiblen Tempobereichs. Reine
    Funktion, unabhängig davon ob values aus Video oder Audio stammt."""
    values = np.asarray(values, dtype=float)
    n = len(values)
    if n < 8:
        return None
    signal = values - np.mean(values)
    spectrum = np.abs(np.fft.rfft(signal))
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate_hz)
    mask = (freqs >= min_hz) & (freqs <= max_hz)
    if not np.any(mask) or not np.any(spectrum[mask] > 0):
        return None
    band_spectrum = spectrum[mask]
    band_freqs = freqs[mask]
    return float(band_freqs[np.argmax(band_spectrum)])


def audio_envelope(samples, sample_rate, window_ms=50.0):
    """Kurzzeit-Energiehüllkurve (RMS je Fenster). Macht aus dem
    hochfrequenten Rohsignal (typisch mehrere kHz) eine Kurve im Bereich der
    Bewegungsrate (<5Hz), auf der sich überhaupt erst eine Tempo-FFT lohnt.
    Gibt (envelope, envelope_rate_hz) zurück; envelope ist leer, wenn
    samples zu kurz für mindestens zwei Fenster sind."""
    samples = np.asarray(samples, dtype=float)
    window = max(1, int(round(sample_rate * window_ms / 1000.0)))
    n_windows = len(samples) // window
    if n_windows < 2:
        return np.zeros(0), 0.0
    trimmed = samples[:n_windows * window].reshape(n_windows, window)
    rms = np.sqrt(np.mean(trimmed ** 2, axis=1))
    return rms, 1000.0 / window_ms


def estimate_audio_tempo_hz(samples, sample_rate, window_ms=50.0):
    """Dominantes Tempo der Audio-Energiehüllkurve, in Hz. None, wenn das
    Signal zu kurz oder zu leise/gleichmäßig für eine belastbare Aussage
    ist - dann bleibt die Prüfung aus, statt eine Zufallszahl zu vergleichen."""
    envelope, envelope_rate = audio_envelope(samples, sample_rate, window_ms=window_ms)
    if len(envelope) < 8:
        return None
    return _dominant_frequency_hz(envelope, envelope_rate)


def estimate_script_tempo_hz(actions):
    """Dominantes Tempo der Skript-eigenen Positionskurve. Wie
    quality_doctor.py's Rhythmus-Messung auf ein gleichmäßiges Zeitraster
    resampled (FFT braucht das), da Skript-Actions nicht äquidistant sind."""
    if len(actions) < 8:
        return None
    at = np.array([a["at"] for a in actions], dtype=float)
    pos = np.array([a["pos"] for a in actions], dtype=float)
    order = np.argsort(at)
    at, pos = at[order], pos[order]
    if at[-1] - at[0] <= 0:
        return None
    step_ms = max(float(np.median(np.diff(at))), 10.0)
    grid = np.arange(at[0], at[-1], step_ms)
    if len(grid) < 8:
        return None
    pos_uniform = np.interp(grid, at, pos)
    return _dominant_frequency_hz(pos_uniform, 1000.0 / step_ms)


def compare_tempo(script_hz, audio_hz, tolerance=0.25, harmonics=DEFAULT_HARMONICS):
    """Prüft, ob script_hz zu EINEM der erlaubten Vielfachen von audio_hz
    passt (siehe DEFAULT_HARMONICS). Gibt None zurück, wenn eine der beiden
    Zahlen fehlt (keine Aussage statt eines falschen Vergleichs), sonst ein
    dict mit dem besten Vielfachen und ob es innerhalb der Toleranz liegt."""
    if script_hz is None or audio_hz is None or script_hz <= 0 or audio_hz <= 0:
        return None
    best_harmonic = min(harmonics, key=lambda h: abs(script_hz - audio_hz * h))
    predicted_hz = audio_hz * best_harmonic
    relative_error = abs(script_hz - predicted_hz) / script_hz
    return {
        "matches": relative_error <= tolerance,
        "harmonic": best_harmonic,
        "predicted_hz": predicted_hz,
        "relative_error": relative_error,
    }


def extract_audio_samples(video_path, sample_rate=8000):
    """Dekodiert die Audiospur über ffmpeg zu Mono-Float32-Rohsamples (kein
    Zwischenformat wie WAV nötig - ffmpeg schreibt das Rohsignal direkt auf
    stdout). Braucht ein installiertes ffmpeg auf dem PATH; wirft
    AudioUnavailable statt abzustürzen, wenn ffmpeg fehlt oder das Video
    keine (lesbare) Audiospur hat."""
    cmd = ["ffmpeg", "-i", str(video_path), "-vn", "-ac", "1", "-ar", str(sample_rate),
           "-f", "f32le", "-loglevel", "error", "-"]
    try:
        proc = subprocess.run(cmd, capture_output=True)
    except FileNotFoundError:
        raise AudioUnavailable("ffmpeg ist nicht installiert")
    if proc.returncode != 0 or len(proc.stdout) == 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()[:200]
        raise AudioUnavailable(f"Keine Audiospur lesbar: {detail or 'unbekannter ffmpeg-Fehler'}")
    return np.frombuffer(proc.stdout, dtype=np.float32), sample_rate


def check(video_path, actions, sample_rate=8000, window_ms=50.0, tolerance=0.25):
    """Vergleicht Skript- und Audio-Tempo und liefert eine Warnliste im
    Stil von quality_doctor.evaluate(). PLAUSIBILITÄTSPRÜFUNG, keine
    Korrektur - eine Abweichung kann echte, aber untypische Bewegung sein
    (ruhige Szene, Stille) oder ein Tracking-Fehler; beides braucht einen
    Menschen zur Einordnung, keine automatische Reaktion.

    Zusätzlich: Speech-Hold + Segment-Labels (Review-Hinweise, keine Kurve).
    """
    script_hz = estimate_script_tempo_hz(actions)
    try:
        samples, sr = extract_audio_samples(video_path, sample_rate=sample_rate)
    except AudioUnavailable as exc:
        return {"available": False, "reason": str(exc), "warnings": []}

    audio_hz = estimate_audio_tempo_hz(samples, sr, window_ms=window_ms)
    comparison = compare_tempo(script_hz, audio_hz, tolerance=tolerance)

    warnings = []
    if comparison is not None and not comparison["matches"]:
        warnings.append(
            f"Script tempo ({script_hz:.2f} Hz) does not match an expected multiple "
            f"of audio tempo ({audio_hz:.2f} Hz; nearest {comparison['harmonic']}x "
            f"→ {comparison['predicted_hz']:.2f} Hz) — may be real atypical motion "
            "or a tracking error; no automatic correction")

    segments, speech_hold_ms = analyze_speech_hold_segments(samples, sr)
    _append_speech_hold_hints(warnings, segments, speech_hold_ms, actions)

    return {
        "available": True,
        "script_hz": script_hz,
        "audio_hz": audio_hz,
        "comparison": comparison,
        "warnings": warnings,
        "segments": segments,
        "speech_hold_ms": speech_hold_ms,
    }


# --- Speech-Hold + segment taxonomy (Go parity: audio_segments.go) ------------
# Classical multi-band short-time energy. Review/chapter hints only.

_SEG_FRAME_MS = 100.0
_SEG_HOP_MS = 50.0
_MIN_SEG_MS = 300.0
_LABEL_HOLDING = "holding"
_LABEL_GENTLE = "gentle"
_LABEL_INTENSE = "intense"
_LABEL_CLIMAX = "climax"


def analyze_speech_hold_segments(samples, sample_rate):
    """Return (segments, speech_hold_ms). Empty when signal too short."""
    frames = _band_energy_frames(samples, sample_rate, _SEG_FRAME_MS, _SEG_HOP_MS)
    if len(frames) < 4:
        return [], 0
    rms_vals = np.array([f["rms"] for f in frames], dtype=float)
    rms_med = float(np.median(rms_vals)) if len(rms_vals) else 0.0
    if rms_med <= 0:
        rms_med = 1e-9
    quiet = rms_med * 0.35
    labels, speech_hold, reasons = [], [], []
    for f in frames:
        total = f["impact"] + f["speech"] + f["high"]
        if total <= 0:
            labels.append(_LABEL_HOLDING)
            speech_hold.append(False)
            reasons.append("silence")
            continue
        impact_r = f["impact"] / total
        speech_r = f["speech"] / total
        high_r = f["high"] / total
        rel = f["rms"] / rms_med
        if speech_r >= 0.55 and impact_r <= 0.28 and high_r <= 0.35 and rel < 1.8:
            labels.append(_LABEL_HOLDING)
            speech_hold.append(True)
            reasons.append("speech_like")
        elif (impact_r + high_r) >= 0.50:
            if rel >= 1.4:
                labels.append(_LABEL_INTENSE)
                speech_hold.append(False)
                reasons.append("impact_rhythm")
            else:
                labels.append(_LABEL_GENTLE)
                speech_hold.append(False)
                reasons.append("soft_impact")
        elif f["rms"] < quiet:
            hold = speech_r > 0.45 and impact_r < 0.35
            labels.append(_LABEL_HOLDING)
            speech_hold.append(hold)
            reasons.append("quiet_speech" if hold else "quiet")
        elif rel < 0.7:
            labels.append(_LABEL_HOLDING)
            speech_hold.append(speech_r > impact_r)
            reasons.append("low_energy")
        else:
            labels.append(_LABEL_GENTLE)
            speech_hold.append(False)
            reasons.append("moderate_energy")
    _promote_climax(labels, frames, rms_med)
    _smooth_labels(labels, speech_hold, reasons)
    segments = _merge_segment_hints(frames, labels, speech_hold, reasons, _MIN_SEG_MS)
    hold_ms = sum(s["end_ms"] - s["start_ms"] for s in segments if s.get("speech_hold"))
    return segments, int(hold_ms)


def _label_rank(label):
    return {
        _LABEL_CLIMAX: 4,
        _LABEL_INTENSE: 3,
        _LABEL_GENTLE: 2,
    }.get(label, 1)


def _smooth_labels(labels, speech_hold, reasons):
    if len(labels) < 3:
        return
    orig_l = list(labels)
    orig_h = list(speech_hold)
    orig_r = list(reasons)
    for i in range(1, len(labels) - 1):
        a, b, c = orig_l[i - 1], orig_l[i], orig_l[i + 1]
        if a == c and a != b:
            labels[i] = a
            speech_hold[i] = orig_h[i - 1]
            reasons[i] = orig_r[i - 1]


def _merge_segment_hints(frames, labels, speech_hold, reasons, min_ms):
    if not frames:
        return []
    runs = []
    cur = {
        "label": labels[0], "hold": speech_hold[0], "reason": reasons[0],
        "start": frames[0]["start_ms"], "end": frames[0]["end_ms"],
    }
    for i in range(1, len(frames)):
        same = labels[i] == cur["label"] and speech_hold[i] == cur["hold"]
        if same:
            cur["end"] = frames[i]["end_ms"]
            continue
        runs.append(cur)
        cur = {
            "label": labels[i], "hold": speech_hold[i], "reason": reasons[i],
            "start": frames[i]["start_ms"], "end": frames[i]["end_ms"],
        }
    runs.append(cur)
    out_runs = []
    for r in runs:
        if not out_runs:
            out_runs.append(r)
            continue
        prev = out_runs[-1]
        prev_dur = prev["end"] - prev["start"]
        r_dur = r["end"] - r["start"]
        if r_dur < min_ms:
            prev["end"] = r["end"]
            if prev_dur < min_ms and _label_rank(r["label"]) > _label_rank(prev["label"]):
                prev["label"] = r["label"]
                prev["hold"] = r["hold"]
                prev["reason"] = r["reason"]
            continue
        if prev_dur < min_ms:
            if _label_rank(prev["label"]) > _label_rank(r["label"]):
                prev["end"] = r["end"]
                continue
            prev.update(label=r["label"], hold=r["hold"], reason=r["reason"], end=r["end"])
            continue
        if prev["label"] == r["label"] and prev["hold"] == r["hold"]:
            prev["end"] = r["end"]
            continue
        out_runs.append(r)
    return [{
        "label": r["label"],
        "start_ms": int(round(r["start"])),
        "end_ms": int(round(r["end"])),
        "speech_hold": bool(r["hold"]),
        "reason": r["reason"],
    } for r in out_runs]


def _band_energy_frames(samples, sample_rate, frame_ms, hop_ms):
    samples = np.asarray(samples, dtype=float)
    if sample_rate <= 0 or len(samples) == 0:
        return []
    frame_n = max(16, int(round(sample_rate * frame_ms / 1000.0)))
    hop_n = max(1, int(round(sample_rate * hop_ms / 1000.0)))
    out = []
    for start in range(0, len(samples) - frame_n + 1, hop_n):
        frame = samples[start:start + frame_n]
        window = 0.5 * (1.0 - np.cos(2.0 * np.pi * np.arange(frame_n) / max(frame_n - 1, 1)))
        spectrum = np.abs(np.fft.rfft(frame * window)) ** 2
        freqs = np.fft.rfftfreq(frame_n, d=1.0 / sample_rate)

        def band(lo, hi):
            mask = (freqs >= lo) & (freqs <= hi)
            return float(np.sum(spectrum[mask])) if np.any(mask) else 0.0

        out.append({
            "start_ms": 1000.0 * start / sample_rate,
            "end_ms": 1000.0 * (start + frame_n) / sample_rate,
            "rms": float(np.sqrt(np.mean(frame ** 2))),
            "impact": band(50.0, 250.0),
            "speech": band(250.0, 2000.0),
            "high": band(2000.0, min(3990.0, sample_rate / 2.0 - 10.0)),
        })
    return out


def _promote_climax(labels, frames, rms_med):
    if not labels or rms_med <= 0:
        return
    start = len(labels) * 3 // 4
    best_i, best_len, best_score = -1, 0, 0.0
    i = start
    while i < len(labels):
        if labels[i] != _LABEL_INTENSE:
            i += 1
            continue
        j = i
        score = 0.0
        while j < len(labels) and labels[j] == _LABEL_INTENSE:
            score += frames[j]["rms"] / rms_med
            j += 1
        run = j - i
        if run >= 2 and (score > best_score or (score == best_score and run > best_len)):
            best_i, best_len, best_score = i, run, score
        i = j
    if best_i >= 0 and best_score >= 4.0:
        for k in range(best_i, best_i + best_len):
            labels[k] = _LABEL_CLIMAX


def _append_speech_hold_hints(warnings, segments, speech_hold_ms, actions):
    hold_sec = speech_hold_ms / 1000.0
    if hold_sec >= 0.8:
        msg = (f"Speech-hold ~{hold_sec:.1f}s — during dialogue/quiet prefer "
               "hold/pause review (audio did not rewrite the curve)")
        if msg not in warnings:
            warnings.append(msg)
    if _motion_during_speech_hold(actions, segments):
        msg = ("Script moves during speech-hold windows — review hold/pause "
               "(audio did not rewrite the curve)")
        if msg not in warnings:
            warnings.append(msg)
    if any(s.get("label") == _LABEL_CLIMAX for s in segments):
        msg = ("Audio segment taxonomy marks a climax window — review "
               "chapter/finish (audio did not rewrite the curve)")
        if msg not in warnings:
            warnings.append(msg)


def _motion_during_speech_hold(actions, segments):
    holds = [(s["start_ms"], s["end_ms"]) for s in segments
             if s.get("speech_hold") and s["end_ms"] > s["start_ms"]]
    if len(actions) < 2 or not holds:
        return False
    travel, samples = 0, 0
    for i in range(1, len(actions)):
        at = actions[i]["at"]
        if not any(lo <= at <= hi for lo, hi in holds):
            continue
        travel += abs(actions[i]["pos"] - actions[i - 1]["pos"])
        samples += 1
    if samples < 4:
        return False
    return travel / samples > 8.0


def append_quality_audio_hint(quality_passed: bool, result: dict) -> dict:
    """G1.2: when Signal Quality failed and audio Hz is clear, add English hint.
    Never changes actions — warn only. Mutates and returns result."""
    if not isinstance(result, dict):
        return result
    if quality_passed or not result.get("available"):
        return result
    audio_hz = result.get("audio_hz")
    if audio_hz is None:
        return result
    msg = (
        f"Audio suggests ~{audio_hz:.2f} Hz — check ROI / axis "
        "(Signal Quality failed; audio did not rewrite the curve)"
    )
    warnings = list(result.get("warnings") or [])
    if msg not in warnings:
        warnings.append(msg)
    result["warnings"] = warnings
    return result


def main():
    import argparse
    import json

    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", required=True)
    ap.add_argument("--funscript", required=True, help="Zu prüfende .funscript-Datei")
    ap.add_argument("--tolerance", type=float, default=0.25)
    args = ap.parse_args()

    with open(args.funscript) as f:
        actions = json.load(f)["actions"]

    result = check(args.video, actions, tolerance=args.tolerance)
    if not result["available"]:
        print(f"Audio-Prüfung nicht möglich: {result['reason']}", file=sys.stderr)
        sys.exit(0)
    print(f"Skript-Tempo: {result['script_hz']}, Audio-Tempo: {result['audio_hz']}",
          file=sys.stderr)
    for w in result["warnings"]:
        print(f"WARNUNG: {w}", file=sys.stderr)


if __name__ == "__main__":
    main()
