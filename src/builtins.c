#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : builtins.c
 * DESCRIPTION : Implémentation des commandes intégrées (built-ins) du shell.
 *
 * COURS THÉORIQUE — Commandes Internes vs Externes :
 * Une commande externe s'exécute dans un processus enfant via fork() et
 * execvp(). Cependant, certaines commandes doivent impérativement modifier
 * l'état interne du shell parent (ex: répertoire de travail avec cd/chdir,
 * variables d'environnement avec export/unsetenv, ou fin de session avec exit).
 * Si ces commandes étaient exécutées dans un processus enfant, leurs
 * modifications disparaîtraient dès la fin de l'enfant sans impacter le shell
 * interactif.
 *
 * Fonctions système et bibliothèque C utilisées dans ce module :
 * - strcmp(), strncmp(), strlen(), strchr(), strdup(), strndup() : manipulation
 * de chaînes
 * - chdir()   : modification du répertoire de travail courant du processus
 * - getcwd()  : obtention du chemin absolu du répertoire courant
 * - opendir(), readdir(), closedir() : parcours des flux de répertoires
 * - stat()    : extraction des métadonnées d'un fichier/dossier (taille,
 * permissions, type S_ISDIR)
 * - getenv(), setenv(), unsetenv() : gestion des variables d'environnement du
 * processus
 * - nanosleep() : pause haute précision (en nanosecondes) pour les animations
 * du terminal
 * - isatty()  : détection d'un terminal interactif (évite les codes ANSI dans
 * un pipe/fichier)
 * - printf(), puts(), putchar(), perror() : affichage formaté et messages
 * d'erreur standard
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
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Déclaration externe de la table globale des variables d'environnement (POSIX)
 */
extern char **environ;

/**
 * has - Compare deux chaînes de caractères pour tester l'égalité exacte.
 * @n: Première chaîne (nom de la commande saisie).
 * @s: Seconde chaîne (nom cible attendu).
 *
 * FONCTION UTILISÉE :
 * - strcmp(s1, s2) : renvoie 0 si les deux chaînes sont strictement identiques.
 *
 * Return: 1 si les chaînes sont égales, 0 sinon.
 */
static int has(const char *n, const char *s) {
  return (n && s && strcmp(n, s) == 0);
}

/**
 * animate_cd_start - Lance l'animation de recherche et transition de
 * répertoire.
 * @sh: Pointeur vers l'état global du shell (options no_anim, interactive).
 * @dest: Nom ou chemin du répertoire de destination.
 *
 * FONCTIONS UTILISÉES :
 * - isatty(STDOUT_FILENO) : vérifie si la sortie standard est connectée à un
 * terminal.
 * - nanosleep(&t, NULL)   : endort le processus pendant un délai précis (35
 * ms).
 * - fflush(stdout)        : force l'écriture immédiate du tampon vers le
 * terminal.
 */
static void animate_cd_start(Shell *sh, const char *dest) {
  if (!sh->interactive || sh->no_anim || !isatty(STDOUT_FILENO))
    return;

  const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴"};
  for (int i = 0; i < 6; i++) {
    printf("\r\033[36m[%s]\033[0m \033[90mNavigation vers\033[0m \033[36m%s\033[0m... ",
           frames[i], dest ? dest : "~");
    fflush(stdout);
    struct timespec t = {0, 25000000}; /* 25 millisecondes */
    nanosleep(&t, NULL);
  }
}

/**
 * animate_cd_finish - Affiche le résultat (succès ou échec) après l'appel à
 * chdir().
 * @sh: Pointeur vers l'état du shell.
 * @dest: Répertoire ciblé.
 * @success: 1 si chdir a réussi, 0 en cas d'erreur.
 */
static void animate_cd_finish(Shell *sh, const char *dest, int success) {
  if (!sh->interactive || sh->no_anim || !isatty(STDOUT_FILENO))
    return;

  if (success) {
    printf("\r\033[K\033[32m[ok]\033[0m \033[90mdossier :\033[0m \033[34m%s\033[0m\n",
           dest ? dest : "~");
  } else {
    printf("\r\033[K\033[31m[err]\033[0m \033[90méchec navigation vers :\033[0m \033[31m%s\033[0m\n",
           dest ? dest : "~");
  }
  fflush(stdout);
}

