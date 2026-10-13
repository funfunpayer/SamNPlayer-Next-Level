"""C4 graphics: all vector assets are well-formed and contain no script/network loads."""
from pathlib import Path
from xml.etree import ElementTree as ET

ASSETS = Path(__file__).resolve().parent.parent / 'src' / 'assets' / 'flow'
FILES = (
    'ambient-grid.svg',
    'empty-script.svg',
    'empty-video.svg',
    'training-idle.svg',
    'scene-placeholder.svg',
)


def test_flow_assets():
    for name in FILES:
        source = (ASSETS / name).read_text(encoding='utf-8')
        root = ET.fromstring(source)
        assert root.tag == '{http://www.w3.org/2000/svg}svg', name
        assert root.get('viewBox'), name
        assert root.get('aria-hidden') == 'true', name
        assert '<script' not in source.lower(), name
        assert 'http://' not in source.replace('http://www.w3.org/2000/svg', ''), name
        assert 'https://' not in source, name
        assert '<foreignObject' not in source, name
        print('  OK', name)


if __name__ == '__main__':
    test_flow_assets()
