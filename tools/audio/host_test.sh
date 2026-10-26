#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/meos-audio.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
for ALIGN in 64 128; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -DAUDIO_ALIGN="$ALIGN" -Ikernel/include \
        tools/audio/pcm_pool_test.c kernel/memory/heap.c -o "$OUT/pool-$ALIGN"
    "$OUT/pool-$ALIGN"
done
