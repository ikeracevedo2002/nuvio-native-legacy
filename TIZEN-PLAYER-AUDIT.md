# Auditoria do resumo do player Tizen — 2026-09-05

Base: ramo `tizen-wasm`, HEAD inicial `f86086c`. O texto recebido e um
handoff, nao evidencia de execucao no firmware. A foto anterior comprova
presenca do AVPlay e contexto alpha; nao comprova decodificacao nem camadas.

## Defeitos encontrados e corrigidos nesta revisao

| Defeito no codigo | Correcao |
| --- | --- |
| `estado` chamava `getTotalTime`, inexistente no duble e na interface consultada; o catch escondia a falha e devolvia duracao zero | Usar `getDuration`, convertendo ms para segundos |
| Callback de `prepareAsync` consultava faixas antes de `play`; a leitura de dimensoes podia falhar silenciosamente | Ler depois de `play` bem-sucedido |
| `app_atualizar` podia retornar antes de `video_bombear` | Bombeamento no inicio da funcao, uma unica vez |

A [referencia AVPlay](https://developer.samsung.com/smarttv/develop/api-references/samsung-product-api-references/avplay-api.html)
permite `getTotalTrackInfo` em READY apenas com preparo sincrono. O duble
foi endurecido para detectar esse uso indevido apos preparo assincrono.

## Correcoes ao handoff

| Afirmacao recebida | Avaliacao |
| --- | --- |
| Handler vazio prova que legendas embutidas nao aparecem | Nao. `video_escolher_legenda` solicita desenho nativo com `setSilentSubtitle(false)`. Falta teste real |
| Basta guardar o texto corrente para o overlay | Incompleto: o evento tem duracao e pode representar imagem; precisa expirar cues, tratar pausa/seek/troca de titulo e escolher quem desenha |
| Estilo da legenda embutida ja esta pronto | A copia em `video_legenda_estilo` nao e consumida. O overlay de `player.c` usa outra copia e so consulta `legenda_texto` |
| Ordem do DOM ou `zindex` invalido prova a composicao | Inferencia invalida: ignorar uma propriedade CSS nao isola o mecanismo do plano de hardware |
| 350 ms garantem que seeks nao concorrem | Falso no codigo: ambos os callbacks estao vazios; um seek lento pode sobrepor o seguinte |
| Legenda externa por AVPlay e impossivel | URL direta/MEMFS nao bastam, mas existe caminho via download para armazenamento Tizen. O overlay existente evita implementar essa ponte |
| AVPlay tem teto de 32 faixas | Nao localizado na referencia consultada. O nosso `NV_FAIXA_MAX` e 12 por lista; nao confundir limite local com limite de plataforma |
| DTS-HD e TrueHD implicam troca automatica para outra faixa | Nao demonstrado. A especificacao de 2021 exclui DTS e pede que outras faixas sejam selecionaveis; nao garante fallback automatico |
| Erro de container quase sempre significa audio | Sem evidencia estatistica. Container, codec, perfil, bitrate, arquivo e rede precisam ser considerados |
| WASM Player com textura e plano B garantido nessa TV | A funcionalidade existe; disponibilidade no modelo/firmware e custo da migracao ainda precisam de verificacao |

`setSilentSubtitle(false)` pede exibicao nativa e desativa eventos de legenda;
`true` oculta o desenho. A duracao do evento e em milissegundos. O contrato
de seek exige aguardar um callback antes de outras chamadas AVPlay.
[API oficial](https://developer.samsung.com/smarttv/develop/api-references/samsung-product-api-references/avplay-api.html).

O [guia de legendas](https://developer.samsung.com/smarttv/develop/guides/multimedia/subtitles.html)
mostra download de arquivo externo para armazenamento local. A impossibilidade
absoluta alegada no handoff nao se sustenta; nao implementamos essa alternativa.

A [especificacao de video de 2021](https://developer.samsung.com/smarttv/develop/specifications/media-specifications/2021-tv-video-specifications.html)
inclui MKV e descreve limitacoes por codec, resolucao e taxa. TrueHD nao consta
entre os codecs de audio listados. Isso nao comprova comportamento de fallback.

O [guia WASM Video Decoder](https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/tizen-wasm-player/using-tizen-wasm-video-decoder.html)
documenta textura externa GLES; isso envolve uma integracao diferente do AVPlay.

HDR/Atmos: os getters atuais sao constantes conservadoras, nao deteccao.
Nao assumir suporte ou ausencia de HDR a partir desses retornos. Nao foi
identificado nesta revisao um campo portavel que resolva o estado real no alvo.

## Pendencias reais, nao corrigidas aqui

- Serializar seeks e operacoes conflitantes ate sucesso/erro, incluindo
  encerramento, pausa e troca de faixa. O debounce sozinho nao resolve.
- Evitar callbacks de `prepareAsync` de uma sessao anterior alterarem a nova.
- So atualizar `audioAtual`/`legAtual` quando a selecao for aceita; hoje o C
  ignora o retorno do comando e pode mostrar sucesso falso.
- Testar legenda nativa na TV antes de substituir por overlay. Se for adotado
  overlay, testar duracao, limpeza, imagens, atraso e ausencia de duplicacao.
- Confirmar arranque, video atras dos controles e comportamento com codecs
  reais no aparelho. Nao apresentar teste em navegador como essa confirmacao.

## Validacao

`node tests/tizen-avplay-contract.cjs`: tres falhas antes, quatro verificacoes
passando depois (duracao, dimensoes, linhas de faixas e posicionamento da chamada).
Executa o corpo JS da ponte extraido do codigo; nao valida o parser C nem
simula o firmware. A verificacao de bombeamento e estrutural, nao navegacao de UI.

`bash tools/tizen.sh`: build completa passou. Mudancas concorrentes no pool
e painel (`tools/tizen.sh`, `tools/tizen-shell.html`) foram preservadas;
nao sao parte das correcoes acima. Nenhum novo WGT foi empacotado nesta revisao.

`bash tests/player.sh` falhou em `tests/player_regression.c:85` (expectativa
MP4/Dolby Vision/2160). A recompilacao com `app.c` de HEAD, anterior a nossa
mudanca, falhou na mesma assercao. O backend Tizen nao e compilado nesse teste
macOS; a falha nao foi introduzida pelas correcoes desta revisao.

Durante o trabalho, o commit concorrente `a52723a` incorporou tambem as
alteracoes feitas aqui em `tools/fake-avplay.js` e `tools/teste-avplay.c`.
As alteracoes de `src/app.c`, `src/video_tizen.c`, este documento e o teste
de contrato permanecem no checkout sem commit ao encerrar esta revisao.
