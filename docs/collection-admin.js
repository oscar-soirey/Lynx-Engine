(function () {
  var C = window.LYNX_COLLECTION;
  var $ = function (id) { return document.getElementById(id); };
  var S = { index: [], loaded: false, editing: null, images: {}, slugTouched: false, dirty: false };
  var DRAFT = 'lynx-draft-' + C.dir;

  /* ---------- helpers ---------- */
  function slugify(t) { return String(t).normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, ''); }
  function today() { var d = new Date(); return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0'); }
  function resolver() { return { img: function (p) { return S.images[p] ? S.images[p].url : p; } }; }
  function plainSummary(md) {
    var lines = md.split('\n');
    for (var i = 0; i < lines.length; i++) {
      var l = lines[i].trim();
      if (!l || /^(#|```|!\[|>|[-*]\s|\d+\.\s|https?:)/.test(l)) continue;
      l = l.replace(/\[([^\]]+)\]\([^)]*\)/g, '$1').replace(/[*`~]/g, '');
      return l.length > 160 ? l.slice(0, 157).replace(/\s+\S*$/, '') + '...' : l;
    }
    return '';
  }
  function fields() {
    var tags = Array.from(new Set($('a-tags').value.split(',').map(function (t) { return t.trim(); }).filter(Boolean)));
    var body = $('a-body').value;
    var lv = $('a-level') ? $('a-level').value : '';
    var e = { slug: $('a-slug').value.trim(), title: $('a-title').value.trim(), date: $('a-date').value, summary: $('a-sum').value.trim() || plainSummary(body), tags: tags };
    if ($('a-cover').value) e.cover = $('a-cover').value;
    if (lv) e.level = lv;
    if (!e.summary) delete e.summary;
    if (!tags.length) delete e.tags;
    return { entry: e, body: body };
  }
  function validate(f) {
    var e = f.entry;
    if (!e.title) return 'Title is required.';
    if (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(e.slug)) return 'Slug must be lowercase letters, digits and hyphens (e.g. my-first-post).';
    if (!/^\d{4}-\d{2}-\d{2}$/.test(e.date)) return 'Date is required.';
    if (!f.body.trim()) return 'The post is empty.';
    return '';
  }
  function mergedIndex(entry) {
    var arr = S.index.filter(function (p) { return p.slug !== entry.slug; });
    arr.push(entry);
    arr.sort(function (a, b) { return String(b.date).localeCompare(String(a.date)); });
    return arr;
  }
  function used(f) { return Object.keys(S.images).filter(function (p) { return f.body.indexOf(p) >= 0 || f.entry.cover === p; }); }

  /* ---------- render ---------- */
  var timer;
  function changed() { S.dirty = true; clearTimeout(timer); timer = setTimeout(refresh, 120); }
  function refresh() {
    var f = fields(), exists = S.index.some(function (p) { return p.slug === f.entry.slug; });
    $('preview').innerHTML = (f.entry.title ? '<h2 class="md-title">' + LynxMD.esc(f.entry.title) + '</h2>' : '') + LynxMD.render(f.body, resolver());
    if (window.LynxHL) LynxHL.apply($('preview'));
    var err = validate(f);
    $('a-err').textContent = (f.body || f.entry.title) ? err : '';
    $('a-hint').innerHTML = (exists ? '<b>This slug already exists: the post will be replaced.</b> ' : '') +
      (S.loaded ? 'The package contains the post, its new images and the updated <code>' + C.dir + '/index.json</code>. Unzip it at the root of the site, then commit and push.'
                : '<b>' + C.dir + '/index.json is not loaded</b>, so the package will not contain it. After unzipping, paste the copied index entry into <code>' + C.dir + '/index.json</code> yourself (or load the file above and download again).');
    renderImages(f);
    try { localStorage.setItem(DRAFT, JSON.stringify({ t: $('a-title').value, s: $('a-slug').value, d: $('a-date').value, g: $('a-tags').value, m: $('a-sum').value, b: $('a-body').value, l: $('a-level') ? $('a-level').value : '' })); } catch (x) {}
  }
  function renderImages(f) {
    var paths = Object.keys(S.images), sel = $('a-cover'), keep = sel.getAttribute('data-keep') || '', cur = sel.value || keep;
    sel.innerHTML = '<option value="">None</option>' + (keep && !S.images[keep] ? '<option value="' + LynxMD.esc(keep) + '">' + LynxMD.esc(keep.split('/').pop()) + ' (existing)</option>' : '') +
      paths.map(function (p) { return '<option value="' + LynxMD.esc(p) + '">' + LynxMD.esc(S.images[p].name) + '</option>'; }).join('');
    sel.value = cur && (S.images[cur] || cur === keep) ? cur : '';
    $('imgs').innerHTML = paths.map(function (p) {
      var im = S.images[p], kb = Math.round(im.size / 1024), big = im.size > 1.5 * 1024 * 1024;
      return '<div class="adm-img"><img src="' + im.url + '" alt=""><div><b>' + LynxMD.esc(im.name) + '</b><span class="' + (big ? 'plug-err' : '') + '">' + (kb > 1024 ? (kb / 1024).toFixed(1) + ' MB' : kb + ' KB') + (big ? ' - heavy, consider compressing' : '') + '</span>' +
        '<span><button type="button" class="chip" data-ins="' + LynxMD.esc(p) + '">Insert</button> <button type="button" class="chip" data-rm="' + LynxMD.esc(p) + '">Remove</button></span></div></div>';
    }).join('');
  }
  $('imgs').addEventListener('click', function (ev) {
    var b = ev.target.closest('button'); if (!b) return;
    if (b.dataset.ins) insertText(imgMd(b.dataset.ins));
    if (b.dataset.rm) { URL.revokeObjectURL(S.images[b.dataset.rm].url); delete S.images[b.dataset.rm]; var cv = $('a-cover'); if (cv.value === b.dataset.rm) cv.value = ''; if (cv.getAttribute('data-keep') === b.dataset.rm) cv.setAttribute('data-keep', ''); changed(); }
  });

  /* ---------- editor actions ---------- */
  var T = $('a-body');
  function insertText(txt) { var s = T.selectionStart, v = T.value; T.value = v.slice(0, s) + txt + v.slice(T.selectionEnd); T.focus(); T.setSelectionRange(s + txt.length, s + txt.length); changed(); }
  function wrap(a, b, ph) { var s = T.selectionStart, e = T.selectionEnd, v = T.value, sel = v.slice(s, e) || ph; T.value = v.slice(0, s) + a + sel + b + v.slice(e); T.focus(); T.setSelectionRange(s + a.length, s + a.length + sel.length); changed(); }
  function prefix(p) {
    var v = T.value, s = v.lastIndexOf('\n', T.selectionStart - 1) + 1, e = T.selectionEnd, seg = v.slice(s, e) || 'text';
    var out = seg.split('\n').map(function (l) { return p + l; }).join('\n');
    T.value = v.slice(0, s) + out + v.slice(e || s); T.focus(); T.setSelectionRange(s, s + out.length); changed();
  }
  function imgMd(p) { var n = S.images[p] ? S.images[p].name.replace(/\.\w+$/, '').replace(/[-_]+/g, ' ') : 'image'; return '\n![' + n + '](' + p + ')\n\n'; }
  function addImages(files) {
    var last;
    [].forEach.call(files, function (f) {
      if (!/^image\//.test(f.type)) return;
      var clean = (f.name || 'image').toLowerCase().replace(/[^a-z0-9.]+/g, '-').replace(/^-+|-+$/g, '') || 'image';
      if (!/\.\w+$/.test(clean)) clean += '.' + (f.type.split('/')[1] || 'png').replace('jpeg', 'jpg').replace('svg+xml', 'svg');
      var path = C.dir + '/img/' + Math.random().toString(36).slice(2, 7) + '-' + clean;
      S.images[path] = { path: path, name: f.name || clean, blob: f, url: URL.createObjectURL(f), size: f.size };
      insertText(imgMd(path)); last = path;
    });
    return last;
  }
  var ACT = {
    h2: function () { prefix('## '); }, h3: function () { prefix('### '); }, bold: function () { wrap('**', '**', 'bold'); }, italic: function () { wrap('*', '*', 'italic'); },
    code: function () { wrap('`', '`', 'code'); }, codeblock: function () { wrap('\n```cpp\n', '\n```\n', 'code here'); },
    link: function () { var u = prompt('Link URL', 'https://'); if (u) wrap('[', '](' + u + ')', 'link text'); },
    list: function () { prefix('- '); }, quote: function () { prefix('> '); },
    image: function () { $('a-files').click(); },
    video: function () { var u = prompt('YouTube link'); if (u) insertText('\n' + u.trim() + '\n\n'); }
  };
  document.querySelector('.adm-tb').addEventListener('click', function (ev) { var b = ev.target.closest('button[data-act]'); if (b) ACT[b.dataset.act](); });
  $('a-files').addEventListener('change', function () { addImages(this.files); this.value = ''; });
  T.addEventListener('paste', function (ev) { var fs = ev.clipboardData && ev.clipboardData.files; if (fs && fs.length) { ev.preventDefault(); addImages(fs); } });
  T.addEventListener('dragover', function (ev) { ev.preventDefault(); });
  T.addEventListener('drop', function (ev) { var fs = ev.dataTransfer && ev.dataTransfer.files; if (fs && fs.length) { ev.preventDefault(); addImages(fs); } });
  T.addEventListener('keydown', function (ev) {
    if ((ev.ctrlKey || ev.metaKey) && ev.key === 'b') { ev.preventDefault(); ACT.bold(); }
    if ((ev.ctrlKey || ev.metaKey) && ev.key === 'i') { ev.preventDefault(); ACT.italic(); }
  });
  ['a-title', 'a-slug', 'a-date', 'a-tags', 'a-sum', 'a-body'].forEach(function (id) { $(id).addEventListener('input', changed); });
  if ($('a-level')) $('a-level').addEventListener('change', changed);
  $('a-title').addEventListener('input', function () { if (!S.slugTouched && !S.editing) $('a-slug').value = slugify(this.value); });
  $('a-slug').addEventListener('input', function () { S.slugTouched = true; });
  $('a-cover').addEventListener('change', function () { S.coverTouched = true; this.setAttribute('data-keep', this.value); changed(); });

  /* ---------- zip ---------- */
  function crc32(u8) {
    if (!crc32.t) { crc32.t = []; for (var n = 0; n < 256; n++) { var c = n; for (var k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; crc32.t[n] = c >>> 0; } }
    var c2 = -1; for (var i = 0; i < u8.length; i++) c2 = crc32.t[(c2 ^ u8[i]) & 255] ^ (c2 >>> 8); return (c2 ^ -1) >>> 0;
  }
  function zip(files) {
    var enc = new TextEncoder(), parts = [], central = [], off = 0, d = new Date(), csize = 0;
    var time = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1), date = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    files.forEach(function (f) {
      var name = enc.encode(f.name), crc = crc32(f.data), sz = f.data.length;
      var h = new DataView(new ArrayBuffer(30));
      h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(10, time, true); h.setUint16(12, date, true);
      h.setUint32(14, crc, true); h.setUint32(18, sz, true); h.setUint32(22, sz, true); h.setUint16(26, name.length, true);
      parts.push(h.buffer, name, f.data);
      var c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true); c.setUint16(12, time, true); c.setUint16(14, date, true);
      c.setUint32(16, crc, true); c.setUint32(20, sz, true); c.setUint32(24, sz, true); c.setUint16(28, name.length, true); c.setUint32(42, off, true);
      central.push(c.buffer, name); csize += 46 + name.length; off += 30 + name.length + sz;
    });
    var e = new DataView(new ArrayBuffer(22));
    e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true); e.setUint32(12, csize, true); e.setUint32(16, off, true);
    return new Blob(parts.concat(central, [e.buffer]), { type: 'application/zip' });
  }
  function save(blob, name) { var a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click(); a.remove(); setTimeout(function () { URL.revokeObjectURL(a.href); }, 2000); }
  function ready() { var f = fields(), err = validate(f); $('a-err').textContent = err; return err ? null : f; }
  var utf8 = function (s) { return new TextEncoder().encode(s); };

  $('dl-zip').addEventListener('click', function () {
    var f = ready(); if (!f) return;
    var imgs = used(f);
    Promise.all(imgs.map(function (p) { return S.images[p].blob.arrayBuffer(); })).then(function (bufs) {
      var files = [{ name: C.dir + '/' + f.entry.slug + '.md', data: utf8(f.body.replace(/\s+$/, '') + '\n') }];
      imgs.forEach(function (p, i) { files.push({ name: p, data: new Uint8Array(bufs[i]) }); });
      if (S.loaded) files.push({ name: C.dir + '/index.json', data: utf8(JSON.stringify(mergedIndex(f.entry), null, 2) + '\n') });
      save(zip(files), C.zip + '-' + f.entry.slug + '.zip'); S.dirty = false;
    });
  });
  $('dl-md').addEventListener('click', function () { var f = ready(); if (f) save(new Blob([f.body.replace(/\s+$/, '') + '\n'], { type: 'text/markdown' }), f.entry.slug + '.md'); });
  $('cp-entry').addEventListener('click', function () {
    var f = ready(); if (!f) return; var txt = JSON.stringify(f.entry, null, 2), b = this;
    var ok = function () { var o = b.textContent; b.textContent = 'Copied!'; setTimeout(function () { b.textContent = o; }, 1500); };
    if (navigator.clipboard) navigator.clipboard.writeText(txt).then(ok, function () { prompt('Copy this entry', txt); }); else prompt('Copy this entry', txt);
  });

  /* ---------- existing posts ---------- */
  function setIndex(arr) {
    S.index = arr.filter(function (p) { return p && p.slug; }).sort(function (a, b) { return String(b.date).localeCompare(String(a.date)); }); S.loaded = true;
    $('idx-status').textContent = S.index.length + ' post(s) loaded';
    $('post-list').innerHTML = S.index.map(function (p) { return '<div class="adm-li"><span>' + LynxMD.esc(p.date) + '</span><b>' + LynxMD.esc(p.title) + '</b><button type="button" class="chip" data-edit="' + LynxMD.esc(p.slug) + '">Edit</button></div>'; }).join('') || '<p class="plug-hint">No post yet.</p>';
    refresh();
  }
  $('idx-file').addEventListener('change', function () {
    var f = this.files[0]; if (!f) return;
    f.text().then(function (t) { var d = JSON.parse(t); if (!Array.isArray(d)) throw 0; setIndex(d); }).catch(function () { $('idx-status').textContent = 'Invalid index.json'; });
    this.value = '';
  });
  fetch(C.dir + '/index.json', { cache: 'no-cache' }).then(function (r) { if (!r.ok) throw 0; return r.json(); }).then(setIndex).catch(function () { $('idx-status').textContent = 'Not loaded (open this page through a web server, or load the file)'; });

  function fill(e, body) {
    S.editing = e.slug; S.slugTouched = true; S.images = {}; S.coverTouched = false;
    $('a-title').value = e.title || ''; $('a-slug').value = e.slug; $('a-date').value = e.date || today(); $('a-tags').value = (e.tags || []).join(', ');
    $('a-sum').value = e.summary || ''; if ($('a-level')) $('a-level').value = e.level || ''; $('a-cover').setAttribute('data-keep', e.cover || ''); $('a-body').value = body; S.dirty = false; refresh(); window.scrollTo({ top: document.querySelector('.adm').offsetTop - 70, behavior: 'smooth' });
  }
  $('post-list').addEventListener('click', function (ev) {
    var b = ev.target.closest('button[data-edit]'); if (!b) return;
    var e = S.index.filter(function (p) { return p.slug === b.dataset.edit; })[0];
    if (S.dirty && !confirm('Discard the current changes?')) return;
    fetch(C.dir + '/' + e.slug + '.md', { cache: 'no-cache' }).then(function (r) { if (!r.ok) throw 0; return r.text(); }).then(function (t) { fill(e, t); })
      .catch(function () {
        var inp = document.createElement('input'); inp.type = 'file'; inp.accept = '.md,text/markdown,text/plain';
        inp.onchange = function () { if (inp.files[0]) inp.files[0].text().then(function (t) { fill(e, t); }); };
        alert('Could not fetch ' + C.dir + '/' + e.slug + '.md. Select the file from your computer.'); inp.click();
      });
  });
  function reset() {
    Object.keys(S.images).forEach(function (p) { URL.revokeObjectURL(S.images[p].url); });
    S.images = {}; S.editing = null; S.slugTouched = false; S.coverTouched = false;
    ['a-title', 'a-slug', 'a-tags', 'a-sum', 'a-body'].forEach(function (id) { $(id).value = ''; }); if ($('a-level')) $('a-level').value = '';
    $('a-date').value = today(); $('a-cover').setAttribute('data-keep', ''); S.dirty = false;
    try { localStorage.removeItem(DRAFT); } catch (x) {} refresh();
  }
  $('new-post').addEventListener('click', function () { if (!S.dirty || confirm('Discard the current changes?')) reset(); });
  window.addEventListener('beforeunload', function (ev) { if (S.dirty) { ev.preventDefault(); ev.returnValue = ''; } });

  /* ---------- init (draft) ---------- */
  $('a-date').value = today();
  try {
    var d = JSON.parse(localStorage.getItem(DRAFT) || 'null');
    if (d && (d.b || d.t)) {
      $('a-title').value = d.t || ''; $('a-slug').value = d.s || ''; $('a-date').value = d.d || today(); $('a-tags').value = d.g || ''; $('a-sum').value = d.m || ''; $('a-body').value = d.b || ''; if ($('a-level')) $('a-level').value = d.l || '';
      S.slugTouched = !!d.s;
    }
  } catch (x) {}
  refresh();
  if (d && new RegExp(C.dir + '/img/').test(d.b || '')) $('a-err').textContent = 'Draft restored: images must be added again.';
})();
