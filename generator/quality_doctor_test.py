"""Regressionstest für quality_doctor.evaluate.

Arbeitet bewusst mit synthetischen Signalen statt mit echten Videos: das
Dekodieren eines der 20-Sekunden-Kalibrierungsvideos dauert rund zwei
Minuten, ein Test, der so lange läuft, wird in der Praxis nicht mehr
ausgeführt. Die an echten Videos gemessenen Konzentrationswerte, auf denen
die Schwelle beruht, stehen als Kommentar in quality_doctor.py.

Hintergrund - zwei Fehler, die zusammengehören:

  1. Der Rhythmus-Check lief auf den fertigen Actions. Die Peak/Valley-
     Reduktion behält aber nur abwechselnd Hoch- und Tiefpunkte übrig, also
     per Konstruktion einen alternierenden Zickzack, der auch bei reinem
     Trackingrauschen rhythmisch aussieht.
  2. Die harte Ausschlussschwelle lag bei 0.15.

Am Video eines unbewegten Objekts gemessen: dichtes Signal 0.169, dieselbe
Kurve auf Keyframes reduziert 0.212. Keine der beiden Korrekturen hätte den
Fall allein gefangen - deshalb prüft dieser Test beides getrennt.

Ausführen:  python3 generator/quality_doctor_test.py
"""

import sys

import numpy as np
from scipy.signal import find_peaks

import quality_doctor


FPS = 25.0
DURATION_MS = 20000
FRAME_DT_MS = 1000 / FPS


def _frame_times():
    return np.arange(0, DURATION_MS, FRAME_DT_MS, dtype=float)


def _dense(values):
    """Normalisiert auf 0-100, wie positions_to_funscript es tut."""
    v = np.asarray(values, dtype=float)
    return {"at": _frame_times(), "pos": (v - v.min()) / (v.max() - v.min()) * 100.0}


def _reduce_to_keyframes(dense, min_distance_frames=4):
    """Bildet die Keyframe-Reduktion aus generate_funscript.py nach."""
    t, pos = dense["at"], dense["pos"]
    peaks, _ = find_peaks(pos, distance=min_distance_frames)
    valleys, _ = find_peaks(-pos, distance=min_distance_frames)
    idx = sorted(set([0, len(pos) - 1]) | set(peaks.tolist()) | set(valleys.tolist()))
    return [{"at": int(t[i]), "pos": int(round(pos[i]))} for i in idx]


def _concentration(dense):
    """Dieselbe Kennzahl, die quality_doctor intern bildet - nur um zu
    kontrollieren, dass das Grenzfall-Testsignal im Zielbereich liegt.

    Bewusst über die Funktion des Moduls selbst, nicht nachgebaut: eine
    zweite Implementierung derselben Rechnung läuft zwangsläufig auseinander.
    Genau das ist passiert, als die Messung von global auf fensterweise
    umgestellt wurde - die nachgebaute Prüfung im Test rechnete weiter
    global und meldete Werte, die es so nicht mehr gab."""
    return quality_doctor._windowed_concentration(
        np.asarray(dense["at"], dtype=float), np.asarray(dense["pos"], dtype=float))


def _concentration_global(dense):
    """Globale Variante - nur noch dort verwendet, wo ausdrücklich der
    Unterschied zur fensterweisen Messung gezeigt werden soll."""
    at, pos = np.asarray(dense["at"], float), np.asarray(dense["pos"], float)
    dt = np.diff(at)
    dt = dt[dt > 0]
    grid = np.arange(at[0], at[-1], max(np.median(dt), 10))
    sig = np.interp(grid, at, pos)
    sig = sig - sig.mean()
    spectrum = np.abs(np.fft.rfft(sig))[1:]
    return float(np.sort(spectrum)[-3:].sum() / spectrum.sum())


def rhythmic(freq_hz=1.0):
    return _dense(np.sin(2 * np.pi * freq_hz * _frame_times() / 1000.0))


