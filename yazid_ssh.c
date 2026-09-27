#define _POSIX_C_SOURCE 200809L
/* Prototype autonome yazid_ssh avec interface moderne et animations. */
/*
 * COURS — Programme autonome et comparaison d'implémentations
 * Ce fichier contient son propre main() et une autre boucle de shell. Le
 * Makefile actuel compile les fichiers C du dossier src, donc cette version
 * n'est pas celle obtenue
 * avec `make`; elle se compile séparément. C'est utile pour comparer une
 * version compacte (fork/exec, strtok_r) avec le shell modulaire du dossier
 * src (lexer, pipelines et redirections). Ici split_command() sépare seulement
 * les espaces : quotes, pipes, redirections et opérateurs ne sont pas exécutés,
 * même si print_guide() les décrit. Pour le moment, considérez ce fichier comme
 * une variante pédagogique en développement, pas comme la référence complète.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_LINE_SIZE 128
#define MAX_ARGS 64

/* Affiche le grand écran d'accueil animé. COURS : un tableau de chaînes permet
 * de dessiner ligne par ligne; les codes ANSI changent la couleur du terminal. */
static void welcome_animation(void)
{
    static const char *logo[] = {
        " ██╗   ██╗ █████╗ ███████╗██╗██████╗     ███████╗███████╗██╗  ██╗",
        " ╚██╗ ██╔╝██╔══██╗╚══███╔╝██║██╔══██╗    ██╔════╝██╔════╝██║  ██║",
        "  ╚████╔╝ ███████║  ███╔╝ ██║██║  ██║    ███████╗███████╗███████║",
        "   ╚██╔╝  ██╔══██║ ███╔╝  ██║██║  ██║    ╚════██║╚════██║██╔══██║",
        "    ██║   ██║  ██║███████╗██║██████╔╝_██╗███████║███████║██║  ██║",
        "    ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝╚═════╝ ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝"
    };

    putchar('\n');
    const char *colors[] = {
        "\033[1;36m", "\033[1;36m", "\033[1;34m",
        "\033[1;34m", "\033[1;35m", "\033[1;35m"
    };
    for (size_t i = 0; i < sizeof logo / sizeof logo[0]; i++) {
        printf("  %s%s\033[0m\n", colors[i], logo[i]);
        fflush(stdout);
        struct timespec t = {0, 40000000};
        nanosleep(&t, NULL);
    }
    printf("\n        \033[1;37m───  Y A Z I D _ S S H   ·   T E R M I N A L  ───\033[0m\n\n");
    fflush(stdout);

    /* Animation continue de chargement */
    const char *spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const char *steps[] = {
        "Initialisation du noyau yazid_ssh...",
        "Chargement des variables et des modules...",
        "Configuration de l'environnement...",
        "Préparation de l'historique...",
        "Système prêt !"
    };
    for (int s = 0; s < 5; s++) {
        int progress = (s + 1) * 20;
        char bar[25];
        int filled = progress / 5;
        for (int b = 0; b < 20; b++) {
            bar[b] = (b < filled) ? '=' : ' ';
        }
        bar[20] = '\0';
        printf("\r  \033[1;36m[%s]\033[0m \033[1;34m[%s]\033[0m \033[1;33m%3d%%\033[0m \033[2m%s\033[0m",
               spinner[s % 10], bar, progress, steps[s]);
        fflush(stdout);
        struct timespec t = {0, 60000000};
        nanosleep(&t, NULL);
    }
    printf("\r  \033[32m[ok]\033[0m \033[90m[====================]\033[0m \033[32m100%%\033[0m \033[32mSystème prêt !\033[0m            \n\n");

    puts("  \033[90m┌───────────────────────────────────────────────────────────────────┐\033[0m");
    puts("  \033[90m│\033[0m  \033[33m[tip]\033[0m  Tapez \033[36m« guide »\033[0m pour afficher les commandes et symboles     \033[90m│\033[0m");
    puts("  \033[90m│\033[0m  \033[34m[info]\033[0m \033[34m&\033[0m représente votre dossier personnel (\033[34mHOME\033[0m)                  \033[90m│\033[0m");
    puts("  \033[90m└───────────────────────────────────────────────────────────────────┘\033[0m\n");
}

