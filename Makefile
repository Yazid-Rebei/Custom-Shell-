CC ?= gcc
# Options C11/POSIX strictes; tous les modules src sont liés au même exécutable.
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
SOURCES := $(wildcard src/*.c)
TARGET := yazid_shell
TARGET_SSH := yazid_ssh

.PHONY: all debug clean test
# Construit les deux noms de binaire utilisés par le projet.
all: $(TARGET) $(TARGET_SSH)

$(TARGET): $(SOURCES) include/shell.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@

$(TARGET_SSH): $(TARGET)
	cp -f $(TARGET) $@

debug: CFLAGS += -g -O0 -fsanitize=address,undefined
# Reconstruit avec les outils de diagnostic mémoire et comportement indéfini.
debug: clean all
# Supprime les exécutables produits par le Makefile.

clean:
	rm -f $(TARGET) $(TARGET_SSH) tests/test_parser tests/test_guess

test: $(TARGET)
# Lance les tests unitaires puis le test d'intégration du shell.
	$(CC) $(CFLAGS) tests/test_parser.c src/parser.c src/env.c src/utils.c -o tests/test_parser
	./tests/test_parser
	$(CC) $(CFLAGS) tests/test_guess.c $(filter-out src/main.c, $(SOURCES)) -o tests/test_guess
	./tests/test_guess
	./tests/smoke.sh

