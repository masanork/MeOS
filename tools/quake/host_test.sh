#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/meos-quake.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DM5OS_BOARD_TAB5=1 -DM5OS_QUAKE=1 -Ikernel/include \
    tools/quake/host_test.c apps/quake/libc.c kernel/lib/number.c -lm -o "$OUT/test"
"$OUT/test"
