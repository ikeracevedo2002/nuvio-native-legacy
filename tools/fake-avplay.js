// DUBLE DE webapis.avplay para o alvo Tizen.
//
// Nao ha TV Samsung nesta bancada e webapis.avplay nao existe no Chrome, entao
// o video_tizen.c nunca tinha sido EXECUTADO — so compilado. Este duble nao
// prova que o video toca; prova que o app fala com o AVPlay na ORDEM e com os
// ARGUMENTOS que a Samsung documenta. E o que da para verificar aqui.
(function () {
  // SO NO FIO PRINCIPAL. O --pre-js e prependado ao glue do Emscripten, e esse
  // mesmo glue roda DENTRO DE CADA WORKER de pthread — onde `window` nao
  // existe. A primeira versao disto referenciava `window` direto e derrubava
  // todos os workers com "Uncaught ReferenceError: window is not defined" no
  // arranque; o app nao chegava a desenhar um quadro e o teste parecia nao
  // fazer nada. O duble so faz sentido onde o DOM existe.
  if (typeof window === "undefined" || typeof document === "undefined") return;

  var reg = [];
  function log(nome, args) {
    var a = Array.prototype.slice.call(args).map(function (v) {
      if (typeof v === "function") return "fn";
      if (typeof v === "string" && v.length > 60) return v.slice(0, 60) + "...";
      return JSON.stringify(v);
    }).join(", ");
    reg.push(nome + "(" + a + ")");
    if (typeof Module !== "undefined" && Module.print)
      Module.print("[AV] " + nome + "(" + a + ")");
  }
  var estado = "NONE", ouvinte = null;
  window.webapis = window.webapis || {};
  window.webapis.avplay = {
    open:            function (u) { log("open", arguments); estado = "IDLE"; },
    close:           function ()  { log("close", arguments); estado = "NONE"; },
    stop:            function ()  { log("stop", arguments); estado = "IDLE"; },
    play:            function ()  { log("play", arguments); estado = "PLAYING";
                                    if (ouvinte && ouvinte.oncurrentplaytime) ouvinte.oncurrentplaytime(0); },
    pause:           function ()  { log("pause", arguments); estado = "PAUSED"; },
    prepare:         function ()  { log("prepare", arguments); estado = "READY"; },
    prepareAsync:    function (ok, err) {
                       log("prepareAsync", arguments); estado = "READY";
                       setTimeout(function () { if (ok) ok(); }, 10);
                     },
    seekTo:          function (ms, ok, err) { log("seekTo", arguments); if (ok) setTimeout(ok, 5); },
    setDisplayRect:  function (x, y, w, h) { log("setDisplayRect", arguments); },
    setDisplayMethod:function (m) { log("setDisplayMethod", arguments); },
    setListener:     function (l) { log("setListener", arguments); ouvinte = l; },
    getState:        function ()  { return estado; },
    getCurrentTime:  function ()  { return 0; },
    getDuration:     function ()  { return 7200000; },
    setSelectTrack:  function (t, i) { log("setSelectTrack", arguments); },
    setExternalSubtitlePath: function (p) { log("setExternalSubtitlePath", arguments); },
    getTotalTrackInfo: function () {
      log("getTotalTrackInfo", arguments);
      return [
        { index: 0, type: "VIDEO", extra_info: '{"fourCC":"HEVC","Width":3840,"Height":2160}' },
        { index: 1, type: "AUDIO", extra_info: '{"language":"eng","fourCC":"EAC3","channels":6}' },
        { index: 2, type: "AUDIO", extra_info: '{"language":"por","fourCC":"AC-3","channels":6}' },
        { index: 3, type: "TEXT",  extra_info: '{"track_lang":"por"}' }
      ];
    }
  };
  window.__avReg = function () { return reg; };
  window.__avLimpar = function () { reg = []; };
})();