def broadband_noise(seed=1):
    return _dense(np.random.default_rng(seed).normal(size=len(_frame_times())))


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    def verdict(actions, dense=None):
        return quality_doctor.evaluate(actions, video_duration_ms=DURATION_MS, dense_signal=dense)

    # --- 1. Der Rhythmus-Check muss das dichte Signal auswerten -------------
    # Actions, die für sich genommen perfekt rhythmisch aussehen (genau das
    # Ergebnis jeder Peak/Valley-Reduktion), zusammen mit einem dichten
    # Verlauf aus breitbandigem Rauschen. Das Urteil muss dem dichten Signal
    # folgen, nicht den Actions.
    zigzag = [{"at": int(x), "pos": 20 if i % 2 else 80}
              for i, x in enumerate(np.arange(0, DURATION_MS, 500, dtype=float))]
    noisy = broadband_noise(seed=1)

    r_dense = verdict(zigzag, noisy)
    check("Zickzack-Actions + verrauschtes dichtes Signal -> nicht bestanden",
          not r_dense["passed"], f"score={r_dense['score']}")
    check("Rausch-Warnung wird ausgegeben",
          any("verrauscht" in w for w in r_dense["warnings"]), str(r_dense["warnings"]))

    r_actions_only = verdict(zigzag)
    check("Gegenprobe: ohne dichtes Signal ist der Check blind",
          r_actions_only["passed"] and not r_dense["passed"],
          f"ohne={r_actions_only['score']} mit={r_dense['score']}")

    # --- 2. Die harte Schwelle liegt bei 0.20, nicht bei 0.15 --------------
    # Am Video eines unbewegten Objekts wurde dicht 0.169 gemessen; mit der
    # alten Schwelle 0.15 wäre das durchgegangen. Hier über ein Signal
    # nachgestellt, dessen Konzentration gezielt dazwischen liegt.
    def sine_plus_noise(noise_factor):
        return _dense(np.sin(2 * np.pi * _frame_times() / 1000.0)
                      + noise_factor * np.random.default_rng(4).normal(size=len(_frame_times())))

    # Faktoren an der FENSTERWEISEN Messung ausgemessen - die globale
    # Messung lieferte für dieselben Signale andere Werte, weil sie einen
    # durchgehenden Rhythmus voraussetzt.
    below = sine_plus_noise(0.50)
    conc_below = _concentration(below)
    check("Testsignal 'knapp darunter' liegt zwischen 0.15 und 0.20",
          0.15 < conc_below < 0.20, f"conc={conc_below:.3f}")
    r = verdict(_reduce_to_keyframes(below), below)
    check("Konzentration zwischen 0.15 und 0.20 -> nicht bestanden",
          not r["passed"], f"score={r['score']} conc={conc_below:.3f}")

    # Die Schwelle hat ZWEI Stufen: unter 0.20 harter Ausschluss, unter 0.25
    # nur eine Warnung. Beide werden getrennt geprüft - und ausdrücklich am
    # Konzentrationswert, nicht am Gesamturteil. Ein verrauschtes Testsignal
    # erzeugt nebenbei zu dicht liegende Keyframes und fiele dann aus einem
    # anderen, ebenfalls richtigen Grund durch; ein Test, der beides
    # vermischt, schlägt bei jeder neuen Prüfung an, ohne dass sein eigener
    # Gegenstand betroffen wäre.
    between = sine_plus_noise(0.38)
    conc_between = _concentration(between)
    check("Testsignal 'zwischen den Stufen' liegt zwischen 0.20 und 0.25",
          0.20 < conc_between < 0.25, f"conc={conc_between:.3f}")
    r = verdict(_reduce_to_keyframes(between), between)
    check("zwischen 0.20 und 0.25: Warnung ja, harter Ausschluss nein",
          any("verrauscht" in w for w in r["warnings"]) and r["score"] > 0.0,
          f"conc={conc_between:.3f} score={r['score']}")

    # Deutlich über beiden Stufen: keine Rausch-Warnung mehr.
    clearly_above = sine_plus_noise(0.10)
    conc_above = _concentration(clearly_above)
    check("Testsignal 'deutlich darüber' liegt über 0.25", conc_above > 0.25,
          f"conc={conc_above:.3f}")
    r = verdict(_reduce_to_keyframes(clearly_above), clearly_above)
    check("deutlich über 0.25 löst keine Rausch-Warnung aus",
          not any("verrauscht" in w for w in r["warnings"]),
          f"conc={conc_above:.3f} {r['warnings']}")

    # --- 3. Keine Fehlalarme auf gültiger Bewegung -------------------------
    for freq in (0.5, 1.0, 2.0):
        d = rhythmic(freq)
        r = verdict(_reduce_to_keyframes(d), d)
        check(f"saubere Bewegung {freq}Hz -> bestanden", r["passed"], f"score={r['score']}")
        check(f"saubere Bewegung {freq}Hz -> keine Rausch-Warnung",
              not any("verrauscht" in w for w in r["warnings"]), str(r["warnings"]))

    # --- 4. Reines Trackingrauschen ----------------------------------------
    d = broadband_noise(seed=7)
    r = verdict(_reduce_to_keyframes(d), d)
    check("reines Trackingrauschen -> nicht bestanden", not r["passed"], f"score={r['score']}")

    # --- 5. Bestehende Prüfungen unverändert -------------------------------
    d = rhythmic(1.0)
    gapped = [a for a in _reduce_to_keyframes(d) if a["at"] < 3000 or a["at"] > 15000]
    r = verdict(gapped, d)
    check("große Tracking-Lücke -> nicht bestanden", not r["passed"], f"score={r['score']}")

    # --- 6. Normalisierung: ein Ausreißer darf die Skala nicht kapern -----
    import generate_funscript as gf
    t = np.arange(0, DURATION_MS, FRAME_DT_MS, dtype=float)
    y = 120 + 30 * np.sin(2 * np.pi * t / 1000.0)
    y[250:256] = 5  # kurzer Sprung auf eine falsche Region

    _, dense_minmax = gf.positions_to_funscript(t, y, norm_percentile=0)
    _, dense_perc = gf.positions_to_funscript(t, y, norm_percentile=2.0)
    span = lambda d: float(np.ptp(d["pos"][300:]))  # Bereich ohne den Ausreißer
    check("Min/Max-Normalisierung wird vom Ausreißer gestaucht",
          span(dense_minmax) < 60, f"{span(dense_minmax):.1f}")
    check("Perzentil-Normalisierung nutzt die volle Auslenkung",
          span(dense_perc) > 95, f"{span(dense_perc):.1f}")
    check("Perzentil-Normalisierung bleibt in 0-100",
          dense_perc["pos"].min() >= 0 and dense_perc["pos"].max() <= 100,
          f"{dense_perc['pos'].min()}..{dense_perc['pos'].max()}")

    # --- 7. Tracker-Objektverlust ------------------------------------------
    # In verlorenen Frames wird die letzte bekannte Position fortgeschrieben.
    # Die Kurve sieht dort unauffällig aus - der Zähler ist die einzige Spur,
    # deshalb muss er das Urteil beeinflussen.
    d = rhythmic(1.0)
    acts = _reduce_to_keyframes(d)
    clean = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d)
    check("ohne Verlustangabe unverändert", clean["passed"] and clean["score"] == 1.0,
          str(clean["score"]))

    mild = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d,
                                   tracker_lost_fraction=0.0)
    check("0% Verlust wird nicht bestraft", mild["score"] == clean["score"],
          f"{mild['score']} vs {clean['score']}")

    some = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d,
                                   tracker_lost_fraction=0.30)
    check("30% Verlust senkt den Score, besteht aber noch",
          some["score"] < clean["score"] and some["passed"], f"{some['score']}")
    check("Verlust wird als Warnung genannt",
          any("Tracker" in w for w in some["warnings"]), str(some["warnings"]))

    lots = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d,
                                   tracker_lost_fraction=0.6)
    check("über 50% Verlust -> nicht bestanden trotz sauberer Kurve",
          not lots["passed"], f"{lots['score']}")

    # --- 8. Bewegungsamplitude in Bildkoordinaten --------------------------
    # Die Normalisierung streckt jedes Signal auf 0-100, auch ein Zittern von
    # zwei Pixeln. Nur die Amplitude in Bildkoordinaten verrät, ob überhaupt
    # etwas passiert ist.
    d = rhythmic(1.0)
    acts = _reduce_to_keyframes(d)
    ok_amp = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d,
                                     motion_range_fraction=0.45)
    check("normale Amplitude (45% der Bildhöhe) besteht", ok_amp["passed"], str(ok_amp["score"]))

    tiny = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d,
                                   motion_range_fraction=0.01)
    check("1% der Bildhöhe -> nicht bestanden trotz sauberer Kurve",
          not tiny["passed"], str(tiny["score"]))
    check("Amplitude wird als Warnung genannt",
          any("Bewegung" in w for w in tiny["warnings"]), str(tiny["warnings"]))

    low = quality_doctor.evaluate(acts, video_duration_ms=DURATION_MS, dense_signal=d,
                                  motion_range_fraction=0.05)
    check("5% der Bildhöhe: Abzug, aber kein harter Ausschluss",
          low["passed"] and low["score"] < ok_amp["score"], str(low["score"]))

    # --- 9. Geschwindigkeitsbegrenzung -------------------------------------
    import generate_funscript as gf
    steep = [{"at": 0, "pos": 0}, {"at": 100, "pos": 100}, {"at": 200, "pos": 0}]
    unchanged, n = gf.limit_speed(steep, 0)
    check("Grenze 0 lässt alles unverändert", unchanged == steep and n == 0)

    limited, n = gf.limit_speed(steep, 400)
    check("zu schneller Sprung wird begrenzt", n == 1 and limited[1]["pos"] == 40,
          str([a["pos"] for a in limited]))
    check("Zeitpunkte bleiben unverändert",
          [a["at"] for a in limited] == [a["at"] for a in steep])
    check("Positionen bleiben in 0-100",
          all(0 <= a["pos"] <= 100 for a in limited), str(limited))

    generous, n = gf.limit_speed(steep, 1000)
    check("Sprung genau an der Grenze wird nicht angetastet", n == 0, str(n))

    # --- 10. Drift darf nicht für Rhythmus gehalten werden ----------------
    # Ohne Detrending bekam ein reiner Drift 0.289 - oberhalb der harten
    # Ausschlussschwelle von 0.20, wäre also als brauchbar durchgegangen.
    # Umgekehrt wurde ein gültiges Signal MIT Drift abgewertet, weil die
    # Driftenergie die Konzentration verwässert.
    rng = np.random.default_rng(3)
    drift = _dense(np.linspace(0, 1, len(_frame_times()))
                   + 0.02 * rng.normal(size=len(_frame_times())))
    r = verdict(_reduce_to_keyframes(drift), drift)
    check("reiner Drift gilt nicht als rhythmisch", not r["passed"], str(r["score"]))

    sine_with_drift = _dense(np.sin(2 * np.pi * _frame_times() / 1000.0)
                             + 1.5 * np.linspace(0, 1, len(_frame_times())))
    r = verdict(_reduce_to_keyframes(sine_with_drift), sine_with_drift)
    check("gültiges Signal mit Drift wird nicht abgewertet", r["passed"], str(r["score"]))

    # --- 11. Rekonstruktionsfehler ----------------------------------------
    # Ein zu grob reduziertes Skript sieht in JEDER anderen Prüfung gut aus:
    # nicht verrauscht, keine Ausreißer, keine Lücken, saubere Konzentration.
    # Es gibt nur die Bewegung nicht mehr wieder.
    d = rhythmic(1.0)
    fine = _reduce_to_keyframes(d)
    coarse = _reduce_to_keyframes(d, min_distance_frames=40)

    r_fine = verdict(fine, d)
    r_coarse = verdict(coarse, d)
    check("sauberes Skript hat kleinen Rekonstruktionsfehler",
          r_fine["metrics"]["reconstruction_error"] < 0.12,
          str(r_fine["metrics"]["reconstruction_error"]))
    check("grob reduziertes Skript hat größeren Fehler",
          r_coarse["metrics"]["reconstruction_error"]
          > r_fine["metrics"]["reconstruction_error"],
          f"grob {r_coarse['metrics']['reconstruction_error']} "
          f"gegen fein {r_fine['metrics']['reconstruction_error']}")
    check("grobe Reduktion senkt den Score",
          r_coarse["score"] < r_fine["score"],
          f"{r_coarse['score']} gegen {r_fine['score']}")

    # --- 12. Aktiver Zeitanteil -------------------------------------------
    # Ein einzelner sauberer Ausschlag, danach Stillstand, kam auf eine
    # spektrale Konzentration von 0.402 und bestand damit komfortabel. Die
    # Konzentration beantwortet eben nicht, wie viel der Laufzeit überhaupt
    # Bewegung enthält.
    burst_values = np.concatenate([
        np.zeros(200), np.sin(np.linspace(0, 2 * np.pi, 100)), np.zeros(200)])
    burst = {"at": np.arange(len(burst_values)) * FRAME_DT_MS,
             "pos": (burst_values - burst_values.min())
                    / (burst_values.max() - burst_values.min()) * 100}
    r = quality_doctor.evaluate(_reduce_to_keyframes(burst),
                                video_duration_ms=int(burst["at"][-1]),
                                dense_signal=burst, motion_range_fraction=0.45)
    check("einzelner Ausschlag, sonst Stillstand -> nicht bestanden",
          not r["passed"], f"score={r['score']} aktiv={r['metrics']['active_fraction']}")

    # Gegenprobe: eine langsame, aber durchgehende Bewegung darf NICHT
    # bestraft werden. Eine Mindestzyklenzahl hätte genau das getan -
    # deshalb wird der Zeitanteil gemessen und nicht die Zyklenzahl.
    slow = _dense(np.sin(2 * np.pi * _frame_times() / 10000.0))
    r = verdict(_reduce_to_keyframes(slow), slow)
    check("langsame, aber durchgehende Bewegung besteht", r["passed"],
          f"score={r['score']} aktiv={r['metrics']['active_fraction']}")

    # --- 13. Score bleibt im dokumentierten Bereich -----------------------
    # Ohne Begrenzung summierten sich mehrere Abzüge zu einem negativen
    # Score - in der Oberfläche als Prozentwert angezeigt wäre das kaputt.
    d = broadband_noise(seed=3)
    r = quality_doctor.evaluate(_reduce_to_keyframes(d), video_duration_ms=DURATION_MS,
                                dense_signal=d, motion_range_fraction=0.005,
                                tracker_lost_fraction=0.9)
    check("Score bleibt zwischen 0 und 1 auch bei vielen Abzügen",
          0.0 <= r["score"] <= 1.0, str(r["score"]))

    # --- 14. Mindestabstand wird beim Erzeugen durchgesetzt ---------------
    # Hoch- und Tiefpunkte werden getrennt gesucht, der Mindestabstand gilt
    # also nicht zwischen einem Hochpunkt und dem folgenden Tiefpunkt. Bei
    # verrauschten Signalen lagen dadurch bis zu 45% der Actions unter 100ms
    # - solche Abschnitte kann das Gerät nicht mehr einzeln ausführen.
    import generate_funscript as gf
    import device_profile

    times = _frame_times()
    noisy = np.sin(2 * np.pi * times / 1000.0) * 40 + 120 \
        + 8 * np.random.default_rng(4).normal(size=len(times))

    without, dense_without = gf.positions_to_funscript(times, noisy, min_interval_ms=0)
    with_limit, dense_with = gf.positions_to_funscript(times, noisy, min_interval_ms=100)

    m_without, _ = device_profile.evaluate(without)
    m_with, _ = device_profile.evaluate(with_limit)
    check("ohne Durchsetzung entstehen zu dichte Actions",
          m_without.get("actions_too_close", 0) > 0,
          str(m_without.get("actions_too_close")))
    check("mit Durchsetzung bleibt keine zu dicht",
          m_with.get("actions_too_close", 0) == 0,
          str(m_with.get("actions_too_close")))

    # Entscheidend: die Kurve darf dabei nicht beschädigt werden. Einfach
    # jeden zweiten Punkt zu verwerfen würde die Amplitude kosten.
    r_without = quality_doctor.evaluate(without, video_duration_ms=DURATION_MS,
                                        dense_signal=dense_without)
    r_with = quality_doctor.evaluate(with_limit, video_duration_ms=DURATION_MS,
                                     dense_signal=dense_with)
    check("der Rekonstruktionsfehler steigt kaum",
          r_with["metrics"]["reconstruction_error"]
          < r_without["metrics"]["reconstruction_error"] + 0.02,
          f"{r_with['metrics']['reconstruction_error']} gegen "
          f"{r_without['metrics']['reconstruction_error']}")

    # Ein sauberes Signal darf gar nicht angetastet werden.
    clean_signal = np.sin(2 * np.pi * times / 1000.0) * 40 + 120
    a, _ = gf.positions_to_funscript(times, clean_signal, min_interval_ms=0)
    b, _ = gf.positions_to_funscript(times, clean_signal, min_interval_ms=100)
    check("sauberes Signal bleibt unverändert", len(a) == len(b), f"{len(a)} gegen {len(b)}")

    # --- 15. Rhythmus fensterweise, nicht global --------------------------
    # Ein globales Spektrum setzt EINEN durchgehenden Rhythmus voraus.
    # Echtes Material wechselt ständig das Tempo; die Energie verteilt sich
    # dann über viele Frequenzen und die Konzentration bricht ein, obwohl
    # jeder einzelne Abschnitt sauber rhythmisch ist.
    #
    # An zwei echten Skripten desselben Films gemessen (77 Sekunden): global
    # 0.203 und 0.039, fensterweise 0.396 und 0.324. Die globale Messung
    # stufte BEIDE als verrauscht ein - eines davon stammt aus einem
    # etablierten Fremdprogramm. Hier nachgestellt über ein Signal, dessen
    # Frequenz sich über die Laufzeit ändert.
    long_times = np.arange(0, 60000, FRAME_DT_MS, dtype=float)
    sweeping = np.sin(2 * np.pi * np.cumsum(
        1.0 + 1.5 * np.sin(2 * np.pi * long_times / 30000.0)) * FRAME_DT_MS / 1000.0)
    normalized = (sweeping - sweeping.min()) / (sweeping.max() - sweeping.min()) * 100
    global_value = _concentration_global({"at": long_times, "pos": normalized})
    windowed = quality_doctor._windowed_concentration(long_times, normalized)
    check("Tempowechsel drückt die globale Messung",
          global_value < 0.25, f"{global_value:.3f}")
    check("fensterweise wird derselbe Verlauf als rhythmisch erkannt",
          windowed > global_value + 0.1, f"global {global_value:.3f}, "
                                         f"fensterweise {windowed:.3f}")

    # Gegenprobe: breitbandiges Rauschen darf auch fensterweise nicht
    # plötzlich rhythmisch aussehen.
    noise_windowed = quality_doctor._windowed_concentration(
        long_times, np.random.default_rng(11).normal(size=len(long_times)))
    check("Rauschen bleibt auch fensterweise niedrig", noise_windowed < 0.25,
          f"{noise_windowed:.3f}")

    # --- 16. Gleitende Dynamik --------------------------------------------
    # Eine globale Normalisierung legt EINE Skala über das ganze Video;
    # Abschnitte mit schwächerer Bewegung bleiben dadurch dauerhaft schwach.
    # An einem echten Skript gemessen wurden in der Hälfte aller
    # 6-Sekunden-Fenster nur 30 von 100 Punkten genutzt.
    import device_profile

    times = _frame_times()
    # Erste Hälfte kräftig, zweite Hälfte schwach - dieselbe Bewegung.
    envelope = np.where(np.arange(len(times)) < len(times) // 2, 1.0, 0.25)
    uneven = np.sin(2 * np.pi * times / 1000.0) * envelope * 40 + 120

    plain, _ = gf.positions_to_funscript(times, uneven, dynamic_range_ms=0)
    lifted, _ = gf.positions_to_funscript(times, uneven, dynamic_range_ms=3000)
    m_plain, _ = device_profile.evaluate(plain)
    m_lifted, _ = device_profile.evaluate(lifted)
    check("gleitende Dynamik hebt die Bewegungsstärke",
          m_lifted["avg_intensity"] > m_plain["avg_intensity"] * 1.3,
          f"{m_plain['avg_intensity']} -> {m_lifted['avg_intensity']}")

    # Die Sicherung: ein praktisch unbewegtes Signal darf NICHT aufgeblasen
    # werden - sonst erzeugt die Dynamik genau den Fehler, den die
    # Amplitudenprüfung sonst meldet.
    still = np.full(len(times), 120.0) + 0.3 * np.random.default_rng(9).normal(size=len(times))
    before = float(np.ptp(gf.dynamic_range_normalize(still, 8)))
    after = float(np.ptp(gf.dynamic_range_normalize(still, int(3000 / FRAME_DT_MS))))
    check("unbewegtes Signal wird nicht aufgeblasen", after < before * 6,
          f"{before:.2f} -> {after:.2f}")

    # --- 17. Prominenz unterdrückt Nachschwingungen -----------------------
    # Ein starrer Hub ist eine einzelne Bewegung. Weiches Gewebe schwingt
    # nach dem Anstoß gedämpft aus - diese Nachschwingung ist die FOLGE des
    # Anstoßes, kein eigener Hub. Ohne Prominenzbedingung wird jede davon zu
    # einem eigenen Keyframe.
    impulse_times = np.arange(0, 16000, FRAME_DT_MS, dtype=float)
    phase = (impulse_times / 1000.0) % 0.8
    damped = 45 * np.exp(-3.5 * phase) * np.cos(2 * np.pi * 4.0 * phase) + 120

    without_prom, _ = gf.positions_to_funscript(impulse_times, damped,
                                                min_peak_distance_ms=200)
    with_prom, _ = gf.positions_to_funscript(impulse_times, damped,
                                             min_peak_distance_ms=200,
                                             peak_prominence=0.35)
    check("ohne Prominenz wird jede Nachschwingung zum Keyframe",
          len(without_prom) > 70, str(len(without_prom)))
    # 16 Sekunden bei einem Anstoß alle 800ms sind 20 Anstöße, also rund
    # 40 sinnvolle Keyframes.
    check("mit Prominenz bleiben etwa die Anstöße übrig",
          30 < len(with_prom) < 55, str(len(with_prom)))

    # Entscheidend: ein sauberes Hubsignal darf davon NICHT betroffen sein,
    # sonst wäre die Prominenz keine Profilwahl, sondern ein Schaden.
    clean_dense = rhythmic(1.0)
    a, _ = gf.positions_to_funscript(clean_dense["at"], clean_dense["pos"])
    b, _ = gf.positions_to_funscript(clean_dense["at"], clean_dense["pos"],
                                     peak_prominence=0.35)
    check("sauberes Hubsignal bleibt unverändert", len(a) == len(b),
          f"{len(a)} gegen {len(b)}")

    r = quality_doctor.evaluate([{"at": 0, "pos": 50}])
    check("weniger als 2 Actions -> nicht bestanden", not r["passed"], str(r))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
