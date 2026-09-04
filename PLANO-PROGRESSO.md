# Progresso de reproducao — do que existe ao que o fork tvOS ja acertou

Origem (04/09/2026): comparacao a tres do modelo de progresso, feita com o
grafo do graphify sobre o fork tvOS (NuvioTVOS), o upstream Android
(NuvioMedia/NuvioTV) e este repositorio. Tudo abaixo foi CONFIRMADO lendo o
codigo; o que ainda nao foi executado esta marcado como tal. Nada aqui exige
mudar o servidor: o formato de linha (`content_id`, `content_type`, `position`,
`duration`, `season`, `episode`, `progress_key`, `last_watched`) ja e o mesmo
nos tres clientes — a diferenca e toda deste lado.

Continua [PLANO-CONTA-SYNC.md](PLANO-CONTA-SYNC.md) (Parte 1.5 e 4b). Nao o
substitui.

## Parte 0 — Onde os tres clientes estao

| | LG nativo (este) | Fork tvOS | Upstream Android |
|---|---|---|---|
| Registro local | 5 campos em `progresso.txt` | 10 campos (`isPendingPush`, `lastWatchedAt`) | ~25 campos (ids Trakt/Simkl, `source`) |
| Fileira CW sem provedor remoto | nao existe | derivada do ledger | derivada do modelo |
| Trakt + conta juntos | nao (Trakt manda) | nao (exclusivo) | sim, merge + ranking |
| Conflito pull × local | nenhum | pendente vence servidor | timestamp + rank |
| Linha fora do catalogo | descartada | retida e retentada | retida |
| Push vazio | nunca | nunca | n/a |

Filosofia deste app e do fork tvOS: provedor exclusivo, conta Nuvio separada.
Nao vamos para o modelo do Android (um tipo unico com id de cada provedor):
adicionar provedor la exige mexer no dominio; aqui e no tvOS, nao. O que falta
sao as garantias que o tvOS tem e este nao.

## Parte 1 — Defeitos confirmados no codigo

Ordem: do que perde dado para o que so incomoda.

### 1.1 `progress_key` diferente do web/tvOS → linha paralela no servidor

`sync.c:315` manda `progress_key = local[i].imdb`, ou seja `tt123:4:9`.
O web (`watchProgressSyncService.js:337 toProgressKey`) e o tvOS
(`WatchProgressRecord.progressKey`) usam `tt123_s4e9`. O servidor faz upsert
por `progress_key`: a mesma cena assistida aqui e no celular vira DUAS linhas.
O web ate deduplica na leitura por `(content_id, video_id, season, episode)`,
mas este app nao manda `video_id` (o web manda `__nuvio_episode__:4:9`), entao
nem esse segundo filtro casa.

Filme nao e afetado (`progress_key = content_id` nos dois).

### 1.2 Serie vinda de catalogo e empurrada como FILME

`cat_salvar_progresso_ep` grava 5 colunas: `imdb pos dur temporada episodio`
(`catalogo.c:519`). `lerProgressoLocal` (`sync.c:266`) le TRES
(`"%39s %lf %lf"`) e depois procura temporada/episodio dentro do id, no `:`.
Mas so o item que veio de `trakt_continuar` tem `:` no imdb (`trakt.c:260`);
serie descoberta pelos catalogos dos addons tem `tt123` puro
(`descoberta.c:518`). Resultado no push: `content_type:"movie"`,
`season:null`, `episode:null`, `progress_key:"tt123"` — a conta recebe "parou
o filme tt123 em 23 min" para uma serie. Nos outros aparelhos isso aparece
errado ou some.

### 1.3 Pull reaplicado por cima do local recem-assistido (rollback de 5 min)

Ciclo em `sync.c rodar()`: `puxarProgresso()` (linha 477) ANTES de
`empurrarProgresso()` (482), ordem deliberada e correta. Mas o que foi puxado
fica em `progRem` e so e aplicado depois, em `sync_passo` (562), via
`cat_salvar_progresso_ep` — que sobrescreve o catalogo E regrava
`progresso.txt`. Sequencia: assiste na TV → fecha o player (local novo, sujo) →
ciclo: pull traz o servidor ANTIGO → push manda o novo → `sync_passo` aplica o
antigo por cima. A TV volta a posicao velha ate o proximo ciclo (5 min). Como o
arquivo local tambem foi regravado com o valor velho, o "Retomar" da tela de
detalhe erra nesse intervalo. Sem timestamp nao ha como desempatar.

