#!/bin/sh
# Only reads system/network state and writes a report. Run locally on the VPS yourself.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
umask 077
exec python3 "$script_dir/inspect_environment.py" --output "${1:-gamelink-vps-report.md}"