/**
 * animate_pwd - Affiche une animation spinner lors de la consultation du
 * répertoire courant.
 * @sh: Pointeur vers l'état du shell.
 */
static void animate_pwd(Shell *sh) {
  if (!sh->interactive || sh->no_anim || !isatty(STDOUT_FILENO))
    return;

  const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴"};
  for (int i = 0; i < 6; i++) {
    printf(
        "\r\033[36m[%s]\033[0m \033[90mLocalisation du répertoire...\033[0m",
        frames[i]);
    fflush(stdout);
    struct timespec t = {0, 22000000}; /* 22 ms */
    nanosleep(&t, NULL);
  }
  fputs("\r\033[K", stdout);
  fflush(stdout);
}

/**
 * animate_ls - Affiche un indicateur dynamique d'exploration d'un dossier.
 * @sh: Pointeur vers l'état du shell.
 * @target: Chemin exploré.
 */
static void animate_ls(Shell *sh, const char *target) {
  if (!sh->interactive || sh->no_anim || !isatty(STDOUT_FILENO))
    return;

  const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴"};
  for (int i = 0; i < 6; i++) {
    printf("\r\033[36m[%s]\033[0m \033[90mscan ::\033[0m \033[34m%s\033[0m",
           frames[i], target ? target : ".");
    fflush(stdout);
    struct timespec t = {0, 20000000};
    nanosleep(&t, NULL);
  }
  fputs("\r\033[K", stdout);
  fflush(stdout);
}

/**
 * print_yander - Commande intégrée 'yander' : affiche les métadonnées
 * officielles du shell.
 * @sh: Pointeur vers l'état global du shell.
 */
static int print_yander(Shell *sh) {
  int color = !sh->no_color && isatty(STDOUT_FILENO);

  /* Animation visuelle dynamique avec spinner haute précision et étapes claires */
  if (sh->interactive && !sh->no_anim && isatty(STDOUT_FILENO)) {
    const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const char *steps[] = {"Chargement du sous-système POSIX C11...",
                           "Inspection de l'environnement et des flux...",
                           "Extraction des métadonnées logicielles...",
                           "Prêt"};
    for (int s = 0; s < 4; s++) {
      for (int f = 0; f < 3; f++) {
        printf("\r\033[36m[%s]\033[0m \033[90m%s\033[0m",
               frames[(s * 3 + f) % 10], steps[s]);
        fflush(stdout);
        struct timespec t = {0, 22000000}; /* 22 ms */
        nanosleep(&t, NULL);
      }
    }
    fputs("\r\033[K", stdout);
    fflush(stdout);
  }

  if (color) {
    puts("\033[90m┌────────────────────────────────────────────────────────────┐\033[0m");
    puts("\033[90m│\033[0m               \033[36m:: YAZID SHELL & SSH TERMINAL ::\033[0m             \033[90m│\033[0m");
    puts("\033[90m├────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mInterpréteur\033[0m  : \033[36myazid_shell\033[0m \033[90m(POSIX C11)\033[0m                 \033[90m│\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mVersion\033[0m       : \033[36m1.1.0\033[0m \033[32m(Édition Officielle)\033[0m              \033[90m│\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mMise à jour\033[0m   : \033[36m26 Septembre 2026\033[0m                       \033[90m│\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mArchitecture\033[0m  : Multi-processus & Pipelines              \033[90m│\033[0m");
    puts("\033[90m├────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mCopyright\033[0m     : \033[32m2026\033[0m Yazid et Skander                   \033[90m│\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mAuteurs\033[0m       : \033[36mYazid\033[0m \033[90m(Lead)\033[0m & \033[36mSkander\033[0m \033[90m(Dev)\033[0m            \033[90m│\033[0m");
    puts("\033[90m│\033[0m  \033[90m•\033[0m \033[90mStatut\033[0m        : \033[32m[ok]\033[0m Opérationnel · Prêt pour session   \033[90m│\033[0m");
    puts("\033[90m└────────────────────────────────────────────────────────────┘\033[0m");
  } else {
    puts("==============================================================");
    puts("               :: YAZID SHELL & SSH TERMINAL ::               ");
    puts("--------------------------------------------------------------");
    puts("  • Interpréteur  : yazid_shell (POSIX C11)                 ");
    puts("  • Version       : 1.1.0 (Édition Officielle)              ");
    puts("  • Mise à jour   : 26 Septembre 2026                       ");
    puts("  • Architecture  : Multi-processus & Pipelines             ");
    puts("--------------------------------------------------------------");
    puts("  • Copyright ©   : 2026 Yazid et Skander                   ");
    puts("  • Auteurs       : Yazid (Lead) & Skander (Dev)            ");
    puts("  • Statut        : Opérationnel · Prêt pour utilisation    ");
    puts("==============================================================");
  }
  fflush(stdout);
  return 0;
}

