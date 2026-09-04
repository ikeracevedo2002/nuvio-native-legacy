#include "pausao.h"
#include "catalogo.h"
#include "ajustes.h"
#include "gfx.h"
#include "text.h"
#include "anim.h"
#include "layout.h"
#include <stdio.h>
#include <string.h>

// Os cinco segundos do web (playerScreen.js:567). Nao e um numero de gosto: e o
// que separa "parei um instante" de "parei para ler". Encurtar faz o painel
// pular na cara de quem so ajustou o volume.
#define PAUSAO_ESPERA_MS  5000u

// Faixa DISCRETA, ancorada na base, e nao a tela cheia que o web pinta. Duas
// razoes: aqui o painel toma o lugar dos controles (que nesta tela nao somem
// sozinhos enquanto pausado, player.c:767), e cobrir o quadro inteiro apagaria
// justamente o frame que a pessoa parou para olhar. O veu de baixo ja e o mesmo
// GFX_VEU_BAIXO da barra de controles — nao ha efeito novo nem passada extra.
#define PAUSAO_VEU_H     520.0f
#define PAUSAO_X          96.0f    // mesmo recuo do conteudo do rodape (PLR_MARGEM)
#define PAUSAO_Y0        636.0f
#define PAUSAO_LARG     1160.0f    // largura util do texto; sobra do lado direito
#define PAUSAO_CHIP_H     44.0f
#define PAUSAO_CHIP_PAD   18.0f
#define PAUSAO_CHIP_GAP   10.0f
// Teto do desenho: abaixo disto o conteudo entraria na zona que a TV corta por
// overscan. Quem nao couber simplesmente nao e desenhado — encolher a fonte
// para caber daria uma linha ilegivel a tres metros de distancia.
#define PAUSAO_BASE      (NV_TELA_H - 96.0f)

// Quantos nomes de elenco cabem. O web para em oito (:568); aqui o teto e o do
// dado, nao o do layout: CatItem guarda seis.
#define PAUSAO_ELENCO_MAX 6

static int    visivel;
static float  anim;            // 0..1, a entrada por mola
static Uint32 desdeQuando;     // quando a condicao passou a valer; 0 = nao vale
static int    idxItem = -1;
static char   epLinha[220];

void pausao_fechar(void) {
  visivel = 0;
  anim = 0.0f;
  desdeQuando = 0;
  idxItem = -1;
  epLinha[0] = 0;
}

void pausao_atualizar(float dt, Uint32 agora, int podeSubir, int idx,
                      const char *linhaEp) {
  idxItem = idx;
  snprintf(epLinha, sizeof epLinha, "%s", linhaEp ? linhaEp : "");

  // O ajuste e consultado AQUI e nao na abertura: desligar a opcao com o painel
  // de pe tem de derrubar o painel, e nao valer so no filme seguinte.
  if (!podeSubir || !ajustes_pausa_overlay()) {
    // schedulePauseOverlay/syncPauseOverlayState (:7397): condicao que cai
    // derruba o painel e ZERA o relogio. Rearmar de onde parou faria uma
    // sequencia de pausas curtas somar cinco segundos e o painel subir sozinho
    // no meio de uma cena.
    visivel = 0;
    desdeQuando = 0;
  } else {
    if (!desdeQuando) desdeQuando = agora;
    if (!visivel && agora - desdeQuando >= PAUSAO_ESPERA_MS) visivel = 1;
  }

  anim = anim_mola(anim, visivel ? 1.0f : 0.0f, dt,
                   visivel ? NV_MOLA_FOCO : NV_MOLA_DESFOCO);
  if (!visivel && anim < 0.004f) anim = 0.0f;
}

// Enquanto o painel ainda esta saindo ele continua desenhado, mas ja NAO e
// visivel para quem pergunta: se fosse, o player manteria os controles
// recolhidos durante a saida e a barra so voltaria depois do fade.
int pausao_visivel(void) { return visivel; }

int pausao_evento(const SDL_Event *e) {
  SDL_Keycode k;
  if (!visivel || !e || e->type != SDL_KEYDOWN) return PAUSAO_LIVRE;
  k = e->key.keysym.sym;

  // O Back NAO e tratado aqui, e essa e uma divergencia deliberada do web. La
  // (:22138) o Back derruba o painel e ainda segue para a regra seguinte;
  // aqui o Back e a unica saida da reproducao, e roubar o primeiro toque para
  // fechar um painel informativo faria a pessoa apertar duas vezes para sair de
  // um filme. Quem quer sair, sai.
  if (k == SDLK_ESCAPE || k == SDLK_AC_BACK || k == SDLK_BACKSPACE ||
      k == SDLK_DELETE) return PAUSAO_LIVRE;

  visivel = 0;
  desdeQuando = 0;

  // playerScreen.js:22212 — OK/Play com o painel de pe derruba o painel E
  // retoma. E o gesto obvio: quem esta olhando a ficha e aperta o centro quer
  // voltar ao filme, nao so fechar uma caixa.
  if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE)
    return PAUSAO_RETOMAR;

  // Qualquer outra tecla (:22218): derruba o painel e devolve os controles. O
  // relogio dos 5s recomeca sozinho no proximo `pausao_atualizar`, porque
  // `desdeQuando` foi zerado — que e o `schedulePauseOverlay()` do web.
  return PAUSAO_CONSUMIU;
}

