#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : main.c
 * DESCRIPTION : Point d'entrée principal du shell, gestion des arguments
 *               de ligne de commande, initialisation globale et chargement de
 * .yazidrc.
 *
 * COURS THÉORIQUE — Cycle de Vie du Processus Shell :
 * 1. Initialisation : Le processus démarre dans main(argc, argv). Il configure
 *    les options globales (désactivation des animations avec --no-anim),
 *    initialise les structures de données (shell_init), et vérifie si l'entrée
 *    standard est un terminal interactif (isatty).
 * 2. Configuration des Signaux : signals_init() protège le shell parent contre
 *    les interruptions involontaires (Ctrl+C, Ctrl+Z, Ctrl+\).
 * 3. Persistance et Configuration : history_load() charge l'historique depuis
 *    ~/.yazid_history et config_load() applique les commandes du fichier
 * ~/.yazidrc.
 * 4. Boucle REPL : shell_loop() gère la saisie, l'évaluation et l'affichage.
 * 5. Nettoyage : shell_destroy() libère toutes les allocations mémoire avant
 * exit().
 *
 * Fonctions système et bibliothèque C utilisées :
 * - getenv("HOME") : localise le répertoire personnel de l'utilisateur.
 * - snprintf()     : formate le chemin absolu de ~/.yazidrc en évitant les
 * débordements de tampon.
 * - fopen(), fclose(), getline() : ouverture, fermeture et lecture ligne par
 * ligne de fichiers.
 * - isatty()       : teste si un descripteur (STDIN_FILENO) est associé à un
 * terminal interactif.
 * - fprintf()      : affiche les messages d'aide ou d'erreur sur stderr.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Déclaration de la boucle interactive REPL implémentée dans loop.c */
int shell_loop(Shell *sh);

/**
 * config_load - Lit et exécute les commandes contenues dans le fichier
 * ~/.yazidrc.
 * @sh: Pointeur vers l'état de la session shell.
 *
 * FONCTIONS UTILISÉES :
 * - getenv("HOME") : récupère la variable d'environnement HOME.
 * - fopen(path, "r") : ouvre le fichier de configuration en mode lecture.
 * - getline(&line, &cap, f) : alloue dynamiquement et lit une ligne complète de
 * longueur quelconque.
 * - strcspn(start, "\r\n") : supprime les caractères de fin de ligne.
 * - parse_line() & execute_program() : analyse et exécute chaque instruction.
 * - free_program() & free() : libère la mémoire après exécution de chaque
 * ligne.
 * - fclose(f) : referme le descripteur de fichier.
 *
 * Return: 0 en cas de succès ou si le fichier n'existe pas, -1 si le chemin
 * dépasse le tampon.
 */
int config_load(Shell *sh) {
  const char *home = getenv("HOME");
  if (!home)
    return 0;

  char path[4096];
  if (snprintf(path, sizeof path, "%s/.yazidrc", home) >= (int)sizeof path) {
    return -1;
  }

  FILE *f = fopen(path, "r");
  if (!f)
    return 0;

  char *line = NULL;
  size_t cap = 0;
  ssize_t n;

  while ((n = getline(&line, &cap, f)) >= 0) {
    (void)n;
    char *start = line;

    /* Saut des espaces et tabulations en début de ligne */
    while (*start == ' ' || *start == '\t')
      start++;

    /* Ignorer les lignes vides et les commentaires commençant par '#' */
    if (!*start || *start == '#' || *start == '\n' || *start == '\r') {
      continue;
    }

    /* Suppression des retours à la ligne */
    start[strcspn(start, "\r\n")] = '\0';

    Program p = {0};
    char *err = NULL;
    if (parse_line(start, sh, &p, &err) == 0) {
      execute_program(sh, &p, start);
      free_program(&p);
    }
    free(err);
  }

  free(line);
  fclose(f);
  return 0;
}

/**
 * main - Point d'entrée de Yazid Shell.
 * @argc: Nombre d'arguments passés au binaire.
 * @argv: Tableau des arguments sous forme de chaînes.
 *
 * FONCTIONS UTILISÉES :
 * - isatty(STDIN_FILENO) : détermine si l'entrée standard provient d'un clavier
 * interactif ou d'un script / tube (ex: pipe | ./yazid_shell).
 *
 * Return: Code de sortie final du shell retourné par la session (exit_status).
 */
int main(int argc, char **argv) {
  int no_anim = 0;

  /* Analyse des arguments de ligne de commande */
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--no-anim")) {
      no_anim = 1;
    } else {
      fprintf(stderr, "Usage: %s [--no-anim]\n", argv[0]);
      return 2;
    }
  }

  Shell sh;
  shell_init(&sh, no_anim);
  sh.interactive = isatty(STDIN_FILENO);

  /* Initialisation des sous-systèmes dans l'ordre requis */
  signals_init();
  history_load(&sh);
  config_load(&sh);

  /* Lancement de la boucle principale de lecture / exécution */
  int code = shell_loop(&sh);

  /* Libération des ressources allouées */
  shell_destroy(&sh);
  guess_cleanup();
  return code;
}