/**
 * list_dir - Parcourt et liste le contenu d'un répertoire avec distinction
 * visuelle.
 * @sh: Pointeur vers l'état du shell.
 * @path: Chemin du fichier ou répertoire à examiner.
 * @show_hidden: Si non nul, affiche aussi les fichiers cachés (commençant par
 * '.').
 *
 * FONCTIONS SYSTÈME ET BIBLIOTHÈQUE C :
 * - opendir(path) : ouvre un flux de répertoire et retourne un pointeur DIR*.
 * - readdir(DIR*) : lit séquentiellement chaque entrée struct dirent* du
 * répertoire.
 * - stat(full_path, &st) : inspecte les attributs inode; S_ISDIR(st.st_mode)
 * vérifie s'il s'agit d'un sous-dossier pour lui appliquer la couleur bleue
 * (\033[1;34m).
 * - closedir(DIR*) : libère le descripteur et les ressources associées au
 * répertoire.
 *
 * Return: 0 si la lecture a réussi, 1 en cas d'erreur (ex: dossier
 * inaccessible).
 */
static int list_dir(Shell *sh, const char *path, int show_hidden) {
  int color = !sh->no_color && isatty(STDOUT_FILENO);
  char resolved[4096];

  /* Résolution des raccourcis & et ~ représentant le répertoire HOME */
  if (!path || !strcmp(path, "&") || !strcmp(path, "~")) {
    const char *h = getenv("HOME");
    snprintf(resolved, sizeof resolved, "%s", h ? h : ".");
  } else if ((path[0] == '&' || path[0] == '~') && path[1] == '/') {
    const char *h = getenv("HOME");
    snprintf(resolved, sizeof resolved, "%s%s", h ? h : "", path + 1);
  } else {
    snprintf(resolved, sizeof resolved, "%s", path);
  }

  struct stat path_stat;
  if (stat(resolved, &path_stat) != 0) {
    perror(path ? path : "ls");
    return 1;
  }

  /* Si c'est un fichier simple et non un dossier, affichage direct */
  if (!S_ISDIR(path_stat.st_mode)) {
    if (color)
      printf("\033[90m%s\033[0m\n", path);
    else
      puts(path);
    return 0;
  }

  DIR *d = opendir(resolved);
  if (!d) {
    perror(path ? path : "ls");
    return 1;
  }

  struct dirent *e;
  int column = 0;
  while ((e = readdir(d)) != NULL) {
    /* Filtrage des entrées masquées si -a n'a pas été demandé */
    if (!show_hidden && e->d_name[0] == '.')
      continue;

    char full[4096];
    int n = snprintf(full, sizeof full, "%s/%s", resolved, e->d_name);
    struct stat st;
    int is_dir = (n > 0 && (size_t)n < sizeof full && stat(full, &st) == 0 &&
                  S_ISDIR(st.st_mode));

    char display_name[512];
    if (is_dir)
      snprintf(display_name, sizeof display_name, "%s/", e->d_name);
    else
      snprintf(display_name, sizeof display_name, "%s", e->d_name);

    /* Formatage en grille de 4 colonnes */
    if (color) {
      if (is_dir) {
        printf("\033[1;34m%-20s\033[0m  ", display_name); /* Dossier en bleu */
      } else {
        printf("\033[90m%-20s\033[0m  ", display_name); /* Fichier en gris */
      }
    } else {
      printf("%-20s  ", display_name);
    }
    if (++column % 4 == 0)
      putchar('\n');
  }
  if (column % 4 != 0)
    putchar('\n');
  closedir(d);
  return 0;
}

