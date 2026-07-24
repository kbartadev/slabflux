#!/usr/bin/env bash
set -euo pipefail

DIR="${1:-.}"

echo "Running all executables in: $DIR"
echo

for f in "$DIR"/*; do
    if [[ -f "$f" && -x "$f" ]]; then
        echo "=== Running: $f ==="
        "$f"
        echo
    fi
done

echo "Done."
