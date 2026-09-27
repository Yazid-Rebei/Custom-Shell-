#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : executor.c
 * DESCRIPTION : Exécution des commandes externes, pipelines, redirections d'E/S
 *               et enchaînements conditionnels (&&, ||, ;).
 *
 * COURS THÉORIQUE — Gestion des Processus et Descripteurs POSIX :
 * Un shell Unix est le chef d'orchestre des processus du système :
 * 1. fork() : Duplique le processus parent. L'enfant reçoit une copie exacte
 *    de la mémoire et des descripteurs de fichiers ouverts.
 * 2. execvp() : Remplace l'image mémoire de l'enfant par un nouveau binaire,
 *    en explorant automatiquement les dossiers listés dans la variable PATH.
 * 3. pipe() : Crée un tube unidirectionnel (tableau de 2 fds : lecture fds[0],
 *    écriture fds[1]). La sortie standard d'une commande est branchée sur
 *    l'entrée standard de la suivante via dup2().
 * 4. dup2(oldfd, newfd) : Redirige un descripteur (0=stdin, 1=stdout, 2=stderr)
 *    vers un fichier ou l'extrémité d'un pipe.
 * 5. waitpid() : Attend la terminaison d'un ou plusieurs enfants et extrait
 *    leur code de retour avec les macros WIFEXITED et WEXITSTATUS.
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

/**
 * redirect - Configure les redirections d'entrée, sortie et erreur pour une commande.
 * @c: Pointeur vers la structure Command contenant les redirections et cibles.
 *
 * FONCTIONS SYSTÈME UTILISÉES :
 * - open(fichier, flags, mode) : ouvre ou crée le fichier cible.
 *   * O_RDONLY : lecture seule (<)
 *   * O_WRONLY | O_CREAT | O_TRUNC : écriture en écrasant (>)
 *   * O_WRONLY | O_CREAT | O_APPEND : écriture à la suite (>>)
 * - dup2(fd, dst) : substitue le descripteur dst (0, 1 ou 2) par fd.
 * - close(fd) : ferme le descripteur temporaire après duplication.
 *
 * Return: 0 en cas de succès, -1 en cas d'erreur d'ouverture ou de duplication.
 */
