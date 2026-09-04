#include "posplay.h"
#include "catalogo.h"
#include "extras.h"
#include "gfx.h"
#include "text.h"
#include "tex_cache.h"
#include "layout.h"
#include "anim.h"
#include "ajustes.h"
#include "detail.h"
#include "descoberta.h"
#include "video.h"
#include <stdio.h>
#include <string.h>

// Constantes do web 1.0.6 (postPlayRecommendationController), nao escolhidas
// aqui: 90% para filme, 5 s de contagem final.
// Trocado pelo tempo restante: ver a nota em posplay_atualizar. Fica registrado
// que o web usa 90% para nao parecer que o numero se perdeu.
#define PP_FILME_FIM_S    180.0
#define PP_CONTAGEM_S     5

// Cartazes dos relacionados, no tamanho da grade de "Ver tudo".
#define PP_CARD_W  212.0f
#define PP_CARD_H  318.0f
#define PP_GAP      24.0f
#define PP_MAX       5
#define PP_PAD      32.0f
#define PP_ROTULO_H 44.0f
// Cartao do proximo episodio, no molde do de episodios.c (thumb 184x130),
// ampliado para a distancia de quem esta deitado no sofa.
#define PP_EP_W     980.0f
#define PP_EP_H     260.0f
#define PP_EP_PAD    18.0f
#define PP_EP_THUMB_W 320.0f
#define PP_EP_RAIO    24.0f
// PASSO entre linhas, nao vao: txt_bloco poe a linha i em y + i*leading.
#define PP_LD_SIN     28.0f

static int    visivel, serie, idx = -1, foco;
// DISPENSADO GRUDA. Sem isto o Voltar fechava o painel e o quadro seguinte o
// reabria na hora, porque a condicao de aparecer (passar de 90% do filme)
// continua verdadeira ate o fim — foi o "nao da pra sair, quebra tudo" que o
// dono viu. So volta a valer quando a reproducao SAI da zona, ou quando o
// player abre outro titulo.
static int    dispensado;
static float  anim;
static Uint32 fecharEm;              // 0 = sem contagem
static int    pedT, pedE, pedTitulo = -1;
static int    proxT, proxE;          // proximo episodio, quando ha
static char   proxNome[120];

int posplay_visivel(void) { return visivel; }

void posplay_fechar(void) {
  visivel = 0; fecharEm = 0; foco = 0;
  pedT = pedE = 0; pedTitulo = -1;
  dispensado = 0;   // titulo novo: a dispensa do anterior nao vale mais
}

int posplay_pediu_episodio(int *t, int *e) {
  if (!pedT) return 0;
  if (t) *t = pedT;
  if (e) *e = pedE;
  pedT = pedE = 0;
  return 1;
}
int posplay_pediu_titulo(void) { int v = pedTitulo; pedTitulo = -1; return v; }

// Abertura A PEDIDO, pelo botao do player. Existe porque dispensar passou a
// grudar: sem uma porta de volta, quem apertasse Voltar uma vez nao veria mais
// os relacionados naquele filme. Limpa a dispensa de proposito — o pedido
// explicito vale mais que a recusa anterior.
int posplay_abrir_relacionados(int idxCatalogo) {
  if (extras_n_relacionados() <= 0) return 0;
  idx = idxCatalogo;
  serie = 0;
  foco = 0;
  fecharEm = 0;
  dispensado = 0;
  visivel = 1;
  return 1;
}

// O episodio SEGUINTE ao que esta tocando, na lista unica (ja ordenada por
// temporada e episodio). Devolve 0 quando o que toca e o ultimo.
static int acharProximo(int idxItem, int t, int e) {
  int n = cat_n_episodios(idxItem), i;
  for (i = 0; i < n; i++) {
    const CatEp *ep = cat_episodio(idxItem, i);
    if (!ep || ep->temporada != t || ep->episodio != e) continue;
    { const CatEp *px = cat_episodio(idxItem, i + 1);
      if (!px) return 0;
      proxT = px->temporada; proxE = px->episodio;
      snprintf(proxNome, sizeof proxNome, "%s", px->nome);
      return 1; }
  }
  return 0;
}

