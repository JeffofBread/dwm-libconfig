#!/bin/bash

set -e

REPO_URL="https://git.suckless.org/dwm"
CLONE_DIR="dwm_tmp"
PATCH_NAME="dwm_changes.diff"

FILES=(
        "config.def.h"
        "config.mk"
        "dwm.1"
        "dwm.c"
        "dwm.conf"
        "Makefile"
        "parser.c"
)

git clone "$REPO_URL" "$CLONE_DIR"

for f in "${FILES[@]}"; do
  cp "$f" "$CLONE_DIR/"
done

cd "$CLONE_DIR"

git add "${FILES[@]}"

make clean >/dev/null 2>&1 || true

make

DWM_VERSION=$(./dwm -v 2>&1 || true)
GIT_HASH=$(git rev-parse --short HEAD)
GIT_STAT=$(git diff --stat HEAD)

git commit -m "
dwm-libconfig (${GIT_HASH}) for ${DWM_VERSION}
---
${GIT_STAT}
"

git show HEAD > "../$PATCH_NAME"

cd ..

rm -rf "$CLONE_DIR"
