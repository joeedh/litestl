#!/usr/bin/env bash
# Regenerate table-of-contents blocks in markdown docs via markdown-toc.
set -euo pipefail

cd "$(dirname "$0")"

SCRIPT_NAME="$(basename "$0")"
HEADER=$'<!-- auto-generated with markdown-toc! regenerate with ${SCRIPT_NAME} -->'

FILES=(
    documentation/binding.md
    documentation/containers.md
    binding/Readme.MD
    Readme.MD
)

for f in "${FILES[@]}"; do
    if [[ ! -f "$f" ]]; then
        echo "skip: $f (not found)" >&2
        continue
    fi
    echo "toc: $f"
    npx --yes markdown-toc -i "$f" --append="__NL__$HEADER"
    sed -i 's/__NL__/\n/g' "$f"
done
