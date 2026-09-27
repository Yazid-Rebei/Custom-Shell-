#!/bin/sh
# Test d'intégration : vérifie pipe, redirections, opérateurs conditionnels et commande yander.
set -eu
out=$(printf 'echo "hello world" | tr a-z A-Z\necho ok > /tmp/yazid-shell-test\ncat /tmp/yazid-shell-test\nfalse && echo bad || echo recovered\nyander\nexit\n' | ./yazid_shell --no-anim)
printf '%s\n' "$out" | grep -q 'HELLO WORLD'
printf '%s\n' "$out" | grep -q 'recovered'
printf '%s\n' "$out" | grep -q 'Yazid et Skander'
printf '%s\n' "$out" | grep -q '1.1.0'
rm -f /tmp/yazid-shell-test
echo 'smoke tests passed'
