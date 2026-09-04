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
#include <stdio.h>
#include <string.h>

// Constantes do web 1.0.6 (postPlayRecommendationController), nao escolhidas
// aqui: 90% para filme, 5 s de contagem final.
#define PP_FILME_PCT      0.90
#define PP_CONTAGEM_S     5

// Cartazes dos relacionados, no tamanho da grade de "Ver tudo".
#define PP_CARD_W  212.0f
#define PP_CARD_H  318.0f
#define PP_GAP      24.0f
#define PP_MAX       5

static int    visivel, serie, idx = -1, foco;
static float  anim;
static Uint32 fecharEm;              // 0 = sem contagem
static int    pedT, pedE, pedTitulo = -1;
static int    proxT, proxE;          // proximo episodio, quando ha
static char   proxNome[120];

int posplay_visivel(void) { return visivel; }

void posplay_fechar(void) {
  visivel = 0; fecharEm = 0; foco = 0;
  pedT = pedE = 0; pedTitulo = -1;
}

int posplay_pediu_episodio(int *t, int *e) {
  if (!pedT) return 0;
  if (t) *t = pedT;
  if (e) *e = pedE;
  pedT = pedE = 0;
  return 1;
}
int posplay_pediu_titulo(void) { int v = pedTitulo; pedTitulo = -1; return v; }

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
                       int ehSerie, int idxCatalogo) {
  int deveAparecer = 0;
  anim = anim_mola(anim, visivel ? 1.0f : 0.0f, dt, NV_MOLA_TELA);
  if (durSeg <= 1.0) return;

  if (ehSerie) {
    // SERIE: so nos ultimos segundos. Aparecer a 90% de um episodio de 50 min
    // cobriria cinco minutos de conteudo que o dono ainda esta assistindo.
    double resta = durSeg - posSeg;
    deveAparecer = (resta > 0.0 && resta <= (double)PP_CONTAGEM_S);
  } else {
    deveAparecer = (posSeg / durSeg) >= PP_FILME_PCT;
  }

  if (deveAparecer && !visivel) {
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
        // Contagem para tocar sozinho, como no web.
        fecharEm = agora + PP_CONTAGEM_S * 1000u;
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
    // Dispensar CANCELA a contagem e deixa o video terminar em paz.
    posplay_fechar();
    return 1;
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
      return 1;
    } }
  return 0;
}

void posplay_desenhar(Uint32 agora) {
  float a = anim, x = NV_DETP_X;
  if (a < 0.01f) return;

  // Faixa na base, nao tela cheia: o filme continua correndo atras e cobri-lo
  // por completo tira do dono a chance de simplesmente continuar assistindo.
  { float h = serie ? 300.0f : 560.0f;
    GfxRect r = { 0, NV_TELA_H - h, NV_TELA_W, h };
    gfx_rect(r, 0, GFX_VEU_BAIXO, 0, 0, 0, 0.0f, 0, 0, 0, 0.92f * a); }

  if (serie) {
    float y = NV_TELA_H - 240.0f;
    char cab[64];
    int resta = fecharEm > agora ? (int)((fecharEm - agora + 999) / 1000) : 0;
    if (resta > 0) snprintf(cab, sizeof cab, "A seguir em %d s", resta);
    else           snprintf(cab, sizeof cab, "A seguir");
    { TxtLinha t = txt_linha(TXT_DET_META2, cab, 179, 179, 179, 255);
      txt_desenhar_alpha(t, x, y, a * 0.9f); }
    { char rot[160];
      snprintf(rot, sizeof rot, "T%d:E%d%s%s", proxT, proxE,
               proxNome[0] ? "  ·  " : "", proxNome);
      { TxtLinha t = txt_linha_corta(TXT_TITULO3, rot, 255, 255, 255, 255,
                                     NV_TELA_W - x * 2.0f);
        txt_desenhar_alpha(t, x, y + 40.0f, a); } }
    { TxtLinha t = txt_linha(TXT_DET_META2, "OK para começar agora  ·  Voltar para ficar",
                             150, 154, 163, 255);
      txt_desenhar_alpha(t, x, y + 132.0f, a * 0.85f); }
    return;
  }

  // FILME: os relacionados que o Trakt ja deu ao abrir o titulo.
  { int n = extras_n_relacionados(), i;
    float y = NV_TELA_H - 480.0f;
    if (n > PP_MAX) n = PP_MAX;
    { TxtLinha t = txt_linha(TXT_ROW_TITULO, "Mais como este", 255, 255, 255, 255);
      txt_desenhar_alpha(t, x, y, a); }
    y += 56.0f;
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
        gfx_cor(r, raio, NV_COR_ESQUELETO_R, NV_COR_ESQUELETO_G,
                NV_COR_ESQUELETO_B, a);
      }
      { int c = sel ? 255 : 214;
        TxtLinha l = txt_linha_corta(TXT_DET_META2, extras_relacionado_titulo(i),
                                     c, c, c, 255, PP_CARD_W);
        txt_desenhar_alpha(l, cx, y + PP_CARD_H + 10.0f, a * (sel ? 1.0f : 0.86f)); }
    } }
}