/**
 * print_guide - Affiche le guide interactif avec la description des commandes
 * et symboles.
 * @sh: Pointeur vers l'état du shell.
 *
 * AMÉLIORATIONS APPORTÉES :
 * - Animation spinner lors de l'appel en mode interactif.
 * - Structuration en 4 blocs distincts pour une clarté maximale.
 * - Couleurs contrastées (jaune pour les commandes, vert pour les sections,
 * cyan pour les paramètres).
 * - Alignement calibré à 80 caractères pour une lisibilité parfaite sans
 * débordement.
 */
static void print_guide(Shell *sh) {
  int c = !sh->no_color && isatty(STDOUT_FILENO);

  /* Animation visuelle subtile au lancement du guide */
  if (sh->interactive && !sh->no_anim && isatty(STDOUT_FILENO)) {
    const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
    const char *steps[] = {"Indexation des commandes intégrées...",
                           "Vérification des flux et pipelines...",
                           "Formatage de la syntaxe...",
                           "Guide interactif prêt !"};
    for (int i = 0; i < 4; i++) {
      printf("\r\033[1;36m[%s]\033[0m \033[1;37m%s\033[0m", frames[i],
             steps[i]);
      fflush(stdout);
      struct timespec t = {0, 22000000};
      nanosleep(&t, NULL);
    }
    fputs("\r\033[K", stdout);
    fflush(stdout);
  }

  if (c) {
    puts("\n\033[90m┌──────────────────────────────────────────────────────────────────────────────┐\033[0m");
    puts("\033[90m│\033[0m              \033[36m:: GUIDE DU SHELL · COMMANDES & SYNTAXE ::\033[0m                      \033[90m│\033[0m");
    puts("\033[90m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[36m[1] COMMANDES ESSENTIELLES & NAVIGATION\033[0m                                     \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mhelp, guide\033[0m          Afficher ce guide interactif d'utilisation            \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36m[TAB] ou guess\033[0m       Assistance intelligente, devinette & complétion       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcd\033[0m \033[90m[dossier]\033[0m         Navigation (\033[34mcd &\033[0m ou \033[34mcd ~\033[0m = dossier HOME, \033[34mcd -\033[0m)         \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mpwd\033[0m                  Localiser et afficher le répertoire de travail        \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mls\033[0m \033[90m[-a] [dossier]\033[0m    Lister les fichiers (\033[34mdossiers en bleu\033[0m, fichiers)       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mecho\033[0m \033[90m[-n] [texte]\033[0m    Afficher un message ou la valeur d'une variable        \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mclear\033[0m                Effacer complètement l'écran du terminal              \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mexit\033[0m \033[90m[code]\033[0m          Quitter la session avec code de retour (défaut: 0)     \033[90m│\033[0m");
    puts("\033[90m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[36m[2] GESTION DU SYSTÈME & VARIABLES\033[0m                                          \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mexport\033[0m \033[90mVAR=val\033[0m       Définir ou modifier une variable d'environnement       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36munset\033[0m \033[90mVAR\033[0m            Supprimer une variable d'environnement                 \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mhistory\033[0m              Consulter l'historique (\033[33m!n\033[0m ou \033[33m!!\033[0m pour rejouer)         \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36malias / unalias\033[0m      Créer ou supprimer un alias (ex: alias ll=\"ls -a\")     \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mtype / which\033[0m \033[90mNOM\033[0m     Vérifier si commande builtin ou chemin du binaire      \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mjobs / fg / bg\033[0m       Superviser et contrôler les tâches en arrière-plan     \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36msname\033[0m \033[90m[titre]\033[0m        Personnaliser le titre de session ou le prompt         \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36myander\033[0m               Métadonnées (version, date, copyright, auteurs)        \033[90m│\033[0m");
    puts("\033[90m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[36m[3] TUBES & REDIRECTIONS D'E/S (FLUX)\033[0m                                       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd1\033[0m \033[34m|\033[0m \033[36mcmd2\033[0m          Tube : connecte la sortie stdout1 à l'entrée stdin2    \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd\033[0m \033[34m>\033[0m \033[90mfichier\033[0m        Redirige stdout vers fichier (écrase le contenu)       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd\033[0m \033[34m>>\033[0m \033[90mfichier\033[0m       Redirige stdout vers fichier (ajoute à la fin)         \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd\033[0m \033[34m<\033[0m \033[90mfichier\033[0m        Redirige stdin pour lire depuis un fichier             \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd\033[0m \033[34m2>\033[0m \033[90mfichier\033[0m       Redirige les messages d'erreurs (stderr)               \033[90m│\033[0m");
    puts("\033[90m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[36m[4] OPÉRATEURS, SYMBOLES & EXPANSIONS\033[0m                                       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd1\033[0m \033[34m&&\033[0m \033[36mcmd2\033[0m         ET logique : exécute cmd2 uniquement si cmd1 réussit  \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd1\033[0m \033[34m||\033[0m \033[36mcmd2\033[0m         OU logique : exécute cmd2 uniquement si cmd1 échoue   \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd1\033[0m \033[34m;\033[0m \033[36mcmd2\033[0m          Séquence : exécute cmd1 puis cmd2 successivement        \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[36mcmd\033[0m \033[34m&\033[0m                Arrière-plan : détache la commande sans bloquer         \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[34m&\033[0m ou \033[34m~\033[0m               Dossier HOME personnel (ex: cd &, cd ~/Bureau)         \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[33m$VAR\033[0m, \033[33m$?\033[0m, \033[33m$$\033[0m        Expansions : valeur variable, code retour et PID       \033[90m│\033[0m");
    puts("\033[90m│\033[0m    \033[90m' '\033[0m et \033[90m\" \"\033[0m           Quotes : simples (texte brut) ou doubles (expansions)  \033[90m│\033[0m");
    puts("\033[90m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[90m│\033[0m  \033[33m[tip]\033[0m \033[90mTapez 'yander' pour les métadonnées | 'clear' pour vider l'écran\033[0m       \033[90m│\033[0m");
    puts("\033[90m└──────────────────────────────────────────────────────────────────────────────┘\033[0m\n");
  } else {
    puts("\n========================= YAZID_SSH - GUIDE DU SHELL "
         "=========================");
    puts("[1] COMMANDES ESSENTIELLES & NAVIGATION :");
    puts("  help, guide          Afficher ce guide interactif d'utilisation");
    puts("  [TAB] ou guess       Assistance intelligente, devinette & "
         "completion");
    puts("  cd [dossier]         Navigation (cd & ou cd ~ = dossier HOME, cd "
         "-)");
    puts("  pwd                  Localiser et afficher le repertoire de "
         "travail");
    puts("  ls [-a] [dossier]    Lister les fichiers et dossiers");
    puts("  echo [-n] [texte]    Afficher un message ou la valeur d'une "
         "variable");
    puts("  clear                Effacer completement l'ecran du terminal");
    puts("  exit [code]          Quitter la session avec code de retour "
         "(defaut: 0)");
    puts("[2] GESTION DU SYSTEME & VARIABLES :");
    puts("  export VAR=val       Definir ou modifier une variable "
         "d'environnement");
    puts("  unset VAR            Supprimer une variable d'environnement");
    puts("  history              Consulter l'historique (!n ou !! pour "
         "rejouer)");
    puts("  alias / unalias      Creer ou supprimer un alias (ex: alias "
         "ll=\"ls -a\")");
    puts("  type / which NOM     Verifier si commande builtin ou chemin du "
         "binaire");
    puts("  jobs / fg / bg       Superviser et controler les taches en "
         "arriere-plan");
    puts("  sname [titre]        Personnaliser le titre de session ou le "
         "prompt");
    puts("  yander               Metadonnees (version, date, copyright, "
         "auteurs)");
    puts("[3] TUBES & REDIRECTIONS D'E/S (FLUX) :");
    puts("  cmd1 | cmd2          Tube : connecte la sortie stdout1 a l'entree "
         "stdin2");
    puts("  cmd > fichier        Redirige stdout vers fichier (ecrase le "
         "contenu)");
    puts("  cmd >> fichier       Redirige stdout vers fichier (ajoute a la "
         "fin)");
    puts("  cmd < fichier        Redirige stdin pour lire depuis un fichier");
    puts("  cmd 2> fichier       Redirige les messages d'erreurs (stderr)");
    puts("[4] OPERATEURS, SYMBOLES & EXPANSIONS :");
    puts("  cmd1 && cmd2         ET logique : execute cmd2 uniquement si cmd1 "
         "reussit");
    puts("  cmd1 || cmd2         OU logique : execute cmd2 uniquement si cmd1 "
         "echoue");
    puts("  cmd1 ; cmd2          Sequence : execute cmd1 puis cmd2 "
         "successivement");
    puts("  cmd &                Arriere-plan : detache la commande sans "
         "bloquer");
    puts("  & ou ~               Dossier HOME personnel (ex: cd &, cd "
         "~/Bureau)");
    puts("  $VAR, $?, $$         Expansions : valeur variable, code retour et "
         "PID");
    puts("  ' ' et \" \"           Quotes : simples (texte brut) ou doubles "
         "(expansions)");
    puts("---------------------------------------------------------------------"
         "---------");
    puts("  Astuce : Tapez 'yander' pour les infos auteurs & version | 'clear' "
         "pour vider");
    puts("====================================================================="
         "=========\n");
  }
}

