#!/bin/sh
# WineForge-Internal: native-steam/app-owned-compat-entry-v1.
set -eu
CDPATH=''
tool_dir=$(cd -- "$(dirname -- "$0")" && pwd)
verb=${1:-}
[ "$#" -eq 0 ] || shift
case "$verb" in run|waitforexitandrun|runinprefix|getcompatpath) ;; *) echo 'WineForge Steam Play: unsupported verb.' >&2; exit 2 ;; esac
app=''
IFS= read -r app < "$tool_dir/wineforge-app" || true
case "$app" in /*) ;; *) echo 'WineForge Steam Play: missing application registration.' >&2; exit 2 ;; esac
[ -x "$app" ] || { echo 'WineForge Steam Play: registered application is unavailable.' >&2; exit 2; }
# The application resolves its current database and creates the same launch
# profile for both entry points. Steam supplies the actual Windows argv.
exec "$app" --steam-play "$verb" -- "$@"