void posplay_atualizar(float dt, Uint32 agora, double posSeg, double durSeg,
                       int ehSerie, int idxCatalogo, int janelaSerie) {
  int deveAparecer = 0;
  double creditosSeg = video_creditos();
  anim = anim_mola(anim, visivel ? 1.0f : 0.0f, dt, NV_MOLA_TELA);
  if (durSeg <= 1.0) return;

  if (ehSerie) {
    // A JANELA quem decide e o player: ele e o unico que sabe se os creditos
    // comecaram (intro_ativo/INTRO_CREDITOS) alem dos dois minutos finais. Era
    // a mesma condicao que a caixa antiga de "Próximo episódio" usava, e passar
    // a usa-la aqui foi o que permitiu apagar aquela caixa — havia DUAS
    // interfaces de proximo episodio na mesma tela, uma feia e uma nova.
    //
    // A CONTAGEM continua sendo a do web: so nos 5 s finais. Aparecer cedo e
    // util; comecar a contar cedo tiraria do dono o fim do episodio.
    deveAparecer = janelaSerie;
  } else if (creditosSeg > 1.0 && posSeg >= creditosSeg) {
    // O MARCADOR DE VERDADE, quando o arquivo o traz: o capitulo de creditos do
    // proprio Matroska. Nao e estimativa — e o segundo que o lancamento marcou.
    deveAparecer = 1;
  } else if (creditosSeg > 1.0) {
    // Ha marcador e ele ainda nao chegou: NAO cair no plano B. Os dois juntos
    // fariam o painel subir antes do capitulo, que e o defeito que o marcador
    // existe para resolver.
    deveAparecer = 0;
  } else {
    // FILME SEM CAPITULOS: os ultimos minutos, e nao os 90% do web.
    //
    // O dono viu o painel subir ANTES dos creditos e pediu "o marcador correto
    // de onde comecam os creditos". ELE NAO EXISTE PARA FILME, e e melhor
    // dizer isso do que fingir: o unico marcador deste app vem do introdb, que
    // e pedido em player.c:222 SO para serie e cuja API exige temporada e
    // episodio (intro.c:28). Filme nunca teve segmento nenhum.
    //
    // Entao a regra do web e trocada por uma que erra menos. 90% de um filme de
    // 105 min sao dez minutos antes do fim — foi o que apareceu cedo demais.
    // Os creditos finais raramente passam de tres minutos, e um painel que sobe
    // dentro deles incomoda menos do que um que rouba o desfecho.
    double resta = durSeg - posSeg;
    deveAparecer = (resta > 0.0 && resta <= (double)PP_FILME_FIM_S);
  }

  // Saiu da zona (o dono voltou o filme): a dispensa perde a validade e o
  // painel pode subir de novo quando ele chegar ao fim outra vez.
  if (!deveAparecer) dispensado = 0;

  if (deveAparecer && !visivel && !dispensado) {
    idx = idxCatalogo;
    serie = ehSerie;
    foco = 0;
    proxT = proxE = 0; proxNome[0] = 0;
    if (serie) {
      int t = 0, e = 0;
      const CatItem *ci = cat_item(idx);
      // Qual episodio esta tocando: o que o proprio item guarda.
      if (ci) { t = ci->temporada; e = ci->episodio; }
      if (t > 0 && e > 0 && acharProximo(idx, t, e)) {
        fecharEm = 0;            // a contagem entra so nos segundos finais
        visivel = 1;
      }
      // Sem proximo episodio (fim da serie): nao aparece nada. Mostrar um
      // painel vazio no ultimo episodio seria pior que nao mostrar.
    } else if (extras_n_relacionados() > 0) {
      fecharEm = 0;              // filme nao tem contagem: o dono escolhe
      visivel = 1;
    }
  }

  // A contagem so vale para o proximo episodio.
  if (visivel && fecharEm && agora >= fecharEm) {
    pedT = proxT; pedE = proxE;
    posplay_fechar();
  }
}

int posplay_evento(const SDL_Event *e) {
  int k;
  if (!visivel || e->type != SDL_KEYDOWN) return 0;
  k = e->key.keysym.sym;
  if (k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE ||
      e->key.keysym.scancode == NV_SCANCODE_BACK) {
    // Dispensar CANCELA a contagem e deixa o video terminar em paz. E GRUDA:
    // fechar sem marcar fazia o painel voltar no quadro seguinte.
    dispensado = 1;
    posplay_fechar();
    dispensado = 1;              // posplay_fechar zera; aqui a marca e o ponto
    return 1;
  }
  // BAIXO tira o painel do caminho E devolve os controles. Pedido do dono: com
  // o painel no ar ele quer poder descer para a barra de tempo sem perder a
  // reproducao. Voltar apenas dispensa; BAIXO dispensa e mostra o player.
  if (k == SDLK_DOWN) {
    dispensado = 1;
    posplay_fechar();
    dispensado = 1;
    return 2;
  }
  if (serie) {
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
      pedT = proxT; pedE = proxE; posplay_fechar(); return 1;
    }
    return 0;
  }
  { int n = extras_n_relacionados();
    if (n > PP_MAX) n = PP_MAX;
    if (k == SDLK_RIGHT && foco + 1 < n) { foco++; return 1; }
    if (k == SDLK_LEFT  && foco > 0)     { foco--; return 1; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
      const char *id = extras_relacionado_imdb(foco);
      int alvo = id[0] ? cat_indice_por_imdb(id) : -1;
      if (alvo >= 0) { pedTitulo = alvo; posplay_fechar(); }
      // NAO ESTA NO CATALOGO LOCAL, que e o caso NORMAL: o relacionado vem do
      // Trakt e o catalogo tem os titulos das fileiras da home. Antes o OK
      // simplesmente nao fazia nada — o "nao da pra clicar" do relatorio.
      // desc_pedir_titulo e o mesmo caminho que a pagina de titulo ja usa
      // (detail.c:938) para abrir um relacionado que ainda nao temos.
      else if (id[0]) { desc_pedir_titulo(id); posplay_fechar(); }
      return 1;
    } }
  return 0;
}