/**
 * is_builtin - Vérifie si une commande appartient au jeu de built-ins du shell.
 * @n: Nom de la commande testée.
 *
 * Return: 1 si la commande est gérée en interne, 0 si elle doit être recherchée
 * dans PATH.
 */
int is_builtin(const char *n) {
  if (!n)
    return 0;
  const char *b[] = {"cd",    "help",    "guide", "sname",  "clear", "exit",
                     "pwd",   "ls",      "echo",  "export", "unset", "history",
                     "alias", "unalias", "type",  "which",  "jobs",  "fg",
                     "bg",    "yander",  "guess"};
  for (size_t i = 0; i < sizeof b / sizeof *b; i++) {
    if (has(n, b[i]))
      return 1;
  }
  return 0;
}

/**
 * builtin_run - Exécute une commande interne et renvoie son code de sortie.
 * @sh: Pointeur vers l'état de la session shell.
 * @a: Tableau argv de la commande (a[0] est le nom, a[1...] sont les
 * arguments).
 * @handled: Sortie booléenne (mise à 1 si la commande a été reconnue et
 * traitée).
 *
 * FONCTIONS SYSTÈME ET BIBLIOTHÈQUE C :
 * - chdir(path)   : change le répertoire de travail
 * - getcwd(buf)   : lit le répertoire de travail courant
 * - setenv(k,v,1) : définit ou écrase une variable d'environnement
 * - unsetenv(k)   : retire une variable de l'environnement
 * - access(f,X_OK): teste si un fichier existe et est exécutable
 * - strtok_r()    : découpe réentrante de la variable PATH
 * - strtol()      : convertit une chaîne en entier long avec détection d'erreur
 *
 * Return: Code de retour de la commande (0 pour succès, >0 en cas d'erreur).
 */
