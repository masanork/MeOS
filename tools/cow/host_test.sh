#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/meos-cow.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tools/cow/test.c fs/cow/cow.c -o "$OUT/test"
"$OUT/test"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Ikernel/include tools/cow/m5fs_test.c fs/cow/cow.c fs/cow/blockdev.c \
    fs/m5fs/m5fs.c kernel/lib/crc32.c -o "$OUT/m5fs_test"
"$OUT/m5fs_test"
