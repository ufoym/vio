(function () {
  "use strict";
  var D = window.VIO_DATA;
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; }); }
  function md(s) { return esc(s).replace(/`([^`]+)`/g, "<code>$1</code>"); }
  function fmtX(r) { return r >= 10 ? r.toFixed(0) : r >= 2 ? r.toFixed(1) : r.toFixed(2); }
  function geomean(a) { var s = 0; a.forEach(function (x) { s += Math.log(x); }); return Math.exp(s / a.length); }
  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } }

  /* ───────── Theme ───────── */
  $("#theme-toggle").addEventListener("click", function () {
    var root = document.documentElement;
    var cur = root.dataset.theme || (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
    var next = cur === "dark" ? "light" : "dark";
    root.dataset.theme = next;
    store("vio-theme", next);
  });

  /* ───────── Copy buttons ───────── */
  function flash(btn) { btn.classList.add("done"); setTimeout(function () { btn.classList.remove("done"); }, 1200); }
  function copyText(text, btn) {
    if (navigator.clipboard) navigator.clipboard.writeText(text).then(function () { flash(btn); }, function () {});
  }
  $$("[data-copy]").forEach(function (b) {
    b.addEventListener("click", function () { copyText($(b.dataset.copy).textContent, b); });
  });

  /* ───────── Hero: benchmark race ───────── */
  (function hero() {
    var root = $("#race"); if (!root) return;
    function row(set, cpu, simd) {
      var rows = D.perf[set].rows;
      for (var i = 0; i < rows.length; i++) {
        var l = rows[i].label;
        if (l.join(" ").indexOf(cpu) >= 0 && l[l.length - 1] === simd) return rows[i];
      }
      return null;
    }
    var SCENES = [
      { tab: "JPEG decode", set: "jpeg_dec", cpu: "Apple M4", simd: "NEON", file: "sunset.jpg", mode: "dec", rivals: [["libjpeg-turbo", 0], ["libjpeg", 1]] },
      { tab: "JPEG encode", set: "jpeg_enc", cpu: "Zen 5", simd: "AVX2", name: "AMD Zen 5", file: "sunset.jpg", mode: "enc", rivals: [["libjpeg-turbo", 0], ["libjpeg", 1]] },
      { tab: "PNG decode", set: "png_dec", cpu: "Linux, EPYC 7763", simd: "AVX2", name: "AMD Zen 3", file: "sunset.png", mode: "dec", rivals: [["image-png", 1], ["libpng", 0]] },
      { tab: "PNG encode", set: "png_enc", cpu: "Neoverse-N2", simd: "NEON", file: "sunset.png", mode: "enc", rivals: [["image-png", 3], ["libpng -6", 1]] }
    ].filter(function (sc) {
      var r = row(sc.set, sc.cpu, sc.simd); if (!r) return false;
      sc.lanes = [{ name: "vio", ratio: 1, me: true }].concat(sc.rivals.map(function (rv) { return { name: rv[0], ratio: r.ratio[rv[1]] }; })
        .filter(function (l) { return l.ratio; }).sort(function (x, y) { return x.ratio - y.ratio; }));
      return sc.lanes.length > 1;
    });
    if (!SCENES.length) return;

    // Lane image: a sunset strip, plus its 8×8-block (DC-only) version for the encoder.
    var LW = 560, LH = 80, BS = 8, NX = LW / BS, NY = LH / BS, NB = NX * NY, DPR = 2;
    function paint(g, W, H) {
      var hz = H * 0.66;
      var sky = g.createLinearGradient(0, 0, 0, hz);
      sky.addColorStop(0, "#130a35"); sky.addColorStop(0.45, "#4b1f7a"); sky.addColorStop(0.78, "#c43d6b"); sky.addColorStop(1, "#ffa05a");
      g.fillStyle = sky; g.fillRect(0, 0, W, hz);
      var seed = 11; function rnd() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }
      for (var i = 0; i < 90; i++) { g.fillStyle = "rgba(255,255,255," + (0.35 + rnd() * 0.6) + ")"; g.fillRect(rnd() * W, rnd() * hz * 0.45, 1, 1); }
      var sx = W * 0.72, sy = hz - 4, sr = H * 0.34;
      var sun = g.createRadialGradient(sx, sy, 2, sx, sy, sr);
      sun.addColorStop(0, "#fffbe6"); sun.addColorStop(0.5, "#ffd27a"); sun.addColorStop(1, "rgba(255,160,90,0)");
      g.fillStyle = sun; g.beginPath(); g.arc(sx, sy, sr, 0, 7); g.fill();
      function ridge(base, amp, col, ph) {
        g.fillStyle = col; g.beginPath(); g.moveTo(0, hz + 2);
        for (var x = 0; x <= W; x += 4) g.lineTo(x, base - amp * Math.abs(Math.sin(x * 0.019 + ph)) - amp * 0.45 * Math.abs(Math.sin(x * 0.061 + ph * 2)));
        g.lineTo(W, hz + 2); g.closePath(); g.fill();
      }
      ridge(hz - 2, H * 0.28, "#3a1a66", 0.6);
      ridge(hz + 1, H * 0.2, "#22104a", 2.1);
      ridge(hz + 3, H * 0.11, "#140a30", 4.3);
      var sea = g.createLinearGradient(0, hz, 0, H);
      sea.addColorStop(0, "#2a1450"); sea.addColorStop(1, "#090616");
      g.fillStyle = sea; g.fillRect(0, hz, W, H - hz);
      for (var y = hz + 3; y < H; y += 3) {
        var w = 46 - (y - hz) * 1.3; if (w < 5) w = 5;
        g.fillStyle = "rgba(255,190,110," + Math.max(0.1, 0.85 - (y - hz) / 30) + ")";
        g.fillRect(sx - w / 2 + Math.sin(y * 1.7) * 4, y, w, 1.4);
      }
    }
    var src = document.createElement("canvas"); src.width = LW * DPR; src.height = LH * DPR;
    var sg = src.getContext("2d"); sg.scale(DPR, DPR); paint(sg, LW, LH);
    var tiny = document.createElement("canvas"); tiny.width = NX; tiny.height = NY;
    var tg = tiny.getContext("2d"); tg.imageSmoothingQuality = "high"; tg.drawImage(src, 0, 0, NX, NY);

    // DOM.
    var tabsEl = $("#race-tabs"), lanesEl = $("#race-lanes"), xEl = $("#race-x"), vsEl = $("#race-vs"), subEl = $("#race-sub");
    var tabBtns = SCENES.map(function (sc, i) {
      var b = document.createElement("button");
      b.type = "button"; b.setAttribute("role", "tab"); b.innerHTML = esc(sc.tab) + "<i></i>";
      b.addEventListener("click", function () { start(i); if (reduced) finish(); });
      tabsEl.appendChild(b); return b;
    });
    var LANES = [0, 1, 2].map(function () {
      var el = document.createElement("div"); el.className = "lane";
      el.innerHTML = '<span class="lane-name"></span><div class="lane-img"><canvas></canvas></div><span class="lane-t"></span>';
      lanesEl.appendChild(el);
      var cv = $("canvas", el); cv.width = LW * DPR; cv.height = LH * DPR;
      return { el: el, name: $(".lane-name", el), t: $(".lane-t", el), ctx: cv.getContext("2d"), n: 0 };
    });

    function block(L, i, mode) {
      var bx = i % NX, by = (i / NX) | 0, c = L.ctx, d = BS * DPR, x = bx * d, y = by * d;
      if (mode === "dec") c.drawImage(src, x, y, d, d, x, y, d, d);
      else {
        c.imageSmoothingEnabled = false;
        c.drawImage(tiny, bx, by, 1, 1, x, y, d, d);
        c.fillStyle = "rgba(10,8,30,0.35)"; c.fillRect(x, y + d - 1, d, 1); c.fillRect(x + d - 1, y, 1, d);
      }
    }
    function resetLane(L, mode) {
      var c = L.ctx; L.n = 0;
      if (mode === "enc") { c.drawImage(src, 0, 0); return; }
      c.fillStyle = "#07070e"; c.fillRect(0, 0, LW * DPR, LH * DPR);
      c.fillStyle = "rgba(255,255,255,0.035)";
      for (var x = 0; x < NX; x++) c.fillRect(x * BS * DPR, 0, 1, LH * DPR);
      for (var y = 0; y < NY; y++) c.fillRect(0, y * BS * DPR, LW * DPR, 1);
    }
    // The last few blocks written glow, like a scan front.
    var GLOW = 7;
    function advance(L, n, mode) {
      n = Math.min(NB, n);
      if (n === L.n) return;
      for (var i = Math.max(0, L.n - GLOW); i < n; i++) block(L, i, mode);
      if (n < NB) {
        var c = L.ctx, d = BS * DPR;
        for (var k = 1; k <= GLOW && n - k >= 0; k++) {
          var j = n - k;
          c.fillStyle = "rgba(255,240,220," + (0.55 * (1 - k / (GLOW + 1))) + ")";
          c.fillRect((j % NX) * d, ((j / NX) | 0) * d, d, d);
        }
      }
      L.n = n;
    }
    function fmt(r) { return r.toFixed(2) + "×"; }

    // vio finishes in V ms; each rival takes V × its measured ratio (capped).
    var V = 1500, HOLD = 2300, CAP = 2.8;
    var cur = -1, sc, t = 0, raceEnd = 0, total = 0;
    function start(i) {
      cur = i; sc = SCENES[i]; t = 0;
      raceEnd = V * Math.min(sc.lanes[sc.lanes.length - 1].ratio, CAP); total = raceEnd + HOLD;
      tabBtns.forEach(function (b, j) { b.setAttribute("aria-selected", j === i ? "true" : "false"); b.style.setProperty("--p", 0); });
      $("#race-cpu").innerHTML = esc(sc.name || sc.cpu) + " · <b>" + esc(sc.simd) + "</b>";
      $("#race-file").textContent = "· " + sc.file;
      $$("#race-isa span").forEach(function (s) { s.classList.toggle("on", s.dataset.isa === sc.simd); });
      vsEl.textContent = (sc.mode === "dec" ? "faster decoding" : "faster encoding") + " than " + sc.lanes[1].name;
      subEl.textContent = sc.lanes.slice(2).map(function (l) { return fmt(l.ratio) + " vs " + l.name; }).join(" · ") || "single thread";
      xEl.textContent = "1.00×";
      root.classList.add("running");
      LANES.forEach(function (L, j) {
        var l = sc.lanes[j]; L.el.hidden = !l; if (!l) return;
        L.name.textContent = l.name; L.el.classList.toggle("me", !!l.me); L.el.classList.remove("done");
        L.t.textContent = "0%"; resetLane(L, sc.mode);
      });
      render();
    }
    function render() {
      var e = Math.min(t, raceEnd);
      LANES.forEach(function (L, j) {
        var l = sc.lanes[j]; if (!l) return;
        var p = Math.min(1, e / (V * l.ratio));
        advance(L, Math.round(p * NB), sc.mode);
        if (p >= 1) { L.el.classList.add("done"); L.t.textContent = fmt(l.ratio); }
        else L.t.textContent = t >= raceEnd ? fmt(l.ratio) : Math.floor(p * 100) + "%";
      });
      if (t >= V) {
        root.classList.remove("running");
        var r = sc.lanes[1].ratio, q = Math.min(1, (t - V) / 600);
        xEl.textContent = fmt(1 + (r - 1) * (1 - Math.pow(1 - q, 3)));
      }
      tabBtns[cur].style.setProperty("--p", Math.min(1, t / total));
    }
    function finish() { t = total; render(); }

    start(0);
    if (reduced) { finish(); return; }
    var visible = true, last = null;
    function frame(now) {
      if (last !== null && visible) {
        t += Math.min(64, now - last);
        if (t >= total) start((cur + 1) % SCENES.length); else render();
      }
      last = now;
      requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
    if ("IntersectionObserver" in window) new IntersectionObserver(function (es) { visible = es[0].isIntersecting; }).observe(root);
  })();

  /* ───────── Count-up stats ───────── */
  function countUp(el) {
    var to = parseFloat(el.dataset.count), d = 1200, t0 = null;
    if (reduced) return;
    function step(t) {
      if (t0 === null) t0 = t;
      var e = Math.min(1, (t - t0) / d), v = 1 + (to - 1) * (1 - Math.pow(1 - e, 3));
      el.textContent = v.toFixed(1);
      if (e < 1) requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
  }

  /* ───────── Tabs helper ───────── */
  function tabs(root, attr, onPick) {
    var btns = $$("button", root);
    btns.forEach(function (b) {
      b.addEventListener("click", function () {
        btns.forEach(function (x) { x.setAttribute("aria-selected", x === b ? "true" : "false"); });
        onPick(b.dataset[attr]);
      });
    });
  }

  /* ───────── Performance bars ───────── */
  var PERF = {
    jpeg_enc: { title: "JPEG compression", note: "3632×2384 RGB, best of 5 runs; geometric mean over 4:4:4, 4:2:2, 4:2:0, gray, fast DCT, progressive and arithmetic coding. " },
    jpeg_dec: { title: "JPEG decompression", note: "3632×2384 RGB, best of 5 runs; geometric mean over 4:4:4, 4:2:2, 4:2:0, gray, fast DCT, progressive and arithmetic coding. " },
    png_dec: { title: "PNG decoding", note: "Geometric mean over the 2848 images of the QOI benchmark corpus; every library verifies CRC-32 and Adler-32. " },
    png_enc: { title: "PNG encoding", note: "vs libpng: vio's zlib mode, which writes libpng's files byte for byte. vs fpnge and image-png: vio's single-pass fast mode, with 4–5% smaller files. fpnge has no Arm or scalar build. " }
  };
  var jpegNotes = D.perf.jpeg_dec.notes.join(" ");
  var perfSet = "jpeg_enc", perfCol = {};
  function splitLabel(set, lab) {
    if (set.indexOf("jpeg") === 0) return { group: lab[0], cpu: lab[1], simd: lab[2] };
    var i = lab[0].indexOf(", ");
    return { group: lab[0].slice(0, i), cpu: lab[0].slice(i + 2), simd: lab[1] };
  }
  function renderVs() {
    var t = D.perf[perfSet], box = $("#perf-vs"), col = perfCol[perfSet] || 0;
    box.innerHTML = t.columns.map(function (c, i) {
      return '<button role="radio" data-i="' + i + '" aria-checked="' + (i === col) + '">' + esc(c) + "</button>";
    }).join("");
    $$("button", box).forEach(function (b) {
      b.addEventListener("click", function () { perfCol[perfSet] = +b.dataset.i; renderVs(); renderBars(); });
    });
  }
  function renderBars() {
    var t = D.perf[perfSet], col = perfCol[perfSet] || 0, rows = t.rows;
    var vals = rows.map(function (r) { return r.ratio[col]; }).filter(function (x) { return x != null; });
    var max = Math.max(1.25, Math.max.apply(null, vals)) * 1.02;
    var gm = geomean(vals), lo = Math.min.apply(null, vals), hi = Math.max.apply(null, vals);
    $("#perf-geo").textContent = gm.toFixed(2);
    var vsName = t.columns[col].replace(/^vs /, "");
    $("#perf-geo-cap").textContent = PERF[perfSet].title + " vs " + vsName + " · geometric mean";
    $("#perf-range").textContent = vals.length + " CPU × SIMD configurations, from " + fmtX(lo) + "× to " + fmtX(hi) + "×";
    $("#perf-note").innerHTML = md(PERF[perfSet].note + (perfSet.indexOf("jpeg") === 0 ? jpegNotes : "macOS x86-64 runners vary the most between runs."));

    var box = $("#perf-bars"), groups = [], byName = {};
    rows.forEach(function (r) {
      var l = splitLabel(perfSet, r.label);
      if (!byName[l.group]) { byName[l.group] = { name: l.group, rows: [] }; groups.push(byName[l.group]); }
      byName[l.group].rows.push({ l: l, ratio: r.ratio[col], raw: r.raw[col] });
    });
    var key = perfSet + "/" + rows.length;
    if (box.dataset.key !== key) {
      box.dataset.key = key;
      box.innerHTML = groups.map(function (gr) {
        return '<div class="bar-group"><div class="bar-group-title">' + esc(gr.name) + "</div>" + gr.rows.map(function (r) {
          var simd = r.l.simd !== "scalar";
          return '<div class="bar-row"><div class="bar-label"><span class="chip' + (simd ? " simd" : "") + '">' + esc(r.l.simd) + '</span><span class="cpu" title="' + esc(r.l.cpu) + '">' + esc(r.l.cpu) + '</span></div>' +
            '<div class="track"><span class="par"></span><span class="fill' + (simd ? "" : " scalar") + '"></span><span class="val"></span></div></div>';
        }).join("") + "</div>";
      }).join("");
    }
    var flat = []; groups.forEach(function (gr) { flat = flat.concat(gr.rows); });
    var fills = $$(".fill", box), valsEl = $$(".val", box), pars = $$(".par", box);
    requestAnimationFrame(function () {
      flat.forEach(function (r, i) {
        pars[i].style.left = (100 / max) + "%";
        if (r.ratio == null) {
          fills[i].style.width = "0"; valsEl[i].style.left = "0"; valsEl[i].className = "val na"; valsEl[i].textContent = "— not available";
          return;
        }
        var w = (r.ratio / max) * 100;
        fills[i].style.width = w + "%";
        fills[i].classList.toggle("slow", r.ratio < 1);
        valsEl[i].className = "val"; valsEl[i].style.left = w + "%";
        valsEl[i].innerHTML = fmtX(r.ratio) + "×<small>" + esc(r.raw) + "</small>";
      });
    });
  }
  tabs($("#perf-tabs"), "set", function (s) { perfSet = s; renderVs(); renderBars(); });
  renderVs(); renderBars();

  /* ───────── Heat map ───────── */
  var HEAT = {
    jpeg_enc_cfg: "vio vs libjpeg-turbo, compression. 4:4:4, 4:2:2, 4:2:0 and gray at quality 95; fastDCT, prog. and arith. are 4:2:0 with the fast integer DCT, progressive and arithmetic-coded.",
    jpeg_dec_cfg: "vio vs libjpeg-turbo, decompression. 4:4:4, 4:2:2, 4:2:0 and gray at quality 95; fastDCT, prog. and arith. are 4:2:0 with the fast integer DCT, progressive and arithmetic-coded.",
    png_cat: "Linux x86-64, AMD EPYC 7763, GCC, AVX2, by image category (number of images in parentheses). ¹ zlib mode, level 6. ² Fast mode, with the size difference of its files."
  };
  function renderHeat(set) {
    var t = D.perf[set], all = [];
    t.rows.forEach(function (r) { r.ratio.forEach(function (x) { if (x != null) all.push(x); }); });
    var hi = Math.max.apply(null, all), L = Math.log(hi);
    var h = "<thead><tr><th></th>" + t.columns.map(function (c) { return "<th>" + esc(c) + "</th>"; }).join("") + "</tr></thead><tbody>";
    t.rows.forEach(function (r) {
      h += "<tr><th>" + esc(r.label[0]) + "</th>";
      r.ratio.forEach(function (x, i) {
        var raw = r.raw[i], m = raw.match(/^(\S+)\s*\((.*)\)$/), tt = Math.max(0, Math.log(x) / L);
        var main = m ? m[1] : raw, extra = m ? "<small>" + esc(m[2]) + "</small>" : "";
        var pct = Math.round(Math.pow(tt, 0.8) * 100);
        h += '<td title="' + esc(r.label[0] + " · " + t.columns[i] + ": " + fmtX(x) + "×") + '" style="background:color-mix(in srgb, rgb(var(--heat-hi)) ' + pct + '%, rgb(var(--heat-lo)));' + (pct > 52 ? "color:#fff" : "") + '">' + esc(main) + extra + "</td>";
      });
      h += "</tr>";
    });
    $("#heat").innerHTML = h + "</tbody>";
    $("#heat-sub").textContent = HEAT[set];
    $("#heat-max").textContent = "+" + Math.round((hi - 1) * 100) + "%";
  }
  tabs($("#heat-tabs"), "set", renderHeat);
  renderHeat("jpeg_enc_cfg");

  /* ───────── Optimization log ───────── */
  var logSet = "jpeg", logAll = false, LOG_N = 8;
  $("#log-count").textContent = D.log.jpeg.length + D.log.png.length;
  function badge(gain) {
    var m = gain.match(/[+−-]\d+(?:\.\d+)?(?:–\d+(?:\.\d+)?)?%/);
    if (m) return m[0];
    m = gain.match(/(\d+(?:\.\d+)?)(?: GB\/s)? instead of (\d+(?:\.\d+)?)/);
    if (m) return (parseFloat(m[1]) / parseFloat(m[2])).toFixed(1) + "×";
    return "✓";
  }
  function renderLog() {
    var items = D.log[logSet];
    $("#log").innerHTML = items.map(function (it, i) {
      return '<li class="' + (!logAll && i >= LOG_N ? "hidden" : "") + '"><span class="gain">' + esc(badge(it.gain)) + '</span><span class="change">' + md(it.change) + '</span><span class="detail">' + md(it.gain) + " · " +
        it.commits.map(function (c) { return '<a href="https://github.com/ufoym/vio/commit/' + c + '">' + c + "</a>"; }).join(", ") + "</span></li>";
    }).join("");
    var more = $("#log-more");
    more.hidden = items.length <= LOG_N;
    more.textContent = logAll ? "Show fewer" : "Show all " + items.length;
  }
  $("#log-more").addEventListener("click", function () { logAll = !logAll; renderLog(); if (!logAll) $(".log-card").scrollIntoView({ block: "start" }); });
  tabs($("#log-tabs"), "set", function (s) { logSet = s; logAll = false; renderLog(); });
  renderLog();

  /* ───────── CI matrix ───────── */
  var CI = [
    ["Linux x86-64 · GCC", "AVX2 · SSE2"], ["Linux x86-64 · Clang", "AVX2"], ["Linux i686 · GCC", "AVX2 · SSE2 · SSE+MMX"],
    ["Linux AArch64 · GCC", "NEON"], ["macOS AArch64 · Apple Clang", "NEON"], ["macOS x86-64 · Apple Clang", "AVX2 · SSE2"],
    ["Windows x64 · MSVC", "AVX2 · SSE2"], ["Windows x64 · clang-cl", "AVX2"], ["Windows x86 · MSVC", "AVX2 · SSE2"],
    ["AArch32 · QEMU", "NEON at build time"], ["AArch32 · QEMU", "NEON detected at run time"], ["PowerPC 64 LE · QEMU", "AltiVec · VSX"],
    ["PowerPC 32 BE · QEMU", "AltiVec"], ["MIPS64 · QEMU", "Loongson MMI"], ["RISC-V 64 · QEMU", "RVV · VLEN 128 / 256"]
  ];
  $("#ci-grid").innerHTML = CI.map(function (c) {
    return '<div class="ci-job"><svg viewBox="0 0 16 16"><path d="M3 8.5 6.5 12 13 4.5"/></svg><span>' + esc(c[0]) + "<em>" + esc(c[1]) + "</em></span></div>";
  }).join("");

  /* ───────── Feature comparison ───────── */
  var CMP_SUB = [
    "vio compared with libjpeg-turbo 3.2, IJG libjpeg 10 and stb_image 2.30 (with stb_image_write 1.16 for encoding).",
    "vio compared with libpng 1.8 (with zlib), libspng 0.7.4, image-png 0.18.1 (Rust) and stb_image 2.30. vio runs the reference library's test suite, and its read transformations give the same results bit for bit."
  ];
  function glyph(cell) {
    var g = cell.charAt(0), rest = esc(cell.slice(1));
    var cls = g === "✓" ? "g-yes" : g === "◐" ? "g-part" : g === "✗" ? "g-no" : "";
    return cls ? '<b class="g ' + cls + '">' + g + "</b>" + (rest ? "<sup>" + rest + "</sup>" : "") : md(cell);
  }
  function renderCmp(i) {
    var t = D.features[i], last = null;
    var h = "<thead><tr>" + t.header.map(function (c, j) { return '<th class="' + (j === 1 ? "vio" : "") + '">' + esc(j === 0 ? "" : c) + "</th>"; }).join("") + "</tr></thead><tbody>";
    t.rows.forEach(function (r) {
      var k = r[0].indexOf(": "), grp = r[0].slice(0, k), feat = r[0].slice(k + 2);
      if (grp !== last) h += '<tr class="grp-row"><th colspan="' + r.length + '">' + esc(grp) + "</th></tr>";
      last = grp;
      h += "<tr><td>" + md(feat) + "</td>";
      for (var j = 1; j < r.length; j++) h += '<td class="' + (j === 1 ? "vio" : "") + '">' + glyph(r[j]) + "</td>";
      h += "</tr>";
    });
    $("#cmp").innerHTML = h + "</tbody>";
    var notes = [];
    t.notes.forEach(function (n) { n.split(/\s(?=[¹²³⁴⁵⁶⁷⁸⁹]\s)/).forEach(function (x) { notes.push(x); }); });
    $("#cmp-notes").innerHTML = notes.map(function (n) { return "<li>" + md(n) + "</li>"; }).join("");
    $("#cmp-sub").textContent = CMP_SUB[i];
  }
  tabs($("#cmp-tabs"), "set", function (s) { renderCmp(+s); });
  renderCmp(0);

  /* ───────── Code samples ───────── */
  var KW = /^(?:auto|const|while|for|if|return|int|unsigned|bool|true|false|void|struct|class|namespace|using|static|constexpr)$/;
  function hiCpp(src) {
    var re = /(\/\/[^\n]*|#\s*\w+)|("(?:[^"\\]|\\.)*"|<[\w./]+\.hpp>)|(\b\d+\b)|(\bstd::\w+|\bvio::[\w:]+)|(\b[A-Za-z_]\w*)(?=\s*\()|(\b[A-Za-z_]\w*\b)/g;
    var out = "", last = 0, m;
    while ((m = re.exec(src))) {
      out += esc(src.slice(last, m.index)); last = re.lastIndex;
      var s = esc(m[0]);
      if (m[1]) out += '<span class="' + (m[0].charAt(0) === "#" ? "p" : "c") + '">' + s + "</span>";
      else if (m[2]) out += '<span class="s">' + s + "</span>";
      else if (m[3]) out += '<span class="n">' + s + "</span>";
      else if (m[4]) out += '<span class="t">' + s + "</span>";
      else if (m[5]) out += '<span class="f">' + s + "</span>";
      else out += KW.test(m[0]) ? '<span class="k">' + s + "</span>" : s;
    }
    return out + esc(src.slice(last));
  }
  function hiCmake(src) {
    return src.split("\n").map(function (line) {
      var c = line.indexOf("#"), code = c >= 0 ? line.slice(0, c) : line, com = c >= 0 ? line.slice(c) : "";
      code = esc(code)
        .replace(/\b([A-Za-z_]+)(?=\()/g, '<span class="f">$1</span>')
        .replace(/\b(GIT_REPOSITORY|GIT_TAG|PRIVATE)\b/g, '<span class="k">$1</span>')
        .replace(/(https:\/\/\S+)/g, '<span class="s">$1</span>')
        .replace(/\b(vio::vio)\b/g, '<span class="t">$1</span>');
      return code + (com ? '<span class="c">' + esc(com) + "</span>" : "");
    }).join("\n");
  }
  $$(".code").forEach(function (pre) { pre.innerHTML = pre.dataset.lang === "cmake" ? hiCmake(pre.textContent) : hiCpp(pre.textContent); });
  var codeTabs = $$(".code-tabs [role=tab]");
  codeTabs.forEach(function (b) {
    b.addEventListener("click", function () {
      codeTabs.forEach(function (x) { x.setAttribute("aria-selected", x === b ? "true" : "false"); $("#" + x.dataset.code).hidden = x !== b; });
    });
  });
  var cc = $(".code-copy");
  cc.addEventListener("click", function () { copyText($(".code:not([hidden])").textContent, cc); });

  /* ───────── Reveal on scroll ───────── */
  var targets = $$(".section-head, .pillar, .perf-card, .heat-card, .log-card, .fmt, .isa, .ci, .code-card, .cmp-card, .final-inner, .stats");
  if (!("IntersectionObserver" in window) || reduced) {
    $$(".ci-job").forEach(function (j) { j.classList.add("in"); });
    return;
  }
  targets.forEach(function (el) { el.classList.add("reveal"); });
  var io = new IntersectionObserver(function (es) {
    es.forEach(function (e) {
      if (!e.isIntersecting) return;
      var el = e.target; io.unobserve(el); el.classList.add("in");
      if (el.classList.contains("stats")) $$("[data-count]", el).forEach(countUp);
      if (el.classList.contains("ci")) $$(".ci-job", el).forEach(function (j, i) { setTimeout(function () { j.classList.add("in"); }, 40 * i); });
    });
  }, { rootMargin: "0px 0px -8% 0px", threshold: 0.08 });
  targets.forEach(function (el) { io.observe(el); });
})();
