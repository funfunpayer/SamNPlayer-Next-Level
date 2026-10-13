# Selling SamNPlayer — what we still need

> **Superseded 10 Oct 2026 (Owner):** SamNPlayer is **freeware with an EULA,
> financed by donations** — no keys, no price, no checkout. The key/price
> rows below are history; see `docs/LICENSE_SYSTEM.md`.

Living checklist for turning the product into something people can
**find, trust, buy, and run**. Chat with the owner may be German; this
doc stays English with the rest of the product surface.

Related: `website/` (landing page source), `docs/CLOSED_SOURCE.md`,
`docs/LICENSE_SYSTEM.md`, `docs/COMPETITIVE.md`, `docs/PRODUCTION_ROADMAP.md`
stream E / C3.

---

## Verdict (today)

The app is close enough to demo and invite. It is **not** ready for
hands-off retail yet: license enforcement is off, there is no paid
checkout, and the public face is still a GitHub README rather than a
live Pages site.

---

## Must-have before charging money

| # | Need | Why | Status |
|---|------|-----|--------|
| 1 | **Public landing** with brand, one CTA, portable download | Buyers do not read repos | Source in `website/` — publish to `SamNPlayer-site` + Pages |
| 2 | **Production license keys** | Replace DEV issuer; keep private key offline | Infra built; **DEV pubkey** still embedded |
| 3 | **Enforcement ON** | Trial / unlicensed limits must match the offer | `license.Enforcement = false` |
| 4 | **Price + payment** | Stripe / Lemon Squeezy / similar → webhook → `license-tool issue` | **€40 / year** locked; checkout not built |
| 5 | **Key delivery** | Email the `SNP1` file after payment | Manual only |
| 6 | **Public binary channel** | Releases on `SamNPlayer-site` (or paid CDN) with checksums | Documented; publish script still missing |
| 7 | **Contact that works** | License requests, refunds, device help | Site CTA → showcase GitHub issue; swap to real inbox when ready |
| 8 | **Legal basics** | Proprietary license text, ToS, privacy (local-only claim) | MIT on old public copies; new work needs counsel if closed |

---

## Trust blockers (sell harder once fixed)

| Item | Buyer question it answers |
|------|---------------------------|
| Golden-clip / FunGen numbers on the site (short, honest) | “Is generation actually good?” |
| Windows portable smoke on a clean PC (no system ffmpeg) | “Will it run for me?” |
| Real Neo 2 clip of Play + contact vibration | “Does the device feel right?” |
| macOS build (later) | “Do you support my machine?” |
| Same English copy on README, site, and in-app | “Is this a real product or a hobby dump?” |

Do **not** invent unmeasured quality claims. Prefer one screenshot + one
number over a feature wall (`docs/COMPETITIVE.md`).

---

## Offer (locked)

Already decided in `docs/LICENSE_SYSTEM.md`:

- **Retail:** **€40** / year — 1 person, full Generate + `.samn` play (feature `virtual_person` stamped for key compatibility; VP product cancelled; not an addon)  
- **Trial:** 1 minute Generate; Funscript play without Neo-2 `.samn`  
- **Invite / internal:** no expiry for testers  

Still to decide with the owner (not inferred here):

- Renewal wording (same €40 / year vs. early-bird)  
- Refund window  
- Whether Discord / email is the support channel  
- Whether Anna-class reviewers get invite keys only (no public download)

---

## Distribution shape

```text
  SamNPlayer (private source)
        │  tag release
        ▼
  build Win/Linux (+ portable ffmpeg)
        │  publish assets + checksums
        ▼
  SamNPlayer-site (public)  ←  GitHub Pages = website/
        │
        ├─ Download CTA
        └─ License CTA → payment → SNP1 key → Settings import
```

---

## Suggested order of work

1. Publish `website/` to Pages; keep license CTA on showcase issues (or real inbox)  
2. Owner sets support channel (price locked at **€40** / year)  
3. Production `genkey`, embed pubkey, flip enforcement  
4. Minimal checkout at **€40** → issue key (even if semi-manual at first)  
5. Trust pack: portable Win test + one Neo 2 feel note + optional bench number  
6. Only then: ads, influencers, store listings  

Until step 3–4, treat every “sale” as an **invite + manual key**.

---

## Explicit non-goals (for the first paid wave)

- Phone app store listing  
- Cloud account / sync  
- Multi-seat team plans  
- Funscript marketplace  

Keep the pitch: **local, measured, Sam Neo 2 first-class, small native GUI**.

---

## Site content map (links + blog)

Landing stays thin. Extra pages earn a footer / nav link only if they
help someone **download, trust, buy, or succeed on Neo 2**.

### Links worth putting on the site (footer or short “Guides” row)

| Link | Target | Why |
|------|--------|-----|
| Download (portable) | Showcase releases | Primary CTA |
| License — €40 / year | `#license` / checkout later | Buy path |
| Getting started | `/guides/start` | First-run Win/Linux + portable ffmpeg |
| Sam Neo 2 + Bluetooth | `/guides/neo2-ble` | Dongle tip already in fineprint; expand |
| Import a license key | `/guides/license` | Screenshot of Settings → License |
| Changelog | curated release notes (English) | Trust / “alive product” |
| Showcase GitHub | `SamNPlayer-site` | Issues / license requests |

Optional later: Intiface Central docs (external), SVAKOM product page
(external, careful — not an endorsement claim).

### Blog / notes — high value (reuse measured docs, rewrite for buyers)

Ship as short English posts under `/blog/` or `/notes/`. One idea, one
screenshot or number. Do **not** paste internal `docs/*.md` verbatim.

| Priority | Topic | Source material (private) | Buyer takeaway |
|----------|--------|---------------------------|----------------|
| 1 | AI proposes, tracking writes | `AI_ADAPTER.md`, README | Why no silent AI Funscripts |
| 2 | Signal quality ≠ motion fidelity | `SIGNAL_VS_FIDELITY.md` | How to read Quality Doctor |
| 3 | Portable zip on a clean PC | `FFMPEG_TOOLS.md`, SELLING trust | “It runs without installing ffmpeg” |
| 4 | Tf/Tj + contact vibration | `TF_TJ.md`, `SAMN_FORMAT.md` | Why Neo 2 needs two axes / contact |
| 5 | vs FunGen (honest scope) | `FUNGEN_FEATURE_COMPARE.md` | Player+device+train, not a clone |
| 6 | Golden-clip / one correlation number | Bench when owner has clips | Measured claim, not vibes |
| 7 | Release notes as posts | `CHANGELOG.md` | SEO + “shipping” signal |

### Interesting but secondary

- **FAQ** page: platforms, Mac status, trial 1 min, local-only, €40 seat  
- **Short silent screen recording** of Play → device meters (no NSFW required if script-alone + mock)  
- **Hardware shopping list** (UB500 / USB-BT500) — reduces support load  
- **`.samn` vs `.funscript`** one-pager for Neo 2 buyers  

### Skip for now (dilutes the sell)

- Long engineering journals (`NEXT.md`, rejected experiments)  
- Full competitive feature matrices  
- Community / social Funscript network  
- German mirror of every post (UI/docs stay English; optional DE later)  
- Card grids of “12 features” on the homepage  

### Minimal nav once Guides exist

`Why · Product · Guides · License · Download` — Guides opens an index
of 3–5 posts max at launch; Blog can be the same index until volume grows.