static int redirect(Command *c)
{
    for (size_t i = 0; i < c->nr; i++) {
        int flags = O_CREAT | O_WRONLY;
        int fd;

        if (!strcmp(c->redir[i], "<")) {
            flags = O_RDONLY;
            fd = open(c->target[i], flags);
        } else {
            if (!strcmp(c->redir[i], ">>")) {
                flags |= O_APPEND;
            } else {
                flags |= O_TRUNC;
            }
            fd = open(c->target[i], flags, 0666);
        }

        if (fd < 0) {
            perror(c->target[i]);
            return -1;
        }

        int dst = STDOUT_FILENO;
        if (!strcmp(c->redir[i], "<")) {
            dst = STDIN_FILENO;
        } else if (!strcmp(c->redir[i], "2>")) {
            dst = STDERR_FILENO;
        }

        if (dup2(fd, dst) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

/**
 * runpipe - Exécute un pipeline complet de commandes reliées par des tubes '|'.
 * @sh: Pointeur vers l'état du shell (derniers statuts, jobs, variables).
 * @p: Pipeline à exécuter (contient les commandes, leur nombre et l'indicateur '&').
 * @line: Ligne originale de la commande (utilisée pour nommer les jobs en arrière-plan).
 *
 * GESTION PARTICULIÈRE :
 * - Si le pipeline contient une unique commande builtin et n'est pas en arrière-plan,
 *   elle est exécutée directement dans le processus parent pour préserver ses effets
 *   (cd, export, etc.), tout en sauvegardant et restaurant stdin/stdout/stderr via dup/dup2.
 * - Si le pipeline comporte plusieurs étapes ou une commande externe, fork() est appelé
 *   pour chaque commande et les descripteurs de pipe sont interconnectés.
 *
 * Return: Statut de sortie de la dernière commande du pipeline.
 */
static int runpipe(Shell *sh, Pipeline *p, const char *line)
{
    /* Cas d'optimisation : builtin unique sans arrière-plan dans le shell parent */
    if (p->n == 1 && !p->background && is_builtin(p->cmd[0].words.v[0])) {
        int saved[3];
        for (int i = 0; i < 3; i++) {
            saved[i] = dup(i);
            if (saved[i] < 0) {
                perror("dup");
                while (i--) close(saved[i]);
                return 1;
            }
        }

        if (redirect(&p->cmd[0]) < 0) {
            for (int i = 0; i < 3; i++) {
                dup2(saved[i], i);
                close(saved[i]);
            }
            return 1;
        }

        int handled = 0;
        int ret = builtin_run(sh, p->cmd[0].words.v, &handled);
        fflush(NULL);

        /* Restauration des descripteurs originaux */
        for (int i = 0; i < 3; i++) {
            dup2(saved[i], i);
            close(saved[i]);
        }

        if (sh->should_exit) return sh->exit_status;
        sh->last_status = ret;
        return ret;
    }

    if (p->n > 256) {
        fprintf(stderr, "pipeline: trop de commandes (max 256)\n");
        return 1;
    }

    int prev = -1;
    pid_t pids[256];
    size_t np = 0;

    for (size_t i = 0; i < p->n; i++) {
        int fds[2] = {-1, -1};

        /* Création du tube inter-processus si ce n'est pas la dernière étape */
        if (i + 1 < p->n && pipe(fds) < 0) {
            perror("pipe");
            goto fail;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (fds[0] != -1) close(fds[0]);
            if (fds[1] != -1) close(fds[1]);
            goto fail;
        }

        if (pid == 0) {
            /* Processus Enfant : réinitialisation des signaux par défaut */
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            /* Connexion de l'entrée au tube précédent */
            if (prev != -1) {
                dup2(prev, STDIN_FILENO);
                close(prev);
            }

            /* Connexion de la sortie au tube suivant */
            if (fds[1] != -1) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]);
                close(fds[1]);
            }

            /* Application des redirections <, >, >>, 2> */
            if (redirect(&p->cmd[i]) < 0) {
                _exit(1);
            }

            /* Si c'est un builtin dans un tube (ex: echo test | cat), exécution en sous-processus */
            if (is_builtin(p->cmd[i].words.v[0])) {
                int h = 0;
                int ret = builtin_run(sh, p->cmd[i].words.v, &h);
                fflush(NULL);
                _exit(sh->should_exit ? sh->exit_status : ret);
            }

            /* Exécution du binaire externe via execvp */
            execvp(p->cmd[i].words.v[0], p->cmd[i].words.v);
            int e = errno;
            perror(p->cmd[i].words.v[0]);
            _exit(e == ENOENT ? 127 : 126);
        }

        /* Processus Parent : gestion et fermeture des descripteurs de tube */
        pids[np++] = pid;
        if (prev != -1) close(prev);
        if (fds[1] != -1) close(fds[1]);
        prev = fds[0];
    }

    /* Gestion du lancement en tâche de fond (arrière-plan avec '&') */
    if (p->background) {
        if (sh->job_count < 64) {
            Job *j = &sh->jobs[sh->job_count++];
            j->pid = pids[np - 1];
            j->text = strdup(line ? line : "background job");
            j->running = 1;
            sh->last_bg = j->pid;
            printf("[%zu] %ld\n", sh->job_count, (long)j->pid);
        }
        return 0;
    }

    /* Attente bloquante de la fin de tous les processus du pipeline */
    int status = 0;
    for (size_t i = 0; i < np; i++) {
        int st = 0;
        while (waitpid(pids[i], &st, 0) < 0 && errno == EINTR) {
            /* Reprise automatique en cas d'interruption par un signal */
        }
        if (i + 1 == np) {
            if (WIFEXITED(st)) {
                status = WEXITSTATUS(st);
            } else if (WIFSIGNALED(st)) {
                status = 128 + WTERMSIG(st);
            } else {
                status = 1;
            }
        }
    }
    return status;

fail:
    if (prev != -1) close(prev);
    for (size_t i = 0; i < np; i++) {
        while (waitpid(pids[i], NULL, 0) < 0 && errno == EINTR) {}
    }
    return 1;
}

/**
 * execute_program - Évalue séquentiellement les pipelines du programme en tenant compte
 *                   des opérateurs logiques '&&' et '||' et du séparateur ';'.
 * @sh: Pointeur vers l'état du shell.
 * @p: Pointeur vers le programme complet découpé en pipelines et opérateurs.
 * @line: Chaîne originale saisie par l'utilisateur.
 *
 * RÈGLE D'ÉVALUATION CONDITIONNELLE :
 * - Opérateur '&&' : le pipeline suivant n'est exécuté QUE SI le précédent a réussi (status == 0).
 * - Opérateur '||' : le pipeline suivant n'est exécuté QUE SI le précédent a échoué (status != 0).
 * - Opérateur ';'  : le pipeline suivant est exécuté inconditionnellement.
 *
 * Return: Code de retour de la dernière instruction exécutée.
 */
int execute_program(Shell *sh, Program *p, const char *line)
{
    int status = sh->last_status;

    for (size_t i = 0; i < p->n; i++) {
        /* Court-circuit '&&' : sauté si statut précédent est en échec */
        if (i > 0 && p->op[i - 1] && !strcmp(p->op[i - 1], "&&") && status != 0) {
            continue;
        }

        /* Court-circuit '||' : sauté si statut précédent est en succès */
        if (i > 0 && p->op[i - 1] && !strcmp(p->op[i - 1], "||") && status == 0) {
            continue;
        }

        status = runpipe(sh, &p->pipe[i], line);
        if (sh->should_exit) break;
    }

    sh->last_status = status;
    return status;
}
