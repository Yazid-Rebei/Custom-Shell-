#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : guess.c
 * DESCRIPTION : Moteur d'assistance intelligente, de devinette contextuelle (guess)
 *               et d'auto-complétion en temps réel sur appui de la touche [TAB].
 *
 * COURS THÉORIQUE — Auto-Complétion Intelligente et Correction Typographique :
 * Dans un shell moderne, l'utilisateur attend plus qu'une simple complétion de fichiers :
 * 1. Détection de Commandes Complètes & Valides :
 *    Lorsque l'utilisateur a saisi une commande connue (ou des drapeaux comme 'tar -x'),
 *    le shell fournit instantanément la formule générale (synopsis), les champs/options
 *    possibles et des exemples concrets d'utilisation.
 * 2. Auto-Complétion Préfixe (Complétion Standard) :
 *    Si la commande est incomplète (ex: 'cle', 'ex', 'mk'), le moteur calcule le plus
 *    long préfixe commun et présente les suggestions possibles avec leurs rôles.
 * 3. Correction Typographique & Distance de Damerau-Levenshtein :
 *    Si la commande saisie est inconnue ou contient une faute de frappe (ex: 'mrdir'),
 *    le moteur compare la saisie avec le dictionnaire de commandes via la distance
 *    de Damerau-Levenshtein (qui prend en compte insertions, suppressions, substitutions
 *    et transpositions de lettres adjacentes). Pour 'mrdir', il identifie immédiatement
 *    'rmdir' (transposition 'mr' -> 'rm') et 'mkdir' (substitution 'r' -> 'k'), et affiche
 *    pour chacune la règle générale et des exemples d'usage.
 * 4. Mode Terminal Non-Canonique (Raw Mode) :
 *    L'interception de [TAB] en temps réel sans attendre l'appui sur Entrée nécessite
 *    de basculer temporairement le terminal en mode non-canonique via tcsetattr() et
 *    termios, tout en garantissant la restauration de l'état initial à la sortie.
 *
 * Fonctions système et bibliothèque C utilisées :
 * - tcgetattr(), tcsetattr() : gestion des attributs de terminal (termios)
 * - opendir(), readdir(), closedir() : exploration du système de fichiers et du PATH
 * - stat() : détection des répertoires pour suffixer par '/'
 * - access() : vérification des droits d'exécution X_OK
 * - isatty() : détection d'un terminal interactif
 * - printf(), snprintf(), puts() : affichage formaté et stylisé
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
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>

/** Base de connaissances sur une commande standard ou intégrée. */
typedef struct {
    const char *name;       /** Nom de la commande */
    const char *rule;       /** Règle générale / Formule syntaxique */
    const char *summary;    /** Description courte du rôle */
    const char *options;    /** Champs et options possibles */
    const char *examples;   /** Exemples concrets d'utilisation */
} CmdKnowledge;

/** Base de connaissances contextuelle (commandes avec drapeaux ou sous-commandes). */
typedef struct {
    const char *cmd;        /** Commande parente (ex: "tar", "git") */
    const char *flag;       /** Drapeau ou sous-commande (ex: "-x", "commit") */
    const char *rule;       /** Formule générale spécialisée */
    const char *summary;    /** Rôle spécifique du sous-contexte */
    const char *options;    /** Options pertinentes dans ce contexte */
    const char *examples;   /** Exemples d'utilisation adaptés */
} ContextualKnowledge;

/** Candidat de suggestion avec score de similarité. */
typedef struct {
    const char *name;
    int distance;
    const CmdKnowledge *knowledge;
} SuggestionCandidate;

/* ============================================================================
 * BASE DE CONNAISSANCES COMPLÈTE DU SHELL (Commandes Built-ins et Utilitaires)
 * ============================================================================
 */
