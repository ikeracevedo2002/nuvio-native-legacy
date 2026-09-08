#include "webp.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int      (*FnInfo)(const uint8_t *, size_t, int *, int *);
typedef uint8_t *(*FnRgba)(const uint8_t *, size_t, int *, int *);
typedef void     (*FnFree)(void *);
static FnInfo pInfo; static FnRgba pRgba; static FnFree pFree;
static int tentado;

static void abrir(void) {
  static const char *nomes[] = {
    "libwebp.so.7", "libwebp.so", "libwebp.7.dylib", "libwebp.dylib",
    "@rpath/libwebp.7.dylib", "@rpath/libwebp.dylib", NULL
  };
  void *h = NULL; int i;
  tentado = 1;
  for (i = 0; nomes[i] && !h; i++) h = dlopen(nomes[i], RTLD_NOW);
  if (!h) { printf("[webp] libwebp ausente; .webp nao vai decodificar\n"); return; }
  pInfo = (FnInfo)dlsym(h, "WebPGetInfo");
  pRgba = (FnRgba)dlsym(h, "WebPDecodeRGBA");
  pFree = (FnFree)dlsym(h, "WebPFree");   // ausente em libwebp antiga: free() serve
  if (!pInfo || !pRgba) { printf("[webp] libwebp sem WebPDecodeRGBA\n"); pInfo = NULL; pRgba = NULL; }
}

SDL_Surface *webp_carregar(const char *caminho) {
  FILE *f; long n; unsigned char *dados; int w = 0, h = 0; uint8_t *px; SDL_Surface *s;
  if (!caminho) return NULL;
  f = fopen(caminho, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
  if (n < 16 || n > 32L * 1024 * 1024) { fclose(f); return NULL; }
  dados = malloc((size_t)n);
  if (!dados || fread(dados, 1, (size_t)n, f) != (size_t)n) { free(dados); fclose(f); return NULL; }
  fclose(f);
  if (memcmp(dados, "RIFF", 4) || memcmp(dados + 8, "WEBP", 4)) { free(dados); return NULL; }
  if (!tentado) abrir();
  if (!pRgba || !pInfo(dados, (size_t)n, &w, &h) || w < 1 || h < 1) { free(dados); return NULL; }
  px = pRgba(dados, (size_t)n, &w, &h);
  free(dados);
  if (!px) return NULL;
  // ABGR8888 no SDL = bytes R,G,B,A na memoria em little-endian, que e o que
  // WebPDecodeRGBA entrega. Copia para uma superficie propria: a do SDL_..From
  // apontaria para memoria da libwebp.
  s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
  if (s) {
    int y;
    for (y = 0; y < h; y++) memcpy((char *)s->pixels + y * s->pitch, px + (size_t)y * w * 4, (size_t)w * 4);
  }
  if (pFree) pFree(px); else free(px);
  return s;
}
