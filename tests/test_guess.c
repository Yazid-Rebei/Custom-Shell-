#define _POSIX_C_SOURCE 200809L
/*
 * Tests unitaires du moteur d'assistance intelligente guess et complétion [TAB].
 */
#include "shell.h"
#include "guess.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    Shell sh;
    shell_init(&sh, 1);
    sh.no_color = 1; /* Sortie propre sans codes ANSI pour les tests */

    /* 1. Vérification que 'guess' est bien enregistré comme built-in */
    assert(is_builtin("guess") == 1);

    /* 2. Test Scénario A2 : Auto-complétion préfixe unique ("yand" -> "yander ") */
    char buf[1024] = "yand";
    size_t len = 4;
    size_t pos = 4;
    int res = guess_handle_tab(&sh, buf, &len, sizeof buf, &pos);
    assert(res == 1);
    assert(strcmp(buf, "yander ") == 0);
    assert(len == 7);
    assert(pos == 7);

    /* 2b. Test Scénario A2 : Suggestions multiples et préfixe commun ("cle" -> "clear") */
    strcpy(buf, "cle");
    len = 3;
    pos = 3;
    res = guess_handle_tab(&sh, buf, &len, sizeof buf, &pos);
    assert(res == 1);
    assert(strcmp(buf, "clear") == 0);
    assert(len == 5);
    assert(pos == 5);

    /* 3. Test Scénario A1 : Commande exacte connue ("cd") */
    strcpy(buf, "cd");
    len = 2;
    pos = 2;
    res = guess_handle_tab(&sh, buf, &len, sizeof buf, &pos);
    assert(res == 0); /* Non modifié mais affiche la fiche d'aide */

    /* 4. Test Scénario A3 : Faute de frappe ("mrdir" -> suggestions rmdir, mkdir) */
    strcpy(buf, "mrdir");
    len = 5;
    pos = 5;
    res = guess_handle_tab(&sh, buf, &len, sizeof buf, &pos);
    assert(res == 0); /* Non modifié, affiche les suggestions avec formules et exemples */

    /* 5. Test Scénario B : Commande avec drapeaux contextuels ("tar -x") */
    strcpy(buf, "tar -x");
    len = 6;
    pos = 6;
    res = guess_handle_tab(&sh, buf, &len, sizeof buf, &pos);
    assert(res == 0); /* Affiche la fiche spécialisée extraction */

    /* 6. Test de la commande built-in 'guess' */
    char *argv_guess[] = {"guess", "mrdir", NULL};
    assert(builtin_guess(&sh, argv_guess) == 0);

    char *argv_tar[] = {"guess", "tar", "-x", NULL};
    assert(builtin_guess(&sh, argv_tar) == 0);

    shell_destroy(&sh);
    guess_cleanup();
    puts("guess tests passed successfully");
    return 0;
}
