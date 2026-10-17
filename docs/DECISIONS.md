# Decision log

All decisions taken for SamNPlayer (old Go/Wails repo, up to v0.7.0) that still matter
for SamNPlayer 2, so that none is decided again from zero. Draft of 16 Oct 2026, built
only from the old repo's docs and this repo's `PLAN.md` / `AGENT_STATE.md`.

**Status for SamNPlayer 2:** `kept` = still valid as written · `changed by D-xxx` = a later
decision replaced it · `superseded` = obsolete, kept for history · `open` = no decision yet.
Where two sources disagree the row says `CONFLICT:` and names both sources.
A date of "—" means the source gives none.

**Source keys** (all paths relative to the old repo `/home/user/SamNPlayer`):

| Key | File |
|---|---|
| R26 | `docs/ROADMAP_2026-10.md` |
| PR | `docs/PRODUCTION_ROADMAP.md` |
| OD | `docs/next/OPEN_DECISIONS.md` |
| MP | `docs/next/MASTER_PLAN.md` |
| HO | `docs/next/HANDOFF_2026-10-15.md` |
| CAP | `docs/next/PRIORITIZED_CAPABILITY_REGISTER.md` |
| FE | `docs/next/FIVE_ENGINE_ARCHITECTURE_AND_GUI.md` |
| NR | `docs/next/ROADMAP.md` (gates N0–N6) |
| AS | `docs/AGENT_STATE.md` (old repo) |
| AC | `docs/AGENT_COORD.md` |
| ED | `docs/EDITIONS.md` |
| ES | `docs/ENGINEERING_STANCE.md` |
| TT | `docs/TFTJ_PROFILE_DIRECTION.md` |
| EG | `docs/EVERYDAY_GENERATE.md` |
| DS | `docs/DEVICE_SUPPORT.md` |
| GP | `docs/owner/generator-profile-2026-10.md` |
| RW | `docs/REWRITE_PLAN_FAST.md` |
| MG | `docs/MIGRATION_CPP_PYTHON.md` |
| LS | `docs/LICENSE_SYSTEM.md` |
| CS | `docs/CLOSED_SOURCE.md` |
| LG | `docs/LANGUAGE.md` |
| SB | `docs/SELF_BUILD.md` |
| SA | `docs/SAM_ARCHITECTURE.md` |
| SW | `docs/AI_SCRIPT_WRITER.md` |
| UH | `docs/USER_HANDBOOK.md` |
| CL | `CHANGELOG.md` |
| NP | this repo: `docs/PLAN.md` |
| NS | this repo: `docs/AGENT_STATE.md` |
| NM | this repo: `docs/MODULES.md` |
| BRIEF | the lead's task brief of 16 Oct 2026 (quotes the Owner; not yet written in any repo doc) |

---