O tvOS resolve com `isPendingPush`: "a pending row outranks anything the server
sends back". Nao foi executado para reproduzir; e leitura direta das 3 funcoes.

### 1.4 Linha da conta sem titulo no catalogo e descartada

`sync_passo` (563): `cat_indice_por_imdb` negativo → `continue`. Em `nProgRem = 0`
tudo some. Quem assistiu no celular um titulo que nao esta em nenhuma fileira
carregada aqui nunca ve o progresso, nem quando o titulo aparecer depois.
Foi exatamente o defeito que o tvOS descreve no `WatchProgressLedger`
("recently-watched titles vanish").

### 1.5 Fileira "Continuar assistindo" so existe com Trakt

`descoberta.c:856`: fileira 0 e `trakt_continuar(lote, 8)`. Sem credencial
Trakt a funcao devolve 0 e a fileira nao nasce. O progresso da CONTA (que ja
esta em `CatItem.progresso` depois do pull) alimenta so o "Retomar" e o selo do
card — nunca uma fileira. Quem usa so a conta Nuvio nao tem continue watching
cross-device, que e a promessa central do login.

### 1.6 Guarda `durSeg <= 1.0` mata duas escritas que o codigo acha que faz

`cat_salvar_progresso_ep` (`catalogo.c:497`): `if (durSeg <= 1.0 || ...) return;`.

- Botao do olho, `app.c:303`: `cat_salvar_progresso(i, visto ? 0.0 : 1.0, 1.0)`.
  `dur = 1.0` → retorna sem gravar. O comentario acima diz "Grava progresso
  cheio no arquivo do app"; nao grava. Só o Trakt (`trakt_assistido`) recebe.
  O espelho local do olho nunca muda — o mesmo sintoma que o proprio comentario
  descreve ter consertado no lado Trakt.
- Pos-play, `app.c:652`: `cat_salvar_progresso_ep(idx, 0.0, 0.0, t, e)` para
  "o item passa a apontar para o episodio NOVO". `dur = 0.0` → retorna. O item
  continua apontando para o episodio anterior; o rotulo do player e o alvo do
  proximo pos-play saem do `addons_buscar` seguinte, nao daqui.

### 1.7 Sem `last_watched` no push, `updated_at` ignorado no pull

`empurrarProgresso` nao manda `last_watched`; o web manda `Number(ms)`. O
servidor entao carimba a hora do push (ate 5 min depois do fato), e no celular a
ordem da fileira fica errada quando os dois aparelhos assistem no mesmo
intervalo. No pull, `updated_at` vem em toda linha (`mapProgressRow`) e e
ignorado — e a unica informacao que resolveria 1.3.

### 1.8 Miudezas

- Pull ignora `position_ms`/`duration_ms` NAO: trata (`sync.c:229`). OK.
- Cap `SY_PROG_MAX 240` e `id[40]`; `progress_key` `tt1234567_s10e12` cabe.
- Unidades: local em segundos, rede em ms, conversao correta nos dois sentidos.
- `progresso.txt` gravado em `dirGravacao` = `dirArte`, nao em `dados_dir()`.
  `sync_esquecer_usuario` apaga o de `dados_dir()`? Conferir ao implementar
  2.1 — se o arquivo esta em `dirArte`, o logout nao o apaga (PLANO-CONTA-SYNC
  4b diz que apaga; verificar qual caminho).

## Parte 2 — O que muda

### 2.1 Registro local ganha identidade e tempo (base de tudo)

`progresso.txt` passa a ter uma linha por `progress_key` no formato do web:

```
progress_key<TAB>content_id<TAB>tipo<TAB>temporada<TAB>episodio<TAB>posSeg<TAB>durSeg<TAB>lastWatchedMs<TAB>pendente
tt1234567_s4e9	tt1234567	series	4	9	1432	2640	1757000000000	1
tt7654321	tt7654321	movie	0	0	5400	7200	1756990000000	0
```

