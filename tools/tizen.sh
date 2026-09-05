#!/bin/bash
# Compila para Samsung Tizen: WebAssembly (Emscripten) dentro de um .wgt.
#
# POR QUE WASM E NAO UM BINARIO NATIVO. O perfil TV do Tizen Studio so gera
# .wgt; .tpk nativo e perfil mobile/wearable e o firmware da TV recusa sem
# certificado de parceiro. Tizen .NET para TV acabou e o NaCl foi encerrado em
# 2021. O que a Samsung oferece no lugar e WebAssembly, a partir do Tizen 5.5
# (modelos 2020). Entao "nativo no Tizen" = este mesmo src/*.c compilado para
# WASM, com o video pela API AVPlay atras de um canvas transparente.
#
# EMSDK UPSTREAM, NAO O FORK DA SAMSUNG. O fork so acrescenta as APIs de WASM
# Player / ML deles, que este port nao usa — o video sai pelo AVPlay em JS, que
# e API de web app comum. O emcc de upstream produz WASM padrao, que o Chromium
# da TV executa igual.
set -e
cd "$(dirname "$0")/.."

: "${EMSDK_DIR:=$HOME/emsdk}"
[ -f "$EMSDK_DIR/emsdk_env.sh" ] || {
  echo "tizen.sh: emsdk nao encontrado em $EMSDK_DIR" >&2
  echo "  git clone https://github.com/emscripten-core/emsdk ~/emsdk && cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest" >&2
  exit 2
}
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1

SAIDA="${NUVIO_SAIDA:-build/tizen}"
mkdir -p "$SAIDA"

# Os mesmos -D de servidor do Mac e da TV LG. Sem eles o app compila e a unica
# pista e a tela de login dizendo "Este pacote foi montado sem servidor".
ENV_D=$(tools/env.sh)

# -lidbfs.js NAO E OPCIONAL. Sem ele o objeto IDBFS simplesmente nao existe no
# JS gerado, FS.mount lanca, e a unica pista e a linha "[dados] IDBFS nao
# montou" — o app segue funcionando e esquece a sessao a cada recarga, que no
# alvo Tizen significa refazer o login por QR toda vez.
#
# PTHREADS DE VERDADE, e nao uma emulacao cooperativa.
#
# A duvida era se SharedArrayBuffer existe dentro de um .wgt — sem ele nao ha
# pthread no navegador. A Samsung DOCUMENTA o contrario: a pagina de WebAssembly
# deles manda usar exatamente `-pthread -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=N`
# em app de TV. Com isso os 18 arquivos que criam fio (~40 pthread_create) ficam
# INTOCADOS, e o leque paralelo de src/addons.c continua paralelo — foi ele que
# derrubou de 16,5 s a busca de fontes, consultando os addons ao mesmo tempo.
#
# POOL 12 com STRICT=0: 4 fios de fontes (ADD_FIOS) + legendas + sonda + sync +
# descoberta + artes partindo no arranque. STRICT=0 deixa criar alem do pool em
# vez de falhar; o pool so evita a pausa de criar o worker na hora.
#
# UMA PENDENCIA CONHECIDA: a Samsung tambem passa `-s ENVIRONMENT_MAY_BE_TIZEN`,
# que so existe no fork Emscripten deles. O emcc de upstream nao conhece a
# flag. Se os workers nao subirem NA TV, e aqui que se olha primeiro — e a
# saida e trocar este emsdk pelo fork da Samsung, nao mexer no codigo C.
#
# ASYNCIFY, e nao emscripten_set_main_loop: o laco de quadro em src/main.c tem
# ~140 linhas de telemetria com estado em variaveis locais, e parti-lo num
# callback trocaria uma mudanca de 6 linhas por uma reescrita. O custo e
# tamanho e um pouco de desempenho — ambos a medir na TV, nao a supor.
#
# 128 MB INICIAIS QUE CRESCEM ATE 512 MB — e isto e PALPITE, nao medicao.
#
# A TV Samsung deu TELA PRETA depois de 100%. Nao sei a causa: nao ha aparelho
# nesta bancada e ainda nao houve log de la. Uma das hipoteses baratas e que
# 256 MB pedidos DE UMA VEZ, no arranque, sejam mais do que o firmware concede
# a um widget — e nesse caso a instanciacao do WASM falha antes de qualquer
# linha do app rodar, que e exatamente o sintoma. Comecar pequeno e crescer
# contorna isso se a hipotese estiver certa, e nao custa nada se estiver errada.
#
# O que decide de verdade e o painel de diagnostico de tools/tizen-shell.html.
# Se ele aparecer na tela, a memoria NAO era o problema e o texto dele diz o que
# e. Se a tela continuar preta sem painel nenhum, o proprio HTML nao chegou a
# rodar.
#
# NOTA HISTORICA, para nao voltar atras por engano: ALLOW_MEMORY_GROWTH com
# -pthread ja foi suspeito de causar um "memory access out of bounds" neste
# port. NAO ERA — troquei por memoria fixa e o defeito continuou; a causa era a
# pilha de fio pequena. Entao crescer aqui nao reintroduz aquele bug.
#
# Crescer parece a escolha segura e AQUI E O DEFEITO. Com -pthread, quando a
# memoria cresce as views JS (HEAPU8, HEAP32) dos OUTROS fios ficam obsoletas.
# O emcc avisa disso e o aviso e facil de ignorar, porque nada quebra no
# arranque: o app subiu, logou, sincronizou a conta inteira, e so entao um
# worker morreu com "Uncaught RuntimeError: memory access out of bounds" —
# dentro de nv_http, em src/rede.c, escrevendo o corpo da resposta byte a byte
# numa view que tinha acabado de deixar de valer.
#
# Teto fixo tambem e o que faz sentido numa TV: o orcamento de RAM e conhecido e
# pequeno, e o cache de texturas ja sabe despejar (contador "despejos" na
# telemetria de quadro). 16 MB do padrao nao serviria — acaba na primeira
# fileira de posteres.
# A ARTE VAI NO PACOTE, mas so um pedaco dela. deploy/app/art tem 236 MB, e
# collections/ (165 MB) e o catalogo CURADO de quem empacota — quem instalasse
# veria a colecao de outra pessoa. tools/tizen-art.sh escolhe o que pode sair
# daqui e ABORTA se credencial ou catalogo pessoal entrar no estagio.
#
# Sem isto o app abre com retangulo preto no lugar de cada icone, botao, logo,
# foto de elenco e miniatura de episodio: MEDIDO no navegador, com o log
# repetindo "[tex] decode falhou ... No such file or directory".
ARTE=$(bash tools/tizen-art.sh)

