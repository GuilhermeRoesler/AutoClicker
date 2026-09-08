#!/usr/bin/env bash
# Roteia para a versao C++ (padrao).
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/cpp/run.sh" "$@"
