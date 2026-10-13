"""Quality Doctor: regelbasierte Qualitätsbewertung einer generierten
funscript-Kurve, komplett ohne KI/ML.

Prüft nur, was sich ohne trainiertes Modell oder Vergleich gegen einen
"echten" Referenz-Tracker verlässlich feststellen lässt: Zeitstempel-
Konsistenz, Wertebereich, große zeitliche Lücken (Tracking könnte das
Ziel verloren haben), unnatürliche Geschwindigkeitsspitzen (Tracking-
Sprünge) und ob das Signal rhythmisch wirkt oder eher wie Rauschen.

Eine "echte" automatische Qualitätsbewertung (z.B. per Abgleich gegen
Ground-Truth-Daten oder ein trainiertes Bewertungsmodell) würde
mehrere Tracker-Implementierungen voraussetzen und hier nicht sinnvoll
nachgebaut werden können. Was ohne KI geht (Zeitstempel-/Wertebereichs-
Prüfung, Lücken, Geschwindigkeit, Jitter), ist komplett umgesetzt.
"""

import numpy as np



# Fensterbreite für die Rhythmusmessung in Millisekunden.
#
# Ein globales Spektrum über ein ganzes Video setzt voraus, dass EIN Rhythmus
# durchgehend anliegt. Echtes Material wechselt ständig das Tempo; die
# Energie verteilt sich dann über viele Frequenzen, und die Konzentration
# fällt, obwohl jeder einzelne Abschnitt sauber rhythmisch ist.
#
# An zwei echten Skripten desselben Films gemessen (77 Sekunden): global
# 0.203 und 0.039, fensterweise 0.396 und 0.324. Die globale Messung stufte
# BEIDE als verrauscht ein - eines davon stammt aus einem etablierten
# Fremdprogramm. Das war kein Grenzfall, sondern ein systematischer Fehler
# der Messung an realem Material.
RHYTHM_WINDOW_MS = 8000.0


def _spectral_concentration(values):
    """Anteil der Energie in den drei stärksten Frequenzen."""
    signal = _detrend(np.asarray(values, dtype=float))
    spectrum = np.abs(np.fft.rfft(signal))[1:]
    if spectrum.sum() <= 0 or len(spectrum) <= 3:
        return None
    return float(np.sort(spectrum)[-3:].sum() / spectrum.sum())