static const CmdKnowledge CMD_DATABASE[] = {
    {
        "cd",
        "cd [dossier | & | ~ | -]",
        "Changer de répertoire de travail courant",
        "  & ou ~         Dossier personnel HOME de l'utilisateur\n"
        "  ..             Dossier parent (remonter d'un cran dans l'arborescence)\n"
        "  -              Revenir au répertoire de travail immédiatement précédent\n"
        "  <dossier>      Chemin relatif ou absolu vers un sous-dossier",
        "  cd &                             Aller dans le dossier HOME personnel\n"
        "  cd ..                            Remonter d'un niveau dans l'arborescence\n"
        "  cd -                             Retourner au répertoire précédent\n"
        "  cd Desktop/C_embedded            Accéder au dossier spécifié"
    },
    {
        "mkdir",
        "mkdir [-p] [-m mode] <nom_dossier...>",
        "Créer un ou plusieurs nouveaux répertoires",
        "  -p             Créer les répertoires parents manquants sans erreur\n"
        "  -m <mode>      Définir les permissions d'accès octales (ex: 755)",
        "  mkdir nouveau_dossier             Créer un répertoire simple\n"
        "  mkdir -p src/modules/core        Créer toute l'arborescence parente\n"
        "  mkdir -m 700 secret              Créer un dossier avec accès restreint"
    },
    {
        "rmdir",
        "rmdir [-p] <dossier_vide...>",
        "Supprimer un ou plusieurs répertoires vides",
        "  -p             Supprimer aussi les répertoires parents s'ils sont vides",
        "  rmdir ancien_dossier             Supprimer un répertoire vide\n"
        "  rmdir -p parent/enfant           Supprimer les dossiers imbriqués vides"
    },
    {
        "ls",
        "ls [-a] [-l] [-h] [-t] [-r] [dossier...]",
        "Lister le contenu de répertoires et fichiers",
        "  -a             Afficher tous les fichiers (inclus cachés débutant par .)\n"
        "  -l             Format long détaillé (droits, propriétaire, taille, date)\n"
        "  -h             Afficher les tailles en Ko, Mo, Go lisibles\n"
        "  -t             Trier par date de modification (récents d'abord)\n"
        "  -r             Inverser l'ordre du tri",
        "  ls -la                           Lister tous les fichiers au format long\n"
        "  ls -lh /tmp                      Lister avec tailles lisibles\n"
        "  ls -lt src/                      Lister trié par date de modification"
    },
    {
        "tar",
        "tar [options] <archive.tar[.*]> [cibles...]",
        "Archiver ou extraire des collections de fichiers",
        "  -x             Extraire les fichiers d'une archive existante\n"
        "  -c             Créer une nouvelle archive\n"
        "  -t             Lister le contenu d'une archive sans l'extraire\n"
        "  -f <fichier>   Spécifier le nom du fichier archive (obligatoire)\n"
        "  -v             Mode verbeux (afficher la liste des fichiers traités)\n"
        "  -z             Compression / décompression gzip (.tar.gz / .tgz)\n"
        "  -j             Compression / décompression bzip2 (.tar.bz2)\n"
        "  -C <dossier>   Répertoire cible pour l'extraction",
        "  tar -czvf backup.tar.gz dossier/  Créer une archive compressée .tar.gz\n"
        "  tar -xzvf archive.tar.gz          Extraire une archive gzip (.tar.gz)\n"
        "  tar -xvf archive.tar -C /tmp      Extraire le contenu vers le dossier /tmp\n"
        "  tar -tvf archive.tar              Lister le contenu sans extraire"
    },
    {
        "grep",
        "grep [options] \"motif\" [fichiers/dossiers...]",
        "Rechercher des motifs ou expressions régulières dans des fichiers",
        "  -i             Ignorer la casse (minuscules/majuscules)\n"
        "  -r / -R        Recherche récursive dans les sous-dossiers\n"
        "  -n             Afficher le numéro des lignes correspondantes\n"
        "  -v             Inverser la sélection (lignes sans correspondance)\n"
        "  -E             Activer les expressions régulières étendues (regex)",
        "  grep -rn \"main\" src/             Recherche récursive de 'main' avec numéros\n"
        "  grep -i \"error\" /var/log/syslog  Recherche insensible à la casse\n"
        "  grep -v \"^#\" config.conf         Afficher les lignes sans commentaire"
    },
    {
        "chmod",
        "chmod [-R] <mode> <cible...>",
        "Modifier les permissions d'accès d'un fichier ou répertoire",
        "  +x / -x        Ajouter ou retirer le droit d'exécution\n"
        "  755            rwxr-xr-x (exécutable pour tous, écriture propriétaire)\n"
        "  644            rw-r--r-- (lecture pour tous, écriture propriétaire)\n"
        "  -R             Appliquer récursivement à tous les sous-fichiers",
        "  chmod +x script.sh               Rendre un script shell exécutable\n"
        "  chmod 755 programme              Permissions standard pour exécutable\n"
        "  chmod -R 644 ./docs/             Permissions lecture récursives"
    },
    {
        "cp",
        "cp [-r] [-p] [-u] <source...> <destination>",
        "Copier des fichiers et répertoires",
        "  -r / -R        Copie récursive des répertoires et sous-dossiers\n"
        "  -p             Conserver les permissions, dates et attributs\n"
        "  -u             Copier seulement si la source est plus récente",
        "  cp fichier.txt copie.txt         Copier un fichier simple\n"
        "  cp -r src/ backup_src/           Copier récursivement un dossier complet"
    },
    {
        "mv",
        "mv [-f] [-i] [-u] <source...> <destination>",
        "Déplacer ou renommer des fichiers et répertoires",
        "  -f             Forcer le déplacement sans confirmation\n"
        "  -i             Demander confirmation avant d'écraser un fichier",
        "  mv ancien.txt nouveau.txt        Renommer un fichier\n"
        "  mv *.c src/                      Déplacer tous les fichiers .c vers src/"
    },
    {
        "rm",
        "rm [-r] [-f] [-i] <cible...>",
        "Supprimer des fichiers ou répertoires",
        "  -r / -R        Suppression récursive de dossiers et leur contenu\n"
        "  -f             Forcer sans avertissement ni confirmation\n"
        "  -i             Demander confirmation avant chaque suppression",
        "  rm fichier.txt                   Supprimer un fichier simple\n"
        "  rm -rf dossier_temp/             Supprimer un dossier complet (attention !)"
    },
    {
        "cat",
        "cat [-n] [-b] [fichiers...]",
        "Afficher ou concaténer le contenu de fichiers",
        "  -n             Numéroter toutes les lignes en sortie\n"
        "  -b             Numéroter uniquement les lignes non-vides",
        "  cat fichier.txt                  Afficher le contenu du fichier\n"
        "  cat -n main.c                    Afficher avec numéros de lignes\n"
        "  cat f1.txt f2.txt > full.txt     Concaténer deux fichiers"
    },
    {
        "echo",
        "echo [-n] [texte / $VAR...]",
        "Afficher du texte ou développer des variables dans la sortie standard",
        "  -n             Ne pas émettre de saut de ligne final",
        "  echo \"Bonjour tout le monde\"     Afficher un message simple\n"
        "  echo -n \"Saisie : \"              Afficher sans retour à la ligne\n"
        "  echo \"HOME=$HOME | PID=$$\"       Afficher avec variables d'environnement"
    },
    {
        "pwd",
        "pwd",
        "Afficher le chemin absolu du répertoire de travail actuel",
        "  (Aucune option requise)",
        "  pwd                              Afficher le répertoire actuel"
    },
    {
        "clear",
        "clear",
        "Effacer complètement l'écran du terminal",
        "  (Aucune option requise)",
        "  clear                            Nettoyer l'affichage du terminal"
    },
    {
        "history",
        "history",
        "Afficher la liste des commandes saisies dans l'historique",
        "  !!             Réexécuter la commande immédiatement précédente\n"
        "  !n             Réexécuter la n-ième commande de l'historique",
        "  history                          Consulter l'historique de session\n"
        "  !!                               Rejouer la dernière commande\n"
        "  !5                               Rejouer la commande numéro 5"
    },
    {
        "export",
        "export VAR=valeur",
        "Définir ou modifier une variable d'environnement",
        "  VAR=valeur     Affectation de la variable d'environnement",
        "  export PATH=$PATH:/usr/local/bin Ajouter un dossier au PATH\n"
        "  export CC=gcc                    Définir le compilateur par défaut"
    },
    {
        "unset",
        "unset <VARIABLE>",
        "Supprimer une variable d'environnement de la session",
        "  <VARIABLE>     Nom de la variable à supprimer",
        "  unset MA_VAR                     Retirer la variable spécifiée"
    },
    {
        "alias",
        "alias [nom=\"commande\"]",
        "Créer un raccourci de commande ou lister les alias existants",
        "  nom=\"cmd\"      Définition d'un nouvel alias\n"
        "  (sans arg)     Lister tous les alias enregistrés",
        "  alias ll=\"ls -la\"                Créer un alias pour ls -la\n"
        "  alias                            Afficher les alias configurés"
    },
    {
        "unalias",
        "unalias <nom>",
        "Supprimer un alias préalablement défini",
        "  <nom>          Nom de l'alias à retirer",
        "  unalias ll                       Supprimer l'alias ll"
    },
    {
        "jobs",
        "jobs",
        "Lister les tâches exécutées en arrière-plan",
        "  (Aucune option requise)",
        "  jobs                             Consulter les processus détachés"
    },
    {
        "fg",
        "fg [%id | pid]",
        "Basculer un job d'arrière-plan au premier plan",
        "  %n             Numéro du job à reprendre",
        "  fg                               Reprendre le job le plus récent\n"
        "  fg %1                            Reprendre le job numéro 1"
    },
    {
        "bg",
        "bg [%id | pid]",
        "Reprendre l'exécution d'un job suspendu en arrière-plan",
        "  %n             Numéro du job",
        "  bg %1                            Continuer le job 1 en arrière-plan"
    },
    {
        "which",
        "which <commande>",
        "Localiser le binaire d'une commande dans le PATH",
        "  <commande>     Nom du programme à rechercher",
        "  which gcc                        Afficher le chemin absolu du binaire"
    },
    {
        "type",
        "type <commande>",
        "Identifier la nature d'une commande (builtin ou externe)",
        "  <commande>     Nom de la commande à inspecter",
        "  type cd                          Indiquer si la commande est interne"
    },
    {
        "guide",
        "guide",
        "Afficher le guide interactif complet du shell Yazid",
        "  (Aucune option requise)",
        "  guide                            Ouvrir le guide interactif"
    },
    {
        "help",
        "help",
        "Afficher le guide interactif d'aide aux commandes",
        "  (Aucune option requise)",
        "  help                             Ouvrir le guide interactif"
    },
    {
        "sname",
        "sname [titre]",
        "Afficher ou modifier le nom de session affiché dans le prompt",
        "  [titre]        Nouveau nom à attribuer à la session",
        "  sname                            Afficher le nom courant (yazid_ssh)\n"
        "  sname MonTerminal                Changer le titre de session"
    },
    {
        "yander",
        "yander",
        "Afficher les métadonnées, auteurs (Yazid & Skander) et version",
        "  (Aucune option requise)",
        "  yander                           Afficher crédits et version"
    },
    {
        "exit",
        "exit [code]",
        "Quitter la session interactive du shell",
        "  [code]         Code de sortie numérique (défaut : 0)",
        "  exit                             Quitter normalement\n"
        "  exit 0                           Quitter avec code de succès\n"
        "  exit 1                           Quitter avec code d'erreur"
    },
    {
        "git",
        "git <sous_commande> [options]",
        "Système de contrôle de versions décentralisé",
        "  status         Afficher l'état des fichiers modifiés\n"
        "  commit -m      Enregistrer un instantané avec description\n"
        "  push           Envoyer les commits vers le dépôt distant\n"
        "  pull           Récupérer et fusionner les modifications distantes\n"
        "  add <fichiers> Indexer des modifications pour le prochain commit\n"
        "  checkout <br>  Basculer sur une autre branche de développement\n"
        "  diff           Visualiser les modifications non indexées",
        "  git status                       Vérifier l'état de l'arbre de travail\n"
        "  git add .                        Indexer toutes les modifications\n"
        "  git commit -m \"feat: smart tab\"  Créer un commit\n"
        "  git push origin main             Envoyer vers GitHub/GitLab"
    },
    {
        "find",
        "find [chemin] [options] [actions]",
        "Rechercher des fichiers dans l'arborescence",
        "  -name \"motif\"  Filtrer par nom de fichier (ex: \"*.c\")\n"
        "  -type f / d    Filtrer par type (f=fichier ordinaire, d=dossier)\n"
        "  -mtime -n      Fichiers modifiés depuis moins de n jours",
        "  find . -name \"*.c\"               Chercher tous les fichiers C du projet\n"
        "  find /tmp -type f                Lister les fichiers réguliers dans /tmp"
    },
    {
        "touch",
        "touch <fichier...>",
        "Créer un nouveau fichier vide ou actualiser sa date d'accès",
        "  <fichier>      Nom du ou des fichiers à créer/mettre à jour",
        "  touch nouveau.txt                Créer un fichier vide\n"
        "  touch src/main.c                 Actualiser l'horodatage de main.c"
    },
    {
        "gcc",
        "gcc [options] <fichiers.c> -o <cible>",
        "Compilateur GNU pour le langage C",
        "  -Wall -Wextra  Activer tous les avertissements recommandés\n"
        "  -g             Générer les symboles de débogage pour gdb\n"
        "  -O2            Activer l'optimisation de niveau 2\n"
        "  -I<dossier>    Ajouter un dossier d'en-têtes .h",
        "  gcc -Wall -Wextra main.c -o app  Compilation stricte\n"
        "  gcc -g -O0 test.c -o test_bin    Compilation pour débogage"
    },
    {
        "make",
        "make [cible] [VARIABLE=valeur]",
        "Automatiser la compilation et la construction de projets",
        "  all            Compiler la cible par défaut du Makefile\n"
        "  clean          Nettoyer les binaires et fichiers objets\n"
        "  test           Exécuter la suite de tests automatisés",
        "  make                             Construire le projet\n"
        "  make clean                       Nettoyer l'arborescence\n"
        "  make test                        Lancer les tests de validation"
    },
    {
        "curl",
        "curl [options] <URL>",
        "Transférer des données avec des protocoles réseau (HTTP, HTTPS)",
        "  -O             Enregistrer le fichier avec son nom distant d'origine\n"
        "  -s             Mode silencieux (désactiver la barre de progression)\n"
        "  -I             Récupérer uniquement les en-têtes HTTP (headers)",
        "  curl https://example.com         Afficher la page web dans le terminal\n"
        "  curl -O https://site.com/doc.pdf Télécharger un document PDF"
    },
    {
        "kill",
        "kill [-SIGNAL] <PID>",
        "Envoyer un signal à un processus en cours d'exécution",
        "  -9 (-SIGKILL)  Forcer l'arrêt immédiat et inconditionnel\n"
        "  -15 (-SIGTERM) Demande de terminaison normale (par défaut)",
        "  kill 1234                        Arrêter le processus 1234\n"
        "  kill -9 1234                     Tuer de force le processus 1234"
    },
    {
        "ps",
        "ps [options]",
        "Afficher l'état des processus actifs du système",
        "  aux            Format complet affichant CPU, mémoire et commande\n"
        "  -ef            Format standardisé POSIX",
        "  ps aux                           Lister tous les processus actifs\n"
        "  ps aux | grep yazid              Filtrer les processus contenant yazid"
    },
    {
        "guess",
        "guess [commande | préfixe | faute_de_frappe]",
        "Interroger le moteur de devinette et d'aide intelligente",
        "  [cmd]          Nom ou extrait de commande à analyser",
        "  guess tar -x                     Consulter l'aide de tar en extraction\n"
        "  guess mrdir                      Découvrir les suggestions pour mrdir\n"
        "  guess cle                        Compléter le mot 'cle'"
    }
};

