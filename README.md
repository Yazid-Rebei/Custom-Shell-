

# README.md

## Yazid Shell

Mini shell Unix écrit en C.
Implémente des commandes internes (builtins), l’exécution de commandes externes via `fork()` / `execvp()`, et une gestion basique du `PATH`.

---

## Fonctionnalités

### Builtins implémentés

* `cd` — Changement de répertoire
* `help` — Menu d’aide
* `type` — Indique si la commande est builtin ou externe
* `sname` — Affiche le nom du shell

### Commandes externes

Les commandes externes sont recherchées dans la variable d’environnement `PATH` et exécutées via :

* `fork()`
* `execvp()`
* `wait()`

---

## Compilation

```bash
gcc -Wall -Wextra -o yazid_shell shell.c
```

---

## Exécution

```bash
./yazid_shell
```

---

## Architecture

Le programme est structuré autour des composants suivants :

### 1. Boucle principale

`line_loop()`

* Affiche le prompt
* Lit la commande utilisateur
* Tokenize la commande
* Vérifie si builtin
* Exécute builtin ou commande externe

---

### 2. Parsing

`split_command()`
Utilise `strtok()` avec le délimiteur `" "`.

---

### 3. Builtins

Gestion via :

```c
check_command()
```

Dispatch des fonctions via tableau de pointeurs :

```c
void (*builin_function[])(char**)
```

---

### 4. Exécution externe

`execute_command()`

* fork
* execvp
* wait

---

### 5. Recherche PATH

`is_external_command()`

* Parse `PATH`
* Vérifie chaque dossier avec `access(..., X_OK)`

---

## Limitations actuelles

* Pas de gestion des redirections (`>`, `<`)
* Pas de pipes (`|`)
* Pas de gestion des guillemets
* Utilisation de `strtok()` (non thread-safe)
* Fuites mémoire potentielles
* Gestion fragile du `cd` et du prompt
* Signal handler non sécurisé
* Hardcoding du path `/home/yazid`

---

## Améliorations possibles

* Implémenter redirections
* Implémenter pipes
* Support des guillemets
* Gestion propre du `PWD`
* Refactorisation modulaire
* Meilleure gestion mémoire
* Gestion propre des signaux
* Support historique

---

# 🔎 Avis technique sur ton code

Ton projet est **correct pour un mini-shell pédagogique**, mais il mélange :

* parsing
* logique métier
* gestion du prompt
* gestion mémoire
* logique cd
* signal handling

dans un seul fichier.

Pour un projet propre (et niveau universitaire ou GitHub professionnel), il faut le diviser.

---


## Proposition d’architecture modulaire

```
yazid-shell/
│
├── src/
│   ├── main.c
│   ├── loop.c
│   ├── parser.c
│   ├── builtins.c
│   ├── executor.c
│   ├── signals.c
│   └── utils.c
│
├── include/
│   ├── shell.h
│   ├── parser.h
│   ├── builtins.h
│   └── executor.h
│
├── Makefile
└── README.md
```

---

## Séparation logique recommandée

### main.c

Initialisation du shell

### loop.c

Boucle principale

### parser.c

Tokenisation
Gestion future des pipes/redirections

### builtins.c

cd
help
type
sname

### executor.c

fork
exec
wait
PATH resolution

### signals.c

Gestion SIGINT propre



Author : Mohamed Yazid Rebei

