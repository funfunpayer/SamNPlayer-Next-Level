#!/usr/bin/env python3
"""Builds app/qml/HelpTexts.js from 0.7's data-help attributes, verbatim
(PARITY §12: "text must be ported verbatim"). Each text gets a key: the
element's id (or the id its label is for) where it has one, else
<file>.<label>. Run with the old repo's frontend folder:
  python3 ci/help_texts.py ../SamNPlayer/cmd/gui-wails/frontend > app/qml/HelpTexts.js
With --check it compares instead of writing (exit 1 on a difference).
Texts for controls 0.7 did not have live in SAMN2 below, so a rerun keeps them."""
import html
import json
import os
import re
import sys


# SamN2's own controls (no 0.7 data-help): key, text, where, label.
SAMN2 = [
    ('st-reduced-motion',
     'Stops decorative motion: silk bands, breathing glow on the mode tab, Play ring spin and Training stage '
     'drift. Transitions snap. Useful for screenshots, accessibility, or a calmer window.',
     'SamN2 D-092, Settings → General', 'Reduce motion'),
    ('library.local-files',
     'Opens a video or script from disk in the Player. The file also lands in Recent.',
     'SamN2, Player → Library', 'Local Files'),
    ('library.recent',
     'Newest scripts you opened (up to 8). Click to load. Star (★) keeps it in Favorites. Missing files stay listed, struck through.',
     'SamN2, Player → Library', 'Recent'),
    ('library.favorites',
     'Scripts you starred. Click the star on a Recent or list row to add or remove. Up to 50.',
     'SamN2, Player → Library', 'Favorites'),
    ('library.scripts',
     'Imported = opened from disk. Generated = Create results. Edited = scripts you saved after a curve edit. Newest first.',
     'SamN2, Player → Library', 'Scripts'),
    ('library.analysis',
     'Audio Analysis opens the waveform tab under the curve. BPM Detection opens Inspector → Audio (measure the beat / BPM grid). Display only — neither rewrites the stroke.',
     'SamN2, Player → Library', 'Analysis'),
    ('training.mock',
     'On (default): Start uses a simulated device so you can try a session without hardware. Off: Training drives a device you already connected in Devices (or the top-bar Connect). Same idea as Play → Mock.',
     'SamN2, Training → Session Settings', 'Mock'),
    ('training.tier',
     'Gentle / Standard / Intense scales a named program (×0.8 / ×1 / ×1.2). Auto-adjust can nudge from recent sessions of this profile. Training feel only.',
     'SamN2, Training → Intensity', 'Intensity'),
    ('training.session-mode',
     'Stop-Start returns fully to 0 between cycles. Plateau keeps a high floor (edging). Custom only — named programs bring their own shape.',
     'SamN2, Training → Session Mode', 'Session Mode'),
    ('training.stage',
     'Rings: live vibration/suction gauges. Figure: abstract Flow loop of the technique. Video: a film plays along; the device follows the training, not the film.',
     'SamN2, Training → stage switch', 'Rings / Figure / Video'),
    ('training.feedback',
     'Arousal 1–10 while a session runs. Stay at 5–7; 8: Interrupt; 9–10 interrupt the cycle and soften the next. Target is 7.',
     'SamN2, Training → Feedback', 'Feedback'),
]


def slug(s):
    return re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')[:40] or 'help'


def extract(root):
    seen, out = {}, []
    for dirpath, _, files in os.walk(root):
        if 'node_modules' in dirpath or 'wailsjs' in dirpath:
            continue
        for f in sorted(files):
            if not f.endswith(('.js', '.html')):
                continue
            p = os.path.join(dirpath, f)
            src = open(p, encoding='utf-8').read()
            for m in re.finditer(r'data-help="([^"]*)"', src):
                text = html.unescape(m.group(1))
                if text in seen:
                    continue
                start = src.rfind('<', 0, m.start())
                end = src.find('>', m.end())
                tag = src[start:end + 1]
                ident = re.search(r'\bid="([^"$]+)"', tag) or re.search(r'\bfor="([^"$]+)"', tag)
                label = re.sub(r'\s+', ' ', src[end + 1:end + 200].split('<')[0]).strip()
                if not ident and not label:
                    # A wrapper (label.checkbox-row): the control inside it names it.
                    inner = re.match(r'\s*<(input|select|button)\b[^>]*\bid="([^"$]+)"[^>]*>([^<]*)', src[end + 1:end + 400])
                    if inner:
                        ident = re.match(r'(.*)', inner.group(2))
                        label = re.sub(r'\s+', ' ', inner.group(3)).strip()
                key = ident.group(1) if ident else os.path.splitext(f)[0] + '.' + slug(label)
                while key in (o['key'] for o in out):
                    key += '-2'
                rel = os.path.relpath(p, root).replace(os.sep, '/')
                line = src.count('\n', 0, m.start()) + 1
                seen[text] = key
                out.append({'key': key, 'text': text, 'source': f'{rel}:{line}', 'label': label})
    for key, text, source, label in SAMN2:
        out.append({'key': key, 'text': text, 'source': source, 'label': label})
    return out


def render(entries):
    lines = [
        '.pragma library',
        '// 0.7\'s "?" help texts, verbatim (data-help in cmd/gui-wails/frontend),',
        '// keyed by the 0.7 element id. Generated by ci/help_texts.py; edit there',
        '// or keep a text verbatim. HelpChip { key: "pb-offset-bake" } shows one.',
        '',
        'const texts = {',
    ]
    for e in sorted(entries, key=lambda e: e['key']):
        lines.append(f'    // {e["source"]}' + (f' ({e["label"][:60]})' if e['label'] else ''))
        lines.append(f'    {json.dumps(e["key"])}: {json.dumps(e["text"], ensure_ascii=False)},')
    lines += ['};', '', 'function text(key) { return texts[key] || ""; }', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--check']
    out = render(extract(args[0]))
    if '--check' in sys.argv:
        here = open(os.path.join(os.path.dirname(__file__), '..', 'app', 'qml', 'HelpTexts.js'), encoding='utf-8').read()
        sys.exit(0 if here == out else 1)
    sys.stdout.write(out)