static const size_t CMD_DB_COUNT = sizeof(CMD_DATABASE) / sizeof(CMD_DATABASE[0]);

/* ============================================================================
 * BASE DE CONNAISSANCES CONTEXTUELLE (Drapeaux et Sous-Commandes)
 * ============================================================================
 */
static const ContextualKnowledge CONTEXT_DATABASE[] = {
    {
        "tar",
        "-x",
        "tar -x[v][z|j] -f <archive.tar[.*]> [-C <destination>]",
        "Mode Extraction : Décompresser et extraire le contenu d'une archive",
        "  -f <archive>   Spécifie le fichier archive cible (obligatoire)\n"
        "  -v             Mode verbeux (afficher la liste des fichiers extraits)\n"
        "  -z             Décompresser une archive gzip (.tar.gz / .tgz)\n"
        "  -j             Décompresser une archive bzip2 (.tar.bz2)\n"
        "  -C <dossier>   Extraire vers le dossier cible indiqué",
        "  tar -xvf archive.tar             Extraire une archive tar standard\n"
        "  tar -xzvf archive.tar.gz         Extraire une archive compressée tar.gz\n"
        "  tar -xjvf archive.tar.bz2        Extraire une archive compressée tar.bz2\n"
        "  tar -xvf archive.tar -C /tmp     Extraire dans le dossier /tmp"
    },
    {
        "tar",
        "-c",
        "tar -c[v][z|j] -f <archive.tar[.*]> <source...>",
        "Mode Création : Regrouper des fichiers/dossiers dans une nouvelle archive",
        "  -f <archive>   Nom de l'archive résultante (obligatoire)\n"
        "  -v             Mode verbeux (afficher chaque fichier ajouté)\n"
        "  -z             Compresser en gzip (.tar.gz)\n"
        "  -j             Compresser en bzip2 (.tar.bz2)",
        "  tar -cvf mon_archive.tar dossier/          Créer une archive simple\n"
        "  tar -czvf mon_archive.tar.gz src/ docs/    Créer une archive compressée .tar.gz\n"
        "  tar -cjvf mon_archive.tar.bz2 data/        Créer une archive compressée .tar.bz2"
    },
    {
        "tar",
        "-t",
        "tar -t[v][z|j] -f <archive.tar[.*]>",
        "Mode Liste : Consulter le contenu d'une archive sans l'extraire",
        "  -f <archive>   Fichier archive à inspecter (obligatoire)\n"
        "  -v             Afficher les permissions, propriétaire, taille et date\n"
        "  -z             Inspecter une archive compressée gzip",
        "  tar -tvf archive.tar             Lister le contenu détaillé\n"
        "  tar -tzvf archive.tar.gz         Lister le contenu d'un tar.gz"
    },
    {
        "git",
        "commit",
        "git commit -m \"message\" [-a]",
        "Enregistrer les modifications indexées dans l'historique du dépôt",
        "  -m \"<msg>\"     Message clair décrivant les changements apportés\n"
        "  -a             Indexer automatiquement tous les fichiers suivis modifiés\n"
        "  --amend        Modifier le dernier commit sans créer de nouveau nœud",
        "  git commit -m \"feat: ajout de la completion intelligente TAB\"\n"
        "  git commit -am \"fix: correction rapide\"\n"
        "  git commit --amend -m \"nouveau message\""
    },
    {
        "git",
        "push",
        "git push [remote] [branche]",
        "Envoyer les commits locaux vers le dépôt distant",
        "  -u             Associer la branche locale à la branche distante (upstream)\n"
        "  --force        Forcer l'envoi (attention : réécrit l'historique distant)",
        "  git push origin main             Pousser sur la branche main distante\n"
        "  git push -u origin feature-tab   Publier et lier une nouvelle branche"
    },
    {
        "git",
        "checkout",
        "git checkout [-b] <branche | fichier>",
        "Basculer de branche ou restaurer l'état de fichiers",
        "  -b <nom>       Créer une nouvelle branche et basculer dessus immédiatement\n"
        "  -- <fichier>   Rétablir un fichier à l'état du dernier commit",
        "  git checkout main                Aller sur la branche main\n"
        "  git checkout -b feature/nouveau  Créer et activer la branche feature/nouveau"
    },
    {
        "ls",
        "-l",
        "ls -l[a][h] [dossier...]",
        "Format long détaillé avec permissions, taille et date",
        "  -a             Inclure les fichiers cachés (débutant par .)\n"
        "  -h             Tailles en Ko, Mo, Go lisibles pour l'humain",
        "  ls -la\n  ls -lh /tmp"
    },
    {
        "grep",
        "-r",
        "grep -r[n][i] \"motif\" [dossier...]",
        "Recherche récursive dans tous les sous-dossiers",
        "  -n             Numéro de ligne\n"
        "  -i             Insensible à la casse",
        "  grep -rn \"main\" src/\n  grep -rni \"error\" /var/log/"
    },
    {
        "mkdir",
        "-p",
        "mkdir -p <chemin/complet/du/dossier>",
        "Créer des sous-dossiers imbriqués en cascade sans erreur",
        "  -p             Crée automatiquement tous les répertoires parents manquants",
        "  mkdir -p src/modules/core\n  mkdir -p build/obj"
    },
    {
        "chmod",
        "+x",
        "chmod +x <fichier_ou_script>",
        "Accorder les droits d'exécution sur un fichier",
        "  +x             Activer le bit d'exécution pour l'utilisateur",
        "  chmod +x script.sh\n  chmod +x ./build.sh"
    }
};

