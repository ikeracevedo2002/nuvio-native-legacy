// Backend de video nativo para macOS Intel.
//
// O libmpv fica embutido no processo e entrega o quadro ao contexto OpenGL do
// SDL. Nenhuma janela secundaria e criada: o decoder renderiza numa textura
// persistente, e este modulo compoe essa textura no framebuffer da janela antes
// de a UI ser desenhada.
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)

#include "video.h"
#include "gfx.h"
#include "layout.h"
#include "gl_compat.h"
#include <SDL2/SDL.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NV_MPVEV_POS       1
#define NV_MPVEV_DUR       2
#define NV_MPVEV_BUFFER    3
#define NV_MPVEV_W         4
#define NV_MPVEV_H         5
#define NV_MPVEV_PAUSE     6
#define NV_MPVEV_AID       7
#define NV_MPVEV_SID       8
#define NV_MPVEV_VCODEC    9
#define NV_MPVEV_ACODEC    10
#define NV_MPVEV_HDR       11

typedef struct {
  mpv_handle *mpv;
  mpv_render_context *render;
  int initialized;
  int active;
  int ready;
  int paused;
  int buffering;
  int forceSdr;
  volatile int redraw;
  double pos;
  double duration;
  double bufferDuration;
  int videoW;
  int videoH;
  char url[2048];
  char hdr[64];
  char videoCodec[64];
  char audioCodec[64];
  char lastError[256];
  int sourceDv;
  int sourceMp4;
  VideoFaixa audio[NV_FAIXA_MAX];
  VideoFaixa subs[NV_FAIXA_MAX];
  int audioIds[NV_FAIXA_MAX];
  int subIds[NV_FAIXA_MAX];
  int nAudio;
  int nSub;
  int audioCurrent;
  int subCurrent;
  VideoLegendaEstilo style;
  int styleValid;

  GLuint fbo;
  GLuint frameTex;
  int fboW;
  int fboH;
  GLuint composeProgram;
  GLuint quadVbo;
  GLint uScreen;
  GLint uSource;
  GLint uDest;
  GLint uVideo;
  int srcX;
  int srcY;
  int srcW;
  int srcH;
  int dstX;
  int dstY;
  int dstW;
  int dstH;
} NvVideoMac;

static NvVideoMac V;

static void copy_text(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) src = "";
  snprintf(dst, cap, "%s", src);
}

static int has_text(const char *s, const char *needle) {
  size_t n;
  if (!s || !needle) return 0;
  n = strlen(needle);
  if (!n) return 1;
  while (*s) {
    size_t i = 0;
    while (i < n && s[i] &&
           tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i])) i++;
    if (i == n) return 1;
    s++;
  }
  return 0;
}

static void set_error(const char *where, int code) {
  const char *msg = mpv_error_string(code);
  snprintf(V.lastError, sizeof V.lastError, "%s: %s", where,
           msg ? msg : "erro desconhecido");
  fprintf(stderr, "[video] %s\n", V.lastError);
}

static int set_option(const char *name, const char *value) {
  int r = mpv_set_option_string(V.mpv, name, value);
  if (r < 0) set_error(name, r);
  return r >= 0;
}

static int commandv(const char *const *args) {
  int r;
  if (!V.mpv || !args) return 0;
  r = mpv_command_async(V.mpv, 0, args);
  if (r < 0) set_error(args[0] ? args[0] : "command", r);
  return r >= 0;
}

static void set_property(const char *name, const char *value) {
  int r;
  if (!V.mpv || !name || !value) return;
  r = mpv_set_property_string(V.mpv, name, value);
  if (r < 0) set_error(name, r);
}

static void read_string_property(const char *name, char *dst, size_t cap) {
  char *s;
  if (!V.mpv) return;
  s = mpv_get_property_string(V.mpv, name);
  if (s) {
    copy_text(dst, cap, s);
    mpv_free(s);
  }
}

static int read_int_property(const char *name, int *dst) {
  int64_t value = 0;
  if (!V.mpv || mpv_get_property(V.mpv, name, MPV_FORMAT_INT64, &value) < 0)
    return 0;
  if (dst) *dst = (int)value;
  return 1;
}

