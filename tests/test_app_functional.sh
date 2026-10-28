#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary="${1:-$root/build/conway}"
PYTHONDONTWRITEBYTECODE=1 python3 "$root/tests/test_text_catalog.py" "$binary" "$root"
