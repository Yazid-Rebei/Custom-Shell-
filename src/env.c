#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : env.c
 * DESCRIPTION : Expansion des variables d'environnement, des variables spéciales
 *               ($?, $$, $!, $0) et des raccourcis du répertoire utilisateur (~, &).
 *
 * COURS THÉORIQUE — Expansion des Paramètres dans un Shell :
 * Avant qu'une commande ne soit exécutée, le shell remplace les variables par
 * leur valeur textuelle courante :
 * - $VAR : Variable d'environnement lue via getenv(). Si la variable n'existe pas,
 *   elle se résout en chaîne vide "", conformément aux règles standard POSIX.
 * - $?   : Code de retour de la dernière commande exécutée (0 = succès, >0 = code d'erreur).
 * - $$   : Identifiant de processus (PID) du shell courant, obtenu avec getpid().
 * - $!   : Identifiant de processus (PID) du dernier job lancé en arrière-plan avec '&'.
 * - $0   : Nom de l'interpréteur de commandes ("yazid_shell").
 * - ~ / & : Raccourcis menant vers le dossier personnel (variable $HOME).
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
 * expand_text - Développe les variables et le tilde dans une chaîne source.
 * @sh: Pointeur vers l'état du shell pour accéder à last_status et last_bg.
 * @s: Chaîne brute à développer.
 * @quoted: Indicateur de protection par des guillemets (réservé pour extensions futures).
 *
 * FONCTIONS SYSTÈME ET BIBLIOTHÈQUE C :
 * - getenv(nom)  : recherche la valeur de la variable d'environnement dans le bloc environ.
 * - getpid()     : appel système retournant le PID du processus appelant.
 * - malloc(), realloc(), free() : gestion dynamique de la mémoire du tampon de sortie.
 * - snprintf()   : conversion sécurisée d'entiers en chaînes décimales.
 * - memcpy()     : copie rapide d'octets de valeur vers le tampon de destination.
 *
 * Return: Nouvelle chaîne allouée dynamiquement contenant le texte développé.
 *         L'appelant est responsable de sa libération avec free().
 */
char *expand_text(Shell *sh, const char *s, int quoted)
{
    (void)quoted;
    if (!s) return NULL;

    size_t cap = strlen(s) * 4 + 64;
    size_t n = 0;
    char *out = malloc(cap);
    if (!out) return NULL;

    for (size_t i = 0; s[i] != '\0'; ) {
        char value[64];
        const char *v = NULL;
        size_t used = 1;

        /* Remplacement du tilde '~' ou de '&' au début d'un chemin par la valeur de HOME */
        if ((s[i] == '~' || s[i] == '&') && i == 0 && (s[i + 1] == '/' || s[i + 1] == '\0')) {
            v = getenv("HOME");
            if (!v) v = "";
        } else if (s[i] == '$') {
            /* Variables spéciales du shell */
            if (s[i + 1] == '?') {
                /* $? : dernier code de retour */
                snprintf(value, sizeof value, "%d", sh->last_status);
                v = value;
                used = 2;
            } else if (s[i + 1] == '$') {
                /* $$ : PID du shell */
                snprintf(value, sizeof value, "%ld", (long)getpid());
                v = value;
                used = 2;
            } else if (s[i + 1] == '!') {
                /* $! : PID du dernier processus d'arrière-plan */
                snprintf(value, sizeof value, "%ld", (long)sh->last_bg);
                v = value;
                used = 2;
            } else if (s[i + 1] == '0') {
                /* $0 : nom du shell */
                v = "yazid_shell";
                used = 2;
            } else {
                /* Variable nommée : recherche des caractères alphanumériques et '_' */
                size_t j = i + 1;
                while ((s[j] >= 'A' && s[j] <= 'Z') ||
                       (s[j] >= 'a' && s[j] <= 'z') ||
                       (s[j] >= '0' && s[j] <= '9') ||
                       s[j] == '_') {
                    j++;
                }

                if (j > i + 1) {
                    char *key = strndup(s + i + 1, j - i - 1);
                    if (key) {
                        v = getenv(key);
                        free(key);
                    }
                    /* Si la variable n'est pas définie dans l'environnement, substitution par "" */
                    if (!v) {
                        v = "";
                    }
                    used = j - i;
                }
            }
        }

        if (v != NULL) {
            size_t z = strlen(v);
            if (n + z + 2 > cap) {
                cap = (n + z + 2) * 2;
                char *next = realloc(out, cap);
                if (!next) {
                    free(out);
                    return NULL;
                }
                out = next;
            }
            if (z > 0) {
                memcpy(out + n, v, z);
                n += z;
            }
            i += used;
        } else {
            /* Caractère littéral copié directement */
            if (n + 2 > cap) {
                cap *= 2;
                char *next = realloc(out, cap);
                if (!next) {
                    free(out);
                    return NULL;
                }
                out = next;
            }
            out[n++] = s[i++];
        }
    }

    out[n] = '\0';
    return out;
}
