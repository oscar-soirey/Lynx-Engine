(function () {
  var still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  // pipeline : allume les étapes l'une après l'autre
  var steps = document.querySelectorAll('.flow>div');
  if (steps.length && !still) {
    var i = 0;
    setInterval(function () {
      steps.forEach(function (s) { s.classList.remove('lit'); });
      steps[i++ % steps.length].classList.add('lit');
    }, 900);
  }
  // scène du hero : un clic fait sauter le personnage
  var scene = document.getElementById('scene');
  if (scene) {
    var hero = scene.querySelector('.hero-sprite');
    scene.addEventListener('click', function () {
      hero.classList.remove('jump'); void hero.offsetWidth; hero.classList.add('jump');
    });
  }
})();

/* coloration syntaxique JS / C++ */
(function () {
  var KW = new Set('function const let var if else for while do return new delete this true false null undefined struct class override void float int bool char double auto static virtual public private protected template typename using namespace nullptr switch case break continue try catch throw of in typeof import export default async await constexpr unsigned const_cast final enum'.split(' '));
  var RE = /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|(#[a-z]+[^\n]*)|("(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|`(?:\\.|[^`\\])*`)|(\b(?:0x[\da-f]+|\d+\.?\d*f?)\b)|([A-Za-z_$][\w$]*)|([{}()\[\];,.<>=+\-*\/!&|:?%^~]+)/gi;
  var esc = function (s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); };
  var span = function (c, s) { return '<span class="t-' + c + '">' + esc(s) + '</span>'; };
  function highlight(src) {
    var out = '', last = 0, prev = '', m;
    RE.lastIndex = 0;
    while ((m = RE.exec(src))) {
      out += esc(src.slice(last, m.index)); last = RE.lastIndex;
      var t = m[0];
      if (m[1]) out += span('com', t);
      else if (m[2]) out += span('mac', t);
      else if (m[3]) out += span('str', t);
      else if (m[4]) out += span('num', t);
      else if (m[5]) {
        var rest = src.slice(last), next = (rest.match(/^\s*(\S{1,2})/) || [])[1] || '', cls = '';
        if (KW.has(t)) cls = 'kw';
        else if (/(\.|->)$/.test(prev)) cls = next[0] === '(' ? 'fn' : 'prop';
        else if (next === '::' || /::$/.test(prev)) cls = next[0] === '(' ? 'fn' : 'type';
        else if (next[0] === '(') cls = 'fn';
        else if (/^[A-Z][A-Z0-9_]+$/.test(t)) cls = 'mac';
        else if (/^[A-Z]/.test(t)) cls = 'type';
        out += cls ? span(cls, t) : esc(t);
      } else out += span('pun', t);
      prev = t;
    }
    return out + esc(src.slice(last));
  }
  function apply(root) {
    (root || document).querySelectorAll('pre').forEach(function (pre) {
      var src = pre.textContent;
      pre.innerHTML = highlight(src);
      if (!pre.hasAttribute('data-fixed')) pre.setAttribute('data-lang', /#include|::|->|\bstruct\b|\bvoid\b|\bfloat\b|AddComponent|\bclass\b|LYNX_/.test(src) ? 'C++' : 'JavaScript');
    });
  }
  apply(document);
  window.LynxHL = { apply: apply };
})();

/* toolbar : menu mobile + menus déroulants (Docs, Discover) */
(function () {
  var bar = document.querySelector('.topbar'); if (!bar) return;
  var toggle = bar.querySelector('.nav-toggle'), groups = Array.prototype.slice.call(bar.querySelectorAll('.nav-group'));
  function setGroup(g, open) { g.classList.toggle('open', open); var d = g.querySelector('.nav-drop'); if (d) d.setAttribute('aria-expanded', String(open)); }
  function closeGroups(except) { groups.forEach(function (g) { if (g !== except) setGroup(g, false); }); }
  function setMenu(open) { bar.classList.toggle('open', open); toggle.setAttribute('aria-expanded', String(open)); if (!open) closeGroups(); }
  toggle.addEventListener('click', function () { setMenu(!bar.classList.contains('open')); });
  groups.forEach(function (g) {
    var d = g.querySelector('.nav-drop'); if (!d) return;
    d.addEventListener('click', function (e) { e.stopPropagation(); var open = !g.classList.contains('open'); closeGroups(g); setGroup(g, open); });
  });
  document.addEventListener('click', function (e) { groups.forEach(function (g) { if (!g.contains(e.target)) setGroup(g, false); }); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') { closeGroups(); setMenu(false); } });
  window.addEventListener('resize', function () { if (window.innerWidth > 900) setMenu(false); });
})();
/* lecteur YouTube : refusé (erreur 153) quand la page est ouverte depuis un fichier local */
(function () {
  var frame = document.querySelector('.yt-frame');
  if (!frame || location.protocol !== 'file:') return;
  frame.innerHTML = '<div class="yt-offline"><p>The YouTube player is blocked when the site is opened from a local file. It works once the site is online.</p><a class="button primary" href="https://www.youtube.com/@oscar-soirey" target="_blank" rel="noopener">Watch on YouTube →</a></div>';
})();