void posplay_desenhar(Uint32 agora, float baseY) {
  float a = anim, x = NV_DETP_X;
  if (a < 0.01f) return;

  if (serie) {
    // CARTAO DE EPISODIO, e nao a caixa com texto solto da primeira versao. O
    // dono pediu "igual o card de episodios": still a esquerda, selo T#E# sobre
    // ele, nome e estado a direita. As medidas sao as de episodios.c (thumb
    // 184x130) multiplicadas para a distancia de um painel de fim de episodio.
    const CatItem *ci = cat_item(idx);
    const CatEp *px = NULL;
    int i, n = cat_n_episodios(idx);
    float cardW = PP_EP_W, cardH = PP_EP_H;
    float cy = baseY - cardH;
    float sobe = (1.0f - a) * 20.0f;
    int resta = fecharEm > agora ? (int)((fecharEm - agora + 999) / 1000) : 0;
    char cab[64], num[40];
    GfxRect card, tr;

    for (i = 0; i < n; i++) {
      const CatEp *e = cat_episodio(idx, i);
      if (e && e->temporada == proxT && e->episodio == proxE) { px = e; break; }
    }
    cy += sobe;

    { TxtLinha t;
      if (resta > 0) snprintf(cab, sizeof cab, "A seguir em %d s", resta);
      else           snprintf(cab, sizeof cab, "A seguir");
      t = txt_linha(TXT_DET_META2, cab, 214, 216, 222, 255);
      txt_desenhar_alpha(t, x, cy - t.h - 12.0f, a * 0.92f); }

    // O cartao SEMPRE tem foco: ele e o unico alvo desta tela. Sem o anel ele
    // nao se le como algo que responde ao OK — o "nao ta pra clicar" do
    // relatorio era metade dado (o OK ja funcionava) e metade aparencia.
    card.x = x; card.y = cy; card.w = cardW; card.h = cardH;
    { GfxRect anel = { card.x - 3, card.y - 3, card.w + 6, card.h + 6 };
      gfx_cor(anel, PP_EP_RAIO / anel.h, .94f, .94f, .95f, a); }
    gfx_cor(card, PP_EP_RAIO / cardH, .085f, .085f, .095f, a);

    tr.x = card.x + PP_EP_PAD; tr.y = card.y + PP_EP_PAD;
    tr.w = PP_EP_THUMB_W; tr.h = cardH - PP_EP_PAD * 2.0f;
    { const char *arte = (px && px->thumb[0]) ? px->thumb
                       : (ci ? ci->backdrop : "");
      GLuint t = arte[0] ? tex_obter_larg(arte, tr.w) : 0;
      gfx_cor(tr, PP_EP_RAIO / tr.h, .19f, .19f, .20f, a);
      if (t) {
        gfx_tex_aspect_atual = tex_aspecto(arte);
        gfx_rect(tr, t, GFX_CARD, 0, 0, 0, PP_EP_RAIO / tr.h, 0, 0, 0, a);
        gfx_tex_aspect_atual = 0.0f;
      } }
    snprintf(num, sizeof num, "T%dE%d", proxT, proxE);
    { GfxRect selo = { tr.x + 10.0f, tr.y + tr.h - 44.0f, 96.0f, 34.0f };
      gfx_cor(selo, 0.2f, .025f, .025f, .03f, 0.92f * a);
      txt_desenhar_alpha(txt_linha(TXT_MINI, num, 240, 240, 242, 255),
                         selo.x + 14.0f, selo.y + 6.0f, a); }

    { float tx = tr.x + tr.w + 28.0f;
      float tw = card.w - (tx - card.x) - PP_EP_PAD;
      float ty = card.y + PP_EP_PAD + 4.0f;
      TxtLinha t = txt_linha_corta(TXT_TITULO3,
                                   (px && px->nome[0]) ? px->nome : num,
                                   255, 255, 255, 255, tw);
      txt_desenhar_alpha(t, tx, ty, a);
      ty += t.h + 8.0f;
      if (px && (px->data[0] || px->duracao[0])) {
        char est[96];
        snprintf(est, sizeof est, "%s%s%s", px->data,
                 px->data[0] && px->duracao[0] ? " · " : "", px->duracao);
        { TxtLinha l = txt_linha_corta(TXT_PG_FIM, est, 186, 188, 194, 255, tw);
          txt_desenhar_alpha(l, tx, ty, a * 0.92f);
          ty += l.h + 8.0f; } }
      if (px && px->sinopse[0])
        txt_bloco(TXT_PG_FIM, px->sinopse, 186, 188, 194, tx, ty, tw,
                  PP_LD_SIN, a * 0.88f, 2); }

    { TxtLinha t = txt_linha(TXT_DET_META2,
                             "OK para começar agora  ·  Baixo para o player  ·  Voltar para ficar",
                             150, 154, 163, 255);
      txt_desenhar_alpha(t, x, card.y + cardH + 14.0f, a * 0.85f); }
    return;
  }

  // FILME: os relacionados que o Trakt ja deu ao abrir o titulo.
  { int n = extras_n_relacionados(), i;
    float y, sobe = (1.0f - a) * 20.0f;
    TxtLinha cab;
    if (n > PP_MAX) n = PP_MAX;
    cab = txt_linha(TXT_ROW_TITULO, "Mais como este", 255, 255, 255, 255);
    y = baseY - PP_CARD_H - PP_ROTULO_H - (float)cab.h - 18.0f + sobe;

    // FUNDO PROPRIO. O gradiente do rodape e forte so na base, e esta fileira
    // vive acima dele: sobre uma cena clara os cartazes e os nomes sumiam. Um
    // painel com preenchimento proprio nao depende do que esta atras — mesma
    // decisao do painel de pausa.
    { GfxRect fundo = { x - PP_PAD, y - PP_PAD,
                        NV_TELA_W - (x - PP_PAD) * 2.0f,
                        (float)cab.h + 18.0f + PP_CARD_H + PP_ROTULO_H + PP_PAD * 2.0f };
      gfx_cor(fundo, PP_EP_RAIO / fundo.h, .04f, .04f, .05f, 0.86f * a); }

    txt_desenhar_alpha(cab, x, y, a);
    y += (float)cab.h + 18.0f;
    for (i = 0; i < n; i++) {
      float cx = x + (float)i * (PP_CARD_W + PP_GAP);
      const char *po = extras_relacionado_poster(i);
      GLuint t = po[0] ? tex_obter_larg(po, PP_CARD_W) : 0;
      float raio = ajustes_raio_poster_px() / PP_CARD_W;
      int sel = (i == foco);
      GfxRect r = { cx, y, PP_CARD_W, PP_CARD_H };
      if (sel) {
        GfxRect anel = { cx - 4, y - 4, PP_CARD_W + 8, PP_CARD_H + 8 };
        gfx_cor(anel, ajustes_raio_poster_px() / (PP_CARD_W + 8.0f), 1, 1, 1, a);
      }
      if (t) {
        gfx_tex_aspect_atual = tex_aspecto(po);
        gfx_rect(r, t, GFX_CARD, sel ? 1.0f : 0.0f, 0, 0, raio, 0, 0, 0, a);
        gfx_tex_aspect_atual = 0.0f;
      } else {
        // ESCURO, e o mesmo 0.133 que a pagina de titulo usa para o cartaz que
        // ainda nao chegou. O esqueleto claro que estava aqui virava um bloco
        // BRANCO no meio da cena — o dono fotografou cinco deles.
        gfx_cor(r, raio, .133f, .133f, .133f, a);
      }
      { int c = sel ? 255 : 214;
        TxtLinha l = txt_linha_corta(TXT_DET_META2, extras_relacionado_titulo(i),
                                     c, c, c, 255, PP_CARD_W);
        txt_desenhar_alpha(l, cx, y + PP_CARD_H + 10.0f, a * (sel ? 1.0f : 0.86f)); }
    }
    { TxtLinha t = txt_linha(TXT_DET_META2,
                             "OK para abrir  ·  Baixo para o player  ·  Voltar para dispensar",
                             150, 154, 163, 255);
      txt_desenhar_alpha(t, x, y + PP_CARD_H + PP_ROTULO_H - 4.0f, a * 0.85f); } }
}
