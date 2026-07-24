#!/usr/bin/env bash
# Strict non-interactive POSIX/GNU file re-aliasing operator
# Preserves local environmental integrity by preventing destination overwrites
# Target: Strips 'blueprint_' prefix from files matching the localized pattern

set -euo pipefail

for source_path in blueprint_*; do
    [[ -e "$source_path" ]] || continue

    target_name="${source_path#blueprint_}"

    if [[ -e "$target_name" ]]; then
        continue
    fi

    mv -n "$source_path" "$target_name"
done
