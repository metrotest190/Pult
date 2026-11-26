#!/usr/bin/env bash
# Нативные тесты ядра NeModbus: ne_protocol.c не зависит от HAL.
#
# Компилятор: $CC, иначе gcc/cc/clang, иначе zig (python -m pip install ziglang).
#   ./tests/run_tests.sh
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-}

if [ -z "$CC" ]; then
  for candidate in gcc cc clang; do
    if command -v "$candidate" >/dev/null 2>&1; then
      CC=$candidate
      break
    fi
  done
fi

if [ -z "$CC" ] && python -m ziglang version >/dev/null 2>&1; then
  CC="python -m ziglang cc"
fi

if [ -z "$CC" ]; then
  echo "нет C-компилятора: поставьте gcc/clang или 'python -m pip install ziglang'" >&2
  exit 2
fi

BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

echo "CC: $CC"
# shellcheck disable=SC2086
$CC -std=c99 -Wall -Wextra -O2 -I "$ROOT/Modules/ne_protocol" \
  "$ROOT/tests/test_ne_protocol.c" \
  "$ROOT/Modules/ne_protocol/ne_protocol.c" \
  -o "$BUILD/test_ne_protocol" || exit 2

"$BUILD/test_ne_protocol"
