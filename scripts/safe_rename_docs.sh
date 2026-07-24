#!/bin/bash
# Save as: /home/kris/src/base/slabflux_rte/scripts/safe_rename_docs.sh

DOCS_DIR="/home/kris/src/base/slabflux_rte/docs/technology_map"
echo "[SLABFLUX] Starting Safe Documentation Renamer..."

find "$DOCS_DIR" -type f -name "*.md" | while read -r file; do
    dir=$(dirname "$file")
    base=$(basename "$file" .md)
    new_base=""

    # Map existing chaotic names to the strict schema
    if [[ "$base" == blueprint_* ]]; then
        new_base="${base#blueprint_}_blueprint"
    elif [[ "$base" == foundation_* ]]; then
        new_base="${base#foundation_}_foundation"
    elif [[ "$base" == *.blueprint ]]; then
        new_base="${base%.blueprint}_blueprint"
    elif [[ "$base" == *.foundation ]]; then
        new_base="${base%.foundation}_foundation"
    elif [[ "$base" == *_blueprint ]] || [[ "$base" == *_foundation ]]; then
        continue # Already matches schema
    else
        # Bare files (e.g., panoptic_reticle.md).
        # Default to foundation schema for collision tracking so we can merge them.
        new_base="${base}_foundation"
    fi

    if [[ -n "$new_base" && "$base" != "$new_base" ]]; then
        target="$dir/$new_base.md"
        suffix=1

        # Collision Detection: Append suffix until safe
        while [[ -e "$target" ]]; do
            target="$dir/${new_base}_${suffix}.md"
            ((suffix++))
        done

        mv "$file" "$target"
        echo "  [RENAMED SAFE] $base.md -> $(basename "$target")"
    fi
done

echo "[SLABFLUX] All files renamed to schema safely without overwriting."
