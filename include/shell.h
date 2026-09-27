#ifndef YAZID_SHELL_H
#define YAZID_SHELL_H
/** Types et prototypes partagés par les modules de Yazid Shell. */
#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <sys/types.h>

/** Tableau de mots d'une commande; v est terminé par NULL pour execvp(). */
typedef struct { char **v; size_t n; } Words;
/** Une commande et les paires opérateur/cible de ses redirections. */
typedef struct { Words words; char **redir; char **target; size_t nr; } Command;
/** Une suite de commandes liées par des pipes, éventuellement lancée en fond. */
typedef struct { Command *cmd; size_t n; int background; } Pipeline;
/** Une ligne complète composée de pipelines liés par &&, || ou ;. */
typedef struct { Pipeline *pipe; size_t n; char **op; } Program;
/** État minimal d'un processus d'arrière-plan suivi par le shell. */
typedef struct { pid_t pid; char *text; int running; } Job;
/** État conservé pendant toute la session interactive. */
typedef struct {
 int last_status, should_exit, exit_status, interactive, no_color, no_anim;
 pid_t last_bg;
 char **aliases; size_t alias_count;
 char **history; size_t history_count;
 Job jobs[64]; size_t job_count;
 char *prompt;
} Shell;

/** Initialise l'état du shell et active les options données. */
void shell_init(Shell *sh, int no_anim);
/** Libère les allocations détenues par la session. */
void shell_destroy(Shell *sh);
/** Parse une ligne et produit sa représentation Program. */
int parse_line(const char *line, Shell *sh, Program *program, char **error);
/** Libère la mémoire produite par parse_line(). */
void free_program(Program *program);
/** Exécute les pipelines et applique &&/|| selon le code de retour. */
int execute_program(Shell *sh, Program *program, const char *line);
/** Lance un built-in si argv[0] est connu; renseigne handled. */
int builtin_run(Shell *sh, char **argv, int *handled);
/** Teste si name est un built-in du shell. */
int is_builtin(const char *name);
/** Commande interne de devinette et d'assistance intelligente guess. */
int builtin_guess(Shell *sh, char **argv);
/** Charge l'historique persistant depuis ~/.yazid_history. */
void history_load(Shell *sh);
/** Sauvegarde l'historique courant vers ~/.yazid_history. */
void history_save(Shell *sh);
/** Met à jour l'état des jobs qui ont terminé. */
void jobs_reap(Shell *sh);
/** Installe les gestionnaires de signaux du processus shell parent. */
void signals_init(void);
/** Affiche le prompt adapté au terminal courant. */
void prompt_print(Shell *sh);
/** Développe les variables et le tilde dans une chaîne allouée. */
char *expand_text(Shell *sh, const char *s, int quoted);
/** Charge et exécute les lignes du fichier ~/.yazidrc. */
int config_load(Shell *sh);
#endif

