# Ferramentas de desenvolvimento — app nativo

Coisas que não existiam e sem as quais era impossível trabalhar sozinho nesta
TV. Todas nasceram de um obstáculo concreto, não de gosto.

---

## 0. Compilar e rodar

```bash
bash tools/mac.sh          # compila e roda no Mac, JÁ LOGADO
bash tools/arm.sh          # compila para ARM, instala na TV e lança
bash tools/arm.sh --build  # só compila
bash tools/arm.sh --ipk    # gera também o .ipk, sem credencial nenhuma dentro
bash tools/testa-ipk.sh    # prova que o .ipk não leva credencial (sem docker)
bash tests/conta.sh        # testes de conta: logout, ajustes e QR
```

### A configuração do servidor entra por `-D`, não por arquivo

`tools/env.sh` lê o **mesmo `local.properties` do app web** e imprime os `-D`
que a compilação precisa: `NV_SUPABASE_URL`, `NV_SUPABASE_ANON_KEY`,
`NV_TV_LOGIN_BASE` e `NV_TRAKT_CLIENT_ID`. Nenhum desses valores entra em
arquivo de código versionado — eles viajam da propriedade direto para a linha
de comando do compilador.

**Sem eles o app compila, instala e abre**, e o único sintoma é a tela de login
dizendo "Este pacote foi montado sem servidor". Foi o que aconteceu no primeiro
deploy: os `-D` só tinham sido postos no `mac.sh`, e o `arm.sh` compila dentro
do container com uma linha própria. Hoje as variáveis entram no container por
`docker run --env-file` (passá-las na linha de `sh -c` exigiria aspas dentro de
aspas, que é onde isso quebra de novo) e o `arm.sh` **aborta** se `api.nuvio.tv`
não estiver no binário gerado.

### Ficar logado no Mac, sem escanear QR toda vez

A sessão é gravada em `$NUVIO_DADOS`, e `tools/mac.sh` já aponta essa variável
para `~/.nuvio`. Logue **uma vez** (escaneando o QR que aparece na tela) e a
partir daí todo `bash tools/mac.sh` abre com a conta já dentro — o refresh
token se renova sozinho quando vence.

CONFERIDO: primeiro arranque `[sessao] logado como …`; reiniciando sem escanear
nada, `[sessao] sessao restaurada …` seguido de `[addons] N vindos da conta`.

Para rodar como um usuário NOVO (testar a primeira execução de quem instala),
aponte a variável para uma pasta vazia:

```bash
NUVIO_DADOS=/tmp/nuvio-novo bash tools/mac.sh
```

"Sair da conta", em Ajustes, apaga `~/.nuvio` inteiro — sessão, ajustes e
progresso. Depois disso é preciso escanear de novo.

### O `.ipk` não pode levar `art/*.txt`

`art/` guarda credencial de PESSOA: o token do Trakt, as URLs de addon com a
chave do debrid embutida, as chaves do TMDB e do mdblist (esta com modo 0600) e
o `ajustes.txt` com o layout de quem montou. Enquanto o app dependia desses
arquivos isso não tinha jeito; com a conta, ele não depende mais, e o
empacotamento passa a sair de uma **cópia limpa**.

`tools/arm.sh --ipk` remove esses arquivos do palco e **confere o pacote
pronto**, abortando e apagando o `.ipk` se algum voltar.

> ARMADILHA MEDIDA: o `.ipk` é um pacote Debian (`ar` com `debian-binary` +
> `control.tar.gz` + `data.tar.gz`). `tar tzf pacote.ipk` lista **sem erro
> nenhum** apenas esses três nomes — nunca os arquivos do app. Uma conferência
> escrita assim passa sempre, inclusive com o segredo dentro. É preciso
> desempacotar o `ar` e listar o `data.tar.gz`.

`tools/testa-ipk.sh` prova isso sem docker e foi conferido nos dois sentidos:
com a exclusão o pacote sai limpo; deixando `trakt.txt` entrar de propósito, o
teste acusa e devolve 1.

### Lançar não reinicia

`luna://com.webos.applicationManager/launch` **não reinicia** um app que já está
rodando: apenas o traz para frente. Dois deploys seguidos foram lidos no log de
um processo antigo, e quase concluíram que a correção não tinha funcionado. O
título carrega os 8 primeiros dígitos do md5 do binário justamente para o
launcher responder qual build está ali. Para trocar de verdade:

