#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * MODULE : parser.c
 * DESCRIPTION : Analyse lexicale (tokenisation) et syntaxique des lignes de commande.
 *
 * COURS THÉORIQUE — Chaîne de Compilation et Analyse dans un Shell :
 * 1. Analyse Lexicale (Lexer / Tokenizer) :
 *    Transforme une séquence brute de caractères en une liste de jetons (tokens).
 *    Gère les séparateurs d'espaces, les guillemets simples (quotes littérales ''),
 *    les guillemets doubles (quotes avec expansion ""), les échappements antislash (\),
 *    et les métacaractères opérateurs (|, ;, <, >, >>, 2>, &&, ||, &).
 *
 * 2. Analyse Syntaxique (Parser) :
 *    Transforme les tokens en un Arbre Syntaxique Abstrait (AST) :
 *    - Program : contient une série de Pipelines séparés par &&, || ou ;
 *    - Pipeline : contient une suite de Commands interconnectées par des tubes |
 *    - Command : contient un tableau d'arguments Words (argv) et des redirections
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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/** Liste dynamique de chaînes (utilisée pour stocker les tokens extraits). */
typedef struct {
    char **v;
    size_t n, cap;
} List;

/** Tampon dynamique de caractères (utilisé pour accumuler un mot en cours de parsing). */
typedef struct {
    char *v;
    size_t n, cap;
} Text;

/**
 * set_error - Alloue un message d'erreur s'il n'en existe pas déjà un.
 * @error: Pointeur vers le pointeur de chaîne d'erreur retourné à l'appelant.
 * @message: Message explicatif à stocker.
 *
 * FONCTIONS UTILISÉES :
 * - strdup() : alloue et duplique la chaîne message.
 *
 * Return: Toujours -1 pour faciliter le retour d'erreur.
 */
static int set_error(char **error, const char *message)
{
    if (error && !*error) *error = strdup(message);
    return -1;
}

/**
 * push - Ajoute une chaîne à la liste dynamique, en doublant sa capacité si nécessaire.
 * @list: Pointeur vers la structure List.
 * @value: Chaîne allouée à insérer.
 *
 * FONCTIONS UTILISÉES :
 * - realloc() : redimensionne le tableau de pointeurs.
 *
 * Return: 0 en cas de succès, -1 en cas d'échec mémoire.
 */
static int push(List *list, char *value)
{
    if (list->n == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        char **values = realloc(list->v, cap * sizeof *values);
        if (!values) return -1;
        list->v = values;
        list->cap = cap;
    }
    list->v[list->n++] = value;
    return 0;
}

/**
 * free_list - Libère tous les éléments d'une List ainsi que son tableau de pointeurs.
 * @list: Pointeur vers la liste à vider.
 */
static void free_list(List *list)
{
    if (!list) return;
    for (size_t i = 0; i < list->n; i++) {
        free(list->v[i]);
    }
    free(list->v);
    list->v = NULL;
    list->n = list->cap = 0;
}

/**
 * append_char - Ajoute un caractère au tampon Text avec terminaison nulle automatique.
 * @text: Pointeur vers la structure Text.
 * @c: Caractère à ajouter.
 *
 * Return: 0 en cas de succès, -1 si realloc échoue.
 */
static int append_char(Text *text, char c)
{
    if (text->n + 2 > text->cap) {
        size_t cap = text->cap ? text->cap * 2 : 32;
        char *value = realloc(text->v, cap);
        if (!value) return -1;
        text->v = value;
        text->cap = cap;
    }
    text->v[text->n++] = c;
    text->v[text->n] = '\0';
    return 0;
}

/**
 * append_string - Concatène une chaîne complète dans un tampon Text.
 * @text: Pointeur vers la structure Text.
 * @value: Chaîne à ajouter caractère par caractère.
 *
 * Return: 0 en cas de succès, -1 en cas d'erreur mémoire.
 */
static int append_string(Text *text, const char *value)
{
    while (*value) {
        if (append_char(text, *value++)) return -1;
    }
    return 0;
}

/**
 * is_operator_start - Détecte si le caractère à l'indice i marque le début d'un opérateur.
 * @line: Ligne source.
 * @i: Indice du caractère analysé.
 *
 * Return: 1 si un opérateur commence à cet indice, 0 sinon.
 */
static int is_operator_start(const char *line, size_t i)
{
    return strchr("|;<>", line[i]) != NULL ||
           (line[i] == '&' && line[i + 1] != '/') ||
           (line[i] == '2' && line[i + 1] == '>');
}