static const size_t CONTEXT_DB_COUNT = sizeof(CONTEXT_DATABASE) / sizeof(CONTEXT_DATABASE[0]);

/* ============================================================================
 * GESTION DU CACHE DES EXÉCUTABLES DU SYSTÈME (PATH)
 * ============================================================================
 */
static char **g_path_bins = NULL;
static size_t g_path_bins_count = 0;
static int g_path_scanned = 0;

static void scan_path_binaries(void)
{
    if (g_path_scanned) return;
    g_path_scanned = 1;

    const char *path_env = getenv("PATH");
    if (!path_env) path_env = "/bin:/usr/bin:/usr/local/bin";

    char *copy = strdup(path_env);
    if (!copy) return;

    char *saveptr = NULL;
    char *dir = strtok_r(copy, ":", &saveptr);
    while (dir) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_name[0] == '.') continue;
                /* Vérification rapide d'unicité */
                int found = 0;
                for (size_t i = 0; i < g_path_bins_count; i++) {
                    if (!strcmp(g_path_bins[i], ent->d_name)) {
                        found = 1;
                        break;
                    }
                }
                if (!found && g_path_bins_count < 2048) {
                    char **tmp = realloc(g_path_bins, (g_path_bins_count + 1) * sizeof *tmp);
                    if (tmp) {
                        g_path_bins = tmp;
                        g_path_bins[g_path_bins_count] = strdup(ent->d_name);
                        if (g_path_bins[g_path_bins_count]) g_path_bins_count++;
                    }
                }
            }
            closedir(d);
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(copy);
}

void guess_cleanup(void)
{
    if (g_path_bins) {
        for (size_t i = 0; i < g_path_bins_count; i++) {
            free(g_path_bins[i]);
        }
        free(g_path_bins);
        g_path_bins = NULL;
        g_path_bins_count = 0;
    }
    g_path_scanned = 0;
}

