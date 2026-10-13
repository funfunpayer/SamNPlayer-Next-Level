"""C2 training rings browser smoke: actual values only, adaptive channels, cleanup."""
from playwright.sync_api import sync_playwright
from _harness import serve


def test_training_rings():
    url, shutdown = serve("export const unused = async () => ({});")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(reduced_motion="reduce")
            page.goto(url + "/")
            page.evaluate("""async () => {
              const { createRings } = await import('/src/training_rings.js');
              const host = document.createElement('div');
              document.body.append(host);
              window.__trainingRings = createRings(host, [
                'vibration', 'suction', {id:'twist',label:'Twist Axis'}
              ]);
            }""")
            assert page.locator(".flow-tr__channel").count() == 3
            assert page.locator(".flow-tr__value").all_text_contents() == ['—', '—', '—']
            assert page.locator(".flow-tr--animate").count() == 0
            page.evaluate("""() => {
              window.__trainingRings.update({vibration:0.625,suction:0,twist:1.7});
              window.__trainingRings.setPhase('Plateau');
            }""")
            assert page.locator(".flow-tr__value").all_text_contents() == ['63%', '0%', '100%']
            assert page.locator(".flow-tr__channel[data-active='true']").count() == 2
            assert page.locator(".flow-tr__phase").inner_text() == 'Plateau'
            assert page.locator(".flow-tr__channel[data-axis='vibration'] .flow-tr__spark-line").get_attribute('points') == ''
            page.evaluate("window.__trainingRings.update({vibration:0.25})")
            assert page.locator(".flow-tr__channel[data-axis='vibration'] .flow-tr__spark-line").get_attribute('points')
            page.evaluate("window.__trainingRings.update({vibration:null})")
            assert page.locator(".flow-tr__value").all_text_contents() == ['—', '0%', '100%']
            page.evaluate("window.__trainingRings.setPhase('Paused')")
            assert page.locator(".flow-tr--paused").count() == 1
            page.evaluate("window.__trainingRings.destroy(); window.__trainingRings.destroy()")
            assert page.locator(".flow-tr").count() == 0
            # Ensure no raw HTML from externally-provided labels can execute.
            page.evaluate("""async () => {
              const { createRings } = await import('/src/training_rings.js');
              const host = document.createElement('div');
              document.body.append(host);
              window.__trainingRings2 = createRings(host, [
                {id:'custom',label:'<img src=x onerror=alert(1)>'}
              ]);
            }""")
            assert page.locator(".flow-tr__name img").count() == 0
            assert page.locator(".flow-tr__name").inner_text() == '<img src=x onerror=alert(1)>'
            page.evaluate("window.__trainingRings2.destroy()")
            browser.close()
    finally:
        shutdown()


if __name__ == '__main__':
    test_training_rings()
    print('Training rings C2 browser smoke: OK')
