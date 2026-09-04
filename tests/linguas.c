// A regra que importa: SEM preferencia, nada e filtrado. Foi o contrario disso
// (dois idiomas cravados no codigo) que deixava quem fala espanhol sem legenda
// nenhuma, em silencio.
//
//   cc tests/linguas.c src/linguas.c -Isrc -o /tmp/t-linguas && /tmp/t-linguas
#include "linguas.h"
#include <stdio.h>
#include <string.h>

static int falhas;
static void ok(const char *oque, int cond) {
  if (!cond) { printf("FALHOU: %s\n", oque); falhas++; }
}

int main(void) {
  // Sem preferencia nenhuma: tudo passa, inclusive idioma que a tabela nem
  // conhece.
  ok("vazio aceita ingles",   ling_casa("en", ""));
  ok("vazio aceita coreano",  ling_casa("kor", ""));
  ok("vazio aceita galego",   ling_casa("glg", ""));

  // Variantes do mesmo idioma casam entre si, nas duas familias ISO.
  ok("pt casa com por",   ling_casa("por", "pt"));
  ok("pt casa com pob",   ling_casa("pob", "pt"));
  ok("pt casa com pt-BR", ling_casa("pt-BR", "pt"));
  ok("en casa com eng",   ling_casa("eng", "en"));
  ok("es NAO casa com pt", !ling_casa("spa", "pt"));

  // Codigo desconhecido pelos dois lados: comparacao crua, sem inventar.
  ok("glg casa com glg",  ling_casa("glg", "glg"));
  ok("glg nao casa cat",  !ling_casa("glg", "cat"));

  // Nomes para a tela.
  ok("nome de pob", !strcmp(ling_nome("pob"), "Português (BR)"));
  ok("nome de spa", !strcmp(ling_nome("spa"), "Espanhol"));
  // Sem nome na tabela, o CODIGO em maiusculas — diz mais que "Legenda 3".
  ok("nome de glg", !strcmp(ling_nome("glg"), "GLG"));

  // As sentinelas do app web viram "sem filtro", nunca um idioma inventado.
  ling_conta_audio("DEVICE");   ok("DEVICE = sem filtro",  !ling_audio()[0]);
  ling_conta_audio("DEFAULT");  ok("DEFAULT = sem filtro", !ling_audio()[0]);
  ling_conta_legenda("off");    ok("off = sem filtro",     !ling_legenda()[0]);
  ling_conta_legenda("es");     ok("es entra",             !strcmp(ling_legenda(), "es"));

  // A escolha desta TV ganha da conta, e voltar para "" devolve o comando a ela.
  ling_local_legenda("fr");     ok("local vence a conta",  !strcmp(ling_legenda(), "fr"));
  ling_local_legenda("");       ok("sem local, volta a conta", !strcmp(ling_legenda(), "es"));

  // "Todas" na tela e "*" no codigo, e tem de significar SEM FILTRO.
  ling_local_legenda("*");      ok("* = sem filtro",       !ling_legenda()[0]);

  if (falhas) { printf("%d falha(s)\n", falhas); return 1; }
  printf("linguas ok\n");
  return 0;
}
