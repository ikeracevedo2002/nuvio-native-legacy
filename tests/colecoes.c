// Colecoes da conta no shape do web -> ColFolder, e a chave de fileira por id.
#include "../src/colecoes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  const char *web =
    "[{\"collections_json\":{\"collections\":[{\"id\":\"c1\",\"title\":\"Streaming\",\"backdropImageUrl\":\"https://img/bg.jpg\","
    "\"folders\":[{\"id\":\"f1\",\"title\":\"Netflix\",\"coverImageUrl\":\"https://img/nf.jpg\",\"titleLogoUrl\":\"https://img/nf.png\",\"hideTitle\":true,"
    "\"sources\":[{\"provider\":\"addon\",\"addonId\":\"x\",\"addonBaseUrl\":\"https://addon/abc/manifest.json\",\"type\":\"movie\",\"catalogId\":\"nf_movies\",\"title\":\"Movies\",\"genre\":\"None\"},"
    "{\"provider\":\"tmdb\",\"tmdbSourceType\":\"DISCOVER\"},"
    "{\"addonBaseUrl\":\"https://addon/abc\",\"type\":\"series\",\"catalogId\":\"nf_series\",\"catalogName\":\"Series\"}]},"
    "{\"id\":\"f2\",\"title\":\"Vazia\",\"sources\":[]}]}]}}]";
  assert(col_definir_json(web) == 1);
  const ColFolder *f = col_folder(0);
  assert(f && !strcmp(f->group, "Streaming") && !strcmp(f->groupId, "c1") && !strcmp(f->id, "f1"));
  assert(!strcmp(f->hero, "https://img/bg.jpg") && !strcmp(f->cover, "https://img/nf.jpg") && f->hideTitle == 1);
  assert(f->nSources == 2);
  assert(!strcmp(f->sources[0].base, "https://addon/abc") && !strcmp(f->sources[0].catId, "nf_movies") && !f->sources[0].genre[0]);
  assert(!strcmp(f->sources[1].title, "Series") && !strcmp(f->sources[1].type, "series"));
  puts("ok  shape do web: linha da RPC, manifest.json cortado, tmdb fora, genre None vazio");

  char chave[192];
  col_chave_grupo("Streaming", chave, sizeof chave); assert(!strcmp(chave, "collection_c1"));
  col_chave_grupo("Outro", chave, sizeof chave);     assert(!strcmp(chave, "collection_Outro"));
  puts("ok  chave por id da colecao, nome quando nao ha id");

  // string escapada, como parseRemoteCollectionsPayload aceita
  const char *esc = "{\"collections_json\":\"{\\\"collections\\\":[{\\\"id\\\":\\\"c9\\\",\\\"title\\\":\\\"T\\\",\\\"folders\\\":[{\\\"id\\\":\\\"g\\\",\\\"title\\\":\\\"G\\\",\\\"sources\\\":[{\\\"addonBaseUrl\\\":\\\"https://a\\\",\\\"type\\\":\\\"movie\\\",\\\"catalogId\\\":\\\"k\\\"}]}]}]}\"}";
  assert(col_definir_json(esc) == 1 && !strcmp(col_folder(0)->groupId, "c9"));
  puts("ok  collections_json como string escapada");

  // vazio nao apaga
  assert(col_definir_json("{\"collections\":[]}") == 0 && col_n() == 1);
  puts("ok  vazio mantem o que havia");
  puts("colecoes: tudo ok");
  return 0;
}
