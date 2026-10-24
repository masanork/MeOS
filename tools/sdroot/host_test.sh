#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/meos-sdroot-guards.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -DM5OS_BOARD_TAB5=1 -Ikernel/include tools/sdroot/guards_test.c user/shell/sd.c -o "$OUT/guards"
"$OUT/guards"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -DM5OS_BOARD_TAB5=1 -Ikernel/include tools/sdroot/cache_test.c kernel/app/wasm_cache.c -o "$OUT/cache"
"$OUT/cache"
