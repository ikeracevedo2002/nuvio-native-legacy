# Plano de port — NuvioWeb 0.3.38-beta (fork web) → nuvio-native-legacy

Fonte: `../NuvioWeb-0.3.38-beta`, HEAD `94d6d28`. Destino: este repo, HEAD `1aeed53`.

O que este documento é: a lista completa do que o app web tem e o app nativo não,
com tamanho medido, âncora no código dos dois lados, e o motivo de cada item entrar
ou não. Não é um pedido de aprovação item a item — é a ordem em que pretendo mexer.

## 0. Antes de qualquer coisa: o post-play está solto

`src/posplay.c` e `src/posplay.h` estão **untracked**, e `app.c gfx.c home.c player.c
vertudo.c colecoes.h` estão modificados. É exatamente a build `d96a3d61` que já foi
para a TV e funciona. Enquanto não houver commit, um `git checkout` distraído apaga
o trabalho inteiro.

Também está solto `tools/plan-cinematic-heroes.mjs`.

Ação: commitar isto primeiro, sozinho, antes de abrir qualquer frente nova.

## 1. Levantamento — o delta real

Medido por `find` nos dois lados e `grep` de cada assunto em `src/`. O que **já existe
no nativo** e portanto sai da lista: debrid (`streams.c`, `rede.c`), Trakt e Simkl
(`trakt.c`, `simklauth.c`, `traktauth.c`), perfis e PIN (`perfis.c`, `perfilsel.c`),
coleções (`colecoes.c`), controle parental (`parental.c`), legendas ASS/bitmap
(`legenda.c`, `mkv.c`), sync (`sync.c`), post-play (`posplay.c`).

| # | Item | Web (linhas) | Nativo hoje | Tamanho |
|---|------|--------------|-------------|---------|
| A | Overlay de pausa | `playerScreen.js` (~120 linhas dispersas) | nada | pequeno |
| B | Próximo na home ("Next Up") | 98 linhas em 3 módulos | só `continuar.c` (2,2K) | médio |
| C | Ordem de catálogos da home | `catalogOrderScreen.js` 279 | pull existe, não aplica | médio |
| D | Onboarding (addons essenciais + modo) | 211 linhas em 2 telas | nada | médio |
| E | Layouts de home (classic/grid/modern) | 3 layouts | 1 layout em `home.c` (97K) | grande |
| F | Temas | 421 linhas em 2 módulos | cores fixas em `layout.h` | médio |
| G | Plugins / scrapers | 4.675 linhas em 8 módulos + 2 telas | não existe | **muito grande** |
| H | Supporters / licenças | 2 telas | nada | trivial |

## 2. Fase 1 — o que dá para fazer em cima do que acabou de funcionar

### A. Overlay de pausa
O arquivo `js/ui/screens/player/pauseOverlay.js` (16 linhas) é **casca morta** — um
`div` com `textContent="Paused"` que ninguém importa. O recurso de verdade está em
`playerScreen.js`: `pauseOverlayDelayMs`, `buildPauseOverlayMeta()`,
`hydratePauseOverlayMeta()`, `extractPauseOverlayCast()`. Ou seja: depois de N ms
pausado, sobe um painel com metadados do título e elenco, hidratados sob demanda.

No nativo o gancho já existe: `player.c:626` (`video_pausar`) e `player.c:652`
(OK no centro pausa). O `extras.c` já busca elenco e sinopse do Trakt ao abrir o
título — mesmo dado que o web hidrata, já em memória.

Custo: um `pausao.c/h` no molde do `posplay.c`, com `atualizar/visivel/desenhar`.
Toggle em `ajustes.c` espelhando `playback_pause_overlay` do web.

### B. Próximo na home
Três módulos pequenos e puramente lógicos — `nextUpCandidateResolver.js` (22),
`nextUpEpisodeAnchor.js` (33), `nextUpWatchingPolicy.js` (43). São **regras**, não UI:
qual é o próximo episódio de uma série vista, e se vale mostrar uma série que o
usuário não marcou como acompanhada.