static void refresh_metadata(void) {
  char hdr[64];
  hdr[0] = 0;
  read_string_property("video-params/hdr-format", hdr, sizeof hdr);
  if (!hdr[0]) read_string_property("video-format", hdr, sizeof hdr);
  copy_text(V.hdr, sizeof V.hdr, hdr[0] ? hdr : "none");
  read_string_property("video-codec", V.videoCodec, sizeof V.videoCodec);
  read_string_property("audio-codec", V.audioCodec, sizeof V.audioCodec);
  read_int_property("video-params/w", &V.videoW);
  read_int_property("video-params/h", &V.videoH);
}

static const mpv_node *map_value(const mpv_node *map, const char *key) {
  int i;
  mpv_node_list *list;
  if (!map || map->format != MPV_FORMAT_NODE_MAP || !key) return NULL;
  list = map->u.list;
  if (!list) return NULL;
  for (i = 0; i < list->num; i++)
    if (list->keys[i] && !strcmp(list->keys[i], key)) return &list->values[i];
  return NULL;
}

static const char *node_string(const mpv_node *map, const char *key) {
  const mpv_node *n = map_value(map, key);
  return n && n->format == MPV_FORMAT_STRING && n->u.string ? n->u.string : NULL;
}

static int node_int(const mpv_node *map, const char *key, int *out) {
  const mpv_node *n = map_value(map, key);
  if (!n) return 0;
  if (n->format == MPV_FORMAT_INT64) { *out = (int)n->u.int64; return 1; }
  if (n->format == MPV_FORMAT_DOUBLE) { *out = (int)n->u.double_; return 1; }
  if (n->format == MPV_FORMAT_FLAG) { *out = n->u.flag != 0; return 1; }
  return 0;
}

static void faixa_label(VideoFaixa *f, int numero, const char *kind,
                        const char *lang, const char *title, const char *codec) {
  const char *nome = title && title[0] ? title :
                     lang && lang[0] ? lang : kind;
  snprintf(f->rotulo, sizeof f->rotulo, "%s", nome);
  if (codec && codec[0] && strlen(f->rotulo) + 3 < sizeof f->rotulo)
    snprintf(f->rotulo + strlen(f->rotulo), sizeof f->rotulo - strlen(f->rotulo),
             " · %s", codec);
  copy_text(f->idioma, sizeof f->idioma, lang ? lang : "");
  f->numero = numero;
}

static void refresh_tracks(void) {
  mpv_node root;
  int i;
  memset(&root, 0, sizeof root);
  V.nAudio = V.nSub = 0;
  V.audioCurrent = 0;
  V.subCurrent = -1;
  if (!V.mpv || mpv_get_property(V.mpv, "track-list", MPV_FORMAT_NODE, &root) < 0)
    return;
  if (root.format == MPV_FORMAT_NODE_ARRAY && root.u.list) {
    for (i = 0; i < root.u.list->num; i++) {
      const mpv_node *row = &root.u.list->values[i];
      const char *type = node_string(row, "type");
      const char *lang = node_string(row, "lang");
      const char *title = node_string(row, "title");
      const char *codec = node_string(row, "codec");
      int id = 0, selected = 0;
      node_int(row, "id", &id);
      node_int(row, "selected", &selected);
      if (type && !strcmp(type, "audio") && V.nAudio < NV_FAIXA_MAX) {
        faixa_label(&V.audio[V.nAudio], id, "Audio", lang, title, codec);
        V.audioIds[V.nAudio] = id;
        if (selected) V.audioCurrent = V.nAudio;
        V.nAudio++;
      } else if (type && !strcmp(type, "sub") && V.nSub < NV_FAIXA_MAX) {
        faixa_label(&V.subs[V.nSub], id, "Legenda", lang, title, codec);
        V.subIds[V.nSub] = id;
        if (selected) V.subCurrent = V.nSub;
        V.nSub++;
      }
    }
  }
  mpv_free_node_contents(&root);
}

