// Smoke test de libmpv embebido en SDL2/OpenGL.
// Uso: macos_libmpv_smoke /ruta/a/video.mp4
#if !defined(__APPLE__)
#include <stdio.h>
int main(void) {
  fprintf(stderr, "macos_libmpv_smoke requiere macOS\n");
  return 2;
}
#else

#define GL_SILENCE_DEPRECATION 1
#include <SDL2/SDL.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *get_proc(void *ctx, const char *name) {
  (void)ctx;
  return SDL_GL_GetProcAddress(name);
}

static int property_flag(mpv_handle *mpv, const char *name) {
  int value = 0;
  if (mpv_get_property(mpv, name, MPV_FORMAT_FLAG, &value) < 0) return 0;
  return value != 0;
}

static double property_double(mpv_handle *mpv, const char *name) {
  double value = 0.0;
  if (mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, &value) < 0) return 0.0;
  return value;
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : getenv("NUVIO_SMOKE_VIDEO");
  SDL_Window *window = NULL;
  SDL_GLContext gl = NULL;
  mpv_handle *mpv = NULL;
  mpv_render_context *render = NULL;
  mpv_opengl_init_params gl_init;
  mpv_render_param params[3];
  mpv_render_param draw_params[3];
  mpv_opengl_fbo fbo;
  char *api = MPV_RENDER_API_TYPE_OPENGL;
  int flip = 1;
  int loaded = 0, paused = 0, sought = 0, resumed = 0, running = 1;
  Uint32 start, now;
  double duration, position;
  int audio;
  const char *load[] = { "loadfile", path ? path : "", "replace", NULL };

  if (!path || !path[0]) {
    fprintf(stderr, "uso: %s /ruta/a/video\n", argv[0]);
    return 2;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  window = SDL_CreateWindow("Nuvio libmpv smoke", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 960, 540,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window) { fprintf(stderr, "janela: %s\n", SDL_GetError()); goto fail; }
  gl = SDL_GL_CreateContext(window);
  if (!gl) { fprintf(stderr, "GL: %s\n", SDL_GetError()); goto fail; }
  SDL_GL_SetSwapInterval(0);

  mpv = mpv_create();
  if (!mpv) { fprintf(stderr, "mpv_create falhou\n"); goto fail; }
  mpv_set_option_string(mpv, "config", "no");
  mpv_set_option_string(mpv, "vo", "libmpv");
  mpv_set_option_string(mpv, "gpu-api", "opengl");
  mpv_set_option_string(mpv, "hwdec", "auto");
  mpv_set_option_string(mpv, "osc", "no");
  mpv_set_option_string(mpv, "terminal", "no");
  mpv_set_option_string(mpv, "input-default-bindings", "no");
  mpv_set_option_string(mpv, "keep-open", "yes");
  if (mpv_initialize(mpv) < 0) { fprintf(stderr, "mpv_initialize falhou\n"); goto fail; }

  gl_init.get_proc_address = get_proc;
  gl_init.get_proc_address_ctx = NULL;
  params[0].type = MPV_RENDER_PARAM_API_TYPE;
  params[0].data = api;
  params[1].type = MPV_RENDER_PARAM_OPENGL_INIT_PARAMS;
  params[1].data = &gl_init;
  params[2].type = MPV_RENDER_PARAM_INVALID;
  params[2].data = NULL;
  if (mpv_render_context_create(&render, mpv, params) < 0) {
    fprintf(stderr, "mpv_render_context_create falhou\n"); goto fail;
  }
  if (mpv_command(mpv, load) < 0) {
    fprintf(stderr, "loadfile falhou\n"); goto fail;
  }

  fbo.fbo = 0; fbo.w = 960; fbo.h = 540; fbo.internal_format = 0;
  draw_params[0].type = MPV_RENDER_PARAM_OPENGL_FBO;
  draw_params[0].data = &fbo;
  draw_params[1].type = MPV_RENDER_PARAM_FLIP_Y;
  draw_params[1].data = &flip;
  draw_params[2].type = MPV_RENDER_PARAM_INVALID;
  draw_params[2].data = NULL;
  start = SDL_GetTicks();
  while (running && (now = SDL_GetTicks()) - start < 8000) {
    SDL_Event e;
    mpv_event *ev;
    while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = 0;
    while ((ev = mpv_wait_event(mpv, 0.0)) && ev->event_id != MPV_EVENT_NONE) {
      if (ev->event_id == MPV_EVENT_FILE_LOADED) loaded = 1;
      if (ev->event_id == MPV_EVENT_END_FILE) running = 0;
    }
    if (loaded && now - start >= 2000 && !paused) {
      mpv_set_property_string(mpv, "pause", "yes"); paused = 1;
      printf("pause=ok\n");
    }
    if (loaded && now - start >= 3000 && !sought) {
      const char *seek[] = { "seek", "10", "absolute+exact", NULL };
      mpv_command(mpv, seek); sought = 1;
      printf("seek=ok\n");
    }
    if (loaded && now - start >= 4000 && !resumed) {
      mpv_set_property_string(mpv, "pause", "no"); resumed = 1;
      printf("resume=ok\n");
    }
    if (loaded) {
      int w, h;
      SDL_GL_GetDrawableSize(window, &w, &h);
      fbo.w = w; fbo.h = h;
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
      glViewport(0, 0, w, h);
      mpv_render_context_render(render, draw_params);
      SDL_GL_SwapWindow(window);
    }
    SDL_Delay(4);
  }
  duration = property_double(mpv, "duration");
  position = property_double(mpv, "time-pos");
  audio = property_flag(mpv, "audio-active");
  printf("loaded=%d duration=%.3f position=%.3f audio-active=%d\n",
         loaded, duration, position, audio);
  if (!loaded || !paused || !sought || !resumed || duration <= 0.0 || !audio) {
    fprintf(stderr, "smoke falhou: carregamento/audio/controles incompletos\n");
    goto fail;
  }
  mpv_render_context_free(render);
  mpv_terminate_destroy(mpv);
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;

fail:
  if (render) mpv_render_context_free(render);
  if (mpv) mpv_terminate_destroy(mpv);
  if (gl) SDL_GL_DeleteContext(gl);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
  return 1;
}

#endif
