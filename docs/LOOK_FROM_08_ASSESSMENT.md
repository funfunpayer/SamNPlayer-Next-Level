# Look assessment: SamNPlayer 0.8 (#550) → SamNPlayer Next

**Date:** 22 Oct 2026  
**Scope:** Aussehen only. Crank, playback, Training session logic, and silk/halo *timings* stay untouched.  
**Sources:** old repo PR [#550](https://github.com/funfunpayer/SamNPlayer/pull/550), `docs/sources/old-repo/HANDOFF_2026-10-16_GUI_FROM_08.md`, D-138 / D-150, `theme.h`, Player / Training QML.

## Verdict

**Next is already on the 0.8 Flow look for the surfaces that matter.** Empty Player, window silk, Training rings stage, tokens (cyan/orange, glass, Sora/Figtree) match the Owner direction from #550 and D-150. No big GUI rewrite is needed for “look parity with SamNPlayer.”

What remains open in `PARITY.md` for silk/play is mostly **reduced-motion** and optional ring polish — that is motion policy, not “missing the SamNPlayer face.”

## Already matching

| Surface | SamNPlayer 0.8 | SamNPlayer Next |
|---|---|---|
| Empty Player | Round `.flow-play` + **Open…**, centred; no wordmark / stage art | `PlayerPage` empty: `PlayButton` 168×168 + Open… (D-138) |
| Header Open… | Quiet secondary while empty | `FlowButton` `primary: !page.empty` |
| Play symbol | Soft cyan→orange halo, breathe 4.5 s / 2.4 s playing | `PlayButton.qml` (same periods) |
| Window silk | Cyan top-right, orange bottom-left ribbons | `Main.qml` corner bands |
| Player stage silk | Drift behind video / letterbox | `SilkLayer` in video stage |
| Training | Two glow gauges, silk bridge, name / % / wave | `TrainingRing` + `stage-ribbons` bridge |
| Tokens | Navy `#021024`→`#01070f`, cyan `#00d9f4`, orange `#ff8a23` | `theme.h` |

## Look + motion polish applied (this branch)

1. Static empty-CTA text closer to 0.8 `.pb-empty-open`: letter-spacing and a soft cyan glow under **Open…**.
2. **Play button:** softer silk ring, glyph glow, idle reverse drift (ambient) that quickens while playing; hover bloom.
3. **Empty Player home:** calm ambient bloom + settle-in; still only Play + Open… (no wordmark clutter).
4. **Transport:** progress trail glow, playhead knob pulse while playing.
5. **Tabs / drop overlay / glass cards / silk:** quieter hover scale, fade-in drop, glass catch-light, slightly richer silk.
6. **Training rings:** restored clear gauges (mockup): soft shell, level arc, gentle live halo; silk bridge drifts without opacity pulse.
7. **Icon buttons / Library rows:** hover scale and cyan edge; “Feel without film” hint floats gently.
8. **FlowTabs / toasts / window silk:** current-tab wash, toast scale-in, silk opacity breathe (pauses when inactive).
9. **Inspector channels / Generator progress:** soft bar glow; Settings card settles a little slower.

Controller / crank / open-media logic untouched. Decorative motion pauses when the app is inactive.

## Intentionally not changed (later / not look-blocking)

1. **`prefers-reduced-motion`** — Go CSS pauses decorative motion; Next has no switch yet (`PARITY` § silk / Play). Wire later without redesigning the look.
2. **Empty Play size** — Go empty uses `--fp-size: 128px`; Next uses 168 for a clearer hero. Keep larger unless Owner wants exact pixel match.
3. **Ribbon-trail SVG idle graphic** — superseded on empty home by Play+Open; only relevant for “Feel without film” if someone wants the old Flow stage art back.
4. **Training rings “closer to mockup”** checklist row — composition is already there; further glow tweaks are optional aesthetic, not a gap vs #550.

## For the next agent

- Treat empty-home parity against Go `#pb-empty` / `.flow-play`, not against pre-#550 welcome screenshots.
- Prefer small static token tweaks (colour, spacing, glow strength) over changing animation durations or crank/play wiring.
- Visual targets: `docs/visual-target/player.png`, `training.png`.
