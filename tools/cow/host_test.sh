#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/meos-cow.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tools/cow/test.c fs/cow/cow.c -o "$OUT/test"
"$OUT/test"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tools/cow/store_test.c fs/cow/cow.c fs/cow/store.c -o "$OUT/store_test"
"$OUT/store_test"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Ikernel/include tools/cow/blockdev_test.c fs/cow/cow.c fs/cow/blockdev.c -o "$OUT/blockdev_test"
"$OUT/blockdev_test"
