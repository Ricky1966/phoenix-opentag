#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bin=$(mktemp /tmp/phoenix-tests.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/test_parser.cpp src/tag_parser.cpp src/delivery.cpp -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"
"${CXX:-g++}" -std=c++11 -g -fsanitize=address,undefined \
  -Itests/stubs -Ilib/PN5180-Library tests/test_iso15693.cpp \
  lib/PN5180-Library/PN5180ISO15693.cpp -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/test_official_vectors.cpp src/tag_parser.cpp -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin" tests/fixtures/official

"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc tests/test_writer.cpp src/tag_writer.cpp src/tag_parser.cpp -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc -Itests/stubs tests/test_writer_protocol.cpp src/tag_writer.cpp src/tag_parser.cpp -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"