Este é o melhor custo-benefício da lista: o nativo já tem todo o dado (progresso por
episódio — `sync.c:509` diz isso explicitamente — e lista única de episódios cobrindo
todas as temporadas, conforme `posplay.h`). Falta só a regra e uma fileira.

Portar as três regras 1:1 para C, alimentando a fileira "Continuar" existente
(`continuar.c` + `home.c`), em vez de criar fileira nova.

## 3. Fase 2 — sync que já chega e não é usado

### C. Ordem de catálogos da home
`sync.c:428-430` já chama `sync_pull_home_catalog_settings` com
`p_platform="home_catalog_shared"` — e só **conta** o resultado (`contarRpc(...) > 0`).
O dado chega e é jogado fora. Do outro lado, `homeCatalogSettingsSyncService.js` (740
linhas) mostra o formato completo, e `catalogOrderScreen.js` (279) a tela de reordenar.

Duas metades, em ordem:
1. **Aplicar o pull** — ordenar/ocultar as fileiras da home com o que já vem do
   servidor. Sem tela nova. Baixo risco, ganho imediato para quem já configurou no web.
2. **Tela de reordenar** no nativo, com push.

O passo 2 esbarra na trava documentada em `PLANO-CONTA-SYNC.md`: catálogos da home
são **só leitura** hoje, de propósito, porque push de lista vazia apaga o dado nos
outros aparelhos. A tela só pode entrar junto com o push, e o push só pode entrar
depois da tela — senão empurra vazio. Fazer os dois no mesmo commit ou nenhum.

### D. Onboarding
`essentialAddonSetupScreen.js` (78) + `experienceModeSelectionScreen.js` (133). O
nativo já tem estado vazio na home (feito na sessão de conta/sync), o que cobre metade
do problema. O que falta é a primeira execução guiada: instalar Cinemeta/OpenSubtitles
e escolher o modo.

Valor real depende de quem instala o app. Se todo teste começa por login em conta que
já tem addons, isto quase nunca roda. Fica depois de C.

## 4. Fase 3 — cosmético e caro

### F. Temas
`themeManager.js` (236) + `themeColors.js` (185). No nativo as cores estão fixas em
`layout.h` e espalhadas por `home.c`, `descoberta.c`, `vertudo.c`, `detail.c`. Portar
significa passar tudo por uma tabela de tema. Mecânico, extenso, e regride visual em
qualquer tela esquecida. Sem toggle no `ajustes.c` não vale nada.

### E. Layouts de home
`home.c` tem 97 KB de um layout só. Três layouts significa reescrever a geometria da
tela mais complexa do app. Alto risco de jank numa TV legada, que é exatamente o custo
que este port passou meses derrubando. **Recomendo não fazer**, a menos que apareça
pedido concreto.

### H. Supporters / licenças
Duas telas estáticas. Licenças pode virar obrigação legal dependendo do que o `.ipk`
empacota; supporters é vaidade. Uma tarde, quando não houver mais nada.

## 5. Plugins / scrapers — o item que precisa de decisão, não de plano

`pluginManager.js` (2.121), `pluginWorker.js` (765), `pluginModels.js` (670),
`pluginScraperRuntime.js` (486), `pluginRuntime.js` (248), `pluginPolicy.js` (206),
`pluginExecutionFlight.js` (93), `pluginSecurity.js` (66), mais `pluginsScreen.js`
(1.042) e `pluginScreen.js` (473). **4.675 linhas de JavaScript que existem para
executar JavaScript de terceiros.**

O bloqueio não é tamanho, é natureza. Este app **não tem motor de JavaScript**. O que
`js.c` faz, e o próprio cabeçalho diz, é leitura tolerante de JSON — não é um
interpretador. `jsw.c` escreve JSON. Rodar plugin exigiria embarcar QuickJS ou Duktape
no binário ARM, mais um sandbox equivalente ao `pluginSecurity.js`, mais um worker que
não trave o laço de quadros.

Único traço de plugin no nativo hoje: `perfis.c`/`perfis.h`/`sync.c` carregam o campo
do perfil, sem executar nada.

