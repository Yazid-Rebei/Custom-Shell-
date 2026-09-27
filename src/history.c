#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : history.c
 * DESCRIPTION : Persistance de l'historique des commandes dans ~/.yazid_history.
 *
 * COURS THÉORIQUE — Gestion de l'Historique dans un Shell :
 * L'historique permet à l'utilisateur de retrouver et rejouer ses commandes
 * précédentes (ex: via la commande builtin 'history', ou les raccourcis
 * !! et !n).
 * - En mémoire vive : Un tableau dynamique de chaînes (char **history) grandit
 *   au fil des saisies via realloc().
 * - Sur disque : Au démarrage, history_load() lit le fichier ~/.yazid_history.
 *   À la fermeture du shell, history_save() réécrit l'historique mis à jour.
 *
 * Fonctions système et bibliothèque C utilisées :
 * - getenv("HOME") : localise le répertoire de base de l'utilisateur.
 * - fopen(path, "r"/"w") : ouvre le flux de fichier texte en lecture ou écriture.
 * - getline()      : lit dynamiquement chaque ligne sans limite de taille fixe.
 * - fprintf()      : sérialise chaque entrée de l'historique sur le disque.
 * - fclose()       : flush et libère le flux de fichier.
 * - realloc()      : agrandit dynamiquement le tableau de pointeurs.
 * - strdup()       : copie chaque commande dans le tas (heap).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * histfile - Construit et alloue le chemin absolu du fichier d'historique ~/.yazid_history.
 *
 * FONCTIONS UTILISÉES :
 * - getenv("HOME") : lit le dossier personnel.
 * - malloc()       : réserve la mémoire pour la chaîne du chemin.
 * - snprintf()     : concatène le dossier personnel et le nom de fichier.
 *
 * Return: Pointeur vers la chaîne allouée (à libérer avec free()), ou NULL en cas d'erreur.
 */
static char *histfile(void)
{
    const char *home = getenv("HOME");
    if (!home) return NULL;

    size_t n = strlen(home) + 24;
    char *path = malloc(n);
    if (path) {
        snprintf(path, n, "%s/.yazid_history", home);
    }
    return path;
}

/**
 * history_load - Charge les lignes enregistrées dans ~/.yazid_history dans la mémoire du shell.
 * @sh: Pointeur vers l'état du shell.
 *
 * FONCTIONS UTILISÉES :
 * - fopen(p, "r") : ouvre en lecture seule.
 * - getline(&line, &n, f) : lit chaque ligne de l'historique.
 * - strcspn(line, "\r\n") : élimine les retours chariot et sauts de ligne.
 * - realloc() : agrandit le tableau sh->history pour accueillir le nouveau pointeur.
 * - strdup()  : duplique la ligne lue.
 */
void history_load(Shell *sh)
{
    char *path = histfile();
    if (!path) return;

    FILE *f = fopen(path, "r");
    free(path);
    if (!f) return;

    char *line = NULL;
    size_t n = 0;

    while (getline(&line, &n, f) > 0) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!*line) continue; /* Ignore les lignes vides */

        char *entry = strdup(line);
        if (!entry) break;

        char **new_history = realloc(sh->history, (sh->history_count + 1) * sizeof *new_history);
        if (!new_history) {
            free(entry);
            break;
        }

        sh->history = new_history;
        sh->history[sh->history_count++] = entry;
    }

    free(line);
    fclose(f);
}

/**
 * history_save - Sauvegarde l'ensemble de l'historique en mémoire dans ~/.yazid_history.
 * @sh: Pointeur vers l'état du shell.
 *
 * FONCTIONS UTILISÉES :
 * - fopen(p, "w") : ouvre ou tronque le fichier en écriture.
 * - fprintf(f, "%s\n", line) : écrit chaque commande sur une ligne distincte.
 * - fclose(f)     : synchronise et ferme le fichier.
 */
void history_save(Shell *sh)
{
    char *path = histfile();
    if (!path) return;

    FILE *f = fopen(path, "w");
    free(path);
    if (!f) return;

    for (size_t i = 0; i < sh->history_count; i++) {
        fprintf(f, "%s\n", sh->history[i]);
    }

    fclose(f);
}