# SO png e jpg em SDL2_IMAGE_FORMATS, e NAO webp: o Emscripten nao tem port de
# libwebp, e pedir "webp" quebra o build do proprio SDL_image ("webp/decode.h
# file not found"). Os 41 selos webp sao convertidos para png por tizen-art.sh.
#
# E NAO PONHA COMENTARIO DENTRO DA LISTA DE ARGUMENTOS abaixo: um `#` no meio de
# uma linha continuada por `\` encerra o comando ali. O shell entao executa o
# resto como comando solto ("-sSDL2_IMAGE_FORMATS=[...]: comando nao encontrado"),
# o emcc roda sem os argumentos seguintes — inclusive sem --preload-file — e o
# build SAI, torto, sem arte nenhuma. Perdi uma rodada inteira nisso.

# Chrome 76 tem BigInt em JS, mas NAO a passagem i64 entre JS e WASM
# (Chrome 85). clock_gettime em marco_iniciar e a primeira chamada que a usa:
# o modulo instancia e cria GL, depois falha com "wasm function signature
# contains illegal type". esbuild nao altera a ABI do .wasm; WASM_BIGINT=0
# faz o emcc converter esses parametros para pares de i32. Regressao:
# tests/tizen-clock.sh, usando um V8 anterior ao suporte dessa integracao.