static void apply_style(void) {
  char num[32];
  int opacity;
  if (!V.mpv || !V.styleValid) return;
  snprintf(num, sizeof num, "%d", (V.style.tamanho * 55) / 120);
  set_property("sub-font-size", num);
  snprintf(num, sizeof num, "%d", V.style.atrasoMs);
  set_property("sub-delay", num);
  snprintf(num, sizeof num, "%d", 80 - V.style.posicao * 8);
  set_property("sub-pos", num);
  snprintf(num, sizeof num, "%d", V.style.borda ? 2 : 0);
  set_property("sub-border-size", num);
  snprintf(num, sizeof num, "%d", V.style.borda == 2 ? 2 : 0);
  set_property("sub-shadow-offset", num);

  switch (V.style.cor) {
    case 1: set_property("sub-color", "#ffff00"); break;
    case 2: set_property("sub-color", "#00ff00"); break;
    case 3: set_property("sub-color", "#3399ff"); break;
    case 4: set_property("sub-color", "#ff3333"); break;
    case 5: set_property("sub-color", "#000000"); break;
    default: set_property("sub-color", "#ffffff"); break;
  }
  opacity = 255 - V.style.opacidade * 64;
  if (opacity < 0) opacity = 0;
  snprintf(num, sizeof num, "#%02x000000", opacity);
  set_property("sub-back-color", V.style.fundo ? num : "#00000000");
}

static void property_changed(mpv_event_property *p, uint64_t reply_userdata) {
  if (!p || !p->data) return;
  switch ((intptr_t)reply_userdata) {
    case NV_MPVEV_POS:
      if (p->format == MPV_FORMAT_DOUBLE) V.pos = *(double *)p->data;
      break;
    case NV_MPVEV_DUR:
      if (p->format == MPV_FORMAT_DOUBLE) V.duration = *(double *)p->data;
      break;
    case NV_MPVEV_BUFFER:
      if (p->format == MPV_FORMAT_DOUBLE) V.bufferDuration = *(double *)p->data;
      break;
    case NV_MPVEV_W:
      if (p->format == MPV_FORMAT_INT64) V.videoW = (int)*(int64_t *)p->data;
      break;
    case NV_MPVEV_H:
      if (p->format == MPV_FORMAT_INT64) V.videoH = (int)*(int64_t *)p->data;
      break;
    case NV_MPVEV_PAUSE:
      if (p->format == MPV_FORMAT_FLAG) V.paused = *(int *)p->data != 0;
      break;
    case NV_MPVEV_AID:
    case NV_MPVEV_SID:
      refresh_tracks();
      break;
    case NV_MPVEV_VCODEC:
      if (p->format == MPV_FORMAT_STRING && *(char **)p->data)
        copy_text(V.videoCodec, sizeof V.videoCodec, *(char **)p->data);
      break;
    case NV_MPVEV_ACODEC:
      if (p->format == MPV_FORMAT_STRING && *(char **)p->data)
        copy_text(V.audioCodec, sizeof V.audioCodec, *(char **)p->data);
      break;
    case NV_MPVEV_HDR:
      if (p->format == MPV_FORMAT_STRING && *(char **)p->data)
        copy_text(V.hdr, sizeof V.hdr, *(char **)p->data);
      break;
  }
}

static void drain_events(void) {
  int count = 0;
  mpv_event *ev;
  if (!V.mpv) return;
  while (count++ < 64 && (ev = mpv_wait_event(V.mpv, 0.0)) != NULL) {
    if (ev->event_id == MPV_EVENT_NONE) break;
    switch (ev->event_id) {
      case MPV_EVENT_FILE_LOADED:
        V.ready = 1;
        V.active = 1;
        refresh_metadata();
        refresh_tracks();
        apply_style();
        break;
      case MPV_EVENT_VIDEO_RECONFIG:
        refresh_metadata();
        break;
      case MPV_EVENT_PROPERTY_CHANGE:
        property_changed((mpv_event_property *)ev->data, ev->reply_userdata);
        break;
#ifdef MPV_EVENT_BUFFERING
      case MPV_EVENT_BUFFERING:
        V.buffering = 1;
        break;
#endif
      case MPV_EVENT_END_FILE:
        V.ready = 0;
        V.active = 0;
        V.paused = 0;
        V.pos = 0.0;
        break;
      case MPV_EVENT_SHUTDOWN:
        V.ready = V.active = 0;
        break;
      default:
        break;
    }
  }
}

static void wake_render(void *ctx) {
  NvVideoMac *v = (NvVideoMac *)ctx;
  if (v) v->redraw = 1;
}

