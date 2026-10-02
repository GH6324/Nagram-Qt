#!/usr/bin/env bash
# Delete the Actions caches that a newer cache of the same kind replaces.
#
# Usage: drop_superseded_caches.sh <key>...
# Each key ends in a hash or a run id. Caches of the current ref whose key
# differs from it only in that last part are deleted, but only when a cache
# with the given key exists, so the newest one is never lost.
# Needs GH_TOKEN with actions: write, GITHUB_REPOSITORY and GITHUB_REF.
set -euo pipefail

caches=$(gh cache list -R "$GITHUB_REPOSITORY" --ref "$GITHUB_REF" \
	--limit 100 --json id,key --jq '.[] | "\(.id) \(.key)"')

for keep in "$@"; do
	found=""
	superseded=()
	prefix="${keep%-*}-"
	while read -r id key; do
		if [ "$key" = "$keep" ]; then
			found=1
		elif [ -n "$id" ] && [ "${key%-*}-" = "$prefix" ]; then
			superseded+=("$id $key")
		fi
	done <<<"$caches"
	if [ -z "$found" ]; then
		echo "No cache with the key $keep yet, keeping the older ones."
		continue
	fi
	for entry in ${superseded[@]+"${superseded[@]}"}; do
		echo "Deleting the superseded cache ${entry#* }."
		gh cache delete "${entry%% *}" -R "$GITHUB_REPOSITORY"
	done
done