```bash
PID=$(sshpass -p alpine ssh root@192.168.1.32 \
      "ps aux | grep -a 'native.legacy/nuvio-proto' | grep -v grep | awk '{print \$2}' | head -1")
sshpass -p alpine ssh root@192.168.1.32 "kill $PID"
# e só então lançar
```

E não relance em laço: **cada arranque consome cota do backend**, e quando ela
estoura o login falha, a TV cai em sessão anônima e sincroniza a conta errada —
o que parece bug do app.

---

## 1. Log em arquivo

Quando o SAM lança o app, `/proc/<pid>/fd/1` e `fd/2` apontam para
`/dev/null`. **Todo `printf` era descartado** — FPS, erros de shader, tudo.
Por isso `main.c` faz `freopen("/tmp/nuvio.log", ...)` logo no início.

```bash
(sleep 3; printf 'grep -a FPS /tmp/nuvio.log | tail -5\n'; sleep 3; printf 'exit\n') \
  | nc -w25 192.168.1.32 23 | tr -d '\0'
```

## 2. Captura de tela

O framebuffer não pode ser lido nem como root (`/dev/fb0` →
"Operation not permitted") e `com.webos.service.capture` responde erro. A
única saída é o app se fotografar: `glReadPixels` + BMP escrito à mão.

- BMP e não PNG porque o SDL2_image da TV **não tem escrita** (gravava 0 byte).
- BMP escrito à mão e não `SDL_SaveBMP` porque este converte pixel a pixel
  quando as máscaras não batem, e nessa CPU leva segundos — o arquivo era lido
  pela metade.
- Grava em temporário e renomeia, para nunca existir arquivo parcial.

```bash
bash /tmp/shot.sh 6          # build + deploy + captura + baixa + converte
```

## 3. Injeção de teclas

Permite abrir o detalhe e navegar sem ninguém no sofá com o controle.
Escreva teclas (`up`,`down`,`left`,`right`,`ok`,`back`,`log`) em `/tmp/nuvio-key`.
`log` abre e fecha o painel de registro na tela — o mesmo que a tecla vermelha
faz no controle, e o único jeito de exercitá-lo no Mac.

```bash
bash /tmp/grab.sh nome_do_arquivo down down
```

### A armadilha do sticky bit

`/tmp` é `drwxrwxrwt` e os arquivos criados pelo shell pertencem ao **root**,
enquanto o app roda como uid 5410. Ou seja: **o app não consegue apagá-los.**
Enquanto isso não foi visto, cada pedido era reprocessado a cada quadro — uma
única tecla `down` virava centenas e o foco corria até o fim da página.

Por isso os arquivos são **consumidos truncando** (`fopen("w")`), nunca
removendo, e um pedido só vale enquanto tiver conteúdo. Quem escreve precisa
dar `chmod 666` para que o app possa truncar.

## Tecla Back

**Resolvido, e esta seção estava desatualizada** — o parágrafo abaixo descrevia
uma solução que já não existe no código, e quem fosse depurar o Back por ela ia
procurar no lugar errado.

Hoje o Back **chega como tecla**: `main.c:306` pede
`SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true")` antes de criar a
janela, e a tecla aparece com o **scancode 482**
(`SDL_WEBOS_SCANCODE_BACK`, em `NV_SCANCODE_BACK`). `main.c` a traduz para
`SDLK_AC_BACK`, que é o que todas as telas já tratam. O mesmo cabeçalho do SDK
traz as coloridas: **RED 486, GREEN 487, YELLOW 488, BLUE 489** — lidas do
`SDL_webOS.h` do sysroot, não adivinhadas. Não existe hint equivalente para as
coloridas (só `..._KEYS_BACK` e `..._KEYS_EXIT`), então se a tecla vermelha não
abrir o painel de registro na TV, é aí que se olha primeiro — e não no número.

**Como era antes**, e por que a mudança importa: sem o hint, o Back não produzia
tecla nenhuma. Medido logando todos os eventos SDL: setas e Enter chegavam; o
Back produzia apenas `FOCUS_LOST` → `FOCUS_GAINED` em poucos milissegundos — o
compositor engolia a tecla, tirava o foco para tentar fechar o app e devolvia.
`main.c` tratava esse par dentro de 600 ms como Back, e separava do "sair de
verdade" (tecla Home) porque este produz `FOCUS_LOST` sem retorno. Aquele
código não existe mais.