eval emcc src/*.c -o "$SAIDA/index.html" -O2 "$ENV_D" \
  -sWASM_BIGINT=0 \
  -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sUSE_SDL_TTF=2 \
  -sSDL2_IMAGE_FORMATS='["png","jpg"]' \
  -sMAX_WEBGL_VERSION=1 \
  -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=536870912 -Wno-pthreads-mem-growth \
  `# PILHAS DE 8 MB, e nao o padrao de 64 KB do emscripten. Esta build estava` \
  `# SEM as duas linhas, sozinha entre as builds do projeto: a bancada de teste` \
  `# do AVPlay ja usava 8 MB nos dois. Pilha de fio pequena ja custou caro aqui` \
  `# uma vez — foi ela, e nao o ALLOW_MEMORY_GROWTH que eu acusei na epoca, que` \
  `# causava um "memory access out of bounds". Estouro de pilha nao avisa: com` \
  `# ASSERTIONS=0 o app morre calado, que e o sintoma na TV.` \
  -sSTACK_SIZE=8388608 -sDEFAULT_PTHREAD_STACK_SIZE=8388608 \
  `# 32 KB de pilha do asyncify, o mesmo valor da bancada que roda. O laco de` \
  `# quadro desenrola por aqui a cada SwapWindow; 16 KB era aperto sem motivo.` \
  -sASYNCIFY -sASYNCIFY_STACK_SIZE=32768 \
  `# POOL DE 12. Ja esteve em 4, por um palpite meu que a evidencia derrubou:` \
  `# cortei supondo que o arranque estava LENTO por causa dos doze workers, e os` \
  `# marcos de tempo mostraram depois que ele estava CONGELADO, no WASM_BIGINT.` \
  `# A razao do corte evaporou e o corte tem custo real: o app tem 41 pontos de` \
  `# pthread_create, e quando o pool esgota cada fio novo exige criar um Worker` \
  `# e instanciar de novo os 3,2 MB de wasm. sessao_login_comecar cria fio na` \
  `# tela de login — que e onde a TV travou com o pool em 4.` \
  -pthread -sPTHREAD_POOL_SIZE=12 -sPTHREAD_POOL_SIZE_STRICT=0 \
  -sEXPORTED_FUNCTIONS='["_main","_malloc","_free"]' \
  -lidbfs.js \
  -sEXIT_RUNTIME=0 -sASSERTIONS="${NUVIO_ASSERTS:-1}" \
  --preload-file deploy/app/fonts@/app/fonts \
  --preload-file "$ARTE"@/app/art \
  --shell-file tools/tizen-shell.html

# REBAIXAR O GLUE PARA CHROMIUM 76 — sem isto o app NAO ARRANCA na TV.
#
# MEDIDO NO APARELHO, nao suposto. O userAgent da TV Samsung diz
# "SMART-TV; LINUX; Tizen 6.0 ... 76.0.3809.146". O emcc emite JS moderno e o
# glue morre na PRIMEIRA LINHA com "Uncaught SyntaxError: Unexpected token" —
# antes de uma linha sequer do nosso codigo. Na tela isso e PRETO depois de
# 100%, sem nenhuma outra pista; foi exatamente assim que apareceu.
#
# Contagem no index.js gerado: 45 optional chaining (?.), 36 nullish (??) e
# 11 ??= mais 18 ||= — todos Chrome 80/85, todos numa unica linha minificada de
# 278 KB, o que descarta remendo a mao.
#
# POR QUE NAO -sMIN_CHROME_VERSION=76, que seria o caminho obvio: este emsdk
# recusa com "MIN_CHROME_VERSION older than 85 is not supported". A alternativa
# seria instalar um emsdk antigo; transpilar a saida custa menos e nao prende o
# projeto a uma versao de SDK obsoleta.
#
# Os workers de pthread nascem DESTE MESMO arquivo, entao rebaixa-lo cobre o
# fio principal e os workers de uma vez.
if command -v npx >/dev/null 2>&1; then
  npx --yes esbuild@0.25.0 "$SAIDA/index.js" --target=chrome76 \
      --outfile="$SAIDA/index.chrome76.js" --log-level=warning
  mv "$SAIDA/index.chrome76.js" "$SAIDA/index.js"
  RESTO=$(grep -oE '\?\.|\?\?|\|\|=' "$SAIDA/index.js" | wc -l | tr -d ' ')
  echo "tizen.sh: glue rebaixado para chrome76 (sintaxe nova restante: $RESTO)"
else
  echo "tizen.sh: AVISO — npx ausente, glue NAO rebaixado; a TV vai dar tela preta" >&2
fi

echo "tizen.sh: $SAIDA/index.html  ($(du -h "$SAIDA/index.wasm" | cut -f1) de wasm)"