static void *get_proc(void *ctx, const char *name) {
  (void)ctx;
  return SDL_GL_GetProcAddress(name);
}

static GLuint compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  GLint ok = 0;
  char log[1024];
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLsizei n = 0;
    glGetShaderInfoLog(shader, sizeof log - 1, &n, log);
    log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
    fprintf(stderr, "[video] shader: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static int compose_init(void) {
  static const char *vs =
    "#version 120\n"
    "attribute vec2 aPos;\n"
    "uniform vec2 uScreen;\n"
    "uniform vec4 uSource;\n"
    "uniform vec4 uDest;\n"
    "varying vec2 vUv;\n"
    "void main(){\n"
    "  vec2 p = aPos * uSource.zw + uSource.xy;\n"
    "  vUv = vec2(p.x, 1.0 - p.y);\n"
    "  vec2 d = uDest.xy + aPos * uDest.zw;\n"
    "  gl_Position = vec4(d.x * 2.0 - 1.0, 1.0 - d.y * 2.0, 0.0, 1.0);\n"
    "}\n";
  static const char *fs =
    "#version 120\n"
    "uniform sampler2D uVideo;\n"
    "varying vec2 vUv;\n"
    "void main(){ gl_FragColor = texture2D(uVideo, vUv); }\n";
  GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
  GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
  GLint ok = 0;
  if (!v || !f) {
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    return 0;
  }
  V.composeProgram = glCreateProgram();
  glAttachShader(V.composeProgram, v);
  glAttachShader(V.composeProgram, f);
  glBindAttribLocation(V.composeProgram, 0, "aPos");
  glLinkProgram(V.composeProgram);
  glGetProgramiv(V.composeProgram, GL_LINK_STATUS, &ok);
  glDeleteShader(v);
  glDeleteShader(f);
  if (!ok) {
    glDeleteProgram(V.composeProgram);
    V.composeProgram = 0;
    fprintf(stderr, "[video] falha ao ligar shader de composicao\n");
    return 0;
  }
  V.uScreen = glGetUniformLocation(V.composeProgram, "uScreen");
  V.uSource = glGetUniformLocation(V.composeProgram, "uSource");
  V.uDest = glGetUniformLocation(V.composeProgram, "uDest");
  V.uVideo = glGetUniformLocation(V.composeProgram, "uVideo");
  {
    static const GLfloat quad[] = { 0,0, 1,0, 0,1, 1,1 };
    glGenBuffers(1, &V.quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, V.quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  }
  return 1;
}

static void compose_shutdown(void) {
  if (V.quadVbo) glDeleteBuffers(1, &V.quadVbo);
  if (V.composeProgram) glDeleteProgram(V.composeProgram);
  V.quadVbo = V.composeProgram = 0;
}

static int ensure_fbo(int w, int h) {
  GLenum status;
  if (w < 2 || h < 2) return 0;
  if (V.fbo && V.fboW == w && V.fboH == h) return 1;
  if (V.fbo) glDeleteFramebuffers(1, &V.fbo);
  if (V.frameTex) glDeleteTextures(1, &V.frameTex);
  V.fbo = V.frameTex = 0;
  glGenTextures(1, &V.frameTex);
  glBindTexture(GL_TEXTURE_2D, V.frameTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &V.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, V.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, V.frameTex, 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[video] FBO de video indisponivel (0x%x)\n", status);
    glDeleteFramebuffers(1, &V.fbo);
    glDeleteTextures(1, &V.frameTex);
    V.fbo = V.frameTex = 0;
    return 0;
  }
  V.fboW = w;
  V.fboH = h;
  return 1;
}

static void reset_rect(void) {
  V.srcX = V.srcY = 0;
  V.srcW = V.videoW;
  V.srcH = V.videoH;
  V.dstX = 0;
  V.dstY = 0;
  V.dstW = (int)NV_TELA_W;
  V.dstH = (int)NV_TELA_H;
}

int video_iniciar(void) {
  static const char *const optionPairs[][2] = {
    { "config", "no" },
    { "vo", "libmpv" },
    { "gpu-api", "opengl" },
    { "hwdec", "auto" },
    { "osc", "no" },
    { "terminal", "no" },
    { "input-default-bindings", "no" },
    { "input-cursor", "no" },
    { "keep-open", "no" },
    { "keepaspect", "no" },
    { "cache", "yes" },
    { "sub-auto", "fuzzy" },
    { "audio-client-name", "Nuvio" },
    { NULL, NULL }
  };
  int i;
  mpv_opengl_init_params glInit;
  mpv_render_param params[3];
  char *api = MPV_RENDER_API_TYPE_OPENGL;
  if (V.initialized) return 1;
  memset(&V, 0, sizeof V);
  V.subCurrent = -1;
  V.style.tamanho = 120;
  V.style.cor = 0;
  V.style.fundo = 0;
  V.style.posicao = 3;
  V.style.borda = 1;
  V.style.opacidade = 0;
  V.styleValid = 1;
  V.mpv = mpv_create();
  if (!V.mpv) {
    snprintf(V.lastError, sizeof V.lastError, "mpv_create falhou");
    return 0;
  }
  for (i = 0; optionPairs[i][0]; i++)
    if (!set_option(optionPairs[i][0], optionPairs[i][1])) goto fail;
  if (mpv_initialize(V.mpv) < 0) {
    set_error("mpv_initialize", -1);
    goto fail;
  }

  mpv_observe_property(V.mpv, NV_MPVEV_POS, "time-pos", MPV_FORMAT_DOUBLE);
  mpv_observe_property(V.mpv, NV_MPVEV_DUR, "duration", MPV_FORMAT_DOUBLE);
  mpv_observe_property(V.mpv, NV_MPVEV_BUFFER, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
  mpv_observe_property(V.mpv, NV_MPVEV_W, "video-params/w", MPV_FORMAT_INT64);
  mpv_observe_property(V.mpv, NV_MPVEV_H, "video-params/h", MPV_FORMAT_INT64);
  mpv_observe_property(V.mpv, NV_MPVEV_PAUSE, "pause", MPV_FORMAT_FLAG);
  mpv_observe_property(V.mpv, NV_MPVEV_AID, "aid", MPV_FORMAT_INT64);
  mpv_observe_property(V.mpv, NV_MPVEV_SID, "sid", MPV_FORMAT_INT64);

  glInit.get_proc_address = get_proc;
  glInit.get_proc_address_ctx = NULL;
  params[0].type = MPV_RENDER_PARAM_API_TYPE;
  params[0].data = api;
  params[1].type = MPV_RENDER_PARAM_OPENGL_INIT_PARAMS;
  params[1].data = &glInit;
  params[2].type = MPV_RENDER_PARAM_INVALID;
  params[2].data = NULL;
  if (mpv_render_context_create(&V.render, V.mpv, params) < 0) {
    snprintf(V.lastError, sizeof V.lastError, "mpv_render_context_create falhou");
    goto fail;
  }
  mpv_render_context_set_update_callback(V.render, wake_render, &V);
  if (!compose_init()) goto fail;
  reset_rect();
  V.initialized = 1;
  return 1;

fail:
  video_encerrar();
  return 0;
}

int video_tocar(const char *url) {
  const char *args[4];
  if (!url || !url[0]) return 0;
  if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8) &&
      strncmp(url, "file://", 7) && url[0] != '/') {
    snprintf(V.lastError, sizeof V.lastError, "URL de video recusada");
    return 0;
  }
  if (!V.initialized && !video_iniciar()) return 0;
  // loadfile/replace encerra a fonte anterior dentro do mesmo comando. Evita
  // uma resposta END_FILE atrasada de stop sobrescrevendo FILE_LOADED novo.
  V.active = V.ready = V.paused = 0;
  copy_text(V.url, sizeof V.url, url);
  V.active = 1;
  V.ready = 0;
  V.paused = 0;
  V.buffering = 1;
  V.pos = V.duration = V.bufferDuration = 0.0;
  V.videoW = V.videoH = 0;
  V.hdr[0] = V.videoCodec[0] = V.audioCodec[0] = 0;
  V.nAudio = V.nSub = 0;
  V.audioCurrent = 0;
  V.subCurrent = -1;
  reset_rect();
  args[0] = "loadfile";
  args[1] = url;
  args[2] = "replace";
  args[3] = NULL;
  return commandv(args);
}

