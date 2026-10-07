(function () {
  var REGISTRY = 'https://raw.githubusercontent.com/oscar-soirey/Lynx-Engine/main/registry/plugins.json';
  var MAIN_REPO = 'https://github.com/oscar-soirey/Lynx-Engine';
  var $ = function (id) { return document.getElementById(id); };
  var plugins = [];

  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]; }); }
  function normRepo(r) {
    r = String(r || '').trim().replace(/^https?:\/\/github\.com\//i, '').replace(/\.git$/i, '').replace(/\/+$/, '');
    return /^[\w.-]+\/[\w.-]+$/.test(r) ? r : null;
  }

  /* ---------- browse ---------- */
  function render() {
    var q = $('q').value.trim().toLowerCase(), c = $('cat').value;
    var shown = plugins.filter(function (p) {
      return (!c || p.category === c) && (!q || (p.name + ' ' + p.description + ' ' + p.author + ' ' + p.category).toLowerCase().indexOf(q) >= 0);
    });
    $('count').textContent = shown.length + ' / ' + plugins.length;
    if (!shown.length) { $('list').innerHTML = '<p class="plug-msg">No plugin found.</p>'; return; }
    $('list').innerHTML = shown.map(function (p) {
      var repo = normRepo(p.repo);
      var url = repo ? 'https://github.com/' + repo : '';
      return '<article class="card plug-card"><div class="plug-top"><h3>' + esc(p.name) + '</h3>' + (p.category ? '<span class="plug-tag">' + esc(p.category) + '</span>' : '') + '</div>' +
        '<p>' + esc(p.description) + '</p>' +
        '<div class="plug-meta"><span>by ' + esc(p.author || 'Unknown') + '</span>' + (repo ? '<span>' + esc(repo) + '</span>' : '') + '</div>' +
        (url ? '<div class="actions"><a class="button" href="' + esc(url) + '" target="_blank" rel="noopener">Repository</a><a class="button" href="' + esc(url) + '/releases" target="_blank" rel="noopener">Releases</a></div>' : '') + '</article>';
    }).join('');
  }
  function fillCats() {
    var cats = Array.from(new Set(plugins.map(function (p) { return p.category; }).filter(Boolean))).sort();
    $('cat').innerHTML = '<option value="">All categories</option>' + cats.map(function (c) { return '<option>' + esc(c) + '</option>'; }).join('');
    $('cats').innerHTML = cats.map(function (c) { return '<option value="' + esc(c) + '">'; }).join('');
  }
  function load() {
    $('list').innerHTML = '<p class="plug-msg">Loading registry...</p>';
    fetch(REGISTRY, { cache: 'no-cache' })
      .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .then(function (data) {
        if (!Array.isArray(data)) throw new Error('Invalid registry format');
        plugins = data.filter(function (p) { return p && p.name; });
        fillCats(); render();
      })
      .catch(function (e) {
        $('count').textContent = '';
        $('list').innerHTML = '<p class="plug-msg">Could not load the registry (' + esc(e.message) + '). <a href="' + MAIN_REPO + '/blob/main/registry/plugins.json" target="_blank" rel="noopener"><u>Open it on GitHub</u></a> or <a href="#" id="retry"><u>retry</u></a>.</p>';
        var r = $('retry'); if (r) r.addEventListener('click', function (ev) { ev.preventDefault(); load(); });
      });
  }
  $('q').addEventListener('input', render);
  $('cat').addEventListener('change', render);

  /* ---------- publish ---------- */
  function entry() {
    return {
      name: $('f-name').value.trim(),
      repo: normRepo($('f-repo').value) || $('f-repo').value.trim(),
      description: $('f-desc').value.trim(),
      category: $('f-cat').value.trim(),
      author: $('f-author').value.trim()
    };
  }
  function validate(e) {
    if (!e.name) return 'Name is required.';
    if (!normRepo(e.repo)) return 'Repository must be owner/name or a GitHub URL.';
    if (!e.description) return 'Description is required.';
    if (!e.category) return 'Category is required.';
    if (!e.author) return 'Author is required.';
    if (plugins.some(function (p) { return p.name && p.name.toLowerCase() === e.name.toLowerCase(); })) return 'A plugin named "' + e.name + '" is already in the registry.';
    return '';
  }
  function update() {
    var e = entry(), msg = validate(e), touched = ['f-name','f-repo','f-desc','f-cat','f-author'].some(function (i) { return $(i).value; });
    $('err').textContent = touched ? msg : '';
    $('json').textContent = JSON.stringify(e, null, 2);
    var ok = !msg;
    ['send-issue', 'send-pr', 'copy'].forEach(function (id) { $(id).classList.toggle('disabled', !ok); $(id).setAttribute('aria-disabled', String(!ok)); });
    var json = JSON.stringify(e, null, 2);
    var body = 'Please add this plugin to `registry/plugins.json`:\n\n```json\n' + json + '\n```\n\nRepository: https://github.com/' + e.repo + '\n\n- [ ] `name` matches the `name` in the plugin\'s `plugin.json`\n- [ ] Releases are tagged `v<plugin version>-<Lynx version>`\n- [ ] Each release has a `.zip` asset with `plugin.json` at the root or in a subfolder\n';
    $('send-issue').href = MAIN_REPO + '/issues/new?title=' + encodeURIComponent('Plugin request: ' + e.name) + '&body=' + encodeURIComponent(body);
    $('send-pr').href = MAIN_REPO + '/edit/main/registry/plugins.json';
    return ok;
  }
  document.querySelectorAll('#form input').forEach(function (i) { i.addEventListener('input', update); });
  $('form').addEventListener('submit', function (e) { e.preventDefault(); });
  ['send-issue', 'send-pr'].forEach(function (id) {
    $(id).addEventListener('click', function (ev) { if (!update()) { ev.preventDefault(); return; } if (id === 'send-pr') copy(); });
  });
  function copy() {
    var t = JSON.stringify(entry(), null, 2), b = $('copy'), old = 'Copy entry';
    var done = function () { b.textContent = 'Copied!'; setTimeout(function () { b.textContent = old; }, 1500); };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(t).then(done, function () {});
    else { var ta = document.createElement('textarea'); ta.value = t; document.body.appendChild(ta); ta.select(); try { document.execCommand('copy'); done(); } catch (x) {} ta.remove(); }
  }
  $('copy').addEventListener('click', function () { if (update()) copy(); });

  update(); load();
})();
