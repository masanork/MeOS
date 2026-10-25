#!/bin/sh
if [ -z "${BASH_VERSION:-}" ] && [ -z "${ZSH_VERSION:-}" ]; then
    exec bash "$0" "$@"
fi
set -eu
cd "$(dirname "$0")/../.."
export M5OS_BOARD=tab5 M5OS_QUAKE=1
unset M5OS_DOOM M5OS_DOOM_RELEASE
case "${M5OS_QUAKE_RESOLUTION:-320x200}" in
  320x200) QUAKE_BUILD=build-quake; QUAKE_CONFIG=sdkconfig.quake ;;
  512x300) QUAKE_BUILD=build-quake-512x300; QUAKE_CONFIG=sdkconfig.quake-512x300
    if [ ! -f "$QUAKE_CONFIG" ] && [ -f sdkconfig.quake ]; then cp sdkconfig.quake "$QUAKE_CONFIG"; fi ;;
  *) echo "unsupported M5OS_QUAKE_RESOLUTION" >&2; exit 2 ;;
esac
case "${M5OS_QUAKE_DETAIL_PROFILE:-0}" in
  0) ;;
  1) PROFILE_BASE_CONFIG=$QUAKE_CONFIG
     QUAKE_BUILD=$QUAKE_BUILD-profile; QUAKE_CONFIG=$QUAKE_CONFIG-profile
     if [ ! -f "$QUAKE_CONFIG" ] && [ -f "$PROFILE_BASE_CONFIG" ]; then cp "$PROFILE_BASE_CONFIG" "$QUAKE_CONFIG"; fi ;;
  *) echo "unsupported M5OS_QUAKE_DETAIL_PROFILE" >&2; exit 2 ;;
esac
. tools/env.sh
[ $# -gt 0 ] || set -- build
exec idf.py -B "$QUAKE_BUILD" -DSDKCONFIG="$QUAKE_CONFIG" -DIDF_TARGET=esp32p4 "$@"
