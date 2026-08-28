#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo "[erro] A versao C++ usa MSVC + WebView2 e e Windows-only." >&2
echo "       No Linux/macOS use a versao Python: ./python/run.sh" >&2
exit 1
