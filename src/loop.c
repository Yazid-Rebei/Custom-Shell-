#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : loop.c
 * DESCRIPTION : Boucle REPL (Read-Evaluate-Print-Loop), affichage du prompt,
 *               bannière d'accueil animée et expansion de l'historique (!!, !n).
 *
 * COURS THÉORIQUE — Boucle Principale d'un Shell Interactif :
 * Le cycle fondamental d'un shell se décompose en 4 phases :
 * 1. Read (Lecture)    : Affiche l'invite (prompt_print) et lit une ligne
 *                        complète saisie par l'utilisateur via getline().
 * 2. Evaluate (Éval)   : Développe l'historique (!!/!n), analyse syntaxiquement
 *                        la ligne (parse_line) et l'exécute (execute_program).
 * 3. Print (Affichage) : Affiche la sortie standard et d'erreur des commandes.
 * 4. Loop (Boucle)     : Récolte les processus terminés (jobs_reap) et répète
 *                        le cycle jusqu'à exit ou EOF (Ctrl+D).
 *
 * Fonctions système et bibliothèque C utilisées :
 * - getcwd()       : lit le répertoire de travail pour l'afficher dans l'invite.
 * - gethostname()  : récupère le nom d'hôte de la machine hôte.
 * - getline()      : allocation et agrandissement automatique du tampon de saisie.
 * - nanosleep()    : cadence temporelle des animations de démarrage.
 * - isatty()       : vérifie si la sortie est un terminal pour activer les couleurs ANSI.
 * - fflush(stdout) : vide immédiatement le tampon d'écriture pour rafraîchir l'écran.
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
  #if __has_include("guess.h")
    #include "guess.h"
  #elif __has_include("../include/guess.h")
    #include "../include/guess.h"
  #elif __has_include("include/guess.h")
    #include "include/guess.h"
  #else
    #include "guess.h"
  #endif
#else
  #include "shell.h"
  #include "guess.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/**
 * prompt_print - Compose et affiche l'invite de commande interactive.
 * @sh: Pointeur vers l'état du shell.
 *
 * Affiche l'arborescence courante avec substitution du dossier personnel par '&'.
 * Si les couleurs sont activées, un cadre bicolore cyan/bleu moderne est affiché.
 */
void prompt_print(Shell *sh)
{
    char cwd[1024], display[1024], host[128] = "host";
    const char *user = getenv("USER");
    const char *home = getenv("HOME");

    if (!sh->interactive) return;

    if (!getcwd(cwd, sizeof cwd)) {
        strcpy(cwd, "?");
    }

    /* Remplacement du préfixe HOME par le symbole '&' à la frontière d'un chemin */
    if (home && *home && !strncmp(cwd, home, strlen(home)) &&
        (cwd[strlen(home)] == '/' || cwd[strlen(home)] == '\0')) {
        snprintf(display, sizeof display, "&%s", cwd + strlen(home));
    } else {
        snprintf(display, sizeof display, "%s", cwd);
    }

    if (!user) user = "user";
    gethostname(host, sizeof host - 1);

    if (!sh->no_color && isatty(STDOUT_FILENO)) {
        /* Style Solarized Dark minimaliste et épuré (Gris discret 90, Cyan 36, Bleu 34) */
        printf("\033[90m┌─\033[0m \033[36myazid_ssh\033[0m \033[90m::\033[0m %s@%s \033[90m::\033[0m \033[34m%s\033[0m\n\033[90m└─\033[36m>\033[0m ",
               user, host, display);
    } else {
        printf("yazid_ssh %s@%s:%s > ", user, host, display);
    }
    fflush(stdout);
}

/**
 * welcome_banner - Affiche l'écran d'accueil stylisé Solarized Dark avec animations séquentielles.
 * @sh: Pointeur vers l'état du shell.
 *
 * Utilise des temporisations nanosleep() pour animer le tracé du logo, les étapes
 * de diagnostics et la barre de progression sans surcharge de couleurs.
 */
