#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for SUITE in foundations hardware touch stack peripheral microphone psram lcd ime sd player cow sdroot; do
    echo "Host suite: $SUITE"
    sh "tools/$SUITE/host_test.sh"
done