## 1. Product and editions

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-001 | 10 Oct | SamNPlayer is freeware with an EULA, financed by donations, not open source; the licence-key system was removed and every build is fully unlocked. | Key system and trial limits gave friction while Generate was still moving. | LS (table, top); CL 0.6.0 "Changed" | kept |
| D-002 | 12 Oct | There is one program, free, with voluntary donations; price, supporter threshold, licence pass (P7) and payment provider are dropped. | Owner answer to "Paid product?" = No. | OD "Answered" row 1; ED "Product lines" | kept |
| D-003 | 12 Oct | The paid "Studio" proposal (PR #510, supporter right above EUR 40, expiring previews) is history only; its technical ideas may be reused as proposals. | Superseded by PR #520 on Owner instruction. | ED "Historical paid-Studio proposal"; MP §1 | kept |
| D-004 | 12 Oct | The editions are Free (customers, default build), Suite (internal build with AI training and the batch test system, `-tags suite`) and "2.0 Next" (next major version, also free). | Free is also the team's test system; Suite keeps training internal. | ED "Product lines" | changed by D-009 for the name and repo; the Free/Suite split in SamNPlayer 2 is open (see D-007) |
| D-005 | 12 Oct | Free stays free; moving any feature out of Free is the Owner's decision alone and needs a reviewed change, migration notes and regression tests. | Users must not lose features silently. | ED "Rules" | kept |
| D-006 | 12 Oct | The seven-day feature-preview idea was cancelled (PR #515 closed); no timed feature expiry. | Owner decision, no paid tier. | ED "Rules" | kept |
| D-007 | 11 Oct | AI training, learning data, the golden-clip benchmark tab and related tools belong to the internal Suite build, not to the customer build; whether to ship them later is open (old D6). | Owner: training belongs to the internal system. | ED "Free vs Suite today"; OD D6 | open |
| D-008 | 12 Oct | "Cut AI or batch from a Free edition later?" was answered: dropped; new features ship to everyone. | No paid tier exists. | OD "Answered" row 3; MP §3 rule 1 | kept. CONFLICT: ED "Rules" and OD D6 still allow Suite-only features (D-007); both are in the sources and neither names the other. |
| D-009 | 13–15 Oct | The 2.0 Next plan is "one SamNPlayer, not a second edition"; "Studio functions" is shorthand for advanced capabilities only. On 15 Oct the plan moved into the old repo under `docs/next/` (old D12 left open). | Avoid two code lines and a paywall. | CAP (header); OD "Answered" row 4 and D12; MP header | changed by D-110 (SamNPlayer 2 is built in repo `SamNPlayer-Next-Level`). CONFLICT: R26 §2 says "Next Level" is a separate private repo with its own heavy-innovation scope and Free = stability only; OD/MP/ED/CAP say Next is the same free program inside the old repo. |
| D-010 | 15 Oct | Suite files must never be published: CI only compiles the Suite build, and a guard step fails if any file to publish contains `suite` (old D14). | `release.yml` would have attached `SamNPlayer-suite-*` to the public release. | OD D14; HO task 1; AS "Open" item 1 (done in branch) | kept as a rule for the new release workflow |
| D-011 | — | Source code lives in a private repo; only binaries and the site live in a public repo (`SamNPlayer-site`). | GitHub does not serve release assets of private repos to anonymous users. | CS "The right split"; LS table row "Source" | open. CONFLICT: OD D14 and HO call the old repo and `SamNPlayer-Next-Level` "public"; CS and LS say "private". Visibility is not settled in the sources. |
| D-012 | 15 Oct | Releases of the old app stay on `funfunpayer/SamNPlayer` because installed apps check there (old D13); a move needs a bridge release first. | The updater reads that repo. | OD D13; HO "Findings" | open for SamNPlayer 2 (updater source not decided) |
| D-013 | 15 Oct | The old Go app gets fixes only until SamNPlayer 2 ships; the Go app is archived at the first SamNPlayer 2 release. | No risk for users of v0.7.x. | NP intro; RW Decision 1; RW phase P5 | kept |
| D-014 | 15 Oct | Everything SamNPlayer 0.7 can do is built again in SamNPlayer 2. | Owner direction. | NP "Owner decisions" | kept |
| D-015 | 10 Oct | The old DEV licence key (`license/testdata/issuer.ed25519`) in git history needs no action and must never be reused if keys ever return. | Without a licence system it protects nothing. | OD D10; LS | kept |
| D-016 | 12 Oct | Safety (emergency stop, device limits) stays in every edition. | Safety must not depend on an edition. | ED "Rules"; MP §3 rule 2 | kept (see D-037) |
| D-017 | 12 Oct | The updater matches the exact file name of its own edition; Free never updates to Suite and the other way round. | A suffix match picked the wrong edition in 0.6.0. | ED "How it works"; CL 0.6.0 "Fixed" | kept if editions survive (D-007) |
| D-018 | — | macOS is not a target for now; iOS/Android would be a later player-only app without the generator; Windows is the primary smoke target. | No Mac CI runner or signing. | PLATFORMS.md "Matrix"; MG "Open questions" (asks whether macOS is still not a target); UH FAQ | open (the Owner question in MG was never answered in the sources) |

## 2. Licences and third-party code

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-019 | 15 Oct | No code is copied from GPL, BSL or EULA projects (ScriptPlayer GPL, BetterPlayer BSL/GPL, ScriptPlayer+ closed); MIT code (MultiFunPlayer, Rantark) only with a notice. We build our own versions. | Keep the product's own licence clean. | R26 §3 intro; NP "Rules"; AS "Never" | kept |
| D-020 | 15 Oct | The Qt interface uses an LGPL toolkit shipped as shared libraries, named in the third-party notices, with Qt itself unmodified. | LGPL compliance. | RW Decision 2 "Licences" (written for PySide6) | kept (applies equally to C++ Qt 6, see D-102) |
| D-021 | 11 Oct | The EULA is shown once per EULA version on first start (Accept / Decline and quit); third-party notices (FFmpeg GPL build, OpenCV, fonts …) ship with the app and in Settings. | Legal baseline for freeware. | LS table; CL 0.6.0 "Added" | kept (P5) |
| D-022 | — | Donation links (Patreon, PayPal) live in Settings and a header button, hidden until a URL is set; donations give no extra rights. | Donation-only financing. | LS table; MP §3 rule 1 | kept |
| D-023 | 15 Oct | BLE goes through `bleak` (MIT) because the C++ alternatives are licence-restricted (SimpleBLE is BUSL) or per platform. | Licence and cross-platform reach. | MG "Why Python as the host" | open: depends on D-102 (C++ first); the licence reason stays valid |
| D-024 | — | A self-built piece replaces a dependency only if it is equal or better on clips and tests; worse in any measured point means reject; fallbacks must not replace the better path. | Fewer dependencies must never cost quality. | SB "Gate" | kept |
| D-025 | 24 Sep | Ideas from external libraries and essays are adopted, dependencies are not, unless a golden-clip gate says so; GoCV, MOSSE/KCF, cloud video APIs, webcam/V4L2 and streaming stacks are rejected. | Avoid shopping lists; keep one tracker stack. | ES "Stack stance"; "Go / video / CV link dump" | kept (the Go-specific parts are moot) |
| D-026 | 24 Sep | FFmpeg stays the decode/proxy path (external process, no binding); a replacement needs a measured win. | Already the working decode path. | ES "Keep / prefer"; FE "Dependency policy" | kept |

## 3. Privacy

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-027 | — | Local-first: no cloud required for core functions, no automatic media upload; videos stay on the user's machine. | Privacy; also a selling point against upload-based tools. | SA "Privacy / architecture principles"; MP §3 rule 6; CAP invariant 2; R26 §3b (PeakSync row) | kept |
| D-028 | 24 Sep | No telemetry, no bundled model weights, no auto-download, no cloud call without opt-in. | Local-only AI. | ES "Keep / prefer" (Local-only AI); MP §3 rule 6 | kept |
| D-029 | — | Learning data is opt-in, local, exportable and deletable, and can be switched off; collection is off by default. | User control. | SA "Privacy"; MP §3 rule 6; ES "Owner gates" 3 | kept |
| D-030 | 15 Oct | Sentry is used for development builds only; it must never be in the shipped app (needs a project/DSN from the Owner). | No telemetry in releases. | AS "Open, in order" item 7 | kept; open: Owner has to provide project and DSN |
| D-031 | 13 Oct | Optional AI over LAN (e.g. LM Studio) only when explicitly configured; no model or user-correction upload happens silently. | Trust. | CAP invariant 2; FE "Test matrix" N7 | kept |
| D-032 | 26 Sep | A cloud LLM that writes curve positions is out of the product. | Breaks per-frame spatial identity and privacy. | SW "Product rule"; ES "Reject / defer" | kept |
| D-033 | 13 Oct | Users keep their data: updates never break existing `.samn`, `.funscript`, projects or settings; scripts never expire. | Trust and portability. | MP §3 rule 8; CAP invariant 4 | kept |

## 4. Devices and safety

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-034 | 11 Oct | The Sam Neo 2 is the first and main device and stays fully supported; other devices (The Handy, multi-axis strokers, vibrators) must be supported fully too. | Most users own other devices. | DS header | kept |
| D-035 | 11 Oct | The UI shows the curves and controls of the connected device, and nothing the device cannot do; without a device it shows what the script contains. This replaces the V1 rule "no Axis 3/4". | Device-aware UI. | DS "Direction" 2 | kept |
| D-036 | 11 Oct | Every connection reports its capabilities on connect; the GUI never relies on a fixed device table. | Real report beats assumptions. | DS "Named devices"; step D1 | kept |
| D-037 | 12 Oct | Emergency stop, Disconnect, Pause, Stop and device caps work in every build and take precedence over AI, batch and previews. | Safety first. | MP §3 rule 2; CAP invariant 5 | kept |
| D-038 | 13 Oct | No invented device axes: extra axes (surge, sway, twist, roll, pitch) come only from real companion files or video evidence. | Honest output. | MP §3 rule 4; CAP CAP-04; DS "Direction" 5 | kept |
| D-039 | 11 Oct | Device transports are the own Sam Neo 2 BLE, Intiface/Buttplug (LinearCmd, ScalarCmd, RotateCmd), TCode over Wi-Fi (UDP/TCP) and USB serial (115200 baud), plus a mock; the Handy online API (HSSP) is optional and later. | Covers the named devices. | DS "Today" and "Direction" 4 | kept (P2) |
| D-040 | 15 Oct | Stroker smoothing (PCHIP/MAKIMA, speed limit, idle fill, move home on pause) is a live playback filter on what the device receives; the script file is never changed. | Keep the author's file untouched. | R26 §3 item 2; CL Unreleased; UH §1c | kept |
| D-041 | 14 Oct | After a start, a pause or a jump the first stroker move eases in (at most one full stroke per 0.5 s) instead of slamming. | Device slammed to the new position in 20 ms. | CL 0.7.0 "Fixed" (soft seek) | kept |
| D-042 | 13 Oct | Strokers get a safety range (bottom/top, at least 10 % apart); the script's stroke is scaled into it; companion axes are not scaled by it. | User protection. | DS step D3, D4 | kept |
| D-043 | 15 Oct | The device preview shows what the device really gets after limits and moves the device only after a deliberate "Arm". | No accidental movement. | R26 §4 E8 | kept (editor E8) |
| D-044 | 13 Oct | Physical hardware approval is the Owner's job; agents test with mocks (0/1/2/6 axis devices, stop/disconnect tests). | Agents have no hardware. | CAP CAP-04; AS "Waiting on the Owner"; NS "Next" (P2) | kept |
| D-045 | 24 Sep | The minimum spacing of device commands is 100 ms (metric adopted from funjack/launchcontrol). | Device transmit floor. | ES "Funscript ecosystem links" | kept |
| D-046 | 24 Sep | The speed-cap default for Normal/Soft stays uncapped; Autotune forces 400 if unset; a Neo 2-safe default is an Owner gate that is still open. | Do not change defaults silently. | ES "Product locks" and "Owner gates" 1; EG "Classical heuristics" | open (Owner decision pending) |
| D-047 | 15 Oct | A Lovense-API bridge (imitating a vendor API) and StrokeGPT-style chat control are not built now; legal check first. | Far from the core; legal risk. | R26 §3b, §5 | kept |
| D-048 | 15 Oct | An in-app browser with an EroScripts download is not built. | Legal and technical questions. | R26 §5 | kept |
| D-049 | 24 Sep | Neo 2 raw-value mapping (G2) is measured on real hardware, not guessed; further raw-profile work is parked until the Owner opens it. | "Good funscript" is not the same as good feel. | PR G2; "Dependency picture" | open (needs Owner hardware) |

## 5. Generator and quality gates

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-050 | 23 Sep | Everyday Generate is: find the tip region automatically, track it with CSRT, write Stroke plus Contact vibration; 4-zone and Flow are not Everyday defaults (4-zone is not in the Generate GUI). | CSRT ranked first on the measured clip (windowed r 0.59/0.47 vs 4-zone 0.36); 4-zone fusion caused flatlines. | EG "Measured first choice"; AC "Owner decision — 4-Zone → 1-Zone"; TT "Step 4 measure" | kept (P3) |
| D-051 | 21 Sep | Contact vibration is on by default for Normal and Auto and can be switched off. | Owner answer 2. | TT "Owner answers" 2 | kept |
| D-052 | 21 Sep | Marking is optional and only for contact scenes where contact vibration is wanted; a marked partner must be tracked, not a static box. | "Do not force marking everything"; a static box drifts. | TT "Owner answers" 1; "Compensation rule" | kept |
| D-053 | 21 Sep | Tf/Tj distance profiles are not part of the product GUI (CLI/legacy only); Tf/Tj is a feel recipe on top of the best classical stroke path. | Two-point distance did not close the FunGen gap. | TT "Owner answers" 4, 7 | kept |
| D-054 | 21 Sep | The system shows every usable motion candidate and the user mainly marks the primary stroke; quality must not depend on how well the user paints boxes. | Honest UX. | TT "Owner answers" 5; step 4b | kept |
| D-055 | 21 Sep | A second ROI is never committed silently. | `find_two_rois` measured bad (issue #8). | TT "Owner answers" 5 (original) | changed by D-056 |
| D-056 | 28 Sep | ROI2 may be filled from the AI's partner proposal only when the user switched on "Apply AI setup automatically" (default off); every applied value is shown and undoable, user values win, never with a Tf/Tj distance profile. | Owner: "Wenn das gut ist ja". | TT "Owner answers" 5 (amended); AC Decision log 28 Sep | kept |
| D-057 | 21 Sep | AI never writes the 0–100 curve; classical tracking writes the funscript and AI only proposes (ROI, profile, quality) or verifies. | AI Train would learn noise otherwise; per-frame identity. | TT "Explicit non-goals"; PR "AI rule"; AI_ADAPTER.md; EG | changed by D-058. CONFLICT: TT non-goals ("AI writing the funscript curve") and PR "AI rule (unchanged)" vs SW and PR G3.6 (26 Sep, an opt-in local AI draft that writes the stroke). |
| D-058 | 26 Sep | An opt-in local AI draft (off by default, Advanced only, Quality Doctor and Keep/Discard) is allowed as a second path; Everyday stays CSRT. | Owner: "create a way that AI can also write Funscripts". | SW "Product rule"; PR G3.6; ES "Product locks" | kept, but for SamNPlayer 2 the Owner brief says AI is assist/verify only (see D-059) |
| D-059 | 15 Oct | Every AI result is checked by an independent verifier against video evidence, timebase and device limits; without a model the classical pipeline still works; AI is optional and never the final authority or a device command source. | Models are not ground truth. | MP §3 rule 3; FE "Boundary rules"; CAP CAP-03 | kept |
| D-060 | 24 Sep | Detrend is on by default for stroke profiles (about 2 stroke periods) and is not the same as Speed Cap. | Tracker drift shifted the baseline of the curve (stuck windows 62 % to 3 %). | ES "Product locks"; AC Decision log 23 Sep | kept |
| D-061 | 23 Sep | The rhythm grid stays an opt-in Advanced option until the Owner has at least 4–5 clips; target lock seeds it from the confirmed ROI. | Two clips are not enough to flip a default. | ES "Product locks", "Owner gates" 2; AC Decision log | kept |
| D-062 | 24 Sep | Scene-map scan runs only on the button "Show scene map" (never automatically before Create); learning collect is off; auto-labels need review; marks default to the current scene. | Owner decision on SceneMap §6. | ES "Owner gates" 3 | kept |
| D-063 | 24 Sep | Profile model and scene suggestions only suggest; Apply is required and nothing is applied silently. | No silent default changes. | ES "Product locks" (Profile model); UH FAQ | kept |
| D-064 | 22 Sep | The product path is tip CSRT plus Contact layer; a default Partner CSRT for Everyday, frame-diff as tip finder, YOLO every frame as stroke writer and ByteTrack by default are rejected; MT-ID only if IDs die on goldens. | Proposals are not the stroke writer. | PR "Multi-track Fahrplan"; ES "Reject / defer" | kept |
| D-065 | 24 Sep | F-003 tier 2 (lag tie-breaking) is not implemented; the tier-1 AliasingRisk flag is enough. | Needs its own sign-off. | ES "Product locks"; AC F-003 notes | kept |
| D-066 | — | Reports separate Signal Quality (Quality Doctor) from Motion Fidelity, and whole-clip r is never the only quality number (use windowed r). | Whole-clip r was misleading (lag drift, periodicity aliasing). | TT "Diagnosis", "Explicit non-goals"; PR G2.4; AC F-003 | kept |
| D-067 | 21 Sep | Work is ordered G0 stable CSRT, G1 classical heuristics plus audio, G2 Neo 2 raw-value layer, G3 AI helpers, G4 licence/platform/site; a later phase starts only after the previous gate unless it is marked orthogonal. | The team spread too thin. | PR "Rule: one theme at a time" | kept as the working principle; superseded as a release plan by NP phases |
| D-068 | 15 Oct | A C++ or other fast path replaces a baseline only if it beats the measured baseline on the golden clips with no quality loss: 3 warm-up runs and 5 measured runs, median and p95, peak RAM (the L6 gate). | Speed claims need numbers. | R26 §1; MG "Rules"; MP §6; NP "Rules" | kept |
| D-069 | 15 Oct | The half-size camera estimate (39 % faster, r 0.94 to 0.71) was rejected. | It failed the quality gate. | GP "Experiment" | kept |
| D-070 | 15 Oct | Speed work comes in this order, each behind the gate (equal r, then time): camera estimate on a second thread, scene-cut check on a small picture, CSRT on a cropped region (quality measured first). | 99 % of time is OpenCV/FFmpeg; Go code is about 0 %. | GP "Next candidates"; NP P3 | kept |
| D-071 | 15 Oct | The CSRT/flow tracking loop and the frame pipeline are the first C++ candidates, after a profile on the Owner's clips. | Long clips are where the time goes. | R26 §1 | changed by D-103 (C++ first for everything where it fits); the profile on real clips is still open |
| D-072 | 13 Oct | Golden benchmark (manifest v2, report v2, no-regression gate) comes first for the Next features; Project Recovery comes before Smart Batch, which must not advertise resume before it passes. | Every later claim needs a gate. | MP §4 rank 1; CAP "P0 implementation order"; NR N2/N3 | kept (P3 gate) |
| D-073 | — | Golden clips and private videos stay out of the repo; only manifests are in git. | Licence and size. | GOLDEN_CLIPS.md header; MP §6 (L2); NS "Waiting on the Owner" | kept |
| D-074 | 15 Oct | Test levels L0 static, L1 deterministic fixtures, L2 golden clips, L3 local AI with a mocked server, L4 device mocks, L5 native Windows 11 at 1366x768 and 1920x1080, L6 performance bake-off. | One shared definition of "tested". | MP §6; R26 intro (L0–L7) | kept. CONFLICT: R26 intro says L0–L7, MP §6 lists L0–L6; no L7 is described. |
| D-075 | 14 Oct | The beat-bar reader (Cock Hero / Fap Hero) finds the strip by itself, writes one stroke per beat (down on the beat, up halfway, max 500 units/s, pauses over 2 s rise once and hold) and never overwrites a script. | PMV makers drive devices from music beats. | R26 §3b; CL Unreleased; UH §1b | kept (P3) |
| D-076 | 14 Oct | The 758-script FredTungsten collection is the standing corpus for script loading tests. | Found a crash (script starting at −922 337 203 565 470 ms). | R26 intro; CL 0.7.0 "Fixed" | kept |
| D-077 | 15 Oct | AutoDirector, Smart Batch, Quality Compare, Device Optimizer, Intelligent Repair are the P0 capabilities of "Next"; Scene Studio, Multi-Target, Personal Motion Profiles, Professional Export, Hardware Benchmark are P1; VR/180/360 is a P2 research track. | Priorities of the Owner-confirmed register. | CAP "Feature priorities" | kept as backlog (see ROADMAP.md part b) |
| D-078 | 13 Oct | Batch is orchestration over the same single-clip pipeline, never a second generator. | One quality path. | MP §5 | kept |
| D-079 | 21 Sep | Audio never invents the curve: it is a warning or a spacing hint only. | Loudness is not motion. | EG "Classical heuristics"; PR "Not in this train" | kept |
| D-080 | 15 Oct | Missing data is flagged, not faked; no empty buttons, no unproven claims; a mode that is not built says so. | Honesty. | MP §3 rule 7; CAP invariant 3; NP "Rules" | kept |

## 6. Editor

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-081 | 14–15 Oct | The editor becomes a full editor in ten steps E1–E10; E1–E5 (selection, clipboard/keys, transforms, patterns, history) are basic and belong in the main product, E6–E10 (compare, quality on timeline, device preview, export pack, scale) can follow. | Owner: "a real editor". | R26 §4 | kept (P4) |
| D-082 | 15 Oct | Undo is a command history with transactions (a drag is one step, a multi-axis edit is all or nothing) and survives reopening via a journal. | Safe editing. | R26 §4 E5 | kept |
| D-083 | 15 Oct | The editor must stay smooth for a 1-hour video with 100 000+ points at 1366x768 and 1920x1080. | Scale target. | R26 §4 E10; RW (level-of-detail drawing) | kept |
| D-084 | 14 Oct | The generator edits its new script in place ("Generator with editor"); the video and timeline playheads move together. | Owner request. | CL 0.7.0 "Changed" | kept |
| D-085 | 14 Oct | "Optimize for" has two choices, Any device (fill/heal gaps) and Sam Neo 2 (adds contact vibration, bakes vibration/suction); the generator offers it, off by default. | Make foreign scripts usable without inventing motion. | CL 0.7.0 "Changed"; UH §4 | kept |
| D-086 | 13 Oct | Companion axis files are editable, "+ Axis…" never overwrites an existing file, and other fields and the `inverted` flag are kept. | Do not destroy author data. | DS step D4; CL 0.6.0–0.7.0 | kept |
| D-087 | 14 Oct | When strokes are denser than readable, the stroke curve shows a calm "tempo wave" and real strokes return on zoom (Owner's choice C); Scene Map mode is the Player's scene view (Owner's choice A). | Readability. | CL 0.7.0 "Changed" | kept |
| D-088 | 15 Oct | Editor, library and tools additions are built as our own versions of what other players do (PeakSync Speed Guard to E7, projected output to E8, session recap, Vorze CSV import as small ideas). | Seen in other players; no code copy. | R26 §3b | kept as backlog |

## 7. UI and look

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-089 | 15 Oct | The product UI, docs, logs and screenshots are English; the Owner talks to agents in German. | One language per surface. | LG; AS "Who works how"; NS "Rules" | kept |
| D-090 | 13 Oct | The dark Flow look (cyan/orange accent), the top-level modes and the existing navigation conventions are kept; upgrade usability, not identity. | Approved look. | CAP "Existing Flow GUI"; FE "Complete UI concept" | kept in spirit; the Qt interface is new (D-100). CONFLICT: UH §10 and CL 0.6.0 describe a lilac accent with teal ("Owner preference over honey orange"), while CAP/FE/CL 0.7.0 describe cyan/orange Flow; both are called current. |
| D-091 | 14 Oct | The five modes are Player, Editor, Generator, Training and Scene Map, switched by one "Mode" pill (keys 1–5); Tools is its own button. | Replaces five tabs and the Play/Edit toggle. | CL 0.7.0 "Changed" (#533) | kept |
| D-092 | 13 Oct | At 1366x768 optional inspectors collapse, never the video, timeline or Stop; keyboard, contrast, focus and reduced-motion support are required; Windows-native screenshots gate UI acceptance. | Usability and accessibility. | CAP "Existing Flow GUI"; FE | kept |
| D-093 | 27 Sep | Shipped features must appear in the UI ("GUI load rule"), and every list and control shows real data. | No hidden features, no placeholders. | UH §10; CL 0.7.0 "Changed" | kept |
| D-094 | 19 Sep | Wails v3, a frameless window with custom title bar and card redesigns are skipped. | Chrome was settled. | PR "Not in this train"; docs/ROADMAP.md (19 Sep evaluation) | superseded (new Qt interface, D-100) |
| D-095 | 14 Oct | Video comes first: Open a video or a script (Ctrl+O, drag and drop); a video finds its script (same name beside it, `.samn` before `.funscript`, script folders, tolerant names, variants). | Owner request. | CL 0.7.0 "Changed" and Unreleased; UH §1b | kept (P1) |
| D-096 | 15 Oct | Screenshots use test patterns only; screenshots come from the real build. | No adult media in the repo. | AS "Never"; NS "Rules"; NR N6 | kept |
| D-097 | 15 Oct | The 148 JS test suites and the handbook are the feature checklist for the new interface: each names behaviour that needs its own test. | Nothing gets lost in the rewrite. | RW Decision 2; NP P4 | kept |

## 8. Languages and architecture

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-098 | Sep 2026 | Do not rewrite stacks for language fashion; change a language only for a measured win. | Avoid churn. | LG "What stays as-is" | superseded by D-099 |
| D-099 | 14–15 Oct | Language path: first Go for the core and Rust for AI/video hot paths (14 Oct); then C++ instead of Rust and more Python for AI (15 Oct); then Go replaced completely, step by step, by Python and C++ with parity tests (15 Oct, host pywebview); then a fast rewrite in a new repo (15 Oct). | Owner direction, in four steps on 14–15 Oct. | R26 intro and §1; MG header; RW header | changed by D-102. CONFLICT: MP §3 rule 5 ("Go is the core; Rust only for measured hot paths") and R26 §1 table ("Go, as now") are stale next to MG/RW/NP ("Go is replaced completely"). |
| D-100 | 15 Oct | The new interface is a native Qt interface (PySide6, LGPL) instead of the 26 000-line JS interface in a web view; video via QtMultimedia (FFmpeg backend), curve/heatmap/timeline drawn with custom painting and level of detail, tests with pytest-qt offscreen. | Owner choice: native UI, no browser codec limits. | RW Decision 2; NP "Languages" | changed by D-102 (Qt 6 with QML, C++ first) |
| D-101 | 15 Oct | The pywebview host for the old JS frontend (recommended in MG) is not used. | Replaced by the Qt interface. | MG "Open questions"; RW | superseded by D-100 |
| D-102 | 16 Oct | SamNPlayer 2 is C++ first with Qt 6 and QML; Python is used only where clearly better (AI training and experiments); the UI layer must stay exchangeable; performance is tuned, but quality comes first. | Owner direction as passed on in the lead's brief. | BRIEF (not yet in `docs/PLAN.md`) | kept. CONFLICT: NP "Languages" and NP header say "Qt (PySide6)" for the interface and Python for app logic, devices and generator control; NM lists modules as Python. NP, NM and the existing `samn/` code must be updated by the lead. |
| D-103 | 15 Oct | Rule "measure before C++": a path moves to C++ only when a measurement shows Python is too slow (3 warm-up and 5 measured runs, median and p95, no quality loss). | Avoid C++ with no gain. | NP "Rules"; MG "Rules"; R26 §1 | changed by D-102 (C++ first). The measuring gate itself (D-068) is kept for any speed claim and for choosing Python over C++ where Python is "clearly better". CONFLICT: NP "Rules" vs BRIEF. |
| D-104 | 15 Oct | A role for C# is open (Windows-only device bridge, ideas from MultiFunPlayer); it is a third runtime and needs an Owner decision before any C# code. | Avoid a third runtime without need. | NP "Languages"; NS "Waiting on the Owner" | open |
| D-105 | 15 Oct | File formats stay: `.funscript` is the universal export, `.samn` is extended backwards-compatibly only, settings keep the same keys; users can switch between 0.7 and SamNPlayer 2. | Trust and migration. | NP "Rules"; MP §3 rule 4; MG "Rules" | kept |
| D-106 | 15 Oct | The Go code and its tests (about 23 000 lines of tests) are the specification: a part is done when its ported tests pass and behaviour matches (parity); C++ ports carry parity tests against the Go cases. | Objective definition of done. | NP "Rules"; MG "Rules"; RW | kept. CONFLICT: R26 §1 cites 38 000 lines of tests and 60 000 Go lines "incl. tests", NP/MG/RW cite about 23 000 lines of tests; the numbers differ. |
| D-107 | 15 Oct | SamNPlayer 2 phases are P0 foundation, P1 Player, P2 Devices, P3 Generator, P4 Editor and the rest, P5 Release; the Player comes first so a usable app exists early. | Fastest usable order. | NP "Phases"; RW "Phases" | kept (P0 done) |
| D-108 | 15 Oct | Python can be the glue for AI (onnxruntime, PyTorch/YOLO); training scripts live in Python, product inference prefers ONNX. | The AI ecosystem lives there. | R26 §1; ES "Py train / Go infer" | kept (matches D-102 "Python only where clearly better") |
| D-109 | 13 Oct | The planned engine split is Core, Motion Engine, Intelligence, AI Assist and Device Engine; times are integer microsecond PTS; contracts (`TrackResult`, `JobState`, `EditCommand` …) are proposals until a PR with tests lands. | Clear module boundaries. | FE "Five modules"; MP §5 | kept as design input; its Go/Wails wording is superseded by D-102 |
| D-110 | 15 Oct | SamNPlayer 2 lives in the repo `SamNPlayer-Next-Level` (the working name `SamNPlayer-2` in RW was not used). | Owner decision on 15 Oct. | NP "Owner decisions"; RW Decision 1 | kept. CONFLICT: RW says working name `SamNPlayer-2`; NP says `SamNPlayer-Next-Level`; OD D12 (15 Oct) wanted to develop 2.0 Next in the old repo. NP is the latest. |
| D-111 | 15 Oct | Each module is a folder with README, API, dependencies, Go reference, status and its own tests; one module per PR; `ui` may call every module, no other module imports `ui`; only `samn/native.py` imports C++ modules; module tests must not need another module's internals. | Parallel agent work without collisions. | NM "Rules between modules" | kept (rewrite for C++ names when D-102 is applied) |
| D-112 | 15 Oct | Packaging for Windows and Linux bundles ffmpeg and OpenCV (PyInstaller/Nuitka planned), with an updater switch and release workflow at P5. | Portable, no system dependencies. | NP P5; RW P5 | open (packaging tool depends on D-102) |
| D-113 | 15 Oct | Native CI jobs run on Linux and Windows and must fail, not "SKIP" with exit 0, when a native module is missing (`SAMN_NATIVE_REQUIRED=1`). | A skipped test hid missing code. | AS "In progress" M1; NS "Never" | kept |

## 9. Process

| ID | Date | Decision | Why | Source | Status for SamNPlayer 2 |
|---|---|---|---|---|---|
| D-114 | 15 Oct | Two agents: Claude on `claude/*` (Python app, Qt interface, review of every PR), Grok on `grok/*` (C++ ports in `native/`, each as a PR with parity tests); never push to another agent's branch. ChatGPT and Cursor access was removed. | Stop branch collisions seen on 15 Oct. | NP "Two agents"; NS "Rules"; AS "Who works how" | kept (roles to be re-cut when D-102 makes the app C++) |
| D-115 | 15 Oct | `main` changes only through a pull request with green CI; merge only on green CI. | Quality gate. | NP "Two agents"; NS "Rules"; AS | kept. CONFLICT: AC (27 Sep) "Claude cannot merge, Cursor squash-merges" vs AC header (10 Oct) and AS (15 Oct) "Claude squash-merges its own PRs once CI is green"; the later rule wins. NP/NS do not say who merges in SamNPlayer 2. |
| D-116 | 15 Oct | Never force-push. | Foreign pushes were overwritten. | NS "Rules"; AS "Who works how" | kept. CONFLICT: AC "Rebase hygiene" (22 Sep) lets the tip owner rebase/force-push their own branch; NS/AS (15 Oct) say never force-push. The later rule wins. |
| D-117 | 15 Oct | Before every push run `git diff --stat` and check it; never write a file from a shell substitution that may be empty; never regenerate a workflow from scratch. | Files ended up holding the literal text `$(cat /tmp/...)`. | NS "Rules"; AS "Who works how" | kept |
| D-118 | 15 Oct | The Owner pushes release tags; agents only prepare releases. | The tag is the release switch. | NS "Rules"; AS; PR "Owner pre-release checklist" | kept |
| D-119 | 15 Oct | No adult media in the repo or in screenshots; screenshots use test patterns only. | Repo hygiene. | NS "Never"; AS "Never" | kept |
| D-120 | 15 Oct | No model names in commits, PRs or files. | Owner rule. | NS "Never"; AS "Never" | kept. CONFLICT: the commit/PR attribution lines that Claude Code's harness asks for in this session contain a model name; the lead should decide before the next commit. Not a repo doc. |
| D-121 | 15 Oct | AGENT_STATE is read first in every new session and updated in every PR, finished or not; push often, the repo is the current state. | Context loss between sessions. | NS header; AS header | kept |
| D-122 | 22 Sep | No silent tracker or profile default changes; one theme per agent; one improvement is measured before the next big theme. | Prevent drift and sprawl. | AC "Rules" 1–5, "North star" | kept |
| D-123 | 15 Oct | Tests that skip with exit 0 in CI are forbidden (see D-113); placeholders that pretend to work are forbidden. | Honest CI and UI. | NS "Never" | kept |
| D-124 | 15 Oct | Bug fixes are ported between code lines deliberately and with tests; there is no automatic merging between the old app and the new one. | Avoid regressions in both. | R26 §2 | kept |
| D-125 | 12 Oct | Nothing is claimed as built unless it is built (✅ marks); comparative claims against FunGen need a fair run on identical clips. | Claims must match the build. | R26 intro; NR N6 | kept |
| D-126 | 15 Oct | Owner-only tasks: tag releases, hardware tests with the Sam Neo 2, delete old branches, golden clips for measurement, public download link. | Agents cannot do these. | AS "Waiting on the Owner"; NS "Waiting on the Owner" | kept |

---

## Open questions (no decision in the sources)

1. Edition split in SamNPlayer 2: is there still a Suite (internal) build, and what is "Next Level" (D-004, D-007, D-009)?
2. Repo visibility and release source for SamNPlayer 2 (D-011, D-012).
3. Updater and packaging tool after D-102 (D-112).
4. C# role (D-104), macOS (D-018), Neo 2 speed-cap default (D-046), rhythm-grid default (D-061).
5. BLE library for a C++-first app (D-023).

## Resolutions of the conflicts (16 Oct 2026, Owner's latest decisions)

| Conflict | Resolution |
|---|---|
| D-099, D-102, D-103 (language path) | **C++ first** (Qt 6, QML interface; core library in plain C++17). Python only where it is clearly better (AI training, experiments). "Measure before C++" now applies the other way round: a part goes to Python only when it is clearly better there. Own code over dependencies when it gives an advantage ("our engine"). |
| D-090 (look) | The 0.7 **Flow look** (cyan/orange, glass cards, light ribbons, mode deck), animated as in 0.7. The lilac accent of 0.6 is history. |
| D-110 (repo) | SamNPlayer 2 lives in `SamNPlayer-Next-Level`. |
| D-115 (who merges) | Claude squash-merges its own PRs when CI is green; Grok's work comes as PRs from `grok/*`, reviewed by Claude. |
| D-116 (force-push) | Never force-push (the later rule wins). |
| D-120 (names in commits) | No model identifiers in code, docs or commit messages. The co-author trailer that the Claude Code platform adds to commits stays; it is the tool's attribution, not product content. |
| D-074, D-106 (numbers) | Facts, not decisions: test levels are L0–L7 per `ROADMAP_2026-10.md`; the old Go test code is about 23 000 lines (Go only) and 38 000 lines including frontend and Python tests. |

**Answered by the Owner on 16 Oct:**

| ID | Decision |
|---|---|
| D-127 (resolves D-008) | **One version with everything.** No Suite or edition split in SamNPlayer 2: every feature is for everyone. |
| D-128 (resolves D-009, D-011) | **SamNPlayer 2 is the one free SamNPlayer and replaces 0.7.** The source stays private and the binaries are published. Everything 0.7 has, plus everything still open from the old roadmap, ends up here. |
| D-129 (resolves D-057) | **AI may write curve drafts.** They are opt-in, and the classical verifier checks them, as with 0.7's AI script writer. |
| D-130 | **Every device is supported as well as possible**: a direct connection where we can build one (Sam Neo 2 over BLE, TCode serial and BLE for OSR2/SR6, and the others in the old `DEVICE_SUPPORT.md`), and the **Buttplug interface** (Intiface, JSON over WebSocket, implemented in our C++ core) for all the rest. Each device gets the curves that are correct for it, per axis, and multi-axis scripts are mixed onto it as in 0.7. Device data (profiles, axes, ranges, protocols) is built together with the player, not afterwards. |
| D-131 | **C#: none for now.** The Owner listed detection, Player, Editor and the core GUI; all of these are C++ (QML for the interface) per D-102, and Buttplug needs no C# client. C# comes in only if a concrete part is clearly better in it. To be confirmed with the Owner. |
| D-133 | **OpenCV is the vision library of the core** (`core`, `imgproc`, `videoio`, `video`, `tracking` from contrib, `dnn`), from P3 on. Linux: system packages. Windows: vcpkg with a binary cache in CI. 0.7 already used the same library through cgo. |
| D-134 | **Detection runs in C++** (tracking, backends, post-processing, quality, contact, scene map, audio). The Python generator scripts are ported with their tests and then retired. AI **inference** runs in C++ through OpenCV `dnn` on ONNX models; AI **training** stays Python (`tools/ai/`), using the core through `samn_native`. |
| D-135 | **Better detection is measured, never assumed:** every improvement over 0.7 (hybrid check (D-139), AI re-acquisition, sub-frame peaks, Kalman smoothing) stays behind a switch until the benchmark on the Owner's golden clips shows it beats 0.7. |
| D-136 | **One Editor, shared:** the Generator's Review step opens the result in the same Editor as the Player's Edit view (0.7 `gen_editor.js` borrowed the Player's tools); P3.3 wires it, P4 completes it. |
| D-132 | **Lanes show what the script carries, named in a column left of them** (Owner, 16 Oct; as 0.7 v1). No "device preview" lanes. When a device is connected (P2), the lanes follow that device's axes, so only the curves that go to the selected device are shown. |
| D-137 | **The Generator is the Editor: one mode** (Owner, 16 Oct: "Generator und Editor soll ein und dasselbe sein"). The mode deck has Player, Generator, Training and Scene Map. A script is created and edited on the same page, with the same timeline, lanes and edit tools; opening a script for editing goes there with editing on. Extends D-136; 0.7 had two modes that borrowed the same parts. |
| D-138 | **Empty Player = one round Play button with "Open…" under it** (Owner, 15 Oct, old repo #550). No wordmark, kicker, hint text or stage art there; the Flow stage art stays for "Feel without film". Supersedes the 0.7 empty state ported in #18. |
| D-139 | **One box writes the stroke; better detection means a stronger single box, not fusion** (17 Oct, from `LESSONS.md` GD-01, REJ-05, REJ-19). The P3 plan item "run the backends together with a confidence-weighted fusion" is replaced: flow and the LK grid run beside CSRT as a disagreement check, and the ONNX detector only proposes a verified re-anchor (fail closed). The comparison controller (P3.8) is built right after P3.2, before any default is chosen. Retests of 0.7 results whose limit came from the stack (REJ-11, -13, -16, -18) are allowed through the gate. Reason: 0.7 measured fusion as always between its sources, never above the best; the first P3 plan had repeated a rejected idea. |
