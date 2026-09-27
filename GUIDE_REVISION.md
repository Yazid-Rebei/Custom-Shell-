# Guide de révision — Yazid Shell

Ce guide suit le trajet d’une commande depuis la saisie jusqu’à son exécution.
Lis les fichiers dans cet ordre et garde le terminal ouvert pour essayer chaque
exemple. Le projet est compilé par le `Makefile` depuis les fichiers de `src/`;
le point d’entrée actif est donc `src/main.c`.

## 1. Vue d’ensemble : le trajet d’une commande

Quand le shell démarre, `main()` prépare une structure `Shell`, configure les
signaux, charge l’historique et lit `~/.yazidrc`. Ensuite `shell_loop()` affiche
le prompt et attend une ligne avec `getline()`. Elle traite spécialement la
commande `yazid`, puis transmet les autres lignes à `parse_line()`. Le parser
produit une structure `Program` qui décrit les commandes, les pipes, les
redirections et les opérateurs. Enfin `execute_program()` décide quelles
commandes lancer. Un built-in simple s’exécute dans le processus du shell; une
commande externe est lancée dans un enfant avec `fork()` et `execvp()`. À la
fin de la boucle, l’historique est sauvegardé et la mémoire de `Shell` est
libérée.

Exemple à suivre dans le code : `echo bonjour | tr a-z A-Z`. Le parser crée un
pipeline de deux commandes. L’exécuteur relie la sortie standard du premier
processus à l’entrée standard du second avec `pipe()` et `dup2()`. `execvp()`
lance ensuite `echo` et `tr`.

## 2. Les données communes : `include/shell.h`

Ce fichier définit les structures que plusieurs modules doivent comprendre.
`Words` est une liste de mots, par exemple le nom de la commande et ses
arguments. `Command` contient ces mots et ses redirections. `Pipeline` est une
ou plusieurs commandes reliées par `|`, avec un indicateur pour l’arrière-plan.
`Program` contient plusieurs pipelines liés par `&&`, `||` ou `;`.

La structure `Shell` conserve l’état pendant la session : le dernier code de
retour, la demande de quitter, les options d’animation et de couleur, les
entrées d’historique et les jobs connus. Les déclarations à la fin du fichier
sont les interfaces utilisées entre modules. Pour réviser, relie par exemple
`parse_line()` à son implémentation dans `src/parser.c`, puis à son appel dans
`src/loop.c`.

## 3. Démarrage : `src/main.c`

`main()` lit l’option `--no-anim`; les autres arguments provoquent l’affichage
de l’usage et un code d’erreur. Il initialise ensuite `Shell`, détermine si
l’entrée est un terminal, installe les gestionnaires de signaux, charge
l’historique, lit le fichier de configuration, puis lance `shell_loop()`.
Quand la boucle revient, `shell_destroy()` libère l’état conservé et `main()`
renvoie le code de sortie.

`config_load()` construit le chemin `~/.yazidrc`, lit le fichier ligne par
ligne, ignore les lignes vides et celles qui commencent par `#`, puis analyse
et exécute chaque autre ligne. C’est une configuration rudimentaire : les
lignes sont traitées comme des commandes du shell; il n’existe pas encore de
syntaxe spéciale pour configurer le prompt.

## 4. La boucle interactive : `src/loop.c`

`prompt_print()` affiche `utilisateur@machine:répertoire$`. Elle ne dessine
rien quand l’entrée n’est pas interactive et n’utilise les couleurs que si la
sortie est un terminal et si `NO_COLOR` n’est pas défini.

`expand_history()` remplace une ligne exactement égale à `!!` par la dernière
entrée, ou `!n` par l’entrée numérotée `n`. `shell_loop()` est le REPL (Read,
Evaluate, Print, Loop) : elle actualise les jobs terminés, affiche le prompt,
lit une ligne avec `getline()`, enregistre la commande, puis appelle le parser
et l’exécuteur. Si la ligne est `yazid`, elle affiche « Welcome to Yazid SSH »
avec une animation, sauf si `--no-anim` ou `YAZID_NO_ANIM` la désactive. La
boucle tolère aussi une lecture interrompue par un signal et sauvegarde
l’historique avant de se terminer.