static void welcome_banner(Shell *sh)
{
    static const char *logo[] = {
        " ██╗   ██╗ █████╗ ███████╗██╗██████╗     ███████╗███████╗██╗  ██╗",
        " ╚██╗ ██╔╝██╔══██╗╚══███╔╝██║██╔══██╗    ██╔════╝██╔════╝██║  ██║",
        "  ╚████╔╝ ███████║  ███╔╝ ██║██║  ██║    ███████╗███████╗███████║",
        "   ╚██╔╝  ██╔══██║ ███╔╝  ██║██║  ██║    ╚════██║╚════██║██╔══██║",
        "    ██║   ██║  ██║███████╗██║██████╔╝_██╗███████║███████║██║  ██║",
        "    ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝╚═════╝ ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝"
    };

    static const char *logo_plain[] = {
        "  __     __         _     _           ____ ____  _   _ ",
        "  \\ \\   / /_ _ ____(_) __| |         / ___/ ___|| | | |",
        "   \\ \\ / / _` |_  /| |/ _` |  _____  \\___ \\___ \\| |_| |",
        "    \\ V / (_| |/ / | | (_| | |_____|  ___) |__) |  _  |",
        "     \\_/ \\__,_/___||_|\\__,_|         |____/____/|_| |_|"
    };

    int color = !sh->no_color && isatty(STDOUT_FILENO);
    putchar('\n');

    if (color) {
        /* Tracé animé du logo en Solarized Cyan épuré */
        for (size_t i = 0; i < sizeof logo / sizeof logo[0]; i++) {
            printf("  \033[36m%s\033[0m\n", logo[i]);
            fflush(stdout);
            if (!sh->no_anim) {
                struct timespec t = {0, 30000000}; /* 30 ms */
                nanosleep(&t, NULL);
            }
        }
        printf("\n        \033[90m::\033[0m \033[36mYAZID SSH\033[0m \033[90m·  Solarized Dark Terminal  ::\033[0m\n\n");
        fflush(stdout);

        /* Animation séquentielle par étapes diagnostiques du bootloader */
        if (!sh->no_anim) {
            const char *spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            const char *steps[] = {
                "Vérification du moteur d'exécution POSIX C11...",
                "Chargement des variables et des alias...",
                "Initialisation de l'historique et des tubes...",
                "Indexation du moteur de devinette et complétion...",
                "Environnement prêt."
            };
            for (int s = 0; s < 5; s++) {
                int progress = (s + 1) * 20;
                char bar[25];
                int filled = progress / 5;
                for (int b = 0; b < 20; b++) {
                    bar[b] = (b < filled) ? '=' : ' ';
                }
                bar[20] = '\0';
                for (int f = 0; f < 3; f++) {
                    printf("\r  \033[36m[%s]\033[0m \033[90m[\033[34m%s\033[90m]\033[0m \033[33m%3d%%\033[0m \033[90m%s\033[0m",
                           spinner[(s * 3 + f) % 10], bar, progress, steps[s]);
                    fflush(stdout);
                    struct timespec t = {0, 20000000}; /* 20 ms */
                    nanosleep(&t, NULL);
                }
            }
            printf("\r  \033[32m[ok]\033[0m \033[90m[\033[34m====================\033[90m]\033[0m \033[32m100%%\033[0m \033[32mSystème opérationnel.\033[0m            \n\n");
        }

        puts("  \033[90m┌───────────────────────────────────────────────────────────────────┐\033[0m");
        puts("  \033[90m│\033[0m  \033[33m[tip]\033[0m  Appuyez sur \033[36m[TAB]\033[0m pour l'aide/guess, ou tapez \033[36m'guide'\033[0m   \033[90m│\033[0m");
        puts("  \033[90m│\033[0m  \033[34m[info]\033[0m \033[34m&\033[0m représente le dossier personnel (\033[34mHOME\033[0m)                   \033[90m│\033[0m");
        puts("  \033[90m└───────────────────────────────────────────────────────────────────┘\033[0m\n");
    } else {
        for (size_t i = 0; i < sizeof logo_plain / sizeof logo_plain[0]; i++) {
            puts(logo_plain[i]);
        }
        puts("\n  yazid_ssh - tapez [TAB] pour l'aide intelligente, ou 'guide'\n");
    }
}