int builtin_run(Shell *sh, char **a, int *handled) {
  *handled = 1;
  const char *n = a[0];
  int status = 0;

  if (has(n, "guide") || has(n, "help")) {
    print_guide(sh);
    status = 0;
  } else if (has(n, "guess")) {
    status = builtin_guess(sh, a);
  } else if (has(n, "sname")) {
    puts("yazid_ssh");
    status = 0;
  } else if (has(n, "yander")) {
    status = print_yander(sh);
  } else if (has(n, "clear")) {
    /* Séquence ANSI pour effacer l'écran (\033[2J) et repositionner le curseur
     * en haut à gauche (\033[H) */
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    status = 0;
  } else if (has(n, "pwd")) {
    animate_pwd(sh);
    char b[4096];
    if (getcwd(b, sizeof b)) {
      if (!sh->no_color && isatty(STDOUT_FILENO))
        printf("\033[90mpwd ::\033[0m \033[34m%s\033[0m\n", b);
      else
        puts(b);
      status = 0;
    } else {
      perror("pwd");
      status = 1;
    }
  } else if (has(n, "ls")) {
    int hidden = 0, first = 1;
    /* Détection des options (ex: -a) */
    for (; a[first] && a[first][0] == '-' && a[first][1]; first++) {
      for (const char *p = a[first] + 1; *p; p++) {
        if (*p == 'a')
          hidden = 1;
      }
    }
    int count = 0;
    for (int i = first; a[i]; i++) {
      animate_ls(sh, a[i]);
      if (count++)
        printf("\n\033[1;36m%s:\033[0m\n", a[i]);
      if (list_dir(sh, a[i], hidden) != 0) {
        status = 1;
      }
    }
    if (!count) {
      animate_ls(sh, ".");
      if (list_dir(sh, ".", hidden) != 0) {
        status = 1;
      }
    }
  } else if (has(n, "cd")) {
    char old[4096];
    if (!getcwd(old, sizeof old))
      old[0] = '\0';
    const char *target = a[1];
    char resolved[4096];
    const char *d = NULL;

    if (!target || !strcmp(target, "&") || !strcmp(target, "~")) {
      d = getenv("HOME");
      if (!d) {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
      }
    } else if (!strcmp(target, "-")) {
      d = getenv("OLDPWD");
      if (d) {
        puts(d);
      } else {
        fprintf(stderr, "cd: OLDPWD not set\n");
        return 1;
      }
    } else if ((target[0] == '&' || target[0] == '~') && target[1] == '/') {
      const char *h = getenv("HOME");
      snprintf(resolved, sizeof resolved, "%s%s", h ? h : "", target + 1);
      d = resolved;
    } else {
      d = target;
    }

    animate_cd_start(sh, target ? target : "&");
    if (!d || chdir(d) < 0) {
      animate_cd_finish(sh, target ? target : "&", 0);
      perror("cd");
      status = 1;
    } else {
      animate_cd_finish(sh, target ? target : "&", 1);
      if (old[0])
        setenv("OLDPWD", old, 1);
      char b[4096];
      if (getcwd(b, sizeof b))
        setenv("PWD", b, 1);
      status = 0;
    }
  } else if (has(n, "echo")) {
    int i = 1, nl = 1;
    if (a[i] && !strcmp(a[i], "-n")) {
      nl = 0;
      i++;
    }
    for (; a[i]; i++) {
      printf("%s%s", i > (nl ? 1 : 2) ? " " : "", a[i]);
    }
    if (nl)
      putchar('\n');
    fflush(stdout);
    status = 0;
  } else if (has(n, "export")) {
    if (!a[1]) {
      /* Sans argument, export liste toutes les variables d'environnement
       * actives */
      for (char **ep = environ; ep && *ep; ep++) {
        printf("declare -x %s\n", *ep);
      }
      status = 0;
    } else {
      for (int i = 1; a[i]; i++) {
        char *eq = strchr(a[i], '=');
        if (eq) {
          *eq = '\0';
          if (setenv(a[i], eq + 1, 1) < 0) {
            perror("export");
            status = 1;
          }
          *eq = '=';
        } else {
          const char *v = getenv(a[i]);
          if (v)
            printf("declare -x %s=\"%s\"\n", a[i], v);
        }
      }
    }
  } else if (has(n, "unset")) {
    for (int i = 1; a[i]; i++) {
      unsetenv(a[i]);
    }
    status = 0;
  } else if (has(n, "history")) {
    for (size_t i = 0; i < sh->history_count; i++) {
      printf("%4zu  %s\n", i + 1, sh->history[i]);
    }
    status = 0;
  } else if (has(n, "alias")) {
    if (!a[1]) {
      for (size_t i = 0; i < sh->alias_count; i++) {
        puts(sh->aliases[i]);
      }
      status = 0;
    } else {
      char *name_end = strchr(a[1], '=');
      size_t name_len = name_end ? (size_t)(name_end - a[1]) : strlen(a[1]);
      int updated = 0;

      /* Recherche si un alias de même nom existe déjà pour mise à jour sur
       * place */
      for (size_t i = 0; i < sh->alias_count; i++) {
        char *eq = strchr(sh->aliases[i], '=');
        size_t elen =
            eq ? (size_t)(eq - sh->aliases[i]) : strlen(sh->aliases[i]);
        if (elen == name_len && !strncmp(sh->aliases[i], a[1], name_len)) {
          char *replacement = strdup(a[1]);
          if (replacement) {
            free(sh->aliases[i]);
            sh->aliases[i] = replacement;
          }
          updated = 1;
          break;
        }
      }

      if (!updated) {
        char *alias = strdup(a[1]);
        if (alias) {
          char **p = realloc(sh->aliases, (sh->alias_count + 1) * sizeof *p);
          if (p) {
            sh->aliases = p;
            sh->aliases[sh->alias_count++] = alias;
          } else {
            free(alias);
            status = 1;
          }
        } else {
          status = 1;
        }
      }
    }
  } else if (has(n, "unalias")) {
    if (a[1]) {
      for (int arg = 1; a[arg]; arg++) {
        const char *target = a[arg];
        size_t tlen = strlen(target);
        for (size_t i = 0; i < sh->alias_count; i++) {
          char *eq = strchr(sh->aliases[i], '=');
          size_t elen =
              eq ? (size_t)(eq - sh->aliases[i]) : strlen(sh->aliases[i]);
          if (elen == tlen && !strncmp(sh->aliases[i], target, tlen)) {
            free(sh->aliases[i]);
            /* Décalage des éléments suivants pour compacter le tableau */
            for (size_t j = i; j + 1 < sh->alias_count; j++) {
              sh->aliases[j] = sh->aliases[j + 1];
            }
            sh->alias_count--;
            i--;
          }
        }
      }
    }
    status = 0;
  } else if (has(n, "type") || has(n, "which")) {
    for (int i = 1; a[i]; i++) {
      if (is_builtin(a[i])) {
        printf("%s is a shell builtin\n", a[i]);
      } else {
        char *path = getenv("PATH");
        char *copy = path ? strdup(path) : NULL;
        char *save = NULL;
        char *dir = copy ? strtok_r(copy, ":", &save) : NULL;
        int found = 0;
        while (dir) {
          size_t z = strlen(dir) + strlen(a[i]) + 2;
          char *f = malloc(z);
          if (f) {
            snprintf(f, z, "%s/%s", dir, a[i]);
            if (access(f, X_OK) == 0) {
              printf("%s\n", f);
              found = 1;
              free(f);
              break;
            }
            free(f);
          }
          dir = strtok_r(NULL, ":", &save);
        }
        if (!found) {
          fprintf(stderr, "%s: not found\n", a[i]);
          status = 1;
        }
        free(copy);
      }
    }
  } else if (has(n, "jobs")) {
    jobs_reap(sh);
    for (size_t i = 0; i < sh->job_count; i++) {
      printf("[%zu] %s %s\n", i + 1, sh->jobs[i].running ? "Running" : "Done",
             sh->jobs[i].text);
    }
    status = 0;
  } else if (has(n, "fg") || has(n, "bg")) {
    fprintf(stderr, "%s: job control is not available in this build\n", n);
    status = 1;
  } else if (has(n, "exit")) {
    char *end = NULL;
    long code = a[1] ? strtol(a[1], &end, 10) : sh->last_status;
    if (a[1] && (!end || *end)) {
      fprintf(stderr, "exit: expected numeric status\n");
      return 1;
    }
    if (sh->interactive && !sh->no_anim && isatty(STDOUT_FILENO)) {
      const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴"};
      for (int i = 0; i < 6; i++) {
        printf("\r\033[90m[session]\033[0m \033[36m[%s]\033[0m \033[90mFermeture...\033[0m", frames[i]);
        fflush(stdout);
        struct timespec t = {0, 25000000};
        nanosleep(&t, NULL);
      }
      printf("\r\033[K\033[90m[session]\033[0m \033[32m[ok]\033[0m Session terminée. À bientôt !\n");
      fflush(stdout);
    }
    sh->should_exit = 1;
    sh->exit_status = (unsigned char)code;
    return sh->exit_status;
  } else {
    *handled = 0;
  }

  sh->last_status = status;
  return status;
}
