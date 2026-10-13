#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

mkdir -p "$output/etc/ssl"
cp "$deps/archives/cacert-2026-09-25.pem" "$output/etc/ssl/cert.pem"
