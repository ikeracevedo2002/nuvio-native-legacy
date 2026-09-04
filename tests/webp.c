// Decodifica um WebP real (capa de colecao do CDN do Xperience) pela libwebp
// do sistema, sem SDL_image.
#include "../src/webp.h"
#include <assert.h>
#include <stdio.h>
int main(int argc, char **argv) {
  SDL_Surface *s = webp_carregar(argc > 1 ? argv[1] : "tests/amostra.webp");
  assert(s && s->w > 0 && s->h > 0 && s->format->format == SDL_PIXELFORMAT_ABGR8888);
  printf("ok  webp %dx%d\n", s->w, s->h);
  SDL_FreeSurface(s);
  assert(webp_carregar("tests/webp.c") == NULL);   // nao e WebP: NULL, sem alarde
  puts("webp: tudo ok");
  return 0;
}
