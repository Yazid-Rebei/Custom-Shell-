#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : signals.c
 * DESCRIPTION : Configuration et gestion des signaux POSIX pour le shell parent.
 *
 * COURS THÉORIQUE — Gestion des Signaux dans un Shell Interactif :
 * Sous Unix, les frappes clavier génèrent des signaux asynchrones :
 * - Ctrl+C envoie SIGINT  (interruption du programme)
 * - Ctrl+Z envoie SIGTSTP (suspension du programme)
 * - Ctrl+\ envoie SIGQUIT (quitter avec vidage de mémoire / core dump)
 *
 * Si le shell parent ne configurait pas de gestionnaire pour ces signaux,
 * le shell se fermerait brutalement dès que l'utilisateur appuie sur Ctrl+C
 * pour interrompre une commande !
 *
 * RÈGLE FONDAMENTALE :
 * 1. Le shell parent intercepte ou ignore ces signaux pour rester actif à l'invite.
 * 2. Lors d'un fork(), les processus enfants réactivent immédiatement le
 *    comportement par défaut (SIG_DFL) pour pouvoir être interrompus normalement
 *    par l'utilisateur (voir executor.c).
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
#include <signal.h>
#include <string.h>

/**
 * shell_signal_handler - Gestionnaire de signal volontairement neutre pour le shell parent.
 * @sig: Numéro du signal reçu (SIGINT, SIGTSTP, SIGQUIT).
 *
 * Ce gestionnaire capture le signal sans quitter le shell. Lors de la saisie
 * interactive, l'interruption interrompt getline() avec errno = EINTR,
 * ce qui permet à loop.c de réafficher un prompt propre sur une nouvelle ligne.
 */
static void shell_signal_handler(int sig)
{
    (void)sig;
}

/**
 * signals_init - Configure les dispositions de signaux pour le processus shell.
 *
 * FONCTIONS SYSTÈME UTILISÉES :
 * - sigemptyset(&sa.sa_mask) : initialise un masque vide de signaux bloqués.
 * - sigaction(signum, &act, NULL) : installe le gestionnaire struct sigaction
 *   de façon portable et conforme POSIX (plus robuste que l'ancien signal()).
 */
void signals_init(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = shell_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    /* Protection contre Ctrl+C, Ctrl+Z et Ctrl+\ dans le parent */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}
