#!/bin/sh
# Compila e prova il parser OpenPrintTag sul PC (serve solo g++).
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o /tmp/test_opt_parse test_opt_parse.cpp ../../src/opt_parse.cpp
/tmp/test_opt_parse vectors