void video_bombear(void) {
  if (!V.initialized) return;
  drain_events();
}

void video_render(void) {
  mpv_opengl_fbo target;
  int flip = 1;
  mpv_render_param params[3];
  int vx, vy, vw, vh;
  float sx, sy, sw, sh;
  int sourceW, sourceH;
  if (!V.initialized || !V.render || !V.ready || !V.active) return;
  gfx_obter_viewport(&vx, &vy, &vw, &vh);
  if (V.videoW < 2 || V.videoH < 2) refresh_metadata();
  if (!ensure_fbo(vw, vh)) return;

  target.fbo = (int)V.fbo;
  target.w = V.fboW;
  target.h = V.fboH;
  target.internal_format = 0;
  params[0].type = MPV_RENDER_PARAM_OPENGL_FBO;
  params[0].data = &target;
  params[1].type = MPV_RENDER_PARAM_FLIP_Y;
  params[1].data = &flip;
  params[2].type = MPV_RENDER_PARAM_INVALID;
  params[2].data = NULL;

  glBindFramebuffer(GL_FRAMEBUFFER, V.fbo);
  glViewport(0, 0, V.fboW, V.fboH);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  if (mpv_render_context_render(V.render, params) < 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gfx_estado_externo_alterado();
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(vx, vy, vw, vh);
  glDisable(GL_BLEND);
  glUseProgram(V.composeProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, V.frameTex);
  glUniform1i(V.uVideo, 0);
  glUniform2f(V.uScreen, (float)NV_TELA_W, (float)NV_TELA_H);
  if (V.videoW > 0 && V.videoH > 0) {
    sourceW = V.srcW > 0 ? V.srcW : V.videoW;
    sourceH = V.srcH > 0 ? V.srcH : V.videoH;
    sx = (float)V.srcX / V.videoW;
    sy = (float)V.srcY / V.videoH;
    sw = (float)sourceW / V.videoW;
    sh = (float)sourceH / V.videoH;
  } else {
    sx = sy = 0.0f; sw = sh = 1.0f;
  }
  glUniform4f(V.uSource, sx, sy, sw, sh);
  glUniform4f(V.uDest, (float)V.dstX / NV_TELA_W,
              (float)V.dstY / NV_TELA_H,
              (float)V.dstW / NV_TELA_W,
              (float)V.dstH / NV_TELA_H);
  glBindBuffer(GL_ARRAY_BUFFER, V.quadVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  mpv_render_context_report_swap(V.render);
  gfx_estado_externo_alterado();
}

void video_parar(void) {
  const char *args[] = { "stop", NULL };
  if (V.mpv && V.active) commandv(args);
  V.active = V.ready = V.paused = V.buffering = 0;
  V.pos = V.duration = V.bufferDuration = 0.0;
  V.url[0] = 0;
  V.nAudio = V.nSub = 0;
  V.audioCurrent = 0;
  V.subCurrent = -1;
}

void video_pausar(int pausado) {
  if (!V.mpv || !V.active) return;
  set_property("pause", pausado ? "yes" : "no");
  V.paused = pausado != 0;
}

void video_buscar(double segundos) {
  char value[64];
  const char *args[4];
  if (!V.mpv || !V.active) return;
  if (segundos < 0.0) segundos = 0.0;
  if (V.duration > 0.0 && segundos > V.duration) segundos = V.duration;
  snprintf(value, sizeof value, "%.3f", segundos);
  args[0] = "seek"; args[1] = value; args[2] = "absolute+exact"; args[3] = NULL;
  commandv(args);
  V.pos = segundos;
}

void video_janela(int x, int y, int w, int h) {
  V.srcX = V.srcY = 0;
  V.srcW = V.videoW;
  V.srcH = V.videoH;
  V.dstX = x; V.dstY = y; V.dstW = w; V.dstH = h;
}

void video_janela_fonte(int sx, int sy, int sw, int sh,
                        int dx, int dy, int dw, int dh) {
  if (V.videoW > 0) {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > V.videoW) sx = V.videoW;
    if (sy > V.videoH) sy = V.videoH;
    if (sx + sw > V.videoW) sw = V.videoW - sx;
    if (sy + sh > V.videoH) sh = V.videoH - sy;
  }
  if (sw < 1) sw = V.videoW;
  if (sh < 1) sh = V.videoH;
  V.srcX = sx; V.srcY = sy; V.srcW = sw; V.srcH = sh;
  V.dstX = dx; V.dstY = dy; V.dstW = dw; V.dstH = dh;
}

double video_pos(void) { return V.pos; }
double video_duracao(void) { return V.duration; }
double video_creditos(void) { return 0.0; }
double video_buffer_fim(void) { return V.bufferDuration > 0.0 ? V.pos + V.bufferDuration : 0.0; }
void video_definir_dv(int dv) { V.sourceDv = dv != 0; }
int video_tocando(void) { return V.active && V.ready && !V.paused; }
int video_pronto(void) { return V.active && V.ready; }
int video_ativo(void) { return V.active; }
int video_n_audio(void) { return V.nAudio; }
int video_n_legenda(void) { return V.nSub; }
const VideoFaixa *video_audio(int i) { return i >= 0 && i < V.nAudio ? &V.audio[i] : NULL; }
const VideoFaixa *video_legenda(int i) { return i >= 0 && i < V.nSub ? &V.subs[i] : NULL; }
int video_audio_atual(void) { return V.audioCurrent; }
int video_legenda_atual(void) { return V.subCurrent; }

void video_escolher_audio(int i) {
  char value[32];
  if (!V.mpv || i < 0 || i >= V.nAudio) return;
  snprintf(value, sizeof value, "%d", V.audioIds[i]);
  set_property("aid", value);
  V.audioCurrent = i;
}

void video_escolher_legenda(int i) {
  char value[32];
  if (!V.mpv) return;
  if (i < 0) {
    set_property("sid", "no");
    V.subCurrent = -1;
    return;
  }
  if (i >= V.nSub) return;
  snprintf(value, sizeof value, "%d", V.subIds[i]);
  set_property("sid", value);
  V.subCurrent = i;
}

void video_legenda_externa(const char *url) {
  char normalized[2048];
  const char *args[4];
  if (!V.mpv || !url || !url[0]) return;
  video_normalizar_url_legenda(url, normalized, sizeof normalized);
  args[0] = "sub-add"; args[1] = normalized; args[2] = "select"; args[3] = NULL;
  commandv(args);
}

void video_legenda_estilo(const VideoLegendaEstilo *e) {
  if (!e) return;
  V.style = *e;
  V.styleValid = 1;
  apply_style();
}

void video_definir_mp4(int mp4) { V.sourceMp4 = mp4 != 0; }
int video_tem_atmos(void) { return has_text(V.audioCodec, "atmos"); }
// O contexto OpenGL SDR do macOS nao oferece uma prova de apresentacao Dolby
// Vision. Nao rotulamos o quadro como DV apenas porque a fonte o declara.
int video_tem_dolby_vision(void) { return 0; }
const char *video_hdr(void) { return V.forceSdr ? "none" : (V.hdr[0] ? V.hdr : "none"); }
int video_largura(void) { return V.videoW; }
int video_altura(void) { return V.videoH; }
int video_pode_forcar_sdr(void) { return V.initialized; }

void video_forcar_sdr(void) {
  if (!V.mpv) return;
  V.forceSdr = 1;
  // Estes sao propriedades do renderizador, nao uma troca de fonte. Se uma
  // versao de mpv nao expuser uma delas, o erro fica no log e a reproducao
  // continua em vez de inventar um selo HDR.
  set_property("target-colorspace-hint", "no");
  set_property("target-prim", "bt.709");
  set_property("target-trc", "bt.1886");
}

void video_encerrar(void) {
  if (V.render) {
    mpv_render_context_set_update_callback(V.render, NULL, NULL);
    mpv_render_context_free(V.render);
    V.render = NULL;
  }
  if (V.fbo) glDeleteFramebuffers(1, &V.fbo);
  if (V.frameTex) glDeleteTextures(1, &V.frameTex);
  V.fbo = V.frameTex = 0;
  compose_shutdown();
  if (V.mpv) {
    mpv_terminate_destroy(V.mpv);
    V.mpv = NULL;
  }
  memset(&V, 0, sizeof V);
  V.subCurrent = -1;
}

#endif
