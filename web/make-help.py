#!/usr/bin/env python3
"""Writes the in-page game guide (dist/help.html) for the web build.

The game content comes from the desktop key guides in
~/Desktop/Games/Roguelikes/Docs (build-docs.py + guides.py), so both guides
stay in sync; only the saving and "playing in the browser" parts are
written here, because they differ on the web."""
import html, importlib.util, os, sys

DOCS = os.path.expanduser('~/Desktop/Games/Roguelikes/Docs')
PAGE = 'ia.html'

sys.path.insert(0, DOCS)
spec = importlib.util.spec_from_file_location('build_docs', os.path.join(DOCS, 'build-docs.py'))
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)
from guides import GUIDES   # noqa: E402
import guides as guides_mod   # noqa: E402

game = next(g for g in docs.GAMES if g['file'] == PAGE)
guide = dict(GUIDES.get(PAGE, {}))
info = dict(game['info'])
kbd = docs.kbd
esc = html.escape

SAVING = guides_mod.SAVING[PAGE]

WEB = '''<ul>
<li>The map is the game's own screen (tiles, or text after <em>Tiles</em> in the top bar); status, messages, inventory and what you see are separate windows. <em>A−</em> / <em>A+</em> on a title bar (shown on hover) change that window's size of text or tiles; <em>Windows ▾</em> shows, hides and rearranges them (drag a title bar or the gap between windows). Menus and pop-ups appear as text boxes over the map.</li>
<li><strong>Keys:</strong> the numeric keypad or the arrow keys move you (Shift+arrow for diagonals); keypad <kbd>5</kbd> waits. Bump the stairs to get <em>Descend</em> or <em>Save and quit</em>.</li>
<li><strong>Audio ▾:</strong> the game's own sound effects and its main-menu music (<em>Madness</em> by Musica Cthulhiana, used with permission), both off by default; the choice is remembered. The music is downloaded only when you switch it on.</li>
<li>Browsers keep a few shortcuts for themselves (<kbd>Ctrl+W</kbd>, <kbd>Ctrl+T</kbd>, <kbd>Ctrl+N</kbd>, and <kbd>Cmd</kbd> shortcuts on a Mac), so those never reach the game.</li>
<li>If the game ever crashes, a message appears at the top; reload the page to continue from the last save.</li>
</ul>'''

KEY_HINTS = [
    ('?', 'The game\'s manual'),
    ('X', 'Auto-explore: walk to unexplored places (any key stops)'),
    ('Enter', 'Menu of all commands'),
    ('i', 'Inventory with a cursor: Enter = everything you can do with the item'),
    ('>', 'Go down (walks to the nearest known stairs; press again there)'),
    ('Esc', 'Game menu, or cancel'),
]


def dl(items):
    return '<dl>' + ''.join(f'<dt>{kbd(k)}</dt><dd>{esc(d)}</dd>' for k, d in items) + '</dl>'


def section(anchor, title, body):
    return f'<h2 id="h-{anchor}">{esc(title)}</h2>{body}'


parts = []
toc = [('about', 'About the game'), ('keys', 'Keyboard controls'), ('saving', 'Saving your game'),
       ('tips', 'Tips'), ('guide', "New player's guide"), ('web', 'Playing in the browser')]
parts.append('<p>' + esc(game['tagline']) + '</p>' + info['About the game'] + '<ul class="toc">' +
             ''.join(f'<li><a href="#h-{a}">{esc(t)}</a></li>' for a, t in toc) + '</ul>')


parts.append(section('about', 'About the game',
                     guide.pop('How Infra Arcana differs from Angband')))

ess = ''.join(f'<div class="box"><h3>{esc(cat)}</h3>{dl(items)}</div>' for cat, items in game['essentials'])
all_keys = game['all']() if callable(game['all']) else game['all']
full = ''.join(f'<div>{kbd(k)}<span>{esc(d)}</span></div>' for k, d in all_keys)
parts.append(section('keys', 'Keyboard controls',
                     '<div class="box key"><h3>The keys to remember</h3>' + dl(KEY_HINTS) + '</div>'
                     '<h3>Essential keys</h3><div class="grid">' + ess + '</div>'
                     '<details><summary>Complete key list (' + str(len(all_keys)) + ' commands)</summary>'
                     '<div class="all">' + full + '</div></details>'))

parts.append(section('saving', 'Saving your game', SAVING))
parts.append(section('tips', 'Tips', info.get('Tips') or guide.pop('Tips', '')))
parts.append(section('guide', "New player's guide",
                     ''.join(f'<h3>{esc(t)}</h3>{b}' for t, b in guide.items())))
parts.append(section('web', 'Playing in the browser', WEB))

# RVIP: About this version
parts.append('<h2 id="h-version">About this version</h2><ul>'
             '<li>Based on <strong>Infra Arcana 23.0.0</strong> by Martin Törnqvist (gitlab.com/martin-tornqvist/ia), AGPL-3.0-or-later; tiles by Oryx; sound effects from freesound.org under their own licences (see the game\'s LICENSE-AUDIO.txt); built with Emscripten.</li>'
             '<li>Our changes (auto-explore, stairs walking, command menu, inventory cursor and item menus, web build) '
             'are on GitHub: <a href="https://github.com/memmaker/ia">memmaker/ia</a>.</li></ul>')
out = sys.argv[1] if len(sys.argv) > 1 else None
text = '\n'.join(parts)
if out:
    open(out, 'w').write(text)
else:
    print(text)
