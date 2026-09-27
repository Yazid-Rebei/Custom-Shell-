/* Vérifications unitaires de la structure produite par parser.c. */
/* COURS — Tests unitaires : donner une entrée connue, puis vérifier le résultat.
 * assert() interrompt le test si une attente est fausse. Ici sont contrôlés
 * les arguments, le pipe, la redirection, les opérateurs et une erreur de quote. */
#include "shell.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Vérifie la conversion d'une ligne en pipeline/redirection/opérateurs.
 * Le second cas vérifie que le parser rejette une quote non refermée.
 */
int main(void)
{
    Shell sh;
    Program p = {0};
    char *error = NULL;
    shell_init(&sh, 1);
    assert(parse_line("echo 'hello world' | cat > out && pwd; echo done", &sh, &p, &error) == 0);
    assert(p.n == 3);
    assert(p.pipe[0].n == 2);
    assert(!strcmp(p.pipe[0].cmd[0].words.v[1], "hello world"));
    assert(p.pipe[0].cmd[1].nr == 1);
    assert(!strcmp(p.pipe[0].cmd[1].target[0], "out"));
    assert(!strcmp(p.op[0], "&&"));
    assert(!strcmp(p.op[1], ";"));
    free_program(&p);
    assert(parse_line("echo 'unfinished", &sh, &p, &error) < 0);
    free(error);
    shell_destroy(&sh);
    puts("parser tests passed");
    return 0;
}
