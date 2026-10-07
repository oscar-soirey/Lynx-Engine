/* Public page for a content collection (devlog, tutorials). Config: window.LYNX_COLLECTION */
(function () {
  var C = window.LYNX_COLLECTION, app = document.getElementById('dev-app'), hero = document.getElementById('dev-hero');
  var esc = function (s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]; }); };
  var posts = [], tag = '';
  var link = function (slug) { return C.page + '?' + C.param + '=' + encodeURIComponent(slug); };
  function fmt(d) { var x = new Date(d + 'T00:00:00'); return isNaN(x) ? esc(d) : x.toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric' }); }
  function safeSrc(u) { return /^(https?:\/\/|[\w@.\-\/%]+$)/.test(u || '') ? u : ''; }
  function badges(p) { return (p.level ? '<span class="plug-tag lvl">' + esc(p.level) + '</span>' : '') + (p.tags || []).map(function (t) { return '<span class="plug-tag">' + esc(t) + '</span>'; }).join(''); }
  function failure(msg) { app.innerHTML = '<p class="plug-msg">' + esc(msg) + '</p>'; }

  function list() {
    hero.hidden = false; document.title = C.docTitle;
    var all = Array.from(new Set([].concat.apply([], posts.map(function (p) { return p.tags || []; })))).sort();
    var shown = posts.filter(function (p) { return !tag || (p.tags || []).indexOf(tag) >= 0; });
    var chips = all.length ? '<div class="dev-tools"><button class="chip' + (tag ? '' : ' on') + '" data-tag="">All</button>' + all.map(function (t) { return '<button class="chip' + (t === tag ? ' on' : '') + '" data-tag="' + esc(t) + '">' + esc(t) + '</button>'; }).join('') + '</div>' : '';
    app.innerHTML = chips + (shown.length ? '<div class="dev-grid">' + shown.map(function (p) {
      var c = safeSrc(p.cover);
      return '<a class="card dev-card" href="' + link(p.slug) + '">' + (c ? '<img class="dev-cover" src="' + esc(c) + '" alt="" loading="lazy">' : '') +
        '<div class="dev-body"><span class="dev-date">' + fmt(p.date) + '</span><h3>' + esc(p.title) + '</h3><p>' + esc(p.summary || '') + '</p><div class="dev-tags">' + badges(p) + '</div></div></a>';
    }).join('') + '</div>' : '<p class="plug-msg">Nothing here yet.</p>');
    app.querySelectorAll('.chip').forEach(function (b) { b.addEventListener('click', function () { tag = b.getAttribute('data-tag'); list(); }); });
  }

  function post(slug) {
    var idx = posts.findIndex(function (p) { return p.slug === slug; }), p = posts[idx];
    hero.hidden = true;
    if (!p) { failure('This page does not exist.'); app.insertAdjacentHTML('beforeend', '<p class="plug-msg"><a class="button" href="' + C.page + '">Back to ' + esc(C.title.toLowerCase()) + '</a></p>'); return; }
    document.title = p.title + ' — Lynx ' + C.title;
    fetch(C.dir + '/' + encodeURIComponent(p.slug) + '.md', { cache: 'no-cache' })
      .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.text(); })
      .then(function (md) {
        var older = posts[idx + 1], newer = posts[idx - 1], words = md.split(/\s+/).filter(Boolean).length;
        app.innerHTML = '<article class="card dev-post"><a class="dev-back" href="' + C.page + '">&larr; All ' + esc(C.plural) + '</a><span class="dev-date">' + fmt(p.date) + ' &middot; ' + Math.max(1, Math.round(words / 200)) + ' min read</span><h1>' + esc(p.title) + '</h1><div class="dev-tags">' + badges(p) + '</div><div class="md" id="md-out">' + LynxMD.render(md) + '</div></article>' +
          '<nav class="dev-pn" aria-label="' + esc(C.title) + '">' + (newer ? '<a class="button" href="' + link(newer.slug) + '">&larr; ' + esc(newer.title) + '</a>' : '<span></span>') + (older ? '<a class="button" href="' + link(older.slug) + '">' + esc(older.title) + ' &rarr;</a>' : '') + '</nav>';
        if (window.LynxHL) LynxHL.apply(document.getElementById('md-out'));
        window.scrollTo(0, 0);
      })
      .catch(function (e) { failure('Could not load this page (' + e.message + ').'); });
  }

  fetch(C.dir + '/index.json', { cache: 'no-cache' })
    .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
    .then(function (d) {
      if (!Array.isArray(d)) throw new Error('Invalid index');
      posts = d.filter(function (p) { return p && /^[\w-]+$/.test(p.slug || '') && p.title; }).sort(function (a, b) { return String(b.date).localeCompare(String(a.date)); });
      var s = new URLSearchParams(location.search).get(C.param);
      if (s) post(s); else list();
    })
    .catch(function (e) { failure('Could not load ' + C.title.toLowerCase() + ' (' + e.message + '). If you opened this page as a local file, serve the folder with a local web server.'); });
})();
