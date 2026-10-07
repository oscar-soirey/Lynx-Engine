/* Mini Markdown renderer for the Lynx devlog (no dependencies). */
(function () {
  var esc = function (s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); };
  function safeUrl(u) { u = u.trim(); return /^(https?:|mailto:|#|\/|\.\.?\/|blob:)/i.test(u) || /^[\w@.\-\/%]+$/.test(u) ? u : '#'; }
  var YT = /^(?:https?:\/\/)?(?:www\.|m\.)?(?:youtube\.com\/(?:watch\?(?:[^\s]*&)?v=|embed\/|shorts\/)|youtu\.be\/)([\w-]{11})(?:[?&#]\S*)?$/i;
  var LANG = { cpp: 'C++', 'c++': 'C++', js: 'JavaScript', javascript: 'JavaScript', json: 'JSON', py: 'Python', python: 'Python', cs: 'C#', glsl: 'GLSL', sh: 'Shell', bash: 'Shell' };
  function resolve(o, u) { return o.img ? o.img(u) : u; }
  function inline(t, o) {
    var codes = [];
    t = t.replace(/`([^`]+)`/g, function (_, c) { codes.push(c); return '\u0000' + (codes.length - 1) + '\u0000'; });
    t = esc(t);
    t = t.replace(/!\[([^\]]*)\]\(([^)\s]+)(?:\s+&quot;(.*?)&quot;)?\)/g, function (_, a, u, ti) {
      return '<img src="' + resolve(o, safeUrl(u)) + '" alt="' + a + '"' + (ti ? ' title="' + ti + '"' : '') + ' loading="lazy">';
    });
    t = t.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, function (_, a, u) {
      u = safeUrl(u); return '<a href="' + u + '"' + (/^https?:/i.test(u) ? ' target="_blank" rel="noopener"' : '') + '>' + a + '</a>';
    });
    t = t.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>').replace(/(^|[^*])\*([^*\s][^*]*)\*/g, '$1<em>$2</em>').replace(/~~([^~]+)~~/g, '<del>$1</del>');
    return t.replace(/\u0000(\d+)\u0000/g, function (_, i) { return '<code>' + esc(codes[+i]) + '</code>'; });
  }
  function media(line, o) {
    var m = line.trim().match(/^!?\[[^\]]*\]\((\S+?)(?:\s+"[^"]*")?\)$/), u = m ? m[1] : (/^https?:\/\/\S+$/.test(line.trim()) ? line.trim() : null);
    if (!u) return null;
    var y = u.match(YT);
    if (y) return '<div class="md-embed"><iframe src="https://www.youtube-nocookie.com/embed/' + y[1] + '" title="YouTube video" loading="lazy" allowfullscreen referrerpolicy="strict-origin-when-cross-origin" allow="encrypted-media; picture-in-picture"></iframe></div>';
    if (/\.(mp4|webm)(\?.*)?$/i.test(u)) return '<video controls preload="metadata" src="' + esc(resolve(o, safeUrl(u))) + '"></video>';
    return null;
  }
  var isBlock = function (l) { return /^(```|#{1,3}\s|>|\s*([-*]|\d+\.)\s+|(-{3,}|\*{3,})\s*$)/.test(l); };
  function render(md, o) {
    o = o || {};
    var L = String(md).replace(/\r\n?/g, '\n').split('\n'), out = [], i = 0, m;
    while (i < L.length) {
      var l = L[i];
      if (!l.trim()) { i++; continue; }
      if ((m = l.match(/^```\s*([\w+#-]*)\s*$/))) {
        var buf = []; i++;
        while (i < L.length && !/^```\s*$/.test(L[i])) buf.push(L[i++]);
        i++;
        var lang = m[1] ? (LANG[m[1].toLowerCase()] || m[1]) : '';
        out.push('<pre' + (lang ? ' data-lang="' + esc(lang) + '" data-fixed="1"' : '') + '>' + esc(buf.join('\n')) + '</pre>'); continue;
      }
      if ((m = l.match(/^(#{1,3})\s+(.+?)\s*#*$/))) { var n = m[1].length + 1; out.push('<h' + n + '>' + inline(m[2], o) + '</h' + n + '>'); i++; continue; }
      if (/^(-{3,}|\*{3,})\s*$/.test(l)) { out.push('<hr>'); i++; continue; }
      if (/^>/.test(l)) { var q = []; while (i < L.length && /^>/.test(L[i])) q.push(L[i++].replace(/^>\s?/, '')); out.push('<blockquote>' + q.map(function (x) { return inline(x, o); }).join('<br>') + '</blockquote>'); continue; }
      if (/^\s*([-*]|\d+\.)\s+/.test(l)) {
        var ord = /^\s*\d+\./.test(l), items = [];
        while (i < L.length && /^\s*([-*]|\d+\.)\s+/.test(L[i])) items.push(inline(L[i++].replace(/^\s*([-*]|\d+\.)\s+/, ''), o));
        out.push('<' + (ord ? 'ol' : 'ul') + '>' + items.map(function (x) { return '<li>' + x + '</li>'; }).join('') + '</' + (ord ? 'ol' : 'ul') + '>'); continue;
      }
      var md1 = media(l, o);
      if (md1) { out.push(md1); i++; continue; }
      var p = [];
      while (i < L.length && L[i].trim() && !isBlock(L[i]) && !media(L[i], o)) p.push(L[i++]);
      if (!p.length) { p.push(L[i++]); }
      out.push('<p>' + p.map(function (x) { return inline(x, o); }).join('<br>') + '</p>');
    }
    return out.join('\n');
  }
  window.LynxMD = { render: render, esc: esc };
})();
