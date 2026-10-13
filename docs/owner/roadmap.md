# Roadmap + Meilensteine (kurz)

**Für Owner** · Stand: **2026-09-28** · Baseline **[`v0.5.40`](https://github.com/funfunpayer/SamNPlayer/releases/tag/v0.5.40)**  
**Kein VERSION-Bump** — Docs only.

Lebende Langfassung (Project Context): Cursor Store `docs/roadmap-meilensteine.md`.  
Engineering: [`../PRODUCTION_ROADMAP.md`](../PRODUCTION_ROADMAP.md) · [`../ROADMAP.md`](../ROADMAP.md).

---

## Baseline v0.5.40

| Feature | Kurz |
|---------|------|
| **Contact Verify** | Create → Advanced → Verify with the engine (hybrid, K=1.5) — Default **aus** |
| **Bench Suggest** | Bench → Suggest beside video (FunGen-Ref + Everyday `__hub`) |
| **Anleitung** | Index [`../ANLEITUNG.md`](../ANLEITUNG.md) · Funktionen / Zusammenhang / Bench |

Portable: [v0.5.40 Windows zip](https://github.com/funfunpayer/SamNPlayer/releases/download/v0.5.40/SamNPlayer-portable-windows-amd64.zip)

---

## Meilensteine

### Kurz (ohne Owner-Clips / Claude pausiert)

- Offene CI-Slices mergen (Docs, Tip-Find, Verify-Tests, Clip-Prep-Cutter)
- Tip-Find kompakter · Appearance-Reacquire härten · Clip-Prep In/Out in Bench
- Anleitung ↔ GUI · Context-Store aufräumen · Board-Watch

### Mittel (mit Clips + Claude)

- ≥4–5 Rhythm-Clips messen → erst dann Default-Diskussion  
- Long-Clip-Drift / FunGen-Bake-off / V0 auf Owner-GPU  
- Claude Engine-PRs → Cursor squash-merge

### Länger

- AI-Train-Loop (Bench-Labels → Train → Assist → CSRT)  
- Patch-Kanal · G2 Neo-2 Raw · G3/G4 · macOS/Mobile · Perception 2.0 (je Go/No-Go)

---

## Park / Abbruch

| Status | Was |
|--------|-----|
| **Cancelled / out of scope** | **Virtual Person / [#264](https://github.com/funfunpayer/SamNPlayer/pull/264)** — kein zukünftiger SamNPlayer-Meilenstein |
| Owner-gated | Rhythm/Verify defaults · speed-cap · AIWrite Everyday · Enforcement · Website |
| Skip | Neue Everyday-Tracker · Cloud/Telemetrie · Light-Mode/Card-Redesign |

---

## Abgrenzung

- Produkt-UI Englisch; diese Owner-Notizen Deutsch.  
- Generische `pluginhost/` Drop-Folder-Infra bleibt; VP-Produkt nicht.