Três caminhos honestos:
1. **Não portar.** O nativo tem debrid e addons Stremio; plugins são a terceira fonte.
2. **Portar só o catálogo** — listar e ativar/desativar plugins, com a execução ainda
   no web. Não serve para nada sozinho na TV.
3. **Embarcar QuickJS.** Semanas, não dias, e é a maior superfície de segurança que o
   app já teria. Só com decisão explícita.

Sem escolha tua, sigo com (1).

## 6. Ordem de execução

```
0. commit do post-play + arte cinemática          (solto, risco de perda)
1. A  overlay de pausa                            (molde do posplay, já validado)
2. B  próximo na home                             (98 linhas de regra, dado já existe)
3. C1 aplicar ordem de catálogos do pull          (dado já chega e é descartado)
4. C2 tela de reordenar + push                    (junto, nunca separado)
5. D  onboarding                                  (se alguém instalar sem conta)
6. F  temas                                       (mecânico e extenso)
7. H  supporters/licenças                         (tarde vaga)
   E  layouts de home                             — não recomendado
   G  plugins                                     — precisa de decisão
```

## 7. Verificação — como cada item se prova

O padrão desta base é medir na TV, não no Mac. Para cada item:
- build ARM por `tools/arm.sh`, `.ipk` instalado, md5 conferido;
- para A: pausar um episódio, cronometrar o atraso, confirmar elenco correto;
- para B: perfil com série a meio, conferir que a fileira aponta o episódio certo,
  e que série terminada **não** aparece;
- para C: comparar a ordem das fileiras na TV com a ordem configurada no app web;
- em todos: jank a frio e durante playback não pode regredir do baseline em
  `../PENDENCIAS.md` (frio 3183 ms, quente 0, playback 0).

## Anexo C1 — contrato da ordem de catálogos, lido do serviço web

Levantado em `js/core/profile/homeCatalogSettingsSyncService.js` (740 linhas). O
nativo já chama a RPC em `sync.c:428-430` e descarta a resposta.

**Resposta.** `sync_pull_home_catalog_settings` devolve uma linha com
`settings_json` (aceitar também `settingsJson`, e a resposta pode vir como array
de um elemento — linhas 226-233). Dentro dele, dois formatos:

1. **Moderno** — `items: [{addon_id, type, catalog_id, enabled, order}]`, ou
   `{is_collection: true, collection_id}` para coleção. `type` é normalizado
   para minúsculas. `enabled` é `!== false`, ou seja, ausente significa ligado.
2. **Legado** — arrays de chaves soltas. A ordem sai do primeiro que existir
   entre `catalog_order_keys`, `home_catalog_order`, `catalog_order`, `order`;
   os ocultos, entre `disabled_catalog_keys`, `hidden_catalog_keys`,
   `catalog_disabled_keys`, `home_catalog_disabled`, `disabled`.

Mais dois booleanos: `hide_unreleased_content` e `hide_catalog_underline`. Ambos
só valem quando a chave EXISTE no blob — ausente não é `false`, é "mantém o
local".

**Chave de identidade.** `syncItemKey()` (linha 190): coleção vira
`buildCollectionHomeKey(collection_id)`; catálogo vira
`buildCatalogOrderKey(addon_id, type, catalog_id)`. O nativo precisa montar a
mesma chave a partir de `catalogo.h` — se as três partes não estiverem
disponíveis, o item não casa e a ordem remota é ignorada para ele.

**A armadilha, já medida na OLED65C9 e comentada no fonte (linhas 398-410).**
O remoto só conhece os catálogos que existiam quando foi gravado. Aplicar a
ordem remota crua REMOVE os que apareceram depois. O log real era
`{"localItems":54,"remoteItems":43}` em todo boot: 54 nunca igualava 43, a
assinatura nunca batia, a Home era reescrita e reinvalidada a cada arranque,
sem nunca convergir.

Regra a portar junto, não depois: **união, não substituição**. Ordem remota
primeiro, filtrando o que não existe local; depois os locais que o remoto não
conhece, no fim, na ordem em que já estavam. É a mesma regra que o pull de
addons deste repo já segue, e pelo mesmo motivo.
