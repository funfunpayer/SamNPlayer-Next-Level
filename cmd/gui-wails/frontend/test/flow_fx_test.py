"""C1 Flow idle stage: small browser contract smoke test (no Wails bindings)."""
import pathlib
import sys

from playwright.sync_api import sync_playwright
from _harness import serve

FRONTEND = pathlib.Path(__file__).resolve().parent.parent


def test_flow_fx():
    url, shutdown = serve("export const unused = async () => ({});")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(reduced_motion="reduce")
            page.goto(url + "/")
            page.evaluate("""async () => {
              const m = await import('/src/flow_fx.js');
              const a = document.createElement('div'), b = document.createElement('div');
              document.body.append(a, b);
              window.__fxA = m.mountFlowStage(a);
              window.__fxB = m.mountFlowStage(b, {state:'paused'});
            }""")
            assert page.locator(".flow-fx").count() == 2
            assert page.locator(".flow-fx[data-state='paused']").count() == 1
            assert page.locator(".flow-fx--animate").count() == 0
            ids = page.locator(".flow-fx defs [id]").evaluate_all("(nodes) => nodes.map(n => n.id)")
            assert len(ids) == len(set(ids)), "SVG resource IDs must be distinct across mounts"
            page.evaluate("window.__fxA.setState('playing')")
            assert page.locator(".flow-fx[data-state='playing']").count() == 1
            page.evaluate("window.__fxA.destroy(); window.__fxA.destroy()")
            assert page.locator(".flow-fx").count() == 1
            page.evaluate("window.__fxB.destroy()")
            assert page.locator(".flow-fx").count() == 0
            browser.close()
    finally:
        shutdown()


if __name__ == "__main__":
    test_flow_fx()
    print("Flow FX C1 browser smoke: OK")
