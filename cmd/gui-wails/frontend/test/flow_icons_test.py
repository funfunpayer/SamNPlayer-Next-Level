"""C3 Flow icon system smoke: trusted SVG markup and accessible DOM contract."""
from playwright.sync_api import sync_playwright
from _harness import serve


def test_flow_icons():
    url, shutdown = serve("export const unused = async () => ({});")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page()
            page.goto(url + "/")
            data = page.evaluate("""async () => {
              const { ICONS, ICON_NAMES, icon } = await import('/src/flow_icons.js');
              const names = ['player','training','tools','play','pause','stop',
                'library','video','snapshot','scenemap','curve','device',
                'favorite','settings','undo','redo','effects','lock','shield'];
              const button = document.createElement('button');
              button.setAttribute('aria-label', 'Play');
              button.append(icon('play', {size:32}));
              document.body.append(button);
              let rejected = false;
              try { icon('<img src=x onerror=alert(1)>'); }
              catch (e) { rejected = e instanceof RangeError; }
              return {
                missing: names.filter(name => !ICONS[name]),
                count: ICON_NAMES.length,
                consistent: Object.values(ICONS).every(markup =>
                  markup.includes('viewBox="0 0 24 24"')
                  && markup.includes('stroke-width="1.6"')
                  && markup.includes('stroke="currentColor"')
                  && markup.includes('aria-hidden="true"')),
                buttonLabel: button.getAttribute('aria-label'),
                iconDecorative: button.querySelector('svg').getAttribute('aria-hidden'),
                iconWidth: button.querySelector('svg').getAttribute('width'),
                rejected
              };
            }""")
            assert not data['missing'], data
            assert data['count'] >= 60, data
            assert data['consistent'], data
            assert data['buttonLabel'] == 'Play', data
            assert data['iconDecorative'] == 'true', data
            assert data['iconWidth'] == '32', data
            assert data['rejected'], data
            browser.close()
    finally:
        shutdown()


if __name__ == '__main__':
    test_flow_icons()
    print('Flow icons C3 browser smoke: OK')