/* ============================================================================
 * ALGORITHME DE DISTANCE DE DAMERAU-LEVENSHTEIN
 * ============================================================================
 */
static int min3(int a, int b, int c)
{
    int m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

/**
 * damerau_levenshtein - Calcule la distance d'édition entre deux chaînes.
 * Prise en charge des insertions, suppressions, substitutions et
 * transpositions de deux caractères adjacents (ex: 'mrdir' <-> 'rmdir').
 */
static int damerau_levenshtein(const char *s1, const char *s2)
{
    int len1 = (int)strlen(s1);
    int len2 = (int)strlen(s2);
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    if (len1 > 60 || len2 > 60) return 999;

    int d[64][64];
    for (int i = 0; i <= len1; i++) d[i][0] = i;
    for (int j = 0; j <= len2; j++) d[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (tolower((unsigned char)s1[i - 1]) == tolower((unsigned char)s2[j - 1])) ? 0 : 1;
            d[i][j] = min3(
                d[i - 1][j] + 1,       /* Deletion */
                d[i][j - 1] + 1,       /* Insertion */
                d[i - 1][j - 1] + cost /* Substitution */
            );
            /* Transposition de deux caractères adjacents */
            if (i > 1 && j > 1 &&
                tolower((unsigned char)s1[i - 1]) == tolower((unsigned char)s2[j - 2]) &&
                tolower((unsigned char)s1[i - 2]) == tolower((unsigned char)s2[j - 1])) {
                if (d[i - 2][j - 2] + 1 < d[i][j]) {
                    d[i][j] = d[i - 2][j - 2] + 1;
                }
            }
        }
    }
    return d[len1][len2];
}

/* ============================================================================
 * FONCTIONS DE RECHERCHE ET DE CORRESPONDANCE
 * ============================================================================
 */
static const CmdKnowledge *find_command_knowledge(const char *cmd)
{
    if (!cmd) return NULL;
    for (size_t i = 0; i < CMD_DB_COUNT; i++) {
        if (!strcmp(CMD_DATABASE[i].name, cmd)) {
            return &CMD_DATABASE[i];
        }
    }
    return NULL;
}

static const ContextualKnowledge *find_contextual_knowledge(const char *cmd, const char *arg_line)
{
    if (!cmd || !arg_line) return NULL;
    for (size_t i = 0; i < CONTEXT_DB_COUNT; i++) {
        if (!strcmp(CONTEXT_DATABASE[i].cmd, cmd)) {
            /* Vérifie si le drapeau est présent dans la ligne d'arguments */
            if (strstr(arg_line, CONTEXT_DATABASE[i].flag)) {
                return &CONTEXT_DATABASE[i];
            }
        }
    }
    return NULL;
}

/* ============================================================================
 * RENDU VISUEL DES BOÎTES D'ASSISTANCE INTELLIGENTE
 * ============================================================================
 */
static void print_box_header(int color, const char *title)
{
    if (color) {
        printf("\n\033[90m┌─── \033[36m%s\033[90m ", title);
        int rem = 70 - (int)strlen(title);
        if (rem < 2) rem = 2;
        for (int i = 0; i < rem; i++) fputs("─", stdout);
        printf("┐\033[0m\n");
    } else {
        printf("\n+--- %s ", title);
        int rem = 70 - (int)strlen(title);
        if (rem < 2) rem = 2;
        for (int i = 0; i < rem; i++) fputc('-', stdout);
        printf("+\n");
    }
}

static void print_box_footer(int color)
{
    if (color) {
        printf("\033[90m└────────────────────────────────────────────────────────────────────────────┘\033[0m\n");
    } else {
        printf("+----------------------------------------------------------------------------+\n");
    }
}

/**
 * show_known_command_card - Affiche les détails d'une commande connue ou contextuelle.
 */
static void show_known_command_card(Shell *sh, const char *title, const char *rule,
                                   const char *summary, const char *options, const char *examples)
{
    int c = !sh->no_color && isatty(STDOUT_FILENO);
    print_box_header(c, title);

    if (c) {
        printf("\033[90m│\033[0m  \033[36mRègle générale / Formule :\033[0m\n");
        printf("\033[90m│\033[0m     \033[34m%s\033[0m\n", rule);
        if (summary && *summary) {
            printf("\033[90m│\033[0m  \033[90mUsage :\033[0m %s\n", summary);
        }
        if (options && *options) {
            printf("\033[90m│\033[0m\n");
            printf("\033[90m│\033[0m  \033[33mOptions disponibles :\033[0m\n");
            char *dup = strdup(options);
            if (dup) {
                char *line = strtok(dup, "\n");
                while (line) {
                    printf("\033[90m│\033[0m   \033[90m%s\033[0m\n", line);
                    line = strtok(NULL, "\n");
                }
                free(dup);
            }
        }
        if (examples && *examples) {
            printf("\033[90m│\033[0m\n");
            printf("\033[90m│\033[0m  \033[32mExemples d'utilisation :\033[0m\n");
            char *dup = strdup(examples);
            if (dup) {
                char *line = strtok(dup, "\n");
                while (line) {
                    printf("\033[90m│\033[0m   %s\n", line);
                    line = strtok(NULL, "\n");
                }
                free(dup);
            }
        }
    } else {
        printf("|  [Règle générale / Formule] :\n|     %s\n", rule);
        if (summary && *summary) {
            printf("|  [Usage] : %s\n", summary);
        }
        if (options && *options) {
            printf("|\n|  [Champs & Options] :\n");
            char *dup = strdup(options);
            if (dup) {
                char *line = strtok(dup, "\n");
                while (line) {
                    printf("|   %s\n", line);
                    line = strtok(NULL, "\n");
                }
                free(dup);
            }
        }
        if (examples && *examples) {
            printf("|\n|  [Exemples d'utilisation] :\n");
            char *dup = strdup(examples);
            if (dup) {
                char *line = strtok(dup, "\n");
                while (line) {
                    printf("|   %s\n", line);
                    line = strtok(NULL, "\n");
                }
                free(dup);
            }
        }
    }

    print_box_footer(c);
}

/**
 * show_typo_suggestions - Affiche les suggestions avec formule et exemples
 *                         pour une commande erronée (ex: 'mrdir' -> 'rmdir', 'mkdir').
 */