- `progress_key` calculado por UMA funcao, `prog_chave(content_id, temp, ep)`,
  identica a `toProgressKey` do web: `%s_s%de%d` quando temp>=0 e ep>0, senao
  `content_id`. Usada no arquivo, no push e no casamento com o pull.
- `content_id` sempre o titulo puro (corta no primeiro `:`). Nunca mais o id
  composto do Trakt vai para a rede.
- `lastWatchedMs`: `time(NULL)*1000` na escrita local; `updated_at` (ou
  `last_watched`) na aplicacao de linha remota.
- `pendente`: 1 quando ESTE aparelho escreveu e ainda nao empurrou.
- Leitura aceita o formato antigo (3 a 5 colunas, sem cabecalho) e migra na
  primeira gravacao: `content_id` = coluna 1 cortada no `:`, temp/ep das
  colunas 4-5 ou do `:`, `lastWatchedMs = 0`, `pendente = 1` (o que estava no
  disco nunca foi empurrado com chave certa — empurrar de novo e o que corrige
  as linhas paralelas de 1.1 e 1.2 no servidor).
- Continua temporario + `rename`.
- Sai de `catalogo.c`: vira modulo proprio `progresso.c/h` (o catalogo hoje
  mistura indice de item com identidade de obra; `catalogo.c:45` ja reclama
  disso). `catalogo.c` so recebe `cat_aplicar_progresso(indice, pos, dur, t, e)`
  para atualizar `itens[]`, sem tocar em arquivo.

API:

```c
// progresso.h
typedef struct {
  char chave[48], contentId[24], tipo[8];
  int  temporada, episodio;
  double posSeg, durSeg;
  long long lastWatchedMs;
  int  pendente;
} ProgRegistro;

void prog_chave(char *dst, unsigned n, const char *contentId, int temp, int ep);
int  prog_ler(ProgRegistro *saida, int max);                 // todos
const ProgRegistro *prog_por_chave(const char *chave);
// Escrita LOCAL (player, olho, pos-play): pendente=1, lastWatched=agora.
void prog_gravar_local(const char *contentId, int temp, int ep,
                       double posSeg, double durSeg);
// Aplicacao de linha REMOTA: so grava se !pendente && remotoMs > localMs.
// Devolve 1 quando aplicou.
int  prog_aplicar_remoto(const ProgRegistro *r);
void prog_marcar_empurrados(const char *const *chaves, int n);
int  prog_pendentes(ProgRegistro *saida, int max);
void prog_remover(const char *chave);
void prog_esquecer_tudo(void);                                // logout
```

### 2.2 Push com chave, tipo, episodio e hora certos (fecha 1.1, 1.2, 1.7)

`empurrarProgresso` passa a ler `prog_pendentes()` e a mandar, por linha:

```json
{"content_id":"tt1234567","content_type":"series","video_id":"__nuvio_episode__:4:9",
 "season":4,"episode":9,"position":1432000,"duration":2640000,
 "last_watched":1757000000000,"progress_key":"tt1234567_s4e9"}
```

- `video_id` sintetico igual ao web (`SYNTHETIC_EPISODE_VIDEO_PREFIX`), para
  o segundo filtro de dedupe do web casar. Filme: `video_id = content_id`.
- So pendentes, nao o arquivo inteiro. Push vazio continua nunca saindo.
- Filtro do web `MIN_PROGRESS_SYNC_DURATION_MS = 60000`: nao empurrar
  `durSeg < 60`. Hoje a guarda e `dur <= 1.0`.
- Sucesso 2xx → `prog_marcar_empurrados` das chaves enviadas (nao zerar
  `sujoProgresso` em bloco: o player pode ter gravado outra linha durante a
  viagem).

### 2.3 Pull sem rollback e sem descarte (fecha 1.3, 1.4)

- `puxarProgresso` guarda tambem `updated_at`/`last_watched` (ms; aceitar
  numero em s ou ms e ISO, como `mapProgressRow`) e `progress_key` da linha.
