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
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tools/cow/store_test.c fs/cow/cow.c fs/cow/store.c -o "$OUT/store_test"
"$OUT/store_test"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -pthread \
    -Ikernel/include tools/cow/atomic_test.c fs/cow/cow.c fs/cow/store.c \
    fs/cow/blockdev.c fs/m5fs/m5fs.c fs/m5fs/m5fs_test.c \
    drivers/ramdisk/ramdisk.c kernel/lib/crc32.c -o "$OUT/atomic_test"
"$OUT/atomic_test"