void pausao_desenhar(Uint32 agora) {
  const CatItem *c;
  float a = anim, y, sobe;
  char meta[192];
  int i;
  (void)agora;

  if (a <= 0.004f) return;
  c = cat_item(idxItem);
  if (!c) return;

  // Sobe 24px entrando. E o unico movimento do painel: sem ele o bloco aparece
  // de estalo e se le como falha de desenho, e com mais do que isso o texto
  // ainda esta andando quando ja da para tentar ler.
  sobe = (1.0f - a) * 24.0f;

  { GfxRect veu = { 0, NV_TELA_H - PAUSAO_VEU_H, NV_TELA_W, PAUSAO_VEU_H };
    gfx_rect(veu, 0, GFX_VEU_BAIXO, 0, 0, 0, 0.0f, 0, 0, 0, 0.90f * a); }

  y = PAUSAO_Y0 + sobe;

  // "Você está assistindo" — o .player-pause-kicker do web (:7465). Existe para
  // o bloco inteiro ter um sujeito: sem ele o titulo solto sobre a cena parece
  // um aviso do aparelho, e nao a ficha do que esta tocando.
  { TxtLinha l = txt_linha(TXT_PLR_CORPO, "Você está assistindo",
                           214, 216, 222, 255);
    txt_desenhar_alpha(l, PAUSAO_X, y, a * 0.62f);
    y += l.h + 10.0f; }

  { TxtLinha l = txt_linha_corta(TXT_TITULO2, c->titulo, 255, 255, 255, 255,
                                 PAUSAO_LARG);
    txt_desenhar_alpha(l, PAUSAO_X, y, a);
    y += l.h + 8.0f; }

  // Linha de meta. No web sao "ano • SxEy" (:7467); aqui o ano vem dentro de
  // CatItem.meta ("2022 · 3 temporadas"), que ja e a linha pronta que o resto
  // do app mostra — reparti-la para extrair so o ano daria menos informacao
  // pelo mesmo espaco. O codigo do episodio sai do `linhaEp` que o player ja
  // monta, cortado no separador para nao repetir o nome do episodio, que vem na
  // linha de baixo.
  meta[0] = 0;
  if (c->meta[0]) snprintf(meta, sizeof meta, "%s", c->meta);
  if (epLinha[0]) {
    const char *sep = strstr(epLinha, " · ");
    size_t n = sep ? (size_t)(sep - epLinha) : strlen(epLinha);
    if (n > 0 && n < 32) {
      char cod[32];
      snprintf(cod, sizeof cod, "%.*s", (int)n, epLinha);
      if (meta[0]) {
        char junto[192];
        snprintf(junto, sizeof junto, "%s · %s", meta, cod);
        snprintf(meta, sizeof meta, "%s", junto);
      } else {
        snprintf(meta, sizeof meta, "%s", cod);
      }
    }
  }
  if (meta[0]) {
    TxtLinha l = txt_linha_corta(TXT_PG_FIM, meta, 232, 234, 240, 255,
                                 PAUSAO_LARG);
    txt_desenhar_alpha(l, PAUSAO_X, y, a * 0.72f);
    y += l.h + 6.0f;
  }

  // Nome do episodio (.player-pause-episode-title, :7468). So a parte depois do
  // separador: o "T1E4" ja foi para a linha de cima.
  { const char *sep = epLinha[0] ? strstr(epLinha, " · ") : NULL;
    if (sep && sep[3]) {
      TxtLinha l = txt_linha_corta(TXT_PLR_CORPO, sep + 3, 240, 241, 246, 255,
                                   PAUSAO_LARG);
      txt_desenhar_alpha(l, PAUSAO_X, y, a * 0.90f);
      y += l.h + 14.0f;
    } }

  // Sinopse em DUAS linhas (o web nao limita, mas la o painel e a tela toda).
  // Duas e o que cabe nesta faixa sem empurrar o elenco para fora do quadro.
  if (c->sinopse[0]) {
    float alt = txt_bloco(TXT_DET_SIN, c->sinopse, 214, 216, 222,
                          PAUSAO_X, y, PAUSAO_LARG, 8.0f, a * 0.84f, 2);
    y += alt + 20.0f;
  }

  // ELENCO. Pastilhas so com o NOME, como o .player-pause-cast-chip do web
  // (:7480) — o papel do personagem esta no CatItem, mas o web nao o mostra
  // nesta tela e acrescenta-lo dobraria a largura de cada pastilha.
  if (c->nElenco > 0 && y + 30.0f + PAUSAO_CHIP_H < PAUSAO_BASE) {
    float x = PAUSAO_X;
    { TxtLinha l = txt_linha(TXT_MINI, "Elenco", 226, 228, 234, 255);
      txt_desenhar_alpha(l, PAUSAO_X, y, a * 0.50f);
      y += l.h + 8.0f; }
    for (i = 0; i < c->nElenco && i < PAUSAO_ELENCO_MAX; i++) {
      TxtLinha l = txt_linha(TXT_MINI, c->elenco[i].nome, 240, 241, 246, 255);
      float w = (float)l.w + PAUSAO_CHIP_PAD * 2.0f;
      GfxRect chip;
      if (x + w > PAUSAO_X + PAUSAO_LARG) break;   // uma fileira so
      chip.x = x; chip.y = y; chip.w = w; chip.h = PAUSAO_CHIP_H;
      // Raio e FRACAO do menor lado nesta API (ver gfx.h): 0.5 e a pilula.
      gfx_cor(chip, 0.5f, 1, 1, 1, 0.10f * a);
      txt_desenhar_alpha(l, x + PAUSAO_CHIP_PAD,
                         y + (PAUSAO_CHIP_H - (float)l.h) * 0.5f, a * 0.92f);
      x += w + PAUSAO_CHIP_GAP;
    }
  }
}
