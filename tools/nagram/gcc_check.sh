#!/usr/bin/env bash
# Compile with the GCC of the Linux build image before pushing.
#
# Usage: gcc_check.sh
# Compiles every source that Nagram adds or changes against the upstream base
# of tools/nagram/upstream.json, with warnings as errors as the Debug build of
# the Linux workflow does and with the updater that only its Release build
# has, then builds and runs test_nagram. Every failing file is reported in one
# run, and a later run compiles only what changed since.
#
# Needs the image of Telegram/build/docker/centos_env and a build directory
# that is kept between runs:
#   NAGRAM_LINUX_OUT     build directory on the host, required
#   NAGRAM_LINUX_CCACHE  ccache directory on the host, optional
#   NAGRAM_LINUX_IMAGE   image name, tdesktop:centos_env by default
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
if [ $# -ne 0 ] || [ -z "${NAGRAM_LINUX_OUT:-}" ]; then
	echo "Usage: NAGRAM_LINUX_OUT=<build directory> $0" >&2
	exit 2
fi

policy=$root/tools/nagram/upstream.json
base=$(sed -n 's/^ *"base": *"\([0-9a-f]\{40\}\)",\{0,1\}$/\1/p' "$policy")
if [ -z "$base" ]; then
	echo "$policy does not name the upstream base." >&2
	exit 1
fi
if ! changed=$(git -C "$root" diff --name-only --diff-filter=d "$base" -- \
	'Telegram/SourceFiles/*.cpp'); then
	echo "Cannot compare with the upstream base: git fetch origin $base" >&2
	exit 1
fi
sources=()
while IFS= read -r file; do
	sources+=("${file#Telegram/}")
done <<< "$changed"

api=""
if [ ! -f "$root/Telegram/build/api_credentials.local.cmake" ] \
	&& [ -z "${NAGRAM_API_ID:-}" ]; then
	api="-D TDESKTOP_API_TEST=ON"
fi

mkdir -p "$NAGRAM_LINUX_OUT"
mounts=(-v "$root:/usr/src/tdesktop" -v "$NAGRAM_LINUX_OUT:/usr/src/tdesktop/out")
environment=(-e NAGRAM_API_ID -e NAGRAM_API_HASH)
if [ -n "${NAGRAM_LINUX_CCACHE:-}" ]; then
	mkdir -p "$NAGRAM_LINUX_CCACHE"
	mounts+=(-v "$NAGRAM_LINUX_CCACHE:/ccache")
	environment+=(-e CCACHE_DIR=/ccache -e "CCACHE_SLOPPINESS=pch_defines,time_macros")
fi

# The script inside the image. It configures on every run, so that a source
# added since the last one is known, and names the sources no Linux target
# compiles instead of dropping them silently.
# shellcheck disable=SC2016
inner='
set -e
cd /usr/src/tdesktop/Telegram
./configure.sh \
	-D CMAKE_CONFIGURATION_TYPES=Debug \
	-D DESKTOP_APP_TEST_APPS=ON \
	-D CMAKE_C_FLAGS_DEBUG="-O0 -fpch-preprocess" \
	-D CMAKE_CXX_FLAGS_DEBUG="-O0 -fpch-preprocess" \
	-D CMAKE_COMPILE_WARNING_AS_ERROR=ON \
	-D DESKTOP_APP_DISABLE_AUTOUPDATE=OFF \
	$NAGRAM_CHECK_API
cd ..
objects=$(awk -v list="$*" '\''
	BEGIN {
		count = split(list, sources, " ")
		for (i = 1; i <= count; i++) {
			wanted["/Debug/" sources[i] ".o:"] = sources[i]
		}
	}
	/^build Telegram\/CMakeFiles\/[^ ]*\.o: / {
		key = substr($2, index($2, "/Debug/"))
		if (key in wanted) {
			print substr($2, 1, length($2) - 1)
			found[wanted[key]] = 1
		}
	}
	END {
		for (key in wanted) {
			if (!(wanted[key] in found)) {
				print "Not in the Linux build: " wanted[key] > "/dev/stderr"
			}
		}
	}
'\'' out/CMakeFiles/impl-Debug.ninja)
if [ -z "$objects" ]; then
	echo "No object of the $# sources is in out/CMakeFiles/impl-Debug.ninja." >&2
	exit 1
fi
echo "Compiling $(echo "$objects" | wc -l) objects of $# sources."
cmake --build out --config Debug --target test_nagram $objects -- -k 0
out/nagram-tests/Debug/test_nagram
'

docker run --rm --platform linux/amd64 -u "$(id -u)" \
	"${mounts[@]}" "${environment[@]}" -e NAGRAM_CHECK_API="$api" \
	"${NAGRAM_LINUX_IMAGE:-tdesktop:centos_env}" \
	env -u CCACHE_DISABLE sh -c "$inner" check ${sources[@]+"${sources[@]}"}
