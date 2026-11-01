#!/bin/sh
# Puerta G-WR1: el port del World Rally 2 no toca wrally_ps1/ (ni en el arbol de trabajo ni en el indice).
cd "$(dirname "$0")/../.." || exit 1
# sin git (copia sin historia, como la de la herramienta de herramienta/) no hay con que comparar
git rev-parse --git-dir >/dev/null 2>&1 || { echo "G-WR1: sin git, no se comprueba"; exit 0; }
if ! git diff --quiet HEAD -- wrally_ps1 || ! git diff --cached --quiet -- wrally_ps1; then
  echo "G-WR1: hay cambios en wrally_ps1/ (el WR2 no debe tocarlo):" >&2
  git status --short -- wrally_ps1 >&2
  exit 1
fi
echo "G-WR1: wrally_ps1/ sin cambios"
