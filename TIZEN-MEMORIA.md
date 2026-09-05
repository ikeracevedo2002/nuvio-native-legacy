# Falha de memoria apos selecionar perfil — 2026-09-05

A foto do aparelho mostra falhas repetidas de `WebAssembly.Memory.grow`
partindo de 134217728 bytes (128 MiB), desde 29,8 s. O catalogo ainda publica
182 titulos e 16 fileiras; depois aparecem selecao de perfil, `detail_abrir`,
montagem da home e consultas de addons/episodios. A 34 s ocorre uma assercao
`thread` em `_emscripten_thread_profiler_init` e erro no endereco zero.

O SDK local, em `system/lib/pthread/pthread_create.c`, aloca o bloco da thread
(estrutura, TLS, guarda, pilha) e usa o ponteiro sem testar NULL. Isso explica
o erro secundario apos a falha de alocacao. Nao e evidencia de defeito na
selecao de perfil ou no desenho da home.

## Mudanca

`tools/tizen.sh` reserva 256 MiB inicialmente, sem crescimento, e explicita
`ABORTING_MALLOC=1`. A intencao e evitar depender da expansao recusada e, se
a memoria acabar novamente, abortar na alocacao em vez de corromper NULL.
Pilhas de 8 MiB e pool de 12 foram preservados. Workers ociosos no pool nao
devem ser contados como pilhas C ativas; cada pthread efetivamente criado
precisa da sua pilha, alem dos dados do app.

O painel recebe `[mem] WASM=... malloc=... livre-no-heap=...` a cada relato
de FPS. `malloc` e `livre-no-heap` sao uordblks/fordblks de mallinfo: nao
incluem a RAM total do navegador, texturas GPU nem necessariamente toda a
memoria linear ainda nao incorporada ao alocador.

## Validacao e limites

`bash tests/tizen-memory.sh` cria doze pthreads com pilhas de 8 MiB e mantem
32 MiB de payload real. Com 128 MiB fixos falha por OOM; com 256 MiB passa.
E um teste sintetico de orcamento, nao reproducao da conta do usuario.
Build completa passou. Pacote: `NuvioTV-tizen-mem256.wgt`, sem assinatura.

A foto prova que a expansao falhou; nao prova que 128 MiB sejam um teto
absoluto da TV, nem que uma reserva inicial de 256 MiB sera aceita. Confirmar
no aparelho: arranque, selecao de perfil, navegacao da home e abertura de
detalhes, acompanhando `[mem]`. Se a reserva for recusada ou o consumo continuar
crescendo, medir e reduzir alocacoes/concorrencia; nao aumentar memoria sem fim.

Referencia: [Emscripten — configuracao de memoria](https://emscripten.org/docs/tools_reference/settings_reference.html).
