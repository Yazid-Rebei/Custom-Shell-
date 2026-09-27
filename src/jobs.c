#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : jobs.c
 * DESCRIPTION : Suivi non bloquant et récolte des processus lancés en
 * arrière-plan.
 *
 * COURS THÉORIQUE — Processus d'Arrière-Plan et Processus Zombies :
 * Lorsqu'un processus enfant se termine, le noyau Linux conserve son entrée
 * dans la table des processus (état Zombie) jusqu'à ce que son parent lise son
 * code de terminaison via un appel à wait() ou waitpid(). Si le parent
 * n'attendait jamais ses enfants lancés avec '&', le système accumulerait des
 * zombies et risquerait d'épuiser les PID disponibles.
 *
 * L'option WNOHANG :
 * Contrairement au waitpid() standard qui bloque l'exécution jusqu'à la fin
 * de l'enfant, le flag WNOHANG rend l'appel immédiat :
 * - Si le processus est toujours en cours : waitpid retourne 0 sans bloquer.
 * - Si le processus est terminé : waitpid retourne son PID et nettoie le
 * zombie.
 * - Si une erreur survient : waitpid retourne -1.
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
#include <stdlib.h>
#include <sys/wait.h>

/**
 * jobs_reap - Vérifie l'état des processus en arrière-plan et récolte les
 * zombies.
 * @sh: Pointeur vers l'état du shell contenant la table des jobs.
 *
 * FONCTIONS SYSTÈME UTILISÉES :
 * - waitpid(pid, &status, WNOHANG) : interroge l'état du processus sans bloquer
 * le shell.
 * - free() : libère la description textuelle des jobs purgés si la table est
 * pleine.
 */
void jobs_reap(Shell *sh) {
  for (size_t i = 0; i < sh->job_count; i++) {
    if (sh->jobs[i].running) {
      int status = 0;
      pid_t p = waitpid(sh->jobs[i].pid, &status, WNOHANG);
      if (p > 0) {
        /* Le processus a terminé son exécution */
        sh->jobs[i].running = 0;
      }
    }
  }

  /* Si la table de 64 jobs est pleine, compactage des jobs terminés */
  if (sh->job_count >= 64) {
    size_t write_idx = 0;
    for (size_t i = 0; i < sh->job_count; i++) {
      if (sh->jobs[i].running) {
        sh->jobs[write_idx++] = sh->jobs[i];
      } else {
        free(sh->jobs[i].text);
        sh->jobs[i].text = NULL;
      }
    }
    sh->job_count = write_idx;
  }
}