def _windowed_concentration(grid, values, window_ms=RHYTHM_WINDOW_MS):
    """Konzentration fensterweise, Median über die Fenster.

    Der Median und nicht der Mittelwert: einzelne ruhige Abschnitte in einem
    sonst rhythmischen Video sollen das Ergebnis nicht nach unten ziehen.

    Fällt auf die globale Messung zurück, wenn das Signal für mehrere
    Fenster zu kurz ist.
    """
    grid = np.asarray(grid, dtype=float)
    values = np.asarray(values, dtype=float)
    if len(grid) < 16:
        return None
    step = float(np.median(np.diff(grid))) if len(grid) > 1 else 40.0
    size = int(window_ms / max(step, 1.0))
    if size < 16 or len(values) < size * 2:
        return _spectral_concentration(values)

    scores = []
    for start in range(0, len(values) - size + 1, max(1, size // 2)):
        window_score = _spectral_concentration(values[start:start + size])
        if window_score is not None:
            scores.append(window_score)
    if not scores:
        return _spectral_concentration(values)
    return float(np.median(scores))


def _detrend(values):
    """Zieht die lineare Ausgleichsgerade ab.

    Ohne das verfälscht ein langsamer Drift die Rhythmusbewertung in BEIDE
    Richtungen, und beide sind schädlich:

    * Ein gültiges rhythmisches Signal MIT Drift wird abgewertet, weil die
      Driftenergie im untersten Frequenzband die Konzentration verwässert.
      Gemessen: Sinus mit Drift 0.425 statt 1.000, nach Detrending 0.878.
    * Ein reiner Drift bekommt umgekehrt eine zu hohe Bewertung, weil seine
      Energie ebenfalls konzentriert ist - nur eben bei Frequenz null.
      Gemessen: 0.289 ohne Detrending, also oberhalb unserer harten
      Ausschlussschwelle von 0.20. Ein reiner Drift wäre damit als brauchbar
      durchgegangen. Realistischer Drift mit Trackerrauschen fällt nach
      Detrending auf 0.027.

    Genau davor warnt Abschnitt 5 des Zielkonzepts: Drift darf nicht wegen
    seiner Glätte für rhythmisch gehalten werden.
    """
    values = np.asarray(values, dtype=float)
    if len(values) < 3:
        return values - values.mean() if len(values) else values
    x = np.arange(len(values), dtype=float)
    slope, intercept = np.polyfit(x, values, 1)
    return values - (slope * x + intercept)


def evaluate(actions, video_duration_ms=None, dense_signal=None,
             tracker_lost_fraction=None, scene_ranges=None,
             motion_range_fraction=None, model=None):
    """Bewertet eine Liste von {"at": ms, "pos": 0-100}-Dicts.

    dense_signal ist optional der ungekürzte, normalisierte Kurvenverlauf
    ({"at": ndarray, "pos": ndarray}) vor der Keyframe-Reduktion. Er wird
    ausschließlich für den Rhythmus-/Rausch-Check gebraucht; alle anderen
    Prüfungen beziehen sich bewusst auf die tatsächlich exportierten Actions.
    Ohne ihn fällt der Check auf die Actions zurück und erkennt reines
    Trackingrauschen nicht zuverlässig (siehe Kommentar weiter unten).

    tracker_lost_fraction ist optional der Anteil der Frames, in denen der
    CSRT-Tracker das Objekt verloren gemeldet hat. Das ist das direkteste
    verfügbare Qualitätssignal: in diesen Abschnitten wurde die letzte
    bekannte Position fortgeschrieben, die Kurve ist dort also erfunden und
    nicht gemessen. An der Kurve selbst ist das nicht zu erkennen - sie
    sieht dort nur unauffällig flach aus.

    Gibt ein Dict zurück:
      {"score": 0-1, "warnings": [str, ...], "passed": bool}
    "passed" ist score >= 0.5 UND keines der harten Ausschlusskriterien
    verletzt (siehe unten) - eine Einordnung, die im Log/UI angezeigt wird,
    keine Garantie für ein perfektes Skript.
    """
    warnings = []
    if len(actions) < 2:
        return {"score": 0.0, "warnings": ["Weniger als 2 Actions - kein verwertbares Skript"], "passed": False}

    at = np.array([a["at"] for a in actions], dtype=float)
    pos = np.array([a["pos"] for a in actions], dtype=float)

    penalty = 0.0

    # --- Zeitstempel sortiert und eindeutig? ---
    if np.any(np.diff(at) < 0):
        warnings.append("Zeitstempel nicht aufsteigend sortiert")
        penalty += 0.3
    duplicate_ts = np.sum(np.diff(at) == 0)
    if duplicate_ts > 0:
        warnings.append(f"{duplicate_ts} doppelte Zeitstempel")
        penalty += min(0.2, duplicate_ts * 0.02)

    # --- Positionswerte im gültigen Bereich? ---
    out_of_range = np.sum((pos < 0) | (pos > 100))
    if out_of_range > 0:
        warnings.append(f"{out_of_range} Positionswerte außerhalb 0-100")
        penalty += min(0.3, out_of_range * 0.05)

    # --- Große zeitliche Lücken (Tracking könnte das Ziel verloren haben) ---
    dt = np.diff(at)
    dt = dt[dt > 0]
    if len(dt) > 0:
        median_dt = np.median(dt)
        big_gap_threshold = max(median_dt * 8, 2000)  # mind. 2s, sonst 8x Median
        gap_mask = dt > big_gap_threshold
        big_gaps = np.sum(gap_mask)
        if big_gaps > 0:
            total_gap_ms = dt[gap_mask].sum()
            warnings.append(f"{big_gaps} ungewöhnlich große zeitliche Lücke(n) (>{int(big_gap_threshold)}ms)")
            penalty += min(0.25, big_gaps * 0.08)
            # Zusätzlich proportional zum Anteil der Lücke an der Gesamtlänge -
            # eine einzelne kurze Lücke ist harmlos, aber wenn ein Großteil
            # des Videos gar nicht getrackt wurde, muss das schwerer wiegen.
            if video_duration_ms:
                gap_fraction = total_gap_ms / video_duration_ms
                if gap_fraction > 0.2:
                    warnings.append(f"{gap_fraction:.0%} der Videolänge ohne Tracking-Daten")
                    penalty += min(0.4, gap_fraction * 0.5)

    # --- Geschwindigkeits-Ausreißer (unnatürliche Sprünge) ---
    if len(dt) > 0:
        dpos = np.abs(np.diff(pos))
        dpos = dpos[: len(dt)]
        speed = dpos / np.maximum(dt, 1)  # pos-Einheiten pro ms
        if len(speed) > 4:
            median_speed = np.median(speed)
            mad = np.median(np.abs(speed - median_speed)) + 1e-6
            spike_threshold = median_speed + 10 * mad
            spikes = np.sum(speed > max(spike_threshold, 0.5))
            if spikes > 0:
                warnings.append(f"{spikes} extreme Geschwindigkeitsspitze(n) - deutet auf Tracking-Sprung hin")
                penalty += min(0.3, spikes * 0.05)

    # --- Jitter: hochfrequentes Rauschen statt echter Bewegung ---
    # WICHTIG: Richtungswechsel zu zählen wäre hier falsch - ein normales
    # Auf-Ab-Skript wechselt bei JEDEM Punkt die Richtung, das ist der Sinn
    # der Bewegung, kein Rauschen. Stattdessen wird wie in auto_roi.py die
    # spektrale Konzentration genutzt: ein rhythmisches Signal hat seine
    # Energie auf wenige Frequenzen konzentriert, echtes Rauschen verteilt
    # sie breit über viele Frequenzen (unabhängig davon, wie oft die
    # Richtung wechselt).
    #
    # EBENSO WICHTIG, und ein früherer Kalibrierungsfehler: gemessen werden
    # muss der dichte Verlauf, NICHT die fertigen Actions. Die Peak/Valley-
    # Reduktion behält nur abwechselnd Hoch- und Tiefpunkte übrig - das ist
    # per Konstruktion ein alternierender Zickzack und sieht deshalb selbst
    # dann rhythmisch aus, wenn der Tracker nur zufällig umhergewandert ist.
    # An einem reinen Rauschvideo gemessen: dichtes Signal 0.07, dieselbe
    # Kurve auf Keyframes reduziert 0.37 - also oberhalb jeder sinnvollen
    # Schwelle. Ohne dense_signal ist dieser Check kaum aussagekräftig.
    if dense_signal is not None:
        src_at = np.asarray(dense_signal["at"], dtype=float)
        src_pos = np.asarray(dense_signal["pos"], dtype=float)
        src_dt = np.diff(src_at)
        src_dt = src_dt[src_dt > 0]
    else:
        src_at, src_pos, src_dt = at, pos, dt

    # Bei geschnittenem Material wird pro Szene gemessen und der Median
    # genommen. Über das ganze Video hinweg zu messen wäre falsch: drei
    # Szenen mit 1.0, 0.7 und 1.4 Hz ergeben zwangsläufig ein breites
    # Spektrum, obwohl JEDE Szene für sich sauber rhythmisch ist. Gemessen
    # an einem solchen Testvideo: global 17% (Fehlalarm), pro Szene deutlich
    # darüber.
    if scene_ranges and len(scene_ranges) > 1 and dense_signal is not None:
        per_scene = []
        for start, end in scene_ranges:
            end = min(end, len(src_pos))
            if end - start < 16:
                continue
            seg_at, seg_pos = src_at[start:end], src_pos[start:end]
            seg_dt = np.diff(seg_at)
            seg_dt = seg_dt[seg_dt > 0]
            if len(seg_dt) == 0:
                continue
            grid = np.arange(seg_at[0], seg_at[-1], max(np.median(seg_dt), 10))
            if len(grid) <= 8:
                continue
            sig = _detrend(np.interp(grid, seg_at, seg_pos))
            spectrum = np.abs(np.fft.rfft(sig))[1:]
            if spectrum.sum() > 0 and len(spectrum) > 3:
                per_scene.append(float(np.sort(spectrum)[-3:].sum() / spectrum.sum()))
        if per_scene:
            concentration = float(np.median(per_scene))
            if concentration < 0.25:
                warnings.append(
                    f"Signal wirkt verrauscht statt rhythmisch (spektrale Konzentration "
                    f"{concentration*100:.0f}%, Median über {len(per_scene)} Szenen)")
                penalty += 0.45
    elif len(src_pos) > 8 and len(src_dt) > 0:
        # Auf ein gleichmäßiges Zeitraster interpolieren, da die Punkte nicht
        # zwingend äquidistant sind - FFT braucht gleichmäßige Abtastung.
        uniform_dt = max(np.median(src_dt), 10)
        t_uniform = np.arange(src_at[0], src_at[-1], uniform_dt)
        if len(t_uniform) > 8:
            pos_uniform = np.interp(t_uniform, src_at, src_pos)
            windowed = _windowed_concentration(t_uniform, pos_uniform)
            spectrum = np.abs(np.fft.rfft(_detrend(pos_uniform)))
            if windowed is not None:
                concentration = windowed
                if concentration < 0.25:
                    warnings.append(
                        f"Signal wirkt verrauscht statt rhythmisch (spektrale "
                        f"Konzentration {concentration*100:.0f}%)")
                    penalty += 0.45
            elif spectrum.sum() > 0:
                # Anteil der Energie in den 3 stärksten Frequenzen an der
                # Gesamtenergie (ohne Gleichanteil) - hoch bei rhythmischer
                # Bewegung, niedrig bei breitbandigem Rauschen.
                spectrum_ac = spectrum[1:]
                if len(spectrum_ac) > 3 and spectrum_ac.sum() > 0:
                    top3 = np.sort(spectrum_ac)[-3:].sum()
                    concentration = top3 / spectrum_ac.sum()
                    if concentration < 0.25:
                        warnings.append(f"Signal wirkt verrauscht statt rhythmisch (spektrale Konzentration {concentration:.0%})")
                        penalty += 0.45

    # --- Keyframe-Dichte plausibel für die Videolänge? ---
    if video_duration_ms and video_duration_ms > 0:
        actions_per_min = len(actions) / (video_duration_ms / 60000.0)
        if actions_per_min < 4:
            warnings.append(f"Sehr wenige Keyframes ({actions_per_min:.1f}/min) - Bewegung evtl. nicht erkannt")
            penalty += 0.15

    score = max(0.0, 1.0 - penalty)

    # --- Tracker-Objektverlust ---------------------------------------------
    # Abschnitte, in denen der Tracker das Objekt verloren hatte, enthalten
    # eine fortgeschriebene statt einer gemessenen Position. Ab der Hälfte
    # aller Frames ist das Ergebnis überwiegend erfunden - dann hilft auch
    # ein sauber aussehender Kurvenverlauf nicht mehr.
    if tracker_lost_fraction is not None and tracker_lost_fraction > 0.05:
        warnings.append(
            f"Tracker hat das Objekt in {tracker_lost_fraction*100:.0f}% der Frames "
            "verloren - dort wurde die letzte bekannte Position fortgeschrieben")
        score -= min(0.4, tracker_lost_fraction * 0.6)

    # --- Geräteverträglichkeit ---------------------------------------------
    # Grenzwerte aus den etablierten Werkzeugen der Funscript-Gemeinschaft
    # (siehe device_profile.py). Sie betreffen nicht unser eigenes Gerät,
    # sondern die erzeugte DATEI: sie soll auch auf fremder Hardware und in
    # fremden Playern brauchbar sein.
    try:
        import device_profile
        device_metrics, device_warnings = device_profile.evaluate(actions)
    except Exception:
        device_metrics, device_warnings = {}, []
    warnings.extend(device_warnings)
    # Abzug bewusst gering: das sind Verträglichkeitshinweise, keine
    # Messfehler. Ein Skript kann inhaltlich richtig und trotzdem für ein
    # bestimmtes Gerät zu dicht sein.
    score -= min(0.2, 0.1 * len(device_warnings))

    # --- Aktiver Zeitanteil ------------------------------------------------
    # Wie viel der Laufzeit enthält überhaupt Bewegung? Die spektrale
    # Konzentration beantwortet das nicht: ein einzelner sauberer Ausschlag,
    # danach zwanzig Sekunden Stillstand, kam auf 0.402 und lag damit
    # komfortabel über jeder Schwelle. Abschnitt 6 des Zielkonzepts nennt
    # genau das - "|\/" allein ist keine belastbare Periodizität.
    #
    # Als Kriterium wurde bewusst der Zeitanteil gewählt und NICHT die
    # Zyklenzahl: eine langsame Bewegung mit nur zwei Zyklen ist gültig und
    # würde von einer Mindestzyklenzahl fälschlich bestraft. Gemessen -
    # Einzelausschlag 0.27, zwei langsame Zyklen 0.86, durchgehende
    # Bewegung 1.00.
    active_fraction = None
    if dense_signal is not None:
        dense_values = _detrend(np.asarray(dense_signal["pos"], dtype=float))
        amplitude = float(np.ptp(dense_values))
        if len(dense_values) > 50 and amplitude > 1e-9:
            window = max(10, len(dense_values) // 40)
            local_span = np.array([
                np.ptp(dense_values[max(0, i - window):i + window])
                for i in range(0, len(dense_values), max(1, window // 5))
            ])
            active_fraction = float(np.mean(local_span > amplitude * 0.15))
            if active_fraction < 0.35:
                warnings.append(
                    f"Bewegung findet nur in {active_fraction*100:.0f}% der Laufzeit statt "
                    "- der Rest ist praktisch unbewegt")
                score -= 0.3
            elif active_fraction < 0.6:
                warnings.append(
                    f"In {(1-active_fraction)*100:.0f}% der Laufzeit passiert kaum etwas")
                score -= 0.1

    # --- Rekonstruktionsfehler ---------------------------------------------
    # Wie gut geben die exportierten Actions den tatsächlichen Verlauf
    # wieder? Beim Abspielen wird zwischen den Keyframes linear interpoliert;
    # genau diese Rekonstruktion wird hier mit dem dichten Signal verglichen.
    #
    # Das schließt eine Lücke, die alle bisherigen Prüfungen offenlassen: ein
    # stark geglättetes oder zu grob reduziertes Skript sieht in JEDER
    # einzelnen von ihnen gut aus - es ist nicht verrauscht, hat keine
    # Ausreißer, keine Lücken und eine saubere spektrale Konzentration. Es
    # gibt nur die ursprüngliche Bewegung nicht mehr wieder. Ohne diesen
    # Vergleich gewinnt systematisch die glatteste Variante.
    reconstruction_error = None
    if dense_signal is not None and len(actions) >= 2:
        dense_at = np.asarray(dense_signal["at"], dtype=float)
        dense_pos = np.asarray(dense_signal["pos"], dtype=float)
        if len(dense_at) > 2:
            reconstructed = np.interp(dense_at, at, pos)
            span = float(np.ptp(dense_pos))
            if span > 1e-6:
                # Als Anteil der tatsächlichen Auslenkung, nicht in
                # absoluten Punkten - sonst wäre derselbe Fehler bei einer
                # schwachen Bewegung harmlos und bei einer starken nicht.
                reconstruction_error = float(
                    np.sqrt(np.mean((reconstructed - dense_pos) ** 2)) / span)
                if reconstruction_error > 0.20:
                    warnings.append(
                        f"Das Skript gibt den gemessenen Verlauf nur grob wieder "
                        f"(mittlerer Fehler {reconstruction_error*100:.0f}% der Auslenkung) "
                        "- zu stark geglättet oder zu wenige Keyframes")
                    score -= 0.3
                elif reconstruction_error > 0.12:
                    warnings.append(
                        f"Das Skript weicht spürbar vom gemessenen Verlauf ab "
                        f"({reconstruction_error*100:.0f}% der Auslenkung)")
                    score -= 0.1

    # --- Tatsächliche Bewegungsamplitude -----------------------------------
    # Die Normalisierung streckt JEDES Signal auf 0-100, auch ein Zittern von
    # zwei Pixeln. An der fertigen Kurve ist deshalb nicht zu erkennen, ob
    # überhaupt eine nennenswerte Bewegung stattgefunden hat - dafür braucht
    # es die Amplitude in Bildkoordinaten. Gemessen am Kalibrierungssatz
    # (Anteil an der Bildhöhe): gültige Videos 46-60%, ein unbewegtes Objekt
    # 1.0%. Die Schwelle liegt mit großem Abstand dazwischen.
    if motion_range_fraction is not None:
        if motion_range_fraction < 0.03:
            warnings.append(
                f"Kaum echte Bewegung: die verfolgte Region bewegt sich nur über "
                f"{motion_range_fraction*100:.1f}% der Bildhöhe. Die Kurve entsteht "
                "fast nur durch Hochskalieren von Trackerzittern")
            score -= 0.5
        elif motion_range_fraction < 0.08:
            warnings.append(
                f"Geringe Bewegungsamplitude ({motion_range_fraction*100:.1f}% der "
                "Bildhöhe) - Ergebnis vor Gebrauch prüfen")
            score -= 0.15

    # Harte Ausschlusskriterien: manche Probleme sind so eindeutig, dass sie
    # unabhängig von der aufsummierten Punktzahl zum Scheitern führen sollen -
    # eine Kombination aus mehreren leichten Warnungen darf ein einzelnes
    # gravierendes Problem nicht "verwässern".
    #
    # Die Konzentrations-Schwelle ist an einem synthetischen Kalibrierungssatz
    # gemessen (dichtes Signal, siehe oben - an den Keyframes gemessen wären
    # diese Zahlen bedeutungslos):
    #   gültig:  0.5Hz 0.82 | 1Hz 0.72 | wenig Kontrast 0.71 |
    #            Kameraschwenk 0.68 | 2Hz 0.56 | halb verdeckt 0.32
    #   wertlos: unbewegtes Objekt 0.17 | Rauschen 0.10 / 0.08
    # 0.20 liegt in der Lücke, mit Abstand zum schlechtesten noch gültigen
    # Fall. Achtung bei künftigen Anpassungen: der Satz besteht aus sauberen
    # Sinusbewegungen, echtes Videomaterial liegt systematisch niedriger.
    # Die Schwelle darf deshalb nach oben nur mit echtem Material nachgezogen
    # werden, nicht auf Basis dieser synthetischen Zahlen.
    hard_fail = False
    if 'gap_fraction' in locals() and gap_fraction > 0.4:
        hard_fail = True
    if 'concentration' in locals() and concentration < 0.20:
        hard_fail = True
    # Über die Hälfte der Frames ohne echte Messung: das Skript beschreibt
    # dann überwiegend nicht mehr, was im Video passiert.
    if tracker_lost_fraction is not None and tracker_lost_fraction > 0.5:
        hard_fail = True
    # Praktisch keine Bewegung: die Kurve besteht dann aus hochskaliertem
    # Zittern und beschreibt nichts, was im Video passiert.
    if motion_range_fraction is not None and motion_range_fraction < 0.03:
        hard_fail = True

    # Der Score ist als 0-1 dokumentiert und wird so auch in der Oberfläche
    # als Prozentwert angezeigt. Ohne diese Begrenzung konnte er negativ
    # werden, sobald sich mehrere Abzüge summierten - bei einem Video mit
    # Verdeckung gemessen: -0.18. Als "-18%" angezeigt wäre das schlicht
    # kaputt.
    score = max(0.0, min(1.0, score))
    passed = score >= 0.5 and not hard_fail
    # Ausdrücklich in native Python-Typen wandeln: score kann je nach
    # Rechenweg ein numpy-Skalar sein, und numpy.bool_/numpy.float64 lassen
    # sich nicht als JSON serialisieren - das Ergebnis landet aber direkt in
    # der .funscript-Metadata.
    # Die intern berechneten Kennzahlen werden mit zurückgegeben, nicht nur
    # das Urteil. Ohne sie lässt sich nicht nachvollziehen, WARUM ein Video
    # so bewertet wurde - und die Schwellen lassen sich nicht an echtem
    # Material nachkalibrieren, sondern nur raten.
    metrics = {
        "concentration": round(float(concentration), 3) if "concentration" in locals() else None,
        "gap_fraction": round(float(gap_fraction), 3) if "gap_fraction" in locals() else None,
        "speed_spikes": int(spikes) if "spikes" in locals() else None,
        "tracker_lost_fraction": (round(float(tracker_lost_fraction), 3)
                                  if tracker_lost_fraction is not None else None),
        "motion_range_fraction": (round(float(motion_range_fraction), 4)
                                  if motion_range_fraction is not None else None),
        "action_count": len(actions),
        **device_metrics,
        "active_fraction": (round(active_fraction, 3)
                            if active_fraction is not None else None),
        "reconstruction_error": (round(reconstruction_error, 3)
                                 if reconstruction_error is not None else None),
        "actions_per_minute": (round(len(actions) / (video_duration_ms / 60000.0), 1)
                               if video_duration_ms else None),
    }
    result = {"score": round(float(score), 2), "warnings": [str(w) for w in warnings],
              "passed": bool(passed), "metrics": metrics}

    # Gelerntes Modell, falls vorhanden. Es ERSETZT die Regeln nicht, sondern
    # entscheidet über das Gesamturteil - die Warnungen bleiben unverändert,
    # weil sie erklären, was auffällig ist, und das ist unabhängig davon,
    # wer das Urteil fällt. Ein Modell ohne Begründung wäre für den Anwender
    # wertlos.
    #
    # Übernommen wurde es nur, wenn es in der Kreuzvalidierung besser war als
    # die Regeln (siehe quality_model.train) - deshalb darf es hier führen.
    if model is not None:
        try:
            import quality_model
            probability = quality_model.score(model, metrics)
        except Exception:
            probability = None
        if probability is not None:
            result["model_probability"] = round(probability, 3)
            result["rule_passed"] = result["passed"]
            result["passed"] = bool(probability >= 0.5)
            if result["passed"] != result["rule_passed"]:
                result["warnings"] = list(result["warnings"]) + [
                    f"Urteil vom gelernten Modell ({probability*100:.0f}% brauchbar) - "
                    f"die festen Regeln hätten "
                    f"{'bestanden' if result['rule_passed'] else 'nicht bestanden'} gesagt"]
    return result


def format_report(result):
    """Menschenlesbare Zusammenfassung für Log/stderr."""
    lines = [f"Quality Doctor: Score {result['score']:.2f} ({'OK' if result['passed'] else 'PRÜFEN'})"]
    for w in result["warnings"]:
        lines.append(f"  - {w}")
    if not result["warnings"]:
        lines.append("  keine Auffälligkeiten")
    return "\n".join(lines)