static void show_typo_suggestions(Shell *sh, const char *typed, SuggestionCandidate *candidates, size_t count)
{
    int c = !sh->no_color && isatty(STDOUT_FILENO);
    char title[512];
    snprintf(title, sizeof title, "Commande non reconnue : %s", typed);
    print_box_header(c, title);

    if (c) {
        printf("\033[90m│\033[0m  \033[33mCommandes suggérées :\033[0m\n");
        for (size_t i = 0; i < count; i++) {
            printf("\033[90m│\033[0m\n");
            printf("\033[90m│\033[0m  \033[36m* %s\033[0m\n", candidates[i].name);
            if (candidates[i].knowledge) {
                printf("\033[90m│\033[0m     \033[90m• Formule : \033[0m%s\n", candidates[i].knowledge->rule);
                printf("\033[90m│\033[0m     \033[90m• Usage   : \033[0m%s\n", candidates[i].knowledge->summary);
                if (candidates[i].knowledge->examples) {
                    char *dup = strdup(candidates[i].knowledge->examples);
                    if (dup) {
                        char *line = strtok(dup, "\n");
                        int ex_count = 0;
                        while (line && ex_count < 2) {
                            printf("\033[90m│\033[0m     \033[90m• Exemple : \033[34m%s\033[0m\n", line);
                            line = strtok(NULL, "\n");
                            ex_count++;
                        }
                        free(dup);
                    }
                }
            } else {
                printf("\033[90m│\033[0m     \033[90m• Binaire exécutable disponible dans le PATH système\033[0m\n");
            }
        }
    } else {
        printf("|  Commandes suggérées :\n");
        for (size_t i = 0; i < count; i++) {
            printf("|\n|  * %s\n", candidates[i].name);
            if (candidates[i].knowledge) {
                printf("|     - Règle générale : %s\n", candidates[i].knowledge->rule);
                printf("|     - Usage          : %s\n", candidates[i].knowledge->summary);
            }
        }
    }

    print_box_footer(c);
}

/**
 * show_prefix_suggestions - Affiche les commandes correspondant à un préfixe incomplet.
 */
static void show_prefix_suggestions(Shell *sh, const char *prefix, const char **matches, size_t count)
{
    int c = !sh->no_color && isatty(STDOUT_FILENO);
    char title[512];
    snprintf(title, sizeof title, "Suggestions : %s*", prefix);
    print_box_header(c, title);

    if (c) {
        for (size_t i = 0; i < count; i++) {
            const CmdKnowledge *k = find_command_knowledge(matches[i]);
            if (k) {
                printf("\033[90m│\033[0m   \033[36m%-14s\033[0m \033[90m%-58.58s\033[0m \033[90m│\033[0m\n",
                       matches[i], k->summary ? k->summary : k->rule);
            } else {
                printf("\033[90m│\033[0m   \033[36m%-14s\033[0m \033[90m(binaire système)\033[0m                                     \033[90m│\033[0m\n",
                       matches[i]);
            }
        }
    } else {
        for (size_t i = 0; i < count; i++) {
            const CmdKnowledge *k = find_command_knowledge(matches[i]);
            printf("|   %-14s %s\n", matches[i], (k && k->summary) ? k->summary : "");
        }
    }

    print_box_footer(c);
}

/* ============================================================================
 * COMPLÉTION DES CHEMINS ET DOSSIERS (Arguments de Fichiers)
 * ============================================================================
 */
static int complete_path_argument(char *token, char *completed, size_t max_size, int *is_dir)
{
    char dir_path[1024] = ".";
    char prefix[256] = "";
    *is_dir = 0;

    char *last_slash = strrchr(token, '/');
    if (last_slash) {
        size_t dirlen = last_slash - token;
        if (dirlen == 0) {
            strcpy(dir_path, "/");
        } else {
            snprintf(dir_path, sizeof dir_path, "%.*s", (int)dirlen, token);
        }
        strncpy(prefix, last_slash + 1, sizeof prefix - 1);
    } else {
        strncpy(prefix, token, sizeof prefix - 1);
    }

    DIR *d = opendir(dir_path);
    if (!d) return 0;

    char match[256] = "";
    int match_count = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (prefix[0] != '.' && ent->d_name[0] == '.') continue;
        if (!strncmp(ent->d_name, prefix, strlen(prefix))) {
            if (match_count == 0) {
                strncpy(match, ent->d_name, sizeof match - 1);
            }
            match_count++;
        }
    }
    closedir(d);

    if (match_count == 1) {
        char full[2048];
        if (!strcmp(dir_path, ".")) {
            snprintf(full, sizeof full, "%s", match);
        } else if (!strcmp(dir_path, "/")) {
            snprintf(full, sizeof full, "/%s", match);
        } else {
            snprintf(full, sizeof full, "%s/%s", dir_path, match);
        }

        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            *is_dir = 1;
            snprintf(completed, max_size, "%s/", full);
        } else {
            snprintf(completed, max_size, "%s", full);
        }
        return 1;
    }

    return 0;
}

/* ============================================================================
 * MOTEUR PRINCIPAL GUESS HANDLE TAB
 * ============================================================================
 */
