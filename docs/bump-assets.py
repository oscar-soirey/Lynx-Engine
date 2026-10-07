#!/usr/bin/env python3
"""Build step for the site. Run `python bump-assets.py` from the site folder after
adding or editing a devlog / tutorial, and before every commit.

1. Builds <dir>/data.js for every content folder that has an index.json (posts/, tutorials/).
   It lets the pages work when opened straight from disk (file://), where browsers block fetch().
   Online, the pages still read the live index.json / .md files first.
2. Appends ?v=<hash of the file> to every local CSS/JS reference in the HTML pages,
   so browsers always fetch the new file after a change (GitHub Pages caching).
"""
import glob, hashlib, json, os, re

for index in glob.glob('*/index.json'):
    folder = os.path.dirname(index)
    with open(index, encoding='utf-8') as f:
        entries = json.load(f)
    md = {}
    for e in entries:
        path = os.path.join(folder, e['slug'] + '.md')
        if os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                md[e['slug']] = f.read()
        else:
            print('warning: missing', path)
    payload = json.dumps({'index': entries, 'md': md}, ensure_ascii=True)
    with open(os.path.join(folder, 'data.js'), 'w', encoding='utf-8') as f:
        f.write('window.LYNX_DATA = window.LYNX_DATA || {};\nwindow.LYNX_DATA[' + json.dumps(folder) + '] = ' + payload + ';\n')
    print('built', folder + '/data.js', '(%d entries)' % len(entries))

ASSETS = ['styles.css', 'site.js', 'md.js', 'collection.js', 'collection-admin.js', 'plugins.js'] + sorted(glob.glob('*/data.js'))

def digest(name):
    with open(name, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()[:8]

versions = {}
for name in ASSETS:
    try:
        versions[name] = digest(name)
    except FileNotFoundError:
        pass

changed = 0
for page in glob.glob('*.html'):
    with open(page, encoding='utf-8') as f:
        src = f.read()
    out = src
    for name, v in versions.items():
        out = re.sub(r'((?:href|src)=")' + re.escape(name) + r'(?:\?v=\w+)?(")', r'\g<1>' + name + '?v=' + v + r'\g<2>', out)
    if out != src:
        with open(page, 'w', encoding='utf-8') as f:
            f.write(out)
        changed += 1
print('versions:', versions)
print('pages updated:', changed)