- `sync_passo` chama `prog_aplicar_remoto()` para TODA linha, casando ou nao
  com o catalogo. A regra de conflito fica dentro de `prog_aplicar_remoto`:
  local pendente vence; senao, mais novo vence; empate mantem local.
- So depois, para as linhas que aplicaram E casam com `cat_indice_por_imdb`,
  chama `cat_aplicar_progresso` (memoria, sem arquivo).
- Quando um catalogo e remontado (`desc_repetir`, troca de perfil), o
  catalogo relê `progresso.txt` — o que ja faz em `catalogo.c:696` — e as
  linhas que antes nao casavam passam a casar. Retentativa gratis.

### 2.4 Fileira "Continuar assistindo" sem Trakt (fecha 1.5)

Em `descoberta.c montar()`, depois de `trakt_continuar`:

```c
if (nContinuar == 0 && !trakt_ativo())
  nContinuar = prog_continuar(lote, 8);   // do progresso.txt
```

`prog_continuar`: le registros, filtra `0.01 <= pos/dur < 0.90` (mesmos limites
de `home_registrar_retorno`), ordena por `lastWatchedMs` desc, e para cada um
que casa com um item ja no catalogo copia o `CatItem` e ajusta
temporada/episodio/`restanteMin`. Os que NAO casam ficam de fora desta fileira
(sem arte nao ha card), mas continuam no arquivo. Chave/titulo da fileira
iguais aos de hoje (`continue_watching`), para o resto da home nao saber a
diferenca.

Trakt ativo continua mandando sozinho, como no tvOS
(`ContinueWatchingBuilder.rebuild` sai cedo com provedor remoto). Nao
misturamos as duas fontes — e a decisao de arquitetura da Parte 0.

Serie com progresso >= 90% no ultimo episodio conhecido: proximo episodio como
"a seguir" fica para depois (o tvOS tem `upNextSeeds`; aqui `proximo.c` ja sabe
achar o proximo, da para reaproveitar numa segunda rodada).

### 2.5 Olho e pos-play gravam de verdade (fecha 1.6)

- Olho: `prog_gravar_local(contentId, 0, 0, visto ? 0 : dur, dur)` com
  `dur = duracao conhecida do item` ou, sem ela, um sentinela `dur = 3600, pos
  = 3600` (100%). E `cat_aplicar_progresso` para o selo. Tirar a guarda
  `dur <= 1.0` do caminho do olho; a guarda de qualidade (`dur < 60`) fica so
  no push (2.2).
- Pos-play: trocar `cat_salvar_progresso_ep(idx, 0.0, 0.0, t, e)` por uma
  funcao que so muda `itens[idx].temporada/episodio/nomeEpisodio` — e o que a
  chamada queria — sem passar pelo arquivo. `cat_apontar_episodio(idx, t, e)`.

### 2.6 Logout e perfil

- `sync_esquecer_usuario` chama `prog_esquecer_tudo()`. Confirmar o caminho
  do arquivo (1.8).
- Troca de perfil: `progresso.txt` e por CONTA hoje, nao por perfil. O tvOS
  tem `storageKey` por `profileId`. Fazer o mesmo: `progresso.<perfil>.txt`,
  e `prog_*` consulta `perfis_ativo()`. Sem isso o pull do perfil 2 aplica por
  cima do que o perfil 1 gravou, e o push do perfil 2 manda o que era do 1
  (exatamente a classe de bug que 4b fechou para addons).

## Parte 3 — Ordem de execucao

1. `progresso.c/h` + migracao do formato antigo + testes unitarios (sem rede):
   chave igual ao web, leitura do formato velho, conflito (pendente vence;
   mais novo vence), por perfil. Fixture: linhas reais dos dois formatos.
2. `catalogo.c` deixa de gravar arquivo; `cat_aplicar_progresso` e
   `cat_apontar_episodio`. `player.c`, `app.c` (olho, pos-play) passam a chamar
   `prog_gravar_local`. Teste: fecha o player → linha pendente com chave certa.
