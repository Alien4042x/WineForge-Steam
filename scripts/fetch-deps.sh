#!/bin/sh
# WineForge-Internal: native-steam/pinned-dobby-source-v1.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
pin=5dfc8546954ce3b3198132ab13fddb89ee92cdd7
destination="$root/deps/Dobby"
if [ ! -e "$destination" ]
then
    mkdir -p "$destination"
    git -C "$destination" init --quiet
    git -C "$destination" remote add origin https://github.com/jmpews/Dobby.git
fi
[ -d "$destination/.git" ] || { echo 'Dependency path is not the owned Git checkout.' >&2; exit 1; }
[ "$(git -C "$destination" remote get-url origin)" = https://github.com/jmpews/Dobby.git ] || { echo 'Dependency remote differs from the pinned source.' >&2; exit 1; }
if ! git -C "$destination" rev-parse --verify HEAD >/dev/null 2>&1
then
    git -C "$destination" fetch --depth 1 origin "$pin"
    git -C "$destination" checkout --detach FETCH_HEAD
fi
[ "$(git -C "$destination" rev-parse HEAD)" = "$pin" ] || { echo 'Dobby revision differs from the pinned version. Existing work was kept.' >&2; exit 1; }
[ -z "$(git -C "$destination" status --porcelain)" ] || { echo 'Dobby checkout has local changes. Existing work was kept.' >&2; exit 1; }
echo 'Pinned Dobby source is ready.'
