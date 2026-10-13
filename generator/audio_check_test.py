"""Test für audio_check.py - die Audio/Skript-Tempo-Plausibilitätsprüfung.

Läuft komplett OHNE ffmpeg und ohne echtes Video: die Tempo-Schätzung und der
Vergleich sind reine Funktionen auf synthetischen Signalen (Sinuskurven
bekannter Frequenz statt echter Aufnahmen) - genau wie quality_doctor_test.py
seine Rhythmusprüfung an erzeugten, nicht echten Kurven testet.

Ausführen: python3 generator/audio_check_test.py
"""

import sys

import numpy as np

import audio_check as ac


def sine(freq_hz, duration_s, sample_rate, amplitude=1.0, phase=0.0):
    t = np.arange(0, duration_s, 1.0 / sample_rate)
    return amplitude * np.sin(2 * np.pi * freq_hz * t + phase)


def amplitude_modulated_noise(envelope_freq_hz, duration_s, sample_rate, rng):
    """Simuliert eine Tonspur, deren LAUTSTÄRKE mit envelope_freq_hz
    pulsiert (wie rhythmisches Stöhnen/Geräusch), aber deren Trägersignal
    selbst hochfrequentes Rauschen ist - realistischer als eine reine
    Sinuswelle für "Audioenergie", ohne eine echte Aufnahme zu brauchen."""
    t = np.arange(0, duration_s, 1.0 / sample_rate)
    envelope = 0.5 + 0.5 * np.sin(2 * np.pi * envelope_freq_hz * t)
    carrier = rng.normal(0, 1, size=len(t))
    return envelope * carrier