/**
 * operator_length - Détermine la longueur en caractères d'un opérateur reconnu.
 * @line: Ligne source.
 * @i: Indice de début de l'opérateur.
 *
 * Reconnaît : '2>', '&&', '||', '>>' (2 caractères) ou '|', ';', '<', '>', '&' (1 caractère).
 *
 * Return: 2 ou 1.
 */
static size_t operator_length(const char *line, size_t i)
{
    if ((line[i] == '2' && line[i + 1] == '>') ||
        (line[i] == '&' && line[i + 1] == '&') ||
        (line[i] == '|' && line[i + 1] == '|') ||
        (line[i] == '>' && line[i + 1] == '>'))
        return 2;
    return 1;
}

/**
 * append_expansion - Résout une variable commençant par '$' et injecte son expansion.
 * @sh: Pointeur vers l'état du shell.
 * @word: Tampon de mot en cours de construction.
 * @line: Ligne d'origine.
 * @index: Pointeur vers l'indice courant (avancé au-delà de la variable).
 *
 * Return: 0 en cas de succès, -1 en cas d'erreur.
 */
static int append_expansion(Shell *sh, Text *word, const char *line, size_t *index)
{
    size_t start = *index;
    size_t end = start + 1;

    if (strchr("?$!0", line[end])) {
        end++;
    } else {
        while (isalnum((unsigned char)line[end]) || line[end] == '_') end++;
        if (end == start + 1) return append_char(word, line[(*index)++]);
    }

    char *source = strndup(line + start, end - start);
    if (!source) return -1;
    char *expanded = expand_text(sh, source, 0);
    free(source);
    if (!expanded) return -1;
    int result = append_string(word, expanded);
    free(expanded);
    *index = end;
    return result;
}

/**
 * lex - Découpe la ligne source en jetons (tokens), en respectant les quotes et échappements.
 * @line: Ligne de commande saisie par l'utilisateur.
 * @sh: Pointeur vers l'état du shell pour les expansions.
 * @tokens: Liste où seront empilés les tokens générés.
 * @error: Pointeur vers la chaîne d'erreur si la tokenisation échoue.
 *
 * RÈGLES DE QUOTING :
 * - Simple quote (') : préserve littéralement tous les caractères sans aucune expansion.
 * - Double quote (") : préserve les espaces tout en autorisant l'expansion des $variables.
 * - Antislash (\)    : échappe le caractère suivant hors de guillemets simples.
 *
 * Return: 0 en cas de succès, -1 si quote non fermée ou erreur mémoire.
 */
static int lex(const char *line, Shell *sh, List *tokens, char **error)
{
    for (size_t i = 0; line[i];) {
        /* Saut des espaces blancs initiaux */
        while (isspace((unsigned char)line[i])) i++;
        if (!line[i]) break;

        /* Extraction des opérateurs */
        if (is_operator_start(line, i)) {
            size_t length = operator_length(line, i);
            char *op = strndup(line + i, length);
            if (!op || push(tokens, op)) {
                free(op);
                return set_error(error, "out of memory");
            }
            i += length;
            continue;
        }

        /* Extraction d'un mot ou argument */
        Text word = {0};
        char quote = '\0';
        while (line[i]) {
            char c = line[i];
            if (!quote && (isspace((unsigned char)c) || is_operator_start(line, i)))
                break;

            if (c == '\'' && quote != '"') {
                quote = (quote == '\'') ? '\0' : '\'';
                i++;
            } else if (c == '"' && quote != '\'') {
                quote = (quote == '"') ? '\0' : '"';
                i++;
            } else if (c == '\\' && quote != '\'' && line[i + 1]) {
                if (append_char(&word, line[i + 1])) goto memory_error;
                i += 2;
            } else if (c == '$' && quote != '\'') {
                if (append_expansion(sh, &word, line, &i)) goto memory_error;
            } else if ((c == '~' || c == '&') && !quote && word.n == 0 &&
                       (line[i + 1] == '/' || line[i + 1] == '\0')) {
                char source[2] = {c, '\0'};
                char *expanded = expand_text(sh, source, 0);
                if (!expanded || append_string(&word, expanded)) {
                    free(expanded);
                    goto memory_error;
                }
                free(expanded);
                i++;
            } else {
                if (append_char(&word, c)) goto memory_error;
                i++;
            }
        }

        if (quote) {
            free(word.v);
            return set_error(error, "unclosed quote");
        }
        if (!word.v && !(word.v = strdup(""))) return set_error(error, "out of memory");
        if (push(tokens, word.v)) {
            free(word.v);
            return set_error(error, "out of memory");
        }
        continue;

memory_error:
        free(word.v);
        return set_error(error, "out of memory");
    }
    return 0;
}

