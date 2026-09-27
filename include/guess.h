#ifndef YAZID_GUESS_H
#define YAZID_GUESS_H
/**
 * ============================================================================
 * MODULE : guess.h
 * DESCRIPTION : Interface pour le moteur d'assistance intelligente, de devinette
 *               (guess) et d'auto-complétion déclenché par la touche [TAB].
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

#include <stddef.h>

/**
 * guess_handle_tab - Analyse la saisie lors de l'appui sur [TAB] et affiche
 *                    l'aide contextuelle, les suggestions ou effectue la complétion.
 * @sh: Pointeur vers l'état du shell.
 * @buf: Tampon de saisie contenant la ligne actuelle.
 * @len: Pointeur vers la longueur de la chaîne dans buf.
 * @max_len: Capacité maximale du tampon buf.
 * @pos: Pointeur vers la position actuelle du curseur dans la ligne.
 *
 * Scénarios traités intelligemment :
 * 1. Commande correcte connue : affiche formule générale, champs/options et exemples.
 * 2. Commande incomplète : suggère les complétions possibles ou complète la ligne.
 * 3. Commande erronée (faute de frappe, ex: mrdir) : détecte la similarité
 *    (Damerau-Levenshtein) et propose les commandes voulues (ex: rmdir, mkdir)
 *    avec leurs règles générales et exemples d'usage.
 * 4. Arguments / chemins : propose la complétion des fichiers et sous-dossiers.
 *
 * Return: 1 si le tampon a été modifié (auto-complété), 0 sinon.
 */
int guess_handle_tab(Shell *sh, char *buf, size_t *len, size_t max_len, size_t *pos);

/**
 * guess_read_line - Lecture interactive en mode brut avec support temps réel de [TAB],
 *                   retour arrière, historique (flèches Haut/Bas) et signaux.
 * @sh: Pointeur vers l'état du shell.
 *
 * Return: Ligne allouée dynamiquement (à libérer par free()) ou NULL sur fin de fichier (Ctrl+D).
 */
char *guess_read_line(Shell *sh);

/**
 * guess_cleanup - Libère les ressources globales du module guess (ex: cache PATH).
 */
void guess_cleanup(void);

/**
 * builtin_guess - Commande interne 'guess' permettant d'interroger le moteur.
 * @sh: Pointeur vers l'état du shell.
 * @argv: Arguments passés (ex: guess tar -x, guess mrdir).
 *
 * Return: 0 en cas de succès, >0 en cas d'erreur.
 */
int builtin_guess(Shell *sh, char **argv);

#endif /* YAZID_GUESS_H */