def script_actions(freq_hz, duration_ms, step_ms=40):
    at = np.arange(0, duration_ms, step_ms, dtype=float)
    pos = 50 + 45 * np.sin(2 * np.pi * freq_hz * at / 1000.0)
    return [{"at": int(a), "pos": float(p)} for a, p in zip(at, pos)]


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- _dominant_frequency_hz: erkennt eine bekannte Sinusfrequenz -------
    sr = 100.0
    sig = sine(1.5, 20, sr)
    freq = ac._dominant_frequency_hz(sig, sr)
    check("erkennt 1.5Hz in einem reinen Sinussignal",
          freq is not None and abs(freq - 1.5) < 0.1, str(freq))

    # --- zu kurzes Signal -> None statt Absturz ------------------------------
    check("zu kurzes Signal liefert None", ac._dominant_frequency_hz([1, 2, 3], sr) is None, "")

    # --- audio_envelope: RMS-Hüllkurve einer amplitudenmodulierten Spur -----
    rng = np.random.default_rng(0)
    audio_sr = 4000
    samples = amplitude_modulated_noise(1.0, 30, audio_sr, rng)
    envelope, env_rate = ac.audio_envelope(samples, audio_sr, window_ms=50.0)
    check("Hüllkurve ist deutlich kürzer als das Rohsignal",
          0 < len(envelope) < len(samples), f"{len(envelope)} vs {len(samples)}")
    check("Hüllkurvenrate entspricht 1000/window_ms", env_rate == 20.0, str(env_rate))

    # --- estimate_audio_tempo_hz: erkennt die Hüllkurven-Frequenz -----------
    audio_hz = ac.estimate_audio_tempo_hz(samples, audio_sr, window_ms=50.0)
    check("erkennt 1.0Hz-Lautstärkepulsierung", audio_hz is not None and abs(audio_hz - 1.0) < 0.15,
          str(audio_hz))

    # --- estimate_script_tempo_hz: erkennt die Skript-eigene Frequenz -------
    actions = script_actions(1.0, 20000)
    script_hz = ac.estimate_script_tempo_hz(actions)
    check("erkennt 1.0Hz im Skript", script_hz is not None and abs(script_hz - 1.0) < 0.15,
          str(script_hz))

    check("zu wenige Actions liefert None", ac.estimate_script_tempo_hz(actions[:3]) is None, "")

    # --- compare_tempo: exakte Übereinstimmung -------------------------------
    cmp_exact = ac.compare_tempo(1.0, 1.0)
    check("1:1 Übereinstimmung erkannt", cmp_exact["matches"] and cmp_exact["harmonic"] == 1.0,
          str(cmp_exact))

    # --- compare_tempo: doppeltes Audio-Tempo (2 Töne pro Hub) --------------
    cmp_double = ac.compare_tempo(1.0, 2.0)
    check("erkennt 0.5x als passendes Vielfaches (Audio doppelt so schnell)",
          cmp_double["matches"] and cmp_double["harmonic"] == 0.5, str(cmp_double))

    # --- compare_tempo: klarer Ausreißer, kein Vielfaches passt --------------
    cmp_off = ac.compare_tempo(1.0, 3.7)
    check("deutliche Abweichung wird NICHT als Übereinstimmung gewertet",
          not cmp_off["matches"], str(cmp_off))

    # --- compare_tempo: fehlende Werte -> None statt falscher Vergleich -----
    check("None ohne script_hz", ac.compare_tempo(None, 1.0) is None, "")
    check("None ohne audio_hz", ac.compare_tempo(1.0, None) is None, "")

    # --- extract_audio_samples: klarer Fehler ohne ffmpeg/Datei -------------
    raised = False
    try:
        ac.extract_audio_samples("/nicht/vorhanden.mp4")
    except ac.AudioUnavailable:
        raised = True
    check("extract_audio_samples wirft AudioUnavailable statt abzustürzen", raised, "")

    # --- check(): Gesamtablauf ohne echtes Video -> ehrliche Nichtverfügbarkeit
    result = ac.check("/nicht/vorhanden.mp4", actions)
    check("check() meldet available=False ohne echtes Video, statt zu crashen",
          result["available"] is False and result["warnings"] == [], str(result))

    # --- G1.2 hint (warn only; no curve change)
    hint_in = {"available": True, "audio_hz": 1.5, "warnings": []}
    ac.append_quality_audio_hint(False, hint_in)
    check("G1.2 hint when QD failed + audio Hz clear",
          len(hint_in["warnings"]) == 1 and "check ROI / axis" in hint_in["warnings"][0],
          str(hint_in))
    ac.append_quality_audio_hint(False, hint_in)
    check("G1.2 hint idempotent", len(hint_in["warnings"]) == 1, str(hint_in))
    ok_in = {"available": True, "audio_hz": 1.5, "warnings": []}
    ac.append_quality_audio_hint(True, ok_in)
    check("G1.2 no hint when QD passed", ok_in["warnings"] == [], str(ok_in))

    # --- Speech-Hold / segment taxonomy (review hints; no stroke writer) ---
    sr8 = 8000
    t = np.arange(0, 2.0, 1.0 / sr8)
    speech = 0.35 * np.sin(2 * np.pi * 800 * t)
    t2 = np.arange(0, 2.0, 1.0 / sr8)
    env = 0.15 + 0.85 * np.maximum(0.0, np.sin(2 * np.pi * 2.0 * t2))
    impact = env * (0.6 * np.sin(2 * np.pi * 120 * t2) + 0.5 * np.sin(2 * np.pi * 2800 * t2))
    segs, hold_ms = ac.analyze_speech_hold_segments(np.concatenate([speech, impact]), sr8)
    check("Speech-Hold Segmente nicht leer", len(segs) > 0, str(segs))
    check("Speech-Hold ms > 0 bei Mid-Band-Ton", hold_ms > 0, str(hold_ms))
    labels = {s["label"] for s in segs}
    check("Taxonomie enthält holding oder speech_hold",
          any(s.get("speech_hold") or s["label"] == "holding" for s in segs), str(segs))
    check("Taxonomie enthält motion-Label nach Impact",
          bool(labels & {"gentle", "intense", "climax"}), str(labels))
    short_segs, short_hold = ac.analyze_speech_hold_segments(speech[: int(0.15 * sr8)], sr8)
    check("zu kurzes Signal: keine Segmente", short_segs == [] and short_hold == 0, str(short_segs))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
