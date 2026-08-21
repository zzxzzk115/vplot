#!/usr/bin/env bash
# Build the GitHub Pages site: the static landing page plus every web-capable
# example compiled to WebAssembly, assembled into $OUT (default: _site).
#
# Mirrors VRI's scripts/build_site.sh — vplot consumes VRI, and its examples
# reuse VRI's example scaffolding, so the same wasm/emrun recipe applies.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/_site}"
ART="$ROOT/build/wasm/wasm32/release"

cd "$ROOT"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo dev)"

# VRI defaults to Vulkan, absent on wasm; the root xmake.lua re-requires it with
# the WebGPU + WebGL2 backends when -p wasm. Tests and tools are host-only.
xmake f -y -p wasm -m release --vplot_build_examples=y --vplot_build_tests=n
xmake build -y --all

rm -rf "$OUT"; mkdir -p "$OUT/examples"
cp -r "$ROOT"/web/site/. "$OUT/"
cp "$ART"/example-*.html "$ART"/example-*.js "$ART"/example-*.wasm "$OUT/examples/"
cp "$ART"/example-*.data "$OUT/examples/" 2>/dev/null || true

# Cache-bust the stylesheet / script references with the commit SHA.
grep -rl '{{VPLOT_SHA}}' "$OUT" | while read -r f; do sed -i "s/{{VPLOT_SHA}}/$SHA/g" "$f"; done

# Pages serves through Jekyll by default, which drops files starting with "_".
touch "$OUT/.nojekyll"

echo "site -> $OUT"
