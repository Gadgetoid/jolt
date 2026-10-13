#!/usr/bin/env bash
set -euo pipefail

directory=$(cd "$(dirname "$0")" && pwd)
rm -rf "$directory/../build/apps/bin"
for recipe in "$directory"/libraries/*/build.sh "$directory"/*/build.sh; do
    bash "$recipe"
done