/* COURS — Une animation terminal réécrit la ligne courante avec \r et efface
 * l'ancienne avec ESC[K. isatty() évite les animations lors d'une redirection. */
/* Animations interactives pour cd, pwd et ls */
static void animate_cd(const char *dest)
{
    if (!isatty(STDOUT_FILENO)) return;
    const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
    for (int i = 0; i < 4; i++) {
        printf("\r\033[1;36m[%s]\033[0m \033[2mNavigation vers\033[0m \033[1;34m%s\033[0m... ",
               frames[i], dest ? dest : "&");
        fflush(stdout);
        struct timespec t = {0, 35000000};
        nanosleep(&t, NULL);
    }
    printf("\r\033[K\033[32m[ok]\033[0m \033[90mdossier :\033[0m \033[34m%s\033[0m\n",
           dest ? dest : "&");
    fflush(stdout);
}

static void animate_pwd(void)
{
    if (!isatty(STDOUT_FILENO)) return;
    const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
    for (int i = 0; i < 4; i++) {
        printf("\r\033[1;36m[%s]\033[0m \033[2mLocalisation du répertoire...\033[0m", frames[i]);
        fflush(stdout);
        struct timespec t = {0, 30000000};
        nanosleep(&t, NULL);
    }
    fputs("\r\033[K", stdout);
    fflush(stdout);
}

static void animate_ls(const char *target)
{
    if (!isatty(STDOUT_FILENO)) return;
    const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
    for (int i = 0; i < 4; i++) {
        printf("\r\033[1;36m[%s]\033[0m \033[2mExploration de %s...\033[0m",
               frames[i], target ? target : ".");
        fflush(stdout);
        struct timespec t = {0, 30000000};
        nanosleep(&t, NULL);
    }
    fputs("\r\033[K", stdout);
    fflush(stdout);
}

/* Liste les fichiers (gris) et dossiers (bleu). COURS : opendir/readdir
 * parcourent les entrées; stat() détermine le type; closedir() ferme le dossier. */
static void list_dir(const char *path, int show_hidden)
{
    char resolved[4096];
    if (!path || !strcmp(path, "&") || !strcmp(path, "~")) {
        const char *h = getenv("HOME");
        snprintf(resolved, sizeof resolved, "%s", h ? h : ".");
    } else if (path[0] == '&' && path[1] == '/') {
        const char *h = getenv("HOME");
        snprintf(resolved, sizeof resolved, "%s%s", h ? h : "", path + 1);
    } else {
        snprintf(resolved, sizeof resolved, "%s", path);
    }

    struct stat path_stat;
    if (stat(resolved, &path_stat) == 0 && !S_ISDIR(path_stat.st_mode)) {
        printf("\033[90m%s\033[0m\n", path);
        return;
    }

    DIR *d = opendir(resolved);
    if (!d) {
        perror(path);
        return;
    }

    struct dirent *e;
    int column = 0;
    while ((e = readdir(d))) {
        if (!show_hidden && e->d_name[0] == '.') continue;
        char full[4096];
        int n = snprintf(full, sizeof full, "%s/%s", resolved, e->d_name);
        struct stat st;
        int dir = n > 0 && (size_t)n < sizeof full && stat(full, &st) == 0 && S_ISDIR(st.st_mode);

        if (dir) {
            printf("\033[1;34m%-18s/\033[0m  ", e->d_name);
        } else {
            printf("\033[90m%-19s\033[0m  ", e->d_name);
        }
        if (++column % 4 == 0) putchar('\n');
    }
    if (column % 4) putchar('\n');
    closedir(d);
}