/**
 * expand_history - Remplace les raccourcis !! ou !n par la commande correspondante.
 * @sh: Pointeur vers l'état du shell contenant l'historique en mémoire.
 * @line: Ligne saisie par l'utilisateur.
 *
 * Return: Nouvelle chaîne dupliquée (à libérer par l'appelant avec free()).
 */
static char *expand_history(Shell *sh, const char *line)
{
    if (!line) return NULL;

    /* '!!' : Rejoue la commande immédiatement précédente */
    if (!strcmp(line, "!!") && sh->history_count > 0) {
        return strdup(sh->history[sh->history_count - 1]);
    }

    /* '!n' : Rejoue la n-ième commande de l'historique */
    if (line[0] == '!' && line[1] >= '0' && line[1] <= '9') {
        char *end = NULL;
        long n = strtol(line + 1, &end, 10);
        if (end && !*end && n > 0 && (size_t)n <= sh->history_count) {
            return strdup(sh->history[n - 1]);
        }
    }

    return strdup(line);
}

/**
 * shell_loop - Boucle principale d'interaction REPL du shell.
 * @sh: Pointeur vers l'état global de la session.
 *
 * Gère séquentiellement :
 * - La récolte des jobs d'arrière-plan avec jobs_reap().
 * - L'affichage de l'invite prompt_print().
 * - La lecture de la saisie via guess_read_line() en mode interactif (avec [TAB] temps réel)
 *   ou getline() en mode tube / script.
 * - L'interruption douce par signal (EINTR).
 * - L'enregistrement dans l'historique de session.
 * - Le parsing et l'exécution du programme.
 *
 * Return: Code de sortie final du shell (sh->exit_status).
 */
int shell_loop(Shell *sh)
{
    char *line = NULL;
    size_t capacity = 0;

    if (sh->interactive) {
        welcome_banner(sh);
    }

    while (!sh->should_exit) {
        /* Vérification des tâches de fond terminées */
        jobs_reap(sh);

        char *line_to_process = NULL;
        char *interactive_line = NULL;

        if (sh->interactive && isatty(STDIN_FILENO)) {
            /* Affichage de l'invite de commande interactive */
            prompt_print(sh);
            interactive_line = guess_read_line(sh);
            if (!interactive_line) {
                if (sh->interactive) putchar('\n');
                break;
            }
            line_to_process = interactive_line;
        } else {
            /* Affichage de l'invite en mode non-interactif / pipe */
            prompt_print(sh);
            errno = 0;
            ssize_t length = getline(&line, &capacity, stdin);

            if (length < 0) {
                /* Si l'appel a été interrompu par un signal (ex: Ctrl+C), reprise de la boucle */
                if (errno == EINTR) {
                    clearerr(stdin);
                    if (sh->interactive) putchar('\n');
                    continue;
                }
                /* Fin de fichier (EOF / Ctrl+D) : fin de la session interactive */
                if (sh->interactive) putchar('\n');
                break;
            }

            /* Suppression du saut de ligne final */
            if (length > 0 && line[length - 1] == '\n') {
                line[length - 1] = '\0';
            }
            line_to_process = line;
        }

        /* Résolution des raccourcis d'historique */
        char *command = expand_history(sh, line_to_process);
        free(interactive_line);
        if (!command) continue;

        /* Saut des commandes vides */
        if (!*command) {
            free(command);
            continue;
        }

        /* Enregistrement dans l'historique en mémoire */
        char **entries = realloc(sh->history, (sh->history_count + 1) * sizeof *entries);
        if (entries) {
            sh->history = entries;
            sh->history[sh->history_count] = strdup(command);
            if (sh->history[sh->history_count]) {
                sh->history_count++;
            }
        }

        /* Commande spéciale pour réafficher la bannière */
        if (!strcmp(command, "yazid")) {
            welcome_banner(sh);
        } else {
            Program program = {0};
            char *error = NULL;

            if (parse_line(command, sh, &program, &error) < 0) {
                fprintf(stderr, "parse: %s\n", error ? error : "erreur de syntaxe");
            } else {
                execute_program(sh, &program, command);
                free_program(&program);
            }
            free(error);
        }

        free(command);
    }

    free(line);
    history_save(sh);
    return sh->exit_status;
}
