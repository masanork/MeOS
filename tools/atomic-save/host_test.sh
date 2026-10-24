#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/meos-atomic-save.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DM5OS_BOARD_TAB5=1 -Ikernel/include tools/atomic-save/vfs_test.c \
    fs/vfs/vfs.c fs/vfs/write_file.c -o "$OUT/vfs"
"$OUT/vfs"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined,float-cast-overflow \
    -DM5OS_BOARD_TAB5=1 -Ikernel/include tools/atomic-save/basic_test.c \
    user/basic/file.c user/basic/basic.c third_party/ubasic/ubasic.c third_party/ubasic/tokenizer.c \
    kernel/lib/number.c kernel/lib/number_math.c fs/m5fs/m5fs.c fs/cow/cow.c fs/cow/store.c \
    fs/cow/blockdev.c fs/vfs/vfs.c fs/vfs/m5fs_vfs.c fs/vfs/write_file.c kernel/lib/crc32.c \
    -lm -o "$OUT/basic"
"$OUT/basic"
