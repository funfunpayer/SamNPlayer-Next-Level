"""Test für fungen_compare_windowed.py - siehe Moduldoku und
docs/NEXT.md ("First real Tf/Tj golden-clip measurement"): dieses Skript
muss eine drifende Lag-Verschiebung als drifende Werte pro Fenster zeigen,
statt sie (wie eine einzelne Ganzclip-Suche) zu einer einzigen,
irreführend niedrigen Korrelation zu verwaschen.

Ausführen: python3 generator/fungen_compare_windowed_test.py
"""

import sys

import numpy as np

import fungen_compare_windowed as fcw


def sine_actions(duration_ms, period_ms, amplitude=40, center=50, step_ms=40, t0=0,
                  phase_ms=0):
    actions = []
    t = 0
    while t <= duration_ms:
        pos = center + amplitude * np.sin(2 * np.pi * (t + phase_ms) / period_ms)
        actions.append({"at": t0 + t, "pos": round(float(pos), 1)})
        t += step_ms
    return actions


def drifting_lag_actions(duration_ms, period_ms, max_drift_ms, amplitude=40, center=50,
                          step_ms=40):
    """Same shape as `sine_actions`, but the phase offset ramps linearly
    from 0 to max_drift_ms across the clip - the case a single whole-clip
    lag search cannot represent."""
    actions = []
    t = 0
    while t <= duration_ms:
        drift = max_drift_ms * (t / duration_ms)
        pos = center + amplitude * np.sin(2 * np.pi * (t - drift) / period_ms)
        actions.append({"at": t, "pos": round(float(pos), 1)})
        t += step_ms
    return actions


def main():
    failures = []

    def check(name, condition, detail=""):
        if not condition:
            failures.append(name)
            print(f"FEHLER: {name} ({detail})")

    # --- Konstanter Lag: jedes Fenster soll den gleichen Lag finden ------------
    ref = sine_actions(120000, 4000, t0=0)
    shifted = sine_actions(120000, 4000, t0=0, phase_ms=500)
    results = fcw.windowed_correlation(ref, shifted, window_ms=30000, max_lag_ms=1000)
    confident = [r for r in results if r.get("r") is not None]
    check("konstanter Lag: alle Fenster liefern ein Ergebnis",
          len(confident) == len(results), str(results))
    lags = {r["lag_ms"] for r in confident}
    check("konstanter Lag: alle Fenster finden denselben Lag (kein Drift)",
          len(lags) == 1, str(lags))
    check("konstanter Lag: hohe Korrelation in jedem Fenster",
          all(r["r"] > 0.9 for r in confident), str([r["r"] for r in confident]))

    # --- Driftender Lag: genau der Fall aus der 21.-Sep-Messung -----------------
    ref2 = sine_actions(120000, 4000, t0=0)
    drift2 = drifting_lag_actions(120000, 4000, max_drift_ms=2000)
    results2 = fcw.windowed_correlation(ref2, drift2, window_ms=15000, max_lag_ms=2500,
                                         lag_step_ms=100)
    confident2 = [r for r in results2 if r.get("r") is not None]
    check("driftender Lag: genug Fenster liefern ein Ergebnis",
          len(confident2) >= 6, str(results2))
    early_lag = confident2[0]["lag_ms"]
    late_lag = confident2[-1]["lag_ms"]
    check("driftender Lag: letztes Fenster hat einen deutlich anderen Lag als das erste "
          "(genau das, was eine einzelne Ganzclip-Suche nicht abbilden kann)",
          abs(late_lag - early_lag) > 500, f"early={early_lag} late={late_lag}")
    check("driftender Lag: jedes Fenster bleibt trotzdem gut korreliert",
          all(r["r"] > 0.8 for r in confident2), str([r["r"] for r in confident2]))

    # --- Zu wenige Aktionen in einem Fenster: 'undefined', nicht stillschweigend
    #     übersprungen -----------------------------------------------------------
    sparse_ref = [{"at": 0, "pos": 50}, {"at": 200000, "pos": 60}]
    sparse_variant = sine_actions(200000, 4000, t0=0)
    results3 = fcw.windowed_correlation(sparse_ref, sparse_variant, window_ms=20000,
                                         max_lag_ms=1000)
    check("zu wenige Aktionen im Fenster wird als 'undefined' markiert, nicht "
          "stillschweigend ausgelassen",
          any(r["r"] is None and r.get("reason") == "too_few_actions_in_window"
              for r in results3),
          str(results3))
    check("Fensteranzahl bleibt vollständig (keine Fenster verschwinden)",
          len(results3) == 10, str(len(results3)))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