## 5. Analyse : `src/parser.c`

Le parser transforme une ligne de texte en données structurées sans modifier
la ligne originale. La petite structure `List` sert d’abord à accumuler les
jetons temporaires. `lex()` parcourt les caractères, saute les espaces,
regroupe le contenu des guillemets et reconnaît les opérateurs `|`, `||`, `&`,
`&&`, `;`, `<`, `>`, `>>` et `2>`. Une quote qui n’est pas refermée déclenche
une erreur. Les mots sont envoyés à `expand_text()` pour l’expansion prise en
charge.

`parse_line()` assemble ensuite ces jetons. Il crée une `Command` à chaque
commande, ajoute les cibles de redirection, regroupe les commandes d’un même
pipeline et relie les pipelines par leurs opérateurs conditionnels. `addword()`
ajoute un argument avec un pointeur final `NULL`, comme l’attend `execvp()`;
`addredir()` ajoute une paire opérateur/cible. `free_program()` libère toute
l’arborescence créée par le parser. En cas d’erreur, `parse_line()` fournit un
message et abandonne la structure partielle.

À réviser : compare la chaîne `echo 'deux mots' | cat > resultat` à la structure
`Program` dans le test parser. Les deux mots cités forment un seul argument;
`|` sépare deux commandes, tandis que `>` associe une cible à la commande
`cat`.

## 6. Expansion de variables : `src/env.c`

`expand_text()` construit une nouvelle chaîne en parcourant l’ancienne. Elle
remplace `~` au début d’un mot par `HOME`, et développe `$VAR`, `$?`, `$$`,
`$!` et `$0`. `$?` vient de l’état `Shell`, `$$` est l’identifiant du shell,
et `$!` correspond au dernier processus d’arrière-plan mémorisé. La fonction
renvoie une chaîne allouée : son appelant doit donc en prendre la responsabilité
et la libérer. L’argument `quoted` existe dans l’interface, mais n’est pas
encore utilisé; l’expansion entre guillemets doubles n’a pas toutes les règles
d’un shell POSIX.

## 7. Exécution : `src/executor.c`

`execute_program()` parcourt les pipelines et applique les règles de
`&&` et `||` à partir du dernier statut. `;` laisse simplement continuer vers
la commande suivante. Il conserve le statut final dans `Shell`.

`runpipe()` exécute un built-in isolé dans le processus parent afin qu’un `cd`
modifie réellement le répertoire du shell. Il sauvegarde alors les descripteurs
standard, applique les redirections, exécute le built-in et restaure les
descripteurs. Dans les autres cas, elle crée les pipes, lance un processus par
commande et relie les entrées/sorties avec `dup2()`. Les enfants rétablissent
les signaux par défaut avant `execvp()`. `redirect()` ouvre chaque fichier et
redirige l’entrée, la sortie ou l’erreur standard : `>` tronque, `>>` ajoute,
`<` lit un fichier et `2>` envoie les erreurs vers un fichier.

Pour une commande avec `&`, `runpipe()` ne fait pas d’attente bloquante : elle
enregistre le PID et le texte de la commande. Les pipelines en arrière-plan
sont suivis de façon simplifiée.

## 8. Commandes internes : `src/builtins.c`

`is_builtin()` répond si un nom appartient à la liste de commandes internes.
L’exécuteur utilise cette réponse pour choisir entre `builtin_run()` et
`execvp()`. `builtin_run()` implémente les opérations : `cd` change le
répertoire et met à jour `PWD`/`OLDPWD`; `pwd` affiche le répertoire; `echo`
affiche ses arguments; `export` et `unset` modifient l’environnement du
processus shell; `history` affiche les lignes mémorisées; `type` et `which`
cherchent les commandes; et `exit` demande à la boucle de terminer.

