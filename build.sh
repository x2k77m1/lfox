#!/bin/sh
# Build release/lfox-<version>.elemod from this directory.
#
#   ELEKLOADER=/path/to/elekloader \
#   STOCK=/path/to/Digitone_and_Digitone_Keys_OS1.44.syx sh build.sh
#
# ELEKLOADER is an elekloader checkout (the SDK, the cores and the companion
# machines). ELEKLOADER_CROSS is the cross-toolchain prefix; it defaults to
# m68k-elf-.
set -e

: "${ELEKLOADER:?set ELEKLOADER to an elekloader checkout}"
: "${STOCK:?set STOCK to the stock Digitone mk1 OS 1.44 .syx}"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT"

export ELEKLOADER_CROSS="${ELEKLOADER_CROSS:-m68k-elf-}"
export PYTHONPATH="$ELEKLOADER:${PYTHONPATH:-}"

mkdir -p release
python3 -m elekloader.sdk.build . --stock "$STOCK" --out release
ls -l release/*.elemod