int guess_handle_tab(Shell *sh, char *buf, size_t *len, size_t max_len, size_t *pos)
{
    (void)max_len;
    scan_path_binaries();

    /* 1. Ligne vide : afficher astuce et commandes principales */
    if (*len == 0) {
        int c = !sh->no_color && isatty(STDOUT_FILENO);
        print_box_header(c, "Assistance [TAB] — Aide Intelligente");
        if (c) {
            puts("\033[90m│\033[0m  \033[33mTapez une commande ou son début puis appuyez sur [TAB] :\033[0m            \033[90m│\033[0m");
            puts("\033[90m│\033[0m    • \033[32mCommande complète\033[0m    : Formule générale, options et exemples      \033[90m│\033[0m");
            puts("\033[90m│\033[0m    • \033[36mCommande incomplète\033[0m  : Auto-complétion et suggestions              \033[90m│\033[0m");
            puts("\033[90m│\033[0m    • \033[33mFaute de frappe\033[0m      : Détection intelligente (ex: mrdir -> mkdir) \033[90m│\033[0m");
            puts("\033[90m│\033[0m    • \033[34mDrapeaux/Options\033[0m     : Formule spécifique (ex: tar -x)             \033[90m│\033[0m");
        } else {
            puts("|  Tapez une commande puis [TAB] pour obtenir de l'aide ou la compléter.");
        }
        print_box_footer(c);
        return 0;
    }

    /* Découpage de la ligne saisie */
    char line_copy[1024];
    strncpy(line_copy, buf, sizeof line_copy - 1);
    line_copy[sizeof line_copy - 1] = '\0';

    /* Détection du premier mot (commande) et des arguments */
    char *first_space = strchr(line_copy, ' ');
    char cmd_name[256] = "";
    char args_part[1024] = "";

    if (first_space) {
        size_t c_len = first_space - line_copy;
        if (c_len >= sizeof cmd_name) c_len = sizeof cmd_name - 1;
        strncpy(cmd_name, line_copy, c_len);
        cmd_name[c_len] = '\0';

        char *arg_start = first_space;
        while (*arg_start == ' ') arg_start++;
        strncpy(args_part, arg_start, sizeof args_part - 1);
    } else {
        strncpy(cmd_name, line_copy, sizeof cmd_name - 1);
    }

    /* SCÉNARIO A : L'utilisateur n'a tapé qu'un seul mot (sans espace) */
    if (!first_space) {
        /* A1 : Commande exacte et connue */
        const CmdKnowledge *exact = find_command_knowledge(cmd_name);
        if (exact) {
            char title[512];
            snprintf(title, sizeof title, "Aide : %s", exact->name);
            show_known_command_card(sh, title, exact->rule, exact->summary, exact->options, exact->examples);
            return 0;
        }

        /* A2 : Préfixe de commandes (commande incomplète, ex: "cle", "ex", "mk") */
        const char *matches[64];
        size_t match_count = 0;

        for (size_t i = 0; i < CMD_DB_COUNT && match_count < 64; i++) {
            if (!strncmp(CMD_DATABASE[i].name, cmd_name, strlen(cmd_name))) {
                matches[match_count++] = CMD_DATABASE[i].name;
            }
        }
        for (size_t i = 0; i < g_path_bins_count && match_count < 64; i++) {
            if (!strncmp(g_path_bins[i], cmd_name, strlen(cmd_name))) {
                int exists = 0;
                for (size_t m = 0; m < match_count; m++) {
                    if (!strcmp(matches[m], g_path_bins[i])) { exists = 1; break; }
                }
                if (!exists) matches[match_count++] = g_path_bins[i];
            }
        }

        if (match_count == 1) {
            /* Complétion automatique directe de la commande */
            snprintf(buf, 1024, "%s ", matches[0]);
            *len = strlen(buf);
            *pos = *len;
            const CmdKnowledge *k = find_command_knowledge(matches[0]);
            char title[512];
            snprintf(title, sizeof title, "Complétion : %s", matches[0]);
            if (k) {
                show_known_command_card(sh, title, k->rule, k->summary, k->options, k->examples);
            }
            return 1;
        } else if (match_count > 1) {
            /* Affichage des suggestions multiples et complétion du préfixe commun */
            show_prefix_suggestions(sh, cmd_name, matches, match_count > 12 ? 12 : match_count);

            /* Calcul du plus long préfixe commun */
            char common[256];
            strncpy(common, matches[0], sizeof common - 1);
            for (size_t m = 1; m < match_count; m++) {
                size_t j = 0;
                while (common[j] && matches[m][j] && common[j] == matches[m][j]) j++;
                common[j] = '\0';
            }
            if (strlen(common) > strlen(cmd_name)) {
                strncpy(buf, common, 1024);
                *len = strlen(buf);
                *pos = *len;
                return 1;
            }
            return 0;
        }

        /* A3 : Commande erronée / Faute de frappe (ex: "mrdir" -> "rmdir", "mkdir") */
        SuggestionCandidate candidates[128];
        size_t cand_count = 0;

        for (size_t i = 0; i < CMD_DB_COUNT; i++) {
            int d = damerau_levenshtein(cmd_name, CMD_DATABASE[i].name);
            int threshold = (strlen(cmd_name) <= 4) ? 2 : 3;
            if (d > 0 && d <= threshold) {
                candidates[cand_count].name = CMD_DATABASE[i].name;
                candidates[cand_count].distance = d;
                candidates[cand_count].knowledge = &CMD_DATABASE[i];
                cand_count++;
            }
        }

        /* Tri par distance d'édition croissante (bulles) */
        for (size_t i = 0; i < cand_count; i++) {
            for (size_t j = i + 1; j < cand_count; j++) {
                if (candidates[j].distance < candidates[i].distance) {
                    SuggestionCandidate tmp = candidates[i];
                    candidates[i] = candidates[j];
                    candidates[j] = tmp;
                }
            }
        }

        if (cand_count > 0) {
            size_t show_n = cand_count > 3 ? 3 : cand_count;
            show_typo_suggestions(sh, cmd_name, candidates, show_n);
            return 0;
        } else {
            int c = !sh->no_color && isatty(STDOUT_FILENO);
            char title[512];
            snprintf(title, sizeof title, "Commande inconnue : %s", cmd_name);
            print_box_header(c, title);
            if (c) {
                printf("\033[90m│\033[0m  Aucune correspondance directe. Tapez \033[36m« guide »\033[0m pour l'aide. \033[90m│\033[0m\n");
            } else {
                printf("|  Aucune correspondance. Tapez 'guide' pour l'aide.\n");
            }
            print_box_footer(c);
            return 0;
        }
    }

    /* SCÉNARIO B : Commande suivie d'arguments (ex: "tar -x", "cd ..", "mkdir -p") */
    const ContextualKnowledge *ctx = find_contextual_knowledge(cmd_name, args_part);
    if (ctx) {
        char title[512];
        snprintf(title, sizeof title, "Aide : %s %s", ctx->cmd, ctx->flag);
        show_known_command_card(sh, title, ctx->rule, ctx->summary, ctx->options, ctx->examples);
        return 0;
    }

    /* Si l'utilisateur est en train de saisir un chemin ou fichier (dernier mot) */
    char *last_token = strrchr(buf, ' ');
    if (last_token && *(last_token + 1) != '\0') {
        char *arg_token = last_token + 1;
        char completed[1024];
        int is_dir = 0;
        if (complete_path_argument(arg_token, completed, sizeof completed, &is_dir)) {
            /* Remplacement du token par le chemin complété */
            size_t prefix_len = (last_token + 1) - buf;
            snprintf(buf + prefix_len, 1024 - prefix_len, "%s", completed);
            *len = strlen(buf);
            *pos = *len;
            return 1;
        }
    }

    /* Si commande connue avec arguments génériques */
    const CmdKnowledge *k = find_command_knowledge(cmd_name);
    if (k) {
        char title[512];
        snprintf(title, sizeof title, "Aide : %s", k->name);
        show_known_command_card(sh, title, k->rule, k->summary, k->options, k->examples);
        return 0;
    }

    /* Commande principale erronée */
    SuggestionCandidate candidates[128];
    size_t cand_count = 0;
    for (size_t i = 0; i < CMD_DB_COUNT; i++) {
        int d = damerau_levenshtein(cmd_name, CMD_DATABASE[i].name);
        if (d > 0 && d <= 2) {
            candidates[cand_count].name = CMD_DATABASE[i].name;
            candidates[cand_count].distance = d;
            candidates[cand_count].knowledge = &CMD_DATABASE[i];
            cand_count++;
        }
    }
    if (cand_count > 0) {
        show_typo_suggestions(sh, cmd_name, candidates, cand_count > 3 ? 3 : cand_count);
    }
    return 0;
}

/* ============================================================================
 * LECTURE INTERACTIVE DE LIGNE (Mode Brut / Non-Canonique)
 * ============================================================================
 */
static struct termios orig_termios;
static int raw_mode_active = 0;

static void disable_raw_mode(void)
{
    if (raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_active = 0;
    }
}