`help`, `guide`, `sname`, `yander` et `clear` fournissent les commandes d’information et
d’affichage. `yander` affiche avec animation la version (1.1.0), la date de mise à jour (26 septembre 2026),
la mention Copyright en vert, et les auteurs Yazid et Skander avec deux nuances distinctes de violet.
`alias` et `unalias` mémorisent ou retirent des chaînes, mais la
substitution d’alias avant l’analyse n’est pas encore codée. `jobs` affiche les
processus connus. `fg` et `bg` sont reconnus, mais indiquent explicitement que
le contrôle complet des jobs n’est pas implémenté.

## 9. Historique : `src/history.c`

`histfile()` prépare le chemin `~/.yazid_history`. `history_load()` charge ses
lignes au démarrage dans le tableau `Shell.history`. `history_save()` réécrit
ce tableau à la fin de la session. La mémoire appartient à `Shell` et est
libérée par `shell_destroy()` dans `src/utils.c`. La répétition `!!`/`!n` est
gérée séparément dans `src/loop.c`.

## 10. Jobs : `src/jobs.c`

`jobs_reap()` vérifie chaque job marqué comme actif avec `waitpid(...,
WNOHANG)`. `WNOHANG` signifie que le shell n’attend pas si le processus tourne
encore. Quand `waitpid()` renvoie le PID, l’entrée est marquée terminée. Ce
module ne met pas en place les groupes de processus ni la reprise au premier
plan; c’est pourquoi `fg` et `bg` restent indisponibles.

## 11. Signaux : `src/signals.c`

`signals_init()` installe un gestionnaire simple pour `SIGINT` (Ctrl+C) et
`SIGTSTP` (Ctrl+Z) dans le parent. Il ne fait rien, afin que le shell ne soit
pas interrompu comme une commande ordinaire. L’enfant rétablit les dispositions
par défaut dans `src/executor.c` avant le lancement du programme. Ainsi Ctrl+C
peut interrompre le programme enfant. Le transfert avancé du terminal vers un
groupe de jobs est une amélioration future.

## 12. Mémoire et état : `src/utils.c`

`shell_init()` remet l’état à zéro et lit les options issues de
`YAZID_NO_ANIM` et `NO_COLOR`. `shell_destroy()` libère chaque chaîne
d’historique, chaque alias, les textes associés aux jobs et le champ `prompt`.
Chaque allocation doit avoir une libération correspondante; lorsqu’une
fonction renvoie une chaîne avec `malloc()` ou `strdup()`, vérifie qui en
devient propriétaire.

## 13. En-têtes des modules : `include/*.h`

`include/shell.h` est l’en-tête partagé qui porte actuellement les structures
et déclarations principales. `parser.h`, `builtins.h`, `executor.h`, `jobs.h`,
`signals.h`, `history.h`, `env.h` et `utils.h` sont des en-têtes de façade :
chacun inclut `shell.h`. Ils donnent les noms de fichiers attendus par
l’architecture, mais ne définissent pas encore des interfaces séparées. Pour
le moment, les fichiers `.c` incluent directement `shell.h`.

## 14. Compilation et tests

Le `Makefile` définit le compilateur, les options C11/POSIX strictes, la liste
des sources et le nom du binaire. `all` compile `yazid_shell`; `debug` ajoute
les options de débogage et les sanitizers; `clean` supprime le binaire et le
test compilé; `test` compile puis exécute les deux tests.

`tests/test_parser.c` est un test unitaire : il appelle `parse_line()` et
vérifie les mots cités, le nombre de commandes et les redirections/opérateurs.
`tests/smoke.sh` est un test d’intégration : il lance le shell avec plusieurs
lignes et vérifie le résultat du pipe et du chaînage conditionnel, ainsi que
la lecture/écriture d’un fichier temporaire.

Commandes de révision :

