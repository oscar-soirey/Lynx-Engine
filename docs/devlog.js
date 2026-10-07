(function () {
  var app = document.getElementById('dev-app'), hero = document.getElementById('dev-hero');
  var esc = function (s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]; }); };
  var posts = [], tag = '';
  function fmt(d) { var x = new Date(d + 'T00:00:00'); return isNaN(x) ? esc(d) : x.toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric' }); }
  function safeSrc(u) { return /^(https?:\/\/|[\w@.\-\/%]+$)/.test(u || '') ? u : ''; }
  function tags(p) { return (p.tags || []).map(function (t) { return '<span class="plug-tag">' + esc(t) + '</span>'; }).join(''); }
  function failure(msg) { app.innerHTML = '<p class="plug-msg">' + esc(msg) + '</p>'; }

  function list() {
    hero.hidden = false; document.title = 'Lynx Engine — Devlog';
    var all = Array.from(new Set([].concat.apply([], posts.map(function (p) { return p.tags || []; })))).sort();
    var shown = posts.filter(function (p) { return !tag || (p.tags || []).indexOf(tag) >= 0; });
    var chips = all.length ? '<div class="dev-tools"><button class="chip' + (tag ? '' : ' on') + '" data-tag="">All</button>' + all.map(function (t) { return '<button class="chip' + (t === tag ? ' on' : '') + '" data-tag="' + esc(t) + '">' + esc(t) + '</button>'; }).join('') + '</div>' : '';
    app.innerHTML = chips + (shown.length ? '<div class="dev-grid">' + shown.map(function (p) {
      var c = safeSrc(p.cover);
      return '<a class="card dev-card" href="devlog.html?post=' + encodeURIComponent(p.slug) + '">' + (c ? '<img class="dev-cover" src="' + esc(c) + '" alt="" loading="lazy">' : '') +
        '<div class="dev-body"><span class="dev-date">' + fmt(p.date) + '</span><h3>' + esc(p.title) + '</h3><p>' + esc(p.summary || '') + '</p><div class="dev-tags">' + tags(p) + '</div></div></a>';
    }).join('') + '</div>' : '<p class="plug-msg">No post yet.</p>');
    app.querySelectorAll('.chip').forEach(function (b) { b.addEventListener('click', function () { tag = b.getAttribute('data-tag'); list(); }); });
  }

  function post(slug) {
    var idx = posts.findIndex(function (p) { return p.slug === slug; }), p = posts[idx];
    hero.hidden = true;
    if (!p) { failure('This post does not exist.'); app.insertAdjacentHTML('beforeend', '<p class="plug-msg"><a class="button" href="devlog.html">Back to devlog</a></p>'); return; }
    document.title = p.title + ' — Lynx Devlog';
    fetch('posts/' + encodeURIComponent(p.slug) + '.md', { cache: 'no-cache' })
      .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.text(); })
      .then(function (md) {
        var older = posts[idx + 1], newer = posts[idx - 1], words = md.split(/\s+/).filter(Boolean).length;
        app.innerHTML = '<article class="card dev-post"><a class="dev-back" href="devlog.html">&larr; All posts</a><span class="dev-date">' + fmt(p.date) + ' &middot; ' + Math.max(1, Math.round(words / 200)) + ' min read</span><h1>' + esc(p.title) + '</h1><div class="dev-tags">' + tags(p) + '</div><div class="md" id="md-out">' + LynxMD.render(md) + '</div></article>' +
          '<nav class="dev-pn" aria-label="Posts">' + (newer ? '<a class="button" href="devlog.html?post=' + encodeURIComponent(newer.slug) + '">&larr; ' + esc(newer.title) + '</a>' : '<span></span>') + (older ? '<a class="button" href="devlog.html?post=' + encodeURIComponent(older.slug) + '">' + esc(older.title) + ' &rarr;</a>' : '') + '</nav>';
        if (window.LynxHL) LynxHL.apply(document.getElementById('md-out'));
        window.scrollTo(0, 0);
      })
      .catch(function (e) { failure('Could not load this post (' + e.message + ').'); });
  }

  fetch('posts/index.json', { cache: 'no-cache' })
    .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
    .then(function (d) {
      if (!Array.isArray(d)) throw new Error('Invalid index');
      posts = d.filter(function (p) { return p && /^[\w-]+$/.test(p.slug || '') && p.title; }).sort(function (a, b) { return String(b.date).localeCompare(String(a.date)); });
      var s = new URLSearchParams(location.search).get('post');
      if (s) post(s); else list();
    })
    .catch(function (e) { failure('Could not load the devlog (' + e.message + '). If you opened this page as a local file, serve the folder with a local web server.'); });
})();