/**
 * free_command - Libère les mots et redirections alloués pour une Command.
 * @command: Pointeur vers la commande à vider.
 */
static void free_command(Command *command)
{
    if (!command) return;
    for (size_t i = 0; i < command->words.n; i++) {
        free(command->words.v[i]);
    }
    free(command->words.v);
    for (size_t i = 0; i < command->nr; i++) {
        free(command->redir[i]);
        free(command->target[i]);
    }
    free(command->redir);
    free(command->target);
    memset(command, 0, sizeof *command);
}

/**
 * free_pipeline - Libère les commandes chaînées dans un Pipeline.
 * @pipeline: Pointeur vers le pipeline à vider.
 */
static void free_pipeline(Pipeline *pipeline)
{
    if (!pipeline) return;
    for (size_t i = 0; i < pipeline->n; i++) {
        free_command(&pipeline->cmd[i]);
    }
    free(pipeline->cmd);
    memset(pipeline, 0, sizeof *pipeline);
}

/**
 * free_program - Libère l'intégralité d'un Program (pipelines et opérateurs).
 * @program: Pointeur vers le programme complet.
 */
void free_program(Program *program)
{
    if (!program) return;
    for (size_t i = 0; i < program->n; i++) {
        free_pipeline(&program->pipe[i]);
    }
    free(program->pipe);
    for (size_t i = 0; i + 1 < program->n; i++) {
        free(program->op[i]);
    }
    free(program->op);
    memset(program, 0, sizeof *program);
}

/**
 * add_word - Insère un argument dans le tableau de mots d'une Command.
 * @command: Commande cible.
 * @word: Chaîne du mot à ajouter.
 *
 * Maintient le tableau words.v terminé par un pointeur NULL pour execvp().
 */
static int add_word(Command *command, const char *word)
{
    char *copy = strdup(word);
    if (!copy) return -1;
    char **words = realloc(command->words.v, (command->words.n + 2) * sizeof *words);
    if (!words) {
        free(copy);
        return -1;
    }
    command->words.v = words;
    command->words.v[command->words.n++] = copy;
    command->words.v[command->words.n] = NULL;
    return 0;
}

/**
 * add_redirection - Enregistre une paire (opérateur, cible) dans une commande.
 * @command: Commande recevant la redirection.
 * @operator: Opérateur de redirection ('<', '>', '>>', '2>').
 * @target: Fichier cible de la redirection.
 */
static int add_redirection(Command *command, const char *operator, const char *target)
{
    char *operator_copy = strdup(operator);
    char *target_copy = strdup(target);
    char **operators = malloc((command->nr + 1) * sizeof *operators);
    char **targets = malloc((command->nr + 1) * sizeof *targets);

    if (!operator_copy || !target_copy || !operators || !targets) {
        free(operator_copy);
        free(target_copy);
        free(operators);
        free(targets);
        return -1;
    }

    if (command->nr) {
        memcpy(operators, command->redir, command->nr * sizeof *operators);
        memcpy(targets, command->target, command->nr * sizeof *targets);
    }

    operators[command->nr] = operator_copy;
    targets[command->nr] = target_copy;
    free(command->redir);
    free(command->target);
    command->redir = operators;
    command->target = targets;
    command->nr++;
    return 0;
}

/**
 * add_command - Ajoute une Command au tableau de commandes d'un Pipeline.
 */
static int add_command(Pipeline *pipeline, Command *command)
{
    Command *commands = realloc(pipeline->cmd, (pipeline->n + 1) * sizeof *commands);
    if (!commands) return -1;
    pipeline->cmd = commands;
    pipeline->cmd[pipeline->n++] = *command;
    memset(command, 0, sizeof *command);
    return 0;
}

/**
 * add_pipeline - Ajoute un Pipeline au tableau de pipelines d'un Program.
 */
static int add_pipeline(Program *program, Pipeline *pipeline)
{
    Pipeline *pipelines = realloc(program->pipe, (program->n + 1) * sizeof *pipelines);
    if (!pipelines) return -1;
    program->pipe = pipelines;
    program->pipe[program->n++] = *pipeline;
    memset(pipeline, 0, sizeof *pipeline);
    return 0;
}