```sh
cd Custom-Shell-
make clean all
make test
printf 'echo "salut monde" | tr a-z A-Z\nexit\n' | ./yazid_shell --no-anim
```

## 15. Fichiers hérités et documents

`yazid_ssh.c` est l’ancien prototype monolithique. Il montre l’évolution du
projet, mais le `Makefile` actuel ne le compile pas. `yazid_ssh` et
`yazid_shell` sont des exécutables déjà présents ou générés : pour comprendre
le comportement, privilégie le code source et reconstruis avec `make`.
`description.txt` résume les premières idées du projet. Le fichier HTML
« Cahier des Charges — Yazid Shell » et `Custom-Shell-.zip` sont des documents
de spécification/sauvegarde, pas des modules nécessaires à la compilation.

## 16. Moteur de devinette et complétion intelligente : `src/guess.c`

Le module `src/guess.c` et son interface `include/guess.h` enrichissent le shell
d'une assistance en temps réel déclenchée par la touche `[TAB]` (ou par la commande `guess`) :

1. **Mode brut (termios)** : `guess_read_line()` bascule le terminal en mode non-canonique
   sans écho automatique afin d'intercepter immédiatement la touche `[TAB]`, les flèches
   d'historique (Haut/Bas), le retour arrière et Ctrl+C/Ctrl+D. Le mode normal est restauré dès validation.
2. **Commande correcte ou contextuelle** (ex: `tar -x`, `cd`, `ls`) : Le shell détecte la
   commande et ses drapeaux, puis affiche une fiche complète : Formule générale, options
   possibles et exemples concrets d'utilisation.
3. **Commande incomplète** (ex: `yand` -> `yander `, `cle` -> `clear`) : Complétion automatique
   du préfixe ou liste des suggestions si plusieurs commandes correspondent.
4. **Correction de fautes de frappe / Similarité** (ex: `mrdir`) : L'algorithme de
   distance de Damerau-Levenshtein analyse les transpositions et substitutions pour
   détecter que `mrdir` correspond à `rmdir` et `mkdir`, et affiche pour chacune la
   règle générale, l'usage et des exemples.
5. **Complétion de fichiers/chemins** : Détection des arguments de fichiers/dossiers
   avec suffixe automatique `/` pour les répertoires.

## 17. Thème Solarized Dark, Police Victor Mono & Animations Épurées

1. **Suppression des emojis** : Remplacement de tous les emojis décoratifs (💡, 📌, ℹ️, ⚙️, 📋, ⚠️, 🔹, 🔍, ✨, [✓], [✗], 📍) par des indicateurs textuels épurés et professionnels (`[ok]`, `[err]`, `[tip]`, `[info]`, `pwd ::`).
2. **Palette Solarized Dark** : Utilisation exclusive d'une palette restreinte, sobre et reposante :
   - Fond sombre Solarized Dark (Base03 `#002b36` / Base02 `#073642`)
   - Cadres et séparateurs en gris discret (`\033[90m` Base01)
   - Commandes et en-têtes en Cyan (`\033[36m`)
   - Chemins et contextes en Bleu (`\033[34m`)
   - Statuts de validation en Vert (`\033[32m`)
   - Erreurs en Rouge (`\033[31m`)
   - Astuces et drapeaux en Ambre (`\033[33m`)
3. **Police Victor Mono avec italique cursive** :
   - Police `Victor Mono` installée avec ligatures de programmation.
   - Les commentaires de code dans l'IDE sont configurés en écriture cursive manuscrite distincte (`"fontStyle": "italic"`), assurant une harmonie totale entre terminal et éditeur.
4. **Micro-animations interactives** :
   - Démarrage : Tracé animé du logo ASCII, diagnostic 5 étapes et jauge de progression.
   - Navigation : Spinner 6 images Braille haute fluidité sur `cd`, scan de fichiers sur `ls` et localisation sur `pwd`.
   - Touche `[TAB]` : Micro-pulsation de résolution instantanée.
   - Fermeture : Animation douce de déconnexion de session lors de `exit`.


