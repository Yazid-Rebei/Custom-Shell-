#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : utils.c
 * DESCRIPTION : Fonctions utilitaires, initialisation et libération de l'état du shell.
 *
 * COURS THÉORIQUE — Gestion du Cycle de Vie et Sécurité Mémoire :
 * En langage C, la gestion de la mémoire est explicite. Toute allocation dynamique
 * réalisée avec malloc(), realloc() ou strdup() doit être scrupuleusement libérée
 * avec free() pour éviter les fuites de mémoire (memory leaks).
 *
 * Fonctions de ce module :
 * - shell_init()    : initialise les champs de la structure Shell et prend en compte
 *                     les variables d'environnement NO_COLOR et YAZID_NO_ANIM.
 * - shell_destroy() : désalloue tous les tableaux et chaînes dynamiques à la fin
 *                     de la session utilisateur.
 * ============================================================================
 */

#if defined(__has_include)
  #if __has_include("shell.h")
    #include "shell.h"
  #elif __has_include("../include/shell.h")
    #include "../include/shell.h"
  #elif __has_include("include/shell.h")
    #include "include/shell.h"
  #else
    #include "shell.h"
  #endif
#else
  #include "shell.h"
#endif
#include <stdlib.h>
#include <string.h>

/**
 * shell_init - Initialise à zéro l'état global du shell et configure les options.
 * @sh: Pointeur vers l'état Shell à initialiser.
 * @no_anim: Drapeau indiquant si les animations doivent être désactivées (--no-anim).
 *
 * FONCTIONS UTILISÉES :
 * - memset() : initialise à zéro tous les octets de la structure.
 * - getenv() : lit les variables standards NO_COLOR et YAZID_NO_ANIM.
 * - strdup() : alloue la chaîne par défaut du prompt.
 */
void shell_init(Shell *sh, int no_anim)
{
    if (!sh) return;
    memset(sh, 0, sizeof *sh);

    sh->interactive = 1;
    sh->no_color = (getenv("NO_COLOR") != NULL);
    sh->no_anim = no_anim || (getenv("YAZID_NO_ANIM") != NULL);
    sh->prompt = strdup("");
}

/**
 * shell_destroy - Libère proprement toutes les ressources allouées pour la session.
 * @sh: Pointeur vers l'état Shell à détruire.
 *
 * Libère séquentiellement :
 * 1. Les entrées de l'historique et le tableau de pointeurs sh->history.
 * 2. Les alias définis et le tableau sh->aliases.
 * 3. Les chaînes de description des jobs d'arrière-plan.
 * 4. La chaîne personnalisée sh->prompt.
 */
void shell_destroy(Shell *sh)
{
    if (!sh) return;

    /* Libération de l'historique */
    for (size_t i = 0; i < sh->history_count; i++) {
        free(sh->history[i]);
    }
    free(sh->history);
    sh->history = NULL;
    sh->history_count = 0;

    /* Libération des alias */
    for (size_t i = 0; i < sh->alias_count; i++) {
        free(sh->aliases[i]);
    }
    free(sh->aliases);
    sh->aliases = NULL;
    sh->alias_count = 0;

    /* Libération des commandes des jobs */
    for (size_t i = 0; i < sh->job_count; i++) {
        free(sh->jobs[i].text);
        sh->jobs[i].text = NULL;
    }
    sh->job_count = 0;

    /* Libération du prompt */
    free(sh->prompt);
    sh->prompt = NULL;
}
