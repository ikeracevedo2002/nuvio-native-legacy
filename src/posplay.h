// PÓS-REPRODUÇÃO: o que aparece quando o título está acabando.
//
// Portado do `postPlayRecommendationController` do app web 1.0.6, que por sua
// vez veio do Android TV. As regras de QUANDO aparecer sao as de la, medidas no
// fonte e nao escolhidas aqui:
//   - FILME: quando o progresso passa de 90% da duracao
//     (DEFAULT_POST_PLAY_MOVIE_THRESHOLD_PERCENT).
//   - SERIE: nos ultimos segundos, com contagem regressiva de 5
//     (POST_PLAY_RECOMMENDATION_FINAL_COUNTDOWN_SECONDS).
//   - Em qualquer caso, no fim do fluxo.
//
// O QUE MOSTRA e diferente do web, e de proposito:
//   - SERIE -> o PROXIMO EPISODIO, com contagem para tocar sozinho. E o que
//     mais vale numa serie, e temos o dado: a lista de episodios agora e unica
//     e cobre todas as temporadas.
//   - FILME -> os RELACIONADOS que o extras.c ja busca do Trakt ao abrir o
//     titulo. Sem trailer: nao ha reprodutor de YouTube neste port, e o web usa
//     um proxy local que nos nao temos.
#ifndef NV_POSPLAY_H
#define NV_POSPLAY_H
#include <SDL2/SDL.h>

// Chamada por quadro pelo player, com a posicao e a duracao correntes.
void posplay_atualizar(float dt, Uint32 agora, double posSeg, double durSeg,
                       int ehSerie, int idxCatalogo);
int  posplay_visivel(void);
// 1 se consumiu a tecla.
int  posplay_evento(const SDL_Event *e);
void posplay_desenhar(Uint32 agora);
// Fecha e zera. Chamado quando o player abre outra coisa.
void posplay_fechar(void);

// Pedidos para o roteador, consumidos uma vez:
// proximo episodio (temporada/episodio) ou titulo relacionado (indice).
int  posplay_pediu_episodio(int *temporada, int *episodio);
int  posplay_pediu_titulo(void);
#endif