/**
 * is_separator - Vérifie si un token est un séparateur de pipeline ('&&', '||', ';').
 */
static int is_separator(const char *token)
{
    return !strcmp(token, "&&") || !strcmp(token, "||") || !strcmp(token, ";");
}

/**
 * is_redirection - Vérifie si un token est un opérateur de redirection d'E/S.
 */
static int is_redirection(const char *token)
{
    return !strcmp(token, ">") || !strcmp(token, ">>") ||
           !strcmp(token, "<") || !strcmp(token, "2>");
}

/**
 * parse_line - Parse une ligne complète et construit la structure Program correspondante.
 * @line: Ligne brute tapée dans le shell.
 * @sh: Pointeur vers l'état du shell.
 * @program: Structure de sortie Program initialisée à zéro.
 * @error: Pointeur vers un message d'erreur en cas d'échec d'analyse.
 *
 * Return: 0 si la ligne est syntaxiquement correcte, -1 en cas d'erreur.
 */
int parse_line(const char *line, Shell *sh, Program *program, char **error)
{
    List tokens = {0};
    size_t i = 0;
    memset(program, 0, sizeof *program);
    *error = NULL;

    if (lex(line, sh, &tokens, error)) goto fail;

    while (i < tokens.n) {
        Pipeline pipeline = {0};

        for (;;) {
            Command command = {0};

            while (i < tokens.n && strcmp(tokens.v[i], "|") &&
                   strcmp(tokens.v[i], "&") && !is_separator(tokens.v[i])) {
                if (is_redirection(tokens.v[i])) {
                    const char *op = tokens.v[i++];
                    if (i == tokens.n || is_operator_start(tokens.v[i], 0)) {
                        free_command(&command);
                        set_error(error, "redirection needs a target");
                        goto pipeline_fail;
                    }
                    if (add_redirection(&command, op, tokens.v[i++])) {
                        free_command(&command);
                        set_error(error, "out of memory");
                        goto pipeline_fail;
                    }
                } else if (add_word(&command, tokens.v[i++])) {
                    free_command(&command);
                    set_error(error, "out of memory");
                    goto pipeline_fail;
                }
            }

            if (!command.words.n) {
                free_command(&command);
                set_error(error, "expected command");
                goto pipeline_fail;
            }

            /* Convention Yazid Shell : 'cd &' et 'ls &' ciblent le dossier HOME */
            if (i < tokens.n && !strcmp(tokens.v[i], "&") &&
                command.words.n == 1 &&
                (!strcmp(command.words.v[0], "cd") || !strcmp(command.words.v[0], "ls"))) {
                if (add_word(&command, "&")) {
                    free_command(&command);
                    set_error(error, "out of memory");
                    goto pipeline_fail;
                }
                i++;
            }

            if (add_command(&pipeline, &command)) {
                free_command(&command);
                set_error(error, "out of memory");
                goto pipeline_fail;
            }

            if (i < tokens.n && !strcmp(tokens.v[i], "|")) {
                i++;
                continue;
            }
            break;
        }

        int background = 0;
        if (i < tokens.n && !strcmp(tokens.v[i], "&")) {
            pipeline.background = 1;
            background = 1;
            i++;
        }

        if (add_pipeline(program, &pipeline)) {
            set_error(error, "out of memory");
            goto pipeline_fail;
        }

        if (i == tokens.n) break;

        if (!background && !is_separator(tokens.v[i])) {
            set_error(error, "expected command separator");
            goto fail;
        }

        char *op = strdup(is_separator(tokens.v[i]) ? tokens.v[i++] : ";");
        if (!op) {
            set_error(error, "out of memory");
            goto fail;
        }

        char **operators = realloc(program->op, program->n * sizeof *operators);
        if (!operators) {
            free(op);
            set_error(error, "out of memory");
            goto fail;
        }
        program->op = operators;
        program->op[program->n - 1] = op;

        if (i == tokens.n) {
            /* Libération de l'opérateur orphelin pour éviter une fuite mémoire */
            free(program->op[program->n - 1]);
            program->op[program->n - 1] = NULL;
            set_error(error, "operator needs a command");
            goto fail;
        }
        continue;

pipeline_fail:
        free_pipeline(&pipeline);
        goto fail;
    }

    free_list(&tokens);
    return 0;

fail:
    free_list(&tokens);
    free_program(program);
    if (!*error) set_error(error, "out of memory");
    return -1;
}
