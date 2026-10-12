# Release 0.5.4 — test release (Kontakt + SAM runtime)

Test bed for Kontakt-Vibration and the first SAM runtime layer. Not a claim
that FunGen parity or full SAM Milestone 2 is done.

## Ship contents

- Kontakt product + signal quality (span, curve, gaps, envelope) — #92  
- SAM Enrich, `.sam` sidecar, Densify, RuntimeAdjust — #93  
- Live playback overrides: Stärke / Empfindlichkeit / Kurve (GUI + CLI)  
- Bugfix: Gap-Mute vs Envelope, thin sidecar, CLI `--output` after path  

## Your smoke tests

### Generate (Tf/Tj + Kontakt)

1. Two ROIs, profile Tf/Tj, Kontakt an, Span ≠ default, Kurve soft oder peak.  
2. Expect `.funscript` with `device_recipe.contact_vibration*` and `.sam` beside it.  
3. Log/status: Kontakt-Pfad; sidecar path if shown.

### Playback GUI

1. Load that script → second (orange) vibration track visible.  
2. **Kontakt ab** → track/preview empty or zero; suction still moves.  
3. **Kontakt-Stärke** 0.5 / 1.5 → preview + playback scale; file unchanged.  
4. **Empfindlichkeit** niedriger → earlier vib; höher → only deep contact.  
5. **Kurve** soft vs peak → soft quieter in the mid range.  
6. Extended-O still works on Tf/Tj contact scripts.

### CLI

```text
SamNPlayer sam clip.funscript --output clip.sam
SamNPlayer --script clip.funscript --mock --mute-contact
SamNPlayer --script clip.funscript --mock --contact-intensity 0.5 --contact-span 0.5 --contact-curve peak
```

### Gaps / device

1. Script with `tracking_gaps` (or lost-tracker generate) → vib hard-off in window.  
2. Neo 2: short contact scene — vib only near contact, suction follows distance.

## Explicitly later (not blocking 0.5.4)

- FunGen real-clip goldens  
- Prediction / live webcam  
- Motion classification  
- Other devices beyond Neo 2 family  

See `docs/SAM_ARCHITECTURE.md`, `CHANGELOG.md` [0.5.4].
