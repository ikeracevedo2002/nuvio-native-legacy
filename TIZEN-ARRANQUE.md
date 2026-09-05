# Arranque no Tizen 6.0

A foto do aparelho para em `3.0s [arranque] marco_iniciar`. O dono confirmou
que permanece assim. O carimbo e o instante da mensagem, nao um relogio vivo.

## Incompatibilidade reproduzida

O Emscripten usado nesta build habilita `WASM_BIGINT` por padrao. O glue
gerado chama `clock_time_get` com um argumento i64 (`ignored_precision`).
`marco_iniciar()` chama `clock_gettime`, chegando a essa fronteira WASM/JS.

Tizen 6.0 usa Chromium M76. A integracao i64/BigInt do WebAssembly chegou
ao Chrome 85; ter `BigInt` em JavaScript nao implica ter essa integracao.
O esbuild rebaixa a sintaxe do JS, mas nao legaliza assinaturas do WASM.

O teste compila o proprio `src/marco.c` e o executa em Node 12.22.12
(V8 7.8, sem a integracao habilitada). A build original chega a
`antes de marco_iniciar` e lanca `wasm function signature contains illegal type`.
Com `-sWASM_BIGINT=0`, conclui `marco_iniciar` e `marco`, incluindo o arquivo.
Como controle, habilitar `--experimental-wasm-bigint` no mesmo motor tambem
faz a build original passar. Isso reproduz a incompatibilidade, mas nao
substitui a verificacao do pacote completo no firmware Samsung.

```sh
LEGACY_NODE=/caminho/node-12/bin/node bash tests/tizen-clock.sh
bash tools/tizen.sh
bash tools/tizen-wgt.sh
```

A correcao converte os parametros i64 da fronteira JS/WASM em pares i32.
Os inteiros de 64 bits internos ao C/WASM continuam funcionando. Preservar
o rebaixamento esbuild para Chrome 76, pthreads e pilhas existentes.
O SDK atual avisa que `WASM_BIGINT=0` esta obsoleto: atualizacoes do SDK
precisam manter o teste em motor antigo; compilar com sucesso nao basta.

## Limite da conclusao

Verificacao local em 2026-09-05: teste de regressao passou; build completa
abriu a tela de login e registrou `[t] 961 primeiro quadro na tela` no
navegador desktop. O WASM final importa `clock_time_get` com quatro parametros
i32, sem i64 nessa fronteira. O pacote `NuvioTV-tizen-chrome76-abi.wgt`
foi conferido byte a byte contra HTML, JS, WASM e dados dessa build.

Validar na TV que aparece `rede_preparar` e depois o primeiro quadro.
O aviso anterior de `set_main_loop_timing` nao impediu os logs seguintes;
nao foi usado como explicacao para esta falha. AVPlay e camadas de video
continuam exigindo verificacao no aparelho.

Fontes primarias:

- [Samsung: versoes do motor web](https://developer.samsung.com/smarttv/develop/specifications/web-engine-specifications.html)
- [V8: integracao WebAssembly/BigInt](https://v8.dev/features/wasm-bigint)
- [Emscripten: bloqueios e proxy em pthreads](https://emscripten.org/docs/porting/pthreads.html)
- [Samsung: flags das extensoes do WASM Player](https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/sample-based-tutorials/tizen-wasm-player-sample.html)
