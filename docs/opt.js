(function () {
  "use strict";
  var D = window.VIO_DATA, O = window.VIO_OPT;
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; }); }
  function md(s) { return esc(s).replace(/`([^`]+)`/g, "<code>$1</code>"); }
  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } }

  /* ───────── Theme ───────── */
  $("#theme-toggle").addEventListener("click", function () {
    var root = document.documentElement;
    var cur = root.dataset.theme || (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
    var next = cur === "dark" ? "light" : "dark";
    root.dataset.theme = next;
    store("vio-theme", next);
  });

  /* ───────── Data ───────── */
  // Badge text and a number to rank by: the largest percentage in the first
  // range, or the ratio of "X GB/s instead of Y".
  function gainOf(gain) {
    var m = gain.match(/[+−-](\d+(?:\.\d+)?)(?:–(\d+(?:\.\d+)?))?%/);
    if (m) return { badge: m[0], v: parseFloat(m[2] || m[1]) };
    m = gain.match(/(\d+(?:\.\d+)?)(?: GB\/s)? instead of (\d+(?:\.\d+)?)/);
    if (m) { var r = parseFloat(m[1]) / parseFloat(m[2]); return { badge: r.toFixed(1) + "×", v: (r - 1) * 100 }; }
    return { badge: "✓", v: 0 };
  }
  var ITEMS = [];
  ["jpeg", "png"].forEach(function (codec) {
    D.log[codec].forEach(function (it) {
      var x = O.items[it.commits[0]] || {};
      var g = gainOf(it.gain);
      ITEMS.push({
        n: ITEMS.length + 1, codec: codec, sha: it.commits[0], commits: it.commits, change: it.change, gain: it.gain,
        badge: g.badge, v: g.v, t: x.t || it.change, idea: x.idea, p: x.p, s: x.s, b: x.b, a: x.a, code: x.code
      });
    });
  });
  var IDEAS = Object.keys(O.ideas);
  $("#opt-count").textContent = ITEMS.length;

  /* ───────── Timeline ───────── */
  // "a:2|b:1+||_:2|c:2!" -> [[{l, w, k}...], ...] lanes of boxes.
  function parseRow(s) {
    return s.split("||").map(function (lane) {
      return lane.split("|").map(function (b) {
        var m = b.match(/^(.*):([\d.]+)([+!_]?)$/);
        var l = m[1], k = m[3] === "+" ? "new" : m[3] === "!" ? "slow" : (l === "_" ? "gap" : "work");
        return { l: l === "_" ? "" : l, w: parseFloat(m[2]), k: k };
      });
    });
  }
  function laneLen(lane) { return lane.reduce(function (s, b) { return s + b.w; }, 0); }
  function rowLen(lanes) { return Math.max.apply(null, lanes.map(laneLen)); }
  function lanesHtml(lanes, total) {
    return lanes.map(function (lane) {
      var x = 0;
      return '<div class="tl-lane">' + lane.map(function (b) {
        var h = b.k === "gap" ? "" : '<span class="tl-box k-' + b.k + '" style="left:' + (x / total * 100) + "%;width:" + (b.w / total * 100) + '%" title="' + esc(b.l) + '"><i>' + esc(b.l) + "</i></span>";
        x += b.w; return h;
      }).join("") + "</div>";
    }).join("");
  }
  function timeline(it) {
    if (!it.b || !it.a) return "";
    var B = parseRow(it.b), A = parseRow(it.a), bt = rowLen(B), at = rowLen(A), total = Math.max(bt, at);
    function row(name, lanes, len, saved) {
      var inner = lanesHtml(lanes, total);
      return '<div class="tl-row"><span class="tl-lab">' + name + '</span><div class="tl-track" style="--n:' + lanes.length + '">' +
        (saved ? '<span class="tl-saved" style="left:' + (len / total * 100) + '%"><em>saved</em></span>' : "") +
        '<div class="tl-base">' + inner + '</div><div class="tl-lit">' + inner + '</div><span class="tl-head" aria-hidden="true"></span></div></div>';
    }
    return '<div class="tl">' + row("before", B, bt) + row("after", A, at, at < bt) + "</div>";
  }

  /* ───────── Gain strip ───────── */
  var maxV = Math.max.apply(null, ITEMS.map(function (it) { return it.v; }));
  function barH(v) { return 8 + 92 * Math.log(1 + v) / Math.log(1 + maxV); }
  var strip = $("#strip");
  strip.innerHTML = ITEMS.map(function (it, i) {
    var sep = i && it.codec !== ITEMS[i - 1].codec ? '<span class="strip-sep"></span>' : "";
    return sep + '<button type="button" class="strip-bar" data-i="' + i + '" style="--h:' + barH(it.v).toFixed(1) + "%;--ic:var(--idea-" + (it.idea || "none") + ')" aria-label="' + esc("#" + it.n + " " + it.t + ", " + it.badge) + '"></button>';
  }).join("");
  var tip = $("#strip-tip");
  function showTip(it) {
    tip.innerHTML = it ? '<b>' + esc(it.badge) + "</b> " + esc(it.t) + ' <span class="muted">· ' + esc(it.idea ? O.ideas[it.idea].short : it.codec.toUpperCase()) + "</span>" : "";
  }
  $$(".strip-bar", strip).forEach(function (b) {
    var it = ITEMS[+b.dataset.i];
    b.addEventListener("mouseenter", function () { showTip(it); });
    b.addEventListener("focus", function () { showTip(it); });
    b.addEventListener("click", function () {
      if (!matches(it)) { codec = "all"; idea = null; syncControls(); render(); }
      var el = document.getElementById("o-" + it.sha);
      el.scrollIntoView({ behavior: reduced ? "auto" : "smooth", block: "center" });
      el.classList.remove("flash"); void el.offsetWidth; el.classList.add("flash");
    });
  });
  strip.addEventListener("mouseleave", function () { showTip(null); });

  /* ───────── Ideas ───────── */
  var codec = "all", sort = "log", idea = null;
  $("#ideas-grid").innerHTML = IDEAS.map(function (k) {
    var d = O.ideas[k], n = ITEMS.filter(function (it) { return it.idea === k; }).length;
    return '<button type="button" class="idea" data-idea="' + k + '" aria-pressed="false" style="--ic:var(--idea-' + k + ')">' +
      '<span class="idea-top"><i class="idea-dot"></i><span class="idea-n">' + n + "</span></span>" +
      "<strong>" + esc(d.name) + "</strong><span>" + esc(d.blurb) + "</span></button>";
  }).join("");
  $$(".idea").forEach(function (b) {
    b.addEventListener("click", function () {
      idea = idea === b.dataset.idea ? null : b.dataset.idea;
      syncControls(); render();
      if (idea) $("#changes").scrollIntoView({ behavior: reduced ? "auto" : "smooth" });
    });
  });

  /* ───────── List ───────── */
  function matches(it) { return (codec === "all" || it.codec === codec) && (!idea || it.idea === idea); }
  function card(it) {
    var d = it.idea && O.ideas[it.idea];
    var code = it.code ? '<div class="opt-code"><figure><figcaption>before <span>sketch</span></figcaption><pre>' + esc(it.code[0]) + '</pre></figure><figure><figcaption>after <span>sketch</span></figcaption><pre>' + esc(it.code[1]) + "</pre></figure></div>" : "";
    var body = it.p ? '<div class="opt-cols"><div><h4>Bottleneck</h4><p>' + md(it.p) + '</p></div><div><h4>Idea</h4><p>' + md(it.s) + "</p></div></div>" : "";
    return '<li class="opt" id="o-' + it.sha + '" style="--ic:var(--idea-' + (it.idea || "none") + ')">' +
      '<div class="opt-top"><span class="opt-n">#' + it.n + '</span><span class="chip">' + it.codec.toUpperCase() + "</span>" +
      (d ? '<button type="button" class="chip chip-idea" data-idea="' + it.idea + '"><i class="idea-dot"></i>' + esc(d.short) + "</button>" : "") +
      '<span class="opt-gain">' + esc(it.badge) + "</span></div>" +
      "<h3>" + esc(it.t) + "</h3>" + body + timeline(it) + code +
      '<p class="opt-meta"><span>' + md(it.gain) + '</span><span class="opt-sha">' +
      it.commits.map(function (c) { return '<a href="https://github.com/ufoym/vio/commit/' + c + '">' + c + "</a>"; }).join(", ") + "</span></p></li>";
  }
  var io = "IntersectionObserver" in window && !reduced ? new IntersectionObserver(function (es) {
    es.forEach(function (e) { e.target.classList.toggle("play", e.isIntersecting); });
  }, { rootMargin: "0px 0px -10% 0px" }) : null;
  function render() {
    var list = ITEMS.filter(matches);
    if (sort === "gain") list.sort(function (x, y) { return y.v - x.v; });
    $("#opts").innerHTML = list.map(card).join("");
    $("#opt-showing").innerHTML = list.length === ITEMS.length ? "All " + ITEMS.length + " changes" :
      list.length + " of " + ITEMS.length + " changes" + (idea ? ' · <button type="button" class="linkish" id="clear-idea">show every idea</button>' : "");
    var ci = $("#clear-idea"); if (ci) ci.addEventListener("click", function () { idea = null; syncControls(); render(); });
    $$("#opts .chip-idea").forEach(function (b) {
      b.addEventListener("click", function () { idea = b.dataset.idea; syncControls(); render(); $("#changes").scrollIntoView({ behavior: reduced ? "auto" : "smooth" }); });
    });
    $$("#opts .tl").forEach(function (t) { if (io) io.observe(t); else t.classList.add("static"); });
    $$(".strip-bar", strip).forEach(function (b) { b.classList.toggle("dim", !matches(ITEMS[+b.dataset.i])); });
  }
  function syncControls() {
    $$("#f-codec [role=tab]").forEach(function (b) { b.setAttribute("aria-selected", b.dataset.v === codec ? "true" : "false"); });
    $$("#f-sort [role=tab]").forEach(function (b) { b.setAttribute("aria-selected", b.dataset.v === sort ? "true" : "false"); });
    $$(".idea").forEach(function (b) { b.setAttribute("aria-pressed", b.dataset.idea === idea ? "true" : "false"); });
    $("#ideas-grid").classList.toggle("has-pick", !!idea);
  }
  $$("#f-codec [role=tab]").forEach(function (b) { b.addEventListener("click", function () { codec = b.dataset.v; syncControls(); render(); }); });
  $$("#f-sort [role=tab]").forEach(function (b) { b.addEventListener("click", function () { sort = b.dataset.v; syncControls(); render(); }); });
  render();
})();
