# Yazid Shell

Petit shell Unix pédagogique écrit en C11/POSIX. Le binaire s’appelle `yazid_shell`.

Pour réviser le code fichier par fichier, consulte le [guide de révision](GUIDE_REVISION.md).

## Compiler et lancer

```sh
make
./yazid_shell
./yazid_shell --no-anim
make debug
make test
make clean
```

`YAZID_NO_ANIM=1` désactive aussi l’animation `yazid`. Les couleurs du prompt sont
affichées seulement dans un terminal si `NO_COLOR` n’est pas défini.

## Commandes disponibles

```sh
echo "hello world" | tr a-z A-Z
echo hello > output.txt
echo more >> output.txt
cat < output.txt
false && echo success || echo recovered
cd /tmp; pwd
sleep 10 &
echo "$HOME, status=$?"
history
!!
```

Le shell fournit `cd`, `pwd`, `ls` (`-a`), `echo` (`-n`), `export`, `unset`,
`history`, `alias`/`unalias`, `type`, `which`, `jobs`, `guide` (`help`),
`sname`, `yander`, `clear` et `exit`. `yander` affiche la version 1.1.0, la date
de mise à jour du 26 septembre 2026, la notice Copyright en vert (\033[1;32m),
et les auteurs Yazid et Skander en deux nuances de violet distinctes avec animation.
Le démarrage interactif affiche une bannière et un prompt colorés; `ls` distingue les fichiers et dossiers. `--no-anim` ou
`YAZID_NO_ANIM=1` désactive les animations.
Les commandes externes sont lancées avec `fork` et `execvp`.

## Organisation

- `src/main.c`, `src/loop.c`: démarrage, configuration et boucle interactive.
- `src/parser.c`: tokenizer, expansion et construction des pipelines/opérateurs.
- `src/executor.c`: exécution des processus, pipes et redirections.
- `src/builtins.c`: commandes internes.
- `src/jobs.c`, `src/signals.c`: suivi des processus d’arrière-plan et signaux.
- `src/history.c`, `src/env.c`, `src/utils.c`: historique, expansion et état du shell.
- `include/shell.h`: interfaces et structures partagées.

## Limites connues

Cette version est un mini-shell, pas un shell POSIX complet. Le suivi des jobs
est sommaire : `jobs` affiche les processus suivis, mais `fg` et `bg` indiquent
qu’ils ne sont pas disponibles (pas de groupes de processus/terminal control).
`~` et `$VAR`, `$?`, `$$`, `$!`, `$0` sont développés; le splitting de mots,
les jokers, les substitutions, les here-documents, les descripteurs arbitraires,
et les échappements complexes dans les doubles guillemets ne sont pas implémentés.
Les alias sont listés et stockés en mémoire, mais ne sont pas substitués dans
les commandes. L’historique est persisté dans `~/.yazid_history`; `!!` et `!n`
sont pris en charge. Les lignes de `~/.yazidrc` sont exécutées au démarrage;
les alias n’étant pas substitués, ce fichier sert surtout à `export`.

Le test `make test` vérifie rapidement un pipe, une redirection, des guillemets
et le chaînage `&&`/`||`.