static int enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) return -1;

    struct termios raw = orig_termios;
    /* Désactiver mode canonique, écho automatique et signaux claviers directs */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return -1;
    raw_mode_active = 1;
    return 0;
}

char *guess_read_line(Shell *sh)
{
    if (enable_raw_mode() < 0) return NULL;

    char buf[2048] = "";
    char saved_buf[2048] = "";
    size_t len = 0;
    size_t pos = 0;
    int history_index = (int)sh->history_count;

    while (1) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            disable_raw_mode();
            return NULL;
        }

        /* Ctrl+C (ASCII 3) */
        if (c == 3) {
            fputs("^C\n", stdout);
            buf[0] = '\0';
            len = 0;
            pos = 0;
            history_index = (int)sh->history_count;
            prompt_print(sh);
            fflush(stdout);
            continue;
        }

        /* Ctrl+D (ASCII 4) */
        if (c == 4) {
            if (len == 0) {
                disable_raw_mode();
                return NULL;
            }
            continue;
        }

        /* Entrée (\n ou \r) */
        if (c == '\n' || c == '\r') {
            putchar('\n');
            fflush(stdout);
            disable_raw_mode();
            return strdup(buf);
        }

        /* Touche TAB (\t / ASCII 9) */
        if (c == '\t') {
            if (sh->interactive && !sh->no_anim && isatty(STDOUT_FILENO)) {
                const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
                for (int f = 0; f < 4; f++) {
                    printf("\r\033[90m[tab] \033[36m[%s]\033[0m", frames[f]);
                    fflush(stdout);
                    struct timespec t = {0, 18000000}; /* 18 ms */
                    nanosleep(&t, NULL);
                }
                fputs("\r\033[K", stdout);
                fflush(stdout);
            }
            putchar('\n');
            guess_handle_tab(sh, buf, &len, sizeof buf, &pos);
            prompt_print(sh);
            fputs(buf, stdout);
            if (len > pos) {
                printf("\033[%zuD", len - pos);
            }
            fflush(stdout);
            continue;
        }

        /* Retour arrière (Backspace / ASCII 127 ou 8) */
        if (c == 127 || c == 8) {
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
                /* Réaffichage propre de la ligne */
                fputs("\r\033[K", stdout);
                prompt_print(sh);
                fputs(buf, stdout);
                if (len > pos) {
                    printf("\033[%zuD", len - pos);
                }
                fflush(stdout);
            }
            continue;
        }

        /* Séquences d'échappement (Flèches, Home, End) */
        if (c == 27) {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;

            if (seq[0] == '[') {
                /* Flèche HAUT : Historique précédent */
                if (seq[1] == 'A') {
                    if (sh->history_count > 0 && history_index > 0) {
                        if (history_index == (int)sh->history_count) {
                            strncpy(saved_buf, buf, sizeof saved_buf - 1);
                        }
                        history_index--;
                        strncpy(buf, sh->history[history_index], sizeof buf - 1);
                        len = strlen(buf);
                        pos = len;
                        fputs("\r\033[K", stdout);
                        prompt_print(sh);
                        fputs(buf, stdout);
                        fflush(stdout);
                    }
                    continue;
                }
                /* Flèche BAS : Historique suivant */
                if (seq[1] == 'B') {
                    if (history_index < (int)sh->history_count) {
                        history_index++;
                        if (history_index == (int)sh->history_count) {
                            strncpy(buf, saved_buf, sizeof buf - 1);
                        } else {
                            strncpy(buf, sh->history[history_index], sizeof buf - 1);
                        }
                        len = strlen(buf);
                        pos = len;
                        fputs("\r\033[K", stdout);
                        prompt_print(sh);
                        fputs(buf, stdout);
                        fflush(stdout);
                    }
                    continue;
                }
                /* Flèche DROITE */
                if (seq[1] == 'C') {
                    if (pos < len) {
                        pos++;
                        fputs("\033[1C", stdout);
                        fflush(stdout);
                    }
                    continue;
                }
                /* Flèche GAUCHE */
                if (seq[1] == 'D') {
                    if (pos > 0) {
                        pos--;
                        fputs("\033[1D", stdout);
                        fflush(stdout);
                    }
                    continue;
                }
            }
            continue;
        }

        /* Caractères imprimables standards */
        if (c >= 32 && c <= 126) {
            if (len < sizeof buf - 1) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = c;
                pos++;
                len++;
                buf[len] = '\0';

                if (pos == len) {
                    putchar(c);
                    fflush(stdout);
                } else {
                    fputs("\r\033[K", stdout);
                    prompt_print(sh);
                    fputs(buf, stdout);
                    if (len > pos) {
                        printf("\033[%zuD", len - pos);
                    }
                    fflush(stdout);
                }
            }
        }
    }
}

/* ============================================================================
 * COMMANDE INTERNE BUILT-IN : guess
 * ============================================================================
 */
int builtin_guess(Shell *sh, char **argv)
{
    if (!argv[1]) {
        int c = !sh->no_color && isatty(STDOUT_FILENO);
        print_box_header(c, "Moteur de Devinette & Complétion");
        if (c) {
            puts("\033[90m│\033[0m  \033[33mUsage :\033[0m guess <commande | préfixe | faute_de_frappe>                 \033[90m│\033[0m");
            puts("\033[90m│\033[0m                                                                            \033[90m│\033[0m");
            puts("\033[90m│\033[0m  \033[36mExemples concrets :\033[0m                                                       \033[90m│\033[0m");
            puts("\033[90m│\033[0m    \033[34mguess tar -x\033[0m        Formule et options de décompression tar            \033[90m│\033[0m");
            puts("\033[90m│\033[0m    \033[34mguess mrdir\033[0m         Détecte la faute et suggère rmdir et mkdir         \033[90m│\033[0m");
            puts("\033[90m│\033[0m    \033[34mguess cle\033[0m           Auto-complète vers 'clear'                         \033[90m│\033[0m");
            puts("\033[90m│\033[0m                                                                            \033[90m│\033[0m");
            puts("\033[90m│\033[0m  \033[33m[tip]\033[0m Vous pouvez aussi appuyer directement sur \033[36m[TAB]\033[0m à l'invite !   \033[90m│\033[0m");
        } else {
            puts("Usage : guess <commande | préfixe | faute_de_frappe>");
            puts("Exemples :");
            puts("  guess tar -x");
            puts("  guess mrdir");
        }
        print_box_footer(c);
        return 0;
    }

    char query[1024] = "";
    for (int i = 1; argv[i]; i++) {
        if (i > 1) strncat(query, " ", sizeof query - strlen(query) - 1);
        strncat(query, argv[i], sizeof query - strlen(query) - 1);
    }

    size_t len = strlen(query);
    size_t pos = len;
    guess_handle_tab(sh, query, &len, sizeof query, &pos);
    return 0;
}