3. `sync.c`: push de pendentes com o formato 2.2; pull com `updated_at` e
   `prog_aplicar_remoto`. Teste com `rede_postar_st` falso (estilo
   `tests/trakt_contracts.c`): corpo do push tem `_s4e9`, `series`,
   `video_id` sintetico, `last_watched`; e o cenario 1.3 (pull antigo + local
   pendente) NAO regride a posicao.
4. `descoberta.c`: `prog_continuar` como fileira 0 sem Trakt. Teste de layout
   ja existe (`tests/home_layout.c`); acrescentar caso "sem Trakt, com
   progresso local → fileira continue_watching existe".
5. Logout/perfil (2.6).
6. Na TV, conta real: assistir 2 min de uma SERIE de catalogo, esperar o ciclo,
   conferir no celular que aparece como serie no episodio certo e com a mesma
   linha (nao duplicada). Depois o inverso. Depois o cenario de rollback:
   assistir, fechar, e forcar `sync_iniciar()` — a posicao nao pode voltar.

Passos 1-3 fecham tudo que perde dado. 4 e o que o login promete. 5 e
seguranca. Cada passo compila e roda sozinho; nada de branch longa.

## Parte 3b — Feito (04/09/2026), o que mudou em relacao ao previsto

Passos 1 a 5 implementados no mesmo dia; falta so o 6 (TV, conta real).

- `src/progresso.c/h` — como descrito em 2.1, com duas diferencas: arquivo
  UNICO com coluna de perfil (em vez de um por perfil) para o logout apagar
  tudo sem enumerar; e mutex em toda chamada publica, porque o player grava
  no fio principal enquanto o sync le no dele. `prog_por_chave` copia em vez de
  devolver ponteiro, pelo mesmo motivo. `PROG_MAX 480`.
- `src/syncprog.c/h` — o pull/push saiu de `sync.c` para um modulo testavel
  (`syncprog_puxar` e `syncprog_empurrar` no fio de sync; `syncprog_aplicar` no
  principal). `sync.c` chama os tres. Push roda SEMPRE, nao so com
  `sujoProgresso`: linhas migradas do formato antigo nascem pendentes.
- Hora do pull: texto lido ANTES de `js_num`, porque `js_num` aceita valor
  entre aspas e leria `"2026-09-04T..."` como 2026. ISO parseado em UTC.
- Chave do pull SEMPRE recalculada, nunca copiada do `progress_key` do servidor:
  linhas antigas deste app trazem `tt123:4:9` la, e adota-las perpetuaria a
  duplicata.
- `catalogo.c`: `cat_salvar_progresso_ep` continua existindo (o player nao
  mudou) mas grava via `prog_gravar_local`. Novas `cat_aplicar_progresso` e
  `cat_apontar_episodio`. A releitura do disco casa por titulo e cada item
  recebe SO o registro mais recente.
- `trakt.c`: o enfeite paralelo do Cinemeta virou `trakt_enfeitar_lote`,
  publico, e `descoberta.c` usa para a fileira local (`continuarLocal`), que
  entra quando `trakt_continuar` devolve 0 e `!trakt_ativo()`.
- Pendencia 1.8 fechada de graca: `main.c:428` ja grava em `dados_dir()`.

Testes: `tests/progresso.sh` (7), `tests/syncprog.sh` (5, inclui o rollback
1.3 antes e depois do push e o eco velho), `home.sh`, `ctxmenu_contract.sh`,
`proximo.sh` — todos PASS, `SANITIZE=1` limpo. Sem teste automatizado para
`continuarLocal` (depende de SDL/rede via `descoberta.c`); e o item 6.

## Parte 4 — O que NAO vamos fazer, e por que

- Merge Trakt + conta numa fileira so (modelo Android). Decidido: provedor
  exclusivo, como o tvOS. Evita ranking, evita o Trakt "contaminar" a conta.
- Consumir Simkl. `simklauth.c` so guarda token; sem tela que leia, nao ha o
  que sincronizar. Quando entrar, entra pelo mesmo caminho de `trakt_continuar`
  (fonte exclusiva), nao pelo `progresso.txt`.
- Mudar o servidor ou as RPC. O formato ja e comum; o erro era nosso.
- Empurrar vistos/biblioteca/colecoes. Regra 1.6 do PLANO-CONTA-SYNC continua.
