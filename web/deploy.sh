#!/bin/sh
# Upload web/dist to https://ruzzoli.de/roguelikes/ia/ (RVIP.md stage 5).
# Run after `sh web/build.sh`, from a clean, pushed tree.
set -e
cd "$(dirname "$0")/.."
git fetch -q && [ -z "$(git status --porcelain)" ] && [ "$(git rev-parse @)" = "$(git rev-parse @{u})" ] || { echo "commit + push first"; exit 1; }
[ -f web/dist/ia.wasm ] || { echo "deploy: build first (sh web/build.sh)" >&2; exit 1; }
ssh ruzzoli.de 'sudo mkdir -p /var/www/ruzzoli.de/roguelikes/ia && sudo chown -R felix:www-data /var/www/ruzzoli.de/roguelikes/ia'
rsync -rtz --delete web/dist/ ruzzoli.de:/var/www/ruzzoli.de/roguelikes/ia/
