// GATILHO DE TESTE DO PLAYER TIZEN. Nao entra em nenhuma build de producao:
// src/*.c nao alcanca tools/, e so a linha de teste em tools/teste-avplay.sh
// acrescenta este arquivo.
//
// POR QUE ELE EXISTE. video_tizen.c nunca tinha sido EXECUTADO — nao ha TV
// Samsung nesta bancada e webapis.avplay nao existe no Chrome. Compilar e
// linkar nao prova nada sobre a ORDEM das chamadas nem sobre os argumentos, que
// e justamente onde o AVPlay e exigente: a Samsung documenta
// open -> setDisplayRect -> prepareAsync -> play, e o setDisplayRect quer
// coordenadas no espaco 1920x1080.
//
// O que ESTE teste prova: que o app emite essa sequencia, com esses argumentos,
// contra o duble de tools/fake-avplay.js. O que ele NAO prova, e nao ha como
// provar aqui: que o video decodifica, que o plano de hardware aparece atras da
// pagina, e que o container/codec do arquivo real e aceito.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdio.h>
#include "../src/video.h"

EMSCRIPTEN_KEEPALIVE
void nv_teste_avplay(void) {
  printf("[TESTE] --- inicio ---\n");

  printf("[TESTE] video_iniciar -> %d\n", video_iniciar());

  // A janela ANTES de tocar: e o que o video.h manda e o que faz o
  // setDisplayRect sair com o retangulo certo em vez de tela cheia por padrao.
  video_janela(0, 0, 1920, 1080);

  // Um MKV remux com HEVC, que e a forma tipica do link de debrid — e
  // exatamente o caso que a pesquisa apontou como risco de container.
  printf("[TESTE] video_tocar -> %d\n",
         video_tocar("https://exemplo.invalido/filme.2160p.remux.mkv"));

  printf("[TESTE] --- disparado; o resto sai pelo bombear ---\n");
  fflush(stdout);
}

// Segunda fase, chamada depois que o prepareAsync do duble respondeu: exercita
// o que so faz sentido com o player ja pronto.
EMSCRIPTEN_KEEPALIVE
void nv_teste_avplay_fase2(void) {
  printf("[TESTE] --- fase 2 ---\n");
  printf("[TESTE] pronto=%d tocando=%d dur=%.0f\n",
         video_pronto(), video_tocando(), video_duracao());
  printf("[TESTE] faixas: audio=%d legenda=%d\n",
         video_n_audio(), video_n_legenda());
  video_buscar(120.0);          // amortecido por SEEK_REPOUSO_MS
  video_pausar(1);
  video_janela(480, 270, 960, 540);   // recuo, o mesmo caminho do modo creditos
  fflush(stdout);
}

// Gatilho manual para testes que nao executam o laco do app. app_atualizar
// agora bombeia antes dos retornos de login/perfis/transicoes.
EMSCRIPTEN_KEEPALIVE
void nv_teste_bombear(void) { video_bombear(); }

EMSCRIPTEN_KEEPALIVE
void nv_teste_avplay_fim(void) {
  printf("[TESTE] --- fim ---\n");
  video_parar();
  fflush(stdout);
}
#endif
