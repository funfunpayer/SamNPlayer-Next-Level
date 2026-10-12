"""MT-Seed: ranked motion candidates → Tip+Partner seed (suggest ≠ auto-commit).

Acceptance (PRODUCTION_ROADMAP Multi-track MT-Seed):
1. Show candidates does not write Zone 2 until user acts.
2. Click candidate → Zone 1 only (Zone 2 stays empty).
3. Suggest Tip+2nd proposes without applying; Dismiss clears; Apply seeds both.

Run: python3 cmd/gui-wails/frontend/test/generator_mt_seed_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")

CANDIDATES = [
    {"x": 40, "y": 40, "w": 80, "h": 80, "score": 0.9, "index": 1, "class": "glans"},
    {"x": 400, "y": 200, "w": 90, "h": 70, "score": 0.7, "index": 2, "class": "mouth"},
    {"x": 200, "y": 120, "w": 60, "h": 60, "score": 0.5, "index": 3},
]


def main():
    check = Checker()
    cands_js = json.dumps(CANDIDATES)
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "LoadFirstFrame": (
            f"async () => ({{ width: 640, height: 360, "
            f"pngBase64: '{TINY_PNG_B64}' }})"
        ),
        "LoadFrameAt": (
            f"async () => ({{ width: 640, height: 360, "
            f"pngBase64: '{TINY_PNG_B64}' }})"
        ),
        "SuggestPipeline": (
            "async () => ({ Backend: 'csrt', Profile: 'standard', "
            "Reason: 'mt-seed', GoPath: true })"
        ),
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => true",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
        "AutoDetectROI": (
            "async (path, engine) => { "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:autoroi', {x:10,y:10,w:30,h:30,engine: engine || 'auto',"
            "videoPath: path, seq: 1}), 0); }"
        ),
        "SuggestROICandidates": (
            "async (path) => { "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            f"'generate:roi-candidates', {{ candidates: {cands_js} }}), 0); }}"
        ),
    }))

    harness = FRONTEND / "test" / "_generator_mt_seed_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_mt_seed_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-candidates').disabled === false",
            timeout=5000)

        check("Suggest Tip+2nd hidden before candidates",
              page.locator("#gen-seed-suggest").is_hidden())

        page.click("#gen-candidates")
        page.wait_for_function(
            "() => document.querySelector('#gen-seed-suggest')"
            ".disabled === false",
            timeout=5000)

        seed = page.eval_on_selector(
            "#gen-seed-suggest",
            "e => ({ hidden: e.hidden, disabled: e.disabled })")
        check("Candidates listed without writing Zone 2",
              "No contact" in page.locator("#gen-roi2-label").inner_text())
        check("Suggest Tip+2nd enabled with ≥2 candidates",
              (not seed["hidden"]) and (not seed["disabled"]),
              str(seed))

        page.locator("#roi-canvas").scroll_into_view_if_needed()
        box = page.locator("#roi-canvas").bounding_box()
        nw, nh = 640.0, 360.0
        # Locator position clicks stay on-canvas even when Tip-Find copy
        # pushes the preview down in the default Playwright viewport.
        tip_pos = {"x": 80 * (box["width"] / nw), "y": 80 * (box["height"] / nh)}
        page.locator("#roi-canvas").click(position=tip_pos)
        page.wait_for_function(
            "() => document.querySelector('#gen-roi-label').textContent"
            ".includes('candidate #1')",
            timeout=5000)
        check("Click sets Zone 1 from candidate #1",
              "candidate #1" in page.locator("#gen-roi-label").inner_text())
        check("Click does not auto-fill Zone 2",
              "No contact" in page.locator("#gen-roi2-label").inner_text())

        # Shift-click Partner via locator modifiers (reliable vs mouse+keyboard).
        p2_pos = {
            "x": 445 * (box["width"] / nw),
            "y": 235 * (box["height"] / nh),
        }
        page.locator("#roi-canvas").click(position=p2_pos, modifiers=["Shift"])
        page.wait_for_function(
            "() => document.querySelector('#gen-roi2-label').textContent"
            ".includes('candidate #2')",
            timeout=5000)
        check("Shift-click sets Zone 2 from candidate #2",
              "candidate #2" in page.locator("#gen-roi2-label").inner_text())
        check("Shift-click applies body-part class from candidate",
              page.eval_on_selector("#gen-region-class2", "e => e.value") == "mouth")
        check("Zone 2 label shows Mouth class",
              "Mouth" in page.locator("#gen-roi2-label").inner_text())

        # Clear class to verify Apply nudge path still seeds boxes.
        page.select_option("#gen-region-class2", "")
        page.click("#gen-seed-suggest")
        page.wait_for_function(
            "() => document.querySelector('#gen-seed-status')"
            ".textContent.includes('Suggest Tip')",
            timeout=5000)
        seed_status = page.locator("#gen-seed-status").inner_text()
        check("Suggest shows Tip+2nd without auto-commit",
              "Suggest Tip" in seed_status and "Apply" in seed_status)
        page.locator("#gen-seed-status button").filter(has_text="Dismiss").click()
        page.wait_for_function(
            "() => document.querySelector('#gen-seed-status').textContent === ''",
            timeout=5000)
        check("Dismiss clears pending suggestion",
              page.locator("#gen-seed-status").inner_text() == "")

        page.click("#gen-seed-suggest")
        page.wait_for_selector("#gen-seed-status button")
        page.locator("#gen-seed-status button").filter(has_text="Apply").click()
        page.wait_for_function(
            "() => document.querySelector('#gen-roi-label').textContent"
            ".includes('Tip candidate #1') && "
            "document.querySelector('#gen-roi2-label').textContent"
            ".includes('2nd candidate #2')",
            timeout=5000)
        check("Apply seeds Tip #1 + 2nd #2",
              "Tip candidate #1" in page.locator("#gen-roi-label").inner_text()
              and "2nd candidate #2" in page.locator("#gen-roi2-label").inner_text())
        check("Apply restores mouth class from 2nd candidate",
              page.eval_on_selector("#gen-region-class2", "e => e.value") == "mouth")
        check("Apply sets tip class from candidate when present",
              page.eval_on_selector("#gen-region-class", "e => e.value") == "glans")
        check("Generate ready after Tip+2nd Apply",
              page.locator("#gen-generate").is_enabled())

        # Contact-vibe partner dropdown: mouth | nipple/breast only (no tip classes).
        z2_opts = page.eval_on_selector(
            "#gen-region-class2",
            "e => [...e.options].map(o => o.value).filter(Boolean)")
        check("Zone 2 class list is contact-vibe partners only",
              z2_opts == ["mouth", "nipples", "breasts"],
              str(z2_opts))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