/* Affiche les métadonnées officielles du shell : version, mise à jour, copyright et auteurs */
static void print_yander(void)
{
    if (isatty(STDOUT_FILENO)) {
        const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        const char *steps[] = {
            "Initialisation du système POSIX C11...",
            "Inspection du moteur & des pipelines...",
            "Chargement des métadonnées logicielles...",
            "Prêt !"
        };
        for (int s = 0; s < 4; s++) {
            for (int f = 0; f < 3; f++) {
                printf("\r\033[1;36m[%s]\033[0m \033[1;37m%s\033[0m", frames[(s * 3 + f) % 10], steps[s]);
                fflush(stdout);
                struct timespec t = {0, 22000000}; /* 22 ms */
                nanosleep(&t, NULL);
            }
        }
        fputs("\r\033[K", stdout);
        fflush(stdout);
    }
    puts("\033[1;36m╭────────────────────────────────────────────────────────────╮\033[0m");
    puts("\033[1;36m│\033[0m               \033[1;33m✦ YAZID SHELL & SSH TERMINAL ✦\033[0m               \033[1;36m│\033[0m");
    puts("\033[1;36m├────────────────────────────────────────────────────────────┤\033[0m");
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;37mInterpréteur\033[0m  : \033[1;34myazid_shell\033[0m \033[0;36m(POSIX C11)\033[0m                 \033[1;36m│\033[0m");
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;37mVersion\033[0m       : \033[1;36m1.1.0\033[0m \033[1;32m(Édition Officielle)\033[0m              \033[1;36m│\033[0m");
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;37mMise à jour\033[0m   : \033[1;36m26 Septembre 2026\033[0m                       \033[1;36m│\033[0m");
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;37mArchitecture\033[0m  : \033[0;37mMulti-processus & Pipelines\033[0m             \033[1;36m│\033[0m");
    puts("\033[1;36m├────────────────────────────────────────────────────────────┤\033[0m");
    /* Copyright en VERT VIF (\033[1;32m), Yazid en violet clair (\033[1;35m\033[38;5;201m), Skander en violet améthyste (\033[0;35m\033[38;5;141m) */
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;32mCopyright ©\033[0m   : \033[1;32m2026\033[0m \033[1;35m\033[38;5;201mYazid\033[0m et \033[0;35m\033[38;5;141mSkander\033[0m                   \033[1;36m│\033[0m");
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;37mAuteurs\033[0m       : \033[1;35m\033[38;5;201mYazid\033[0m \033[2m(Lead)\033[0m & \033[0;35m\033[38;5;141mSkander\033[0m \033[2m(Dev)\033[0m            \033[1;36m│\033[0m");
    puts("\033[1;36m│\033[0m  \033[1;36m•\033[0m \033[1;37mStatut\033[0m        : \033[1;32mOpérationnel\033[0m \033[0;37m· Prêt pour utilisation\033[0m    \033[1;36m│\033[0m");
    puts("\033[1;36m╰────────────────────────────────────────────────────────────╯\033[0m");
    fflush(stdout);
}

/* Affiche le guide interactif avec la description des commandes et symboles. */
static void print_guide(void)
{
    if (isatty(STDOUT_FILENO)) {
        const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
        const char *steps[] = {
            "Indexation des commandes intégrées...",
            "Vérification des flux et pipelines...",
            "Formatage de la syntaxe...",
            "Guide interactif prêt !"
        };
        for (int i = 0; i < 4; i++) {
            printf("\r\033[1;36m[%s]\033[0m \033[1;37m%s\033[0m", frames[i], steps[i]);
            fflush(stdout);
            struct timespec t = {0, 22000000};
            nanosleep(&t, NULL);
        }
        fputs("\r\033[K", stdout);
        fflush(stdout);
    }
    if (isatty(STDOUT_FILENO)) {
        puts("\n\033[1;36m╭──────────────────────────────────────────────────────────────────────────────╮\033[0m");
        puts("\033[1;36m│\033[0m             \033[1;33m★  GUIDE INTERACTIF DU SHELL · COMMANDES & SYNTAXE  ★\033[0m            \033[1;36m│\033[0m");
        puts("\033[1;36m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
        puts("\033[1;36m│\033[0m  \033[1;36m◆ [1] COMMANDES ESSENTIELLES & NAVIGATION\033[0m                                   \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mhelp, guide\033[0m          \033[0;37mAfficher ce guide interactif d'utilisation\033[0m           \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcd\033[0m \033[0;36m[dossier]\033[0m         \033[0;37mNavigation (\033[1;34mcd &\033[0;37m ou \033[1;34mcd ~\033[0;37m = dossier HOME, \033[1;34mcd -\033[0;37m)\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mpwd\033[0m                  \033[0;37mLocaliser et afficher le répertoire de travail\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mls\033[0m \033[0;36m[-a] [dossier]\033[0m    \033[0;37mLister les fichiers (\033[1;34mdossiers en bleu\033[0;37m, fichiers)\033[0m     \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mecho\033[0m \033[0;36m[-n] [texte]\033[0m    \033[0;37mAfficher un message ou la valeur d'une variable\033[0m      \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mclear\033[0m                \033[0;37mEffacer complètement l'écran du terminal\033[0m             \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mexit\033[0m \033[0;36m[code]\033[0m          \033[0;37mQuitter la session avec code de retour (défaut: 0)\033[0m   \033[1;36m│\033[0m");
        puts("\033[1;36m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
        puts("\033[1;36m│\033[0m  \033[1;32m◆ [2] GESTION DU SYSTÈME & VARIABLES\033[0m                                        \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mexport\033[0m \033[0;36mVAR=val\033[0m       \033[0;37mDéfinir ou modifier une variable d'environnement\033[0m     \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33munset\033[0m \033[0;36mVAR\033[0m            \033[0;37mSupprimer une variable d'environnement\033[0m               \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mhistory\033[0m              \033[0;37mConsulter l'historique (\033[1;35m!n\033[0;37m ou \033[1;35m!!\033[0;37m pour rejouer)\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33malias / unalias\033[0m      \033[0;37mCréer ou supprimer un alias (ex: alias ll=\"ls -a\")\033[0m   \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mtype / which\033[0m \033[0;36mNOM\033[0m     \033[0;37mVérifier si commande builtin ou chemin du binaire\033[0m    \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mjobs / fg / bg\033[0m       \033[0;37mSuperviser et contrôler les tâches en arrière-plan\033[0m   \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33msname\033[0m \033[0;36m[titre]\033[0m        \033[0;37mPersonnaliser le titre de session ou le prompt\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;35myander\033[0m               \033[0;37mMétadonnées (version, date, copyright, auteurs)\033[0m      \033[1;36m│\033[0m");
        puts("\033[1;36m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
        puts("\033[1;36m│\033[0m  \033[1;33m◆ [3] TUBES & REDIRECTIONS D'E/S (FLUX)\033[0m                                     \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd1 \033[1;35m|\033[0m \033[1;33mcmd2\033[0m          \033[0;37mTube : connecte la sortie stdout1 à l'entrée stdin2\033[0m  \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd \033[1;35m>\033[0m \033[0;36mfichier\033[0m        \033[0;37mRedirige stdout vers fichier (écrase le contenu)\033[0m     \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd \033[1;35m>>\033[0m \033[0;36mfichier\033[0m       \033[0;37mRedirige stdout vers fichier (ajoute à la fin)\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd \033[1;35m<\033[0m \033[0;36mfichier\033[0m        \033[0;37mRedirige stdin pour lire depuis un fichier\033[0m           \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd \033[1;35m2>\033[0m \033[0;36mfichier\033[0m       \033[0;37mRedirige les messages d'erreurs (stderr)\033[0m             \033[1;36m│\033[0m");
        puts("\033[1;36m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
        puts("\033[1;36m│\033[0m  \033[1;35m◆ [4] OPÉRATEURS, SYMBOLES & EXPANSIONS\033[0m                                     \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd1 \033[1;35m&&\033[0m \033[1;33mcmd2\033[0m         \033[0;37mET logique : exécute cmd2 uniquement si cmd1 réussit\033[0m  \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd1 \033[1;35m||\033[0m \033[1;33mcmd2\033[0m         \033[0;37mOU logique : exécute cmd2 uniquement si cmd1 échoue\033[0m   \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd1 \033[1;35m;\033[0m \033[1;33mcmd2\033[0m          \033[0;37mSéquence : exécute cmd1 puis cmd2 successivement\033[0m      \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;33mcmd \033[1;35m&\033[0m                \033[0;37mArrière-plan : détache la commande sans bloquer\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;35m&\033[0m ou \033[1;35m~\033[0m              \033[0;37mDossier HOME personnel (ex: cd &, cd ~/Bureau)\033[0m       \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;35m$VAR\033[0m, \033[1;35m$?\033[0m, \033[1;35m$$\033[0m        Expansions : valeur variable, code retour et PID\033[0m     \033[1;36m│\033[0m");
        puts("\033[1;36m│\033[0m    \033[1;35m' '\033[0m et \033[1;35m\" \"\033[0m           \033[0;37mQuotes : simples (texte brut) ou doubles (expansions)\033[0m\033[1;36m│\033[0m");
        puts("\033[1;36m├──────────────────────────────────────────────────────────────────────────────┤\033[0m");
        puts("\033[1;36m│\033[0m  \033[1;33m✦ Astuce :\033[0m \033[0;37mTapez '\033[1;35myander\033[0;37m' pour les métadonnées | '\033[1;33mclear\033[0;37m' pour vider l'écran \033[1;36m│\033[0m");
        puts("\033[1;36m╰──────────────────────────────────────────────────────────────────────────────╯\033[0m\n");
    } else {
        puts("\n========================= YAZID_SSH - GUIDE DU SHELL =========================");
        puts("[1] COMMANDES ESSENTIELLES & NAVIGATION :");
        puts("  help, guide          Afficher ce guide interactif d'utilisation");
        puts("  cd [dossier]         Navigation (cd & ou cd ~ = dossier HOME, cd -)");
        puts("  pwd                  Localiser et afficher le repertoire de travail");
        puts("  ls [-a] [dossier]    Lister les fichiers et dossiers");
        puts("  echo [-n] [texte]    Afficher un message ou la valeur d'une variable");
        puts("  clear                Effacer completement l'ecran du terminal");
        puts("  exit [code]          Quitter la session avec code de retour (defaut: 0)");
        puts("[2] GESTION DU SYSTEME & VARIABLES :");
        puts("  export VAR=val       Definir ou modifier une variable d'environnement");
        puts("  unset VAR            Supprimer une variable d'environnement");
        puts("  history              Consulter l'historique (!n ou !! pour rejouer)");
        puts("  alias / unalias      Creer ou supprimer un alias (ex: alias ll=\"ls -a\")");
        puts("  type / which NOM     Verifier si commande builtin ou chemin du binaire");
        puts("  jobs / fg / bg       Superviser et controler les taches en arriere-plan");
        puts("  sname [titre]        Personnaliser le titre de session ou le prompt");
        puts("  yander               Metadonnees (version, date, copyright, auteurs)");
        puts("[3] TUBES & REDIRECTIONS D'E/S (FLUX) :");
        puts("  cmd1 | cmd2          Tube : connecte la sortie stdout1 a l'entree stdin2");
        puts("  cmd > fichier        Redirige stdout vers fichier (ecrase le contenu)");
        puts("  cmd >> fichier       Redirige stdout vers fichier (ajoute a la fin)");
        puts("  cmd < fichier        Redirige stdin pour lire depuis un fichier");
        puts("  cmd 2> fichier       Redirige les messages d'erreurs (stderr)");
        puts("[4] OPERATEURS, SYMBOLES & EXPANSIONS :");
        puts("  cmd1 && cmd2         ET logique : execute cmd2 uniquement si cmd1 reussit");
        puts("  cmd1 || cmd2         OU logique : execute cmd2 uniquement si cmd1 echoue");
        puts("  cmd1 ; cmd2          Sequence : execute cmd1 puis cmd2 successivement");
        puts("  cmd &                Arriere-plan : detache la commande sans bloquer");
        puts("  & ou ~               Dossier HOME personnel (ex: cd &, cd ~/Bureau)");
        puts("  $VAR, $?, $$         Expansions : valeur variable, code retour et PID");
        puts("  ' ' et \" \"           Quotes : simples (texte brut) ou doubles (expansions)");
        puts("------------------------------------------------------------------------------");
        puts("  Astuce : Tapez 'yander' pour les infos auteurs & version | 'clear' pour vider");
        puts("==============================================================================\n");
    }
}

/* Lit une ligne de taille dynamique; NULL signale EOF sans caractères.
 * COURS : malloc/realloc font grandir le buffer jusqu'au retour à la ligne. */
static char *read_line(void)
{
    size_t size = INITIAL_LINE_SIZE;
    size_t length = 0;
    char *line = malloc(size);
    int c;

    if (!line) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    while ((c = getchar()) != EOF && c != '\n') {
        if (length + 1 >= size) {
            size *= 2;
            char *grown = realloc(line, size);
            if (!grown) {
                free(line);
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            line = grown;
        }
        line[length++] = (char)c;
    }
    if (c == EOF && length == 0) {
        free(line);
        return NULL;
    }
    line[length] = '\0';
    return line;
}

/* Découpe la ligne sur espaces et tabulations.
 * COURS : strtok_r() remplace temporairement les séparateurs par '\0'; les
 * arguments pointent donc dans line et ne doivent pas être libérés séparément. */
static size_t split_command(char *line, char **args)
{
    size_t count = 0;
    char *save = NULL;
    char *token = strtok_r(line, " \t", &save);
    while (token && count < MAX_ARGS - 1) {
        args[count++] = token;
        token = strtok_r(NULL, " \t", &save);
    }
    args[count] = NULL;
    return count;
}

/* Reconnaît les commandes internes; la comparaison évite un appel à execvp(). */
static int is_builtin(const char *name)
{
    return strcmp(name, "cd") == 0 || strcmp(name, "help") == 0 ||
           strcmp(name, "guide") == 0 || strcmp(name, "sname") == 0 ||
           strcmp(name, "exit") == 0 || strcmp(name, "clear") == 0 ||
           strcmp(name, "pwd") == 0 || strcmp(name, "ls") == 0 ||
           strcmp(name, "yander") == 0;
}

/* Exécute les commandes internes dans le processus shell.
 * COURS : cd doit être builtin pour modifier le répertoire du parent. */
static int run_builtin(char **args)
{
    if (strcmp(args[0], "help") == 0 || strcmp(args[0], "guide") == 0) {
        print_guide();
    } else if (strcmp(args[0], "sname") == 0) {
        puts("yazid_ssh");
    } else if (strcmp(args[0], "yander") == 0) {
        print_yander();
    } else if (strcmp(args[0], "clear") == 0) {
        printf("\033[2J\033[H");
    } else if (strcmp(args[0], "pwd") == 0) {
        animate_pwd();
        char b[4096];
        if (getcwd(b, sizeof b)) {
            printf("\033[90mpwd ::\033[0m \033[34m%s\033[0m\n", b);
        } else {
            perror("pwd");
        }
    } else if (strcmp(args[0], "ls") == 0) {
        int hidden = 0, first = 1;
        for (; args[first] && args[first][0] == '-' && args[first][1]; first++) {
            for (const char *p = args[first] + 1; *p; p++)
                if (*p == 'a') hidden = 1;
        }
        int count = 0;
        for (int i = first; args[i]; i++) {
            animate_ls(args[i]);
            if (count++) printf("\n\033[1;36m%s:\033[0m\n", args[i]);
            list_dir(args[i], hidden);
        }
        if (!count) {
            animate_ls(".");
            list_dir(".", hidden);
        }
    } else if (strcmp(args[0], "cd") == 0) {
        const char *target = args[1];
        char resolved[4096];
        const char *directory = NULL;
        if (!target || !strcmp(target, "&") || !strcmp(target, "~")) {
            directory = getenv("HOME");
        } else if (target[0] == '&' && target[1] == '/') {
            const char *h = getenv("HOME");
            snprintf(resolved, sizeof resolved, "%s%s", h ? h : "", target + 1);
            directory = resolved;
        } else {
            directory = target;
        }
        animate_cd(target ? target : "&");
        if (!directory) {
            fprintf(stderr, "cd: HOME is not set\n");
        } else if (chdir(directory) != 0) {
            perror("cd");
        }
    } else if (strcmp(args[0], "exit") == 0) {
        return 1;
    }
    return 0;
}

/* Crée un enfant puis remplace son image par une commande trouvée via PATH.
 * COURS : fork() retourne 0 dans l'enfant; execvp() remplace son programme;
 * waitpid() permet au parent d'attendre sa fin. */
static void execute_external(char **args)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
    } else if (pid == 0) {
        execvp(args[0], args);
        perror(args[0]);
        _exit(127);
    } else {
        int status;
        while (waitpid(pid, &status, 0) < 0) {
            /* Retry if interrupted by a signal. */
        }
    }
}

/* Compare sans distinguer la casse. COURS : tolower() reçoit un unsigned char
 * pour éviter un comportement indéfini avec les caractères négatifs. */
static int equals_ignore_case(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

/* Boucle principale : prompt, lecture puis exécution.
 * COURS : c'est le cycle REPL; chaque tour alloue les arguments, lit une ligne,
 * choisit un builtin ou un processus enfant, puis libère ses allocations. */
static void line_loop(void)
{
    welcome_animation();
    for (;;) {
        char **args = calloc(MAX_ARGS, sizeof(*args));
        char *line;
        size_t count;
        int should_exit = 0;

        if (!args) {
            perror("calloc");
            return;
        }

        /* Prompt stylé avec & comme dossier personnel */
        char cwd[1024], display[1024], host[128] = "host";
        const char *user = getenv("USER");
        const char *home = getenv("HOME");
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
        if (home && *home && !strncmp(cwd, home, strlen(home)) &&
            (cwd[strlen(home)] == '/' || cwd[strlen(home)] == '\0'))
            snprintf(display, sizeof display, "&%s", cwd + strlen(home));
        else
            snprintf(display, sizeof display, "%s", cwd);
        if (!user) user = "user";
        gethostname(host, sizeof host - 1);

        printf("\033[1;36m╭─[\033[1;35myazid_ssh\033[1;36m]─(\033[1;32m%s@%s\033[1;36m)─[\033[1;34m%s\033[1;36m]\033[0m\n\033[1;36m╰─❯\033[0m ",
               user, host, display);
        fflush(stdout);

        line = read_line();
        if (!line) {
            puts("\nAu revoir !");
            free(args);
            break;
        }
        count = split_command(line, args);
        if (count > 0) {
            if (equals_ignore_case(args[0], "yazid")) {
                welcome_animation();
            } else if (is_builtin(args[0])) {
                should_exit = run_builtin(args);
            } else {
                execute_external(args);
            }
        }
        free(args);
        free(line);
        if (should_exit)
            break;
    }
}

/* Point d'entrée de yazid_ssh. */
int main(void)
{
    line_loop();
    return 0;
}
