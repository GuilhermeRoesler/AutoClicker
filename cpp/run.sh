#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# A implementação atual usa Win32 (WH_MOUSE_LL / SendInput / UI nativa).
uname_s="$(uname -s 2>/dev/null || echo unknown)"
case "$uname_s" in
  MINGW*|MSYS*|CYGWIN*)
    ;;
  *)
    echo "[erro] A versao C++ e Windows-only por enquanto (API Win32)." >&2
    echo "       No Linux/macOS use a versao Python: ./python/run.sh" >&2
    exit 1
    ;;
esac

find_exe() {
  local candidates=(
    "build/bin/AutoClickerM3Cpp.exe"
    "build/bin/Release/AutoClickerM3Cpp.exe"
    "build/bin/Debug/AutoClickerM3Cpp.exe"
    "build/Release/AutoClickerM3Cpp.exe"
    "build/Debug/AutoClickerM3Cpp.exe"
  )
  local p
  for p in "${candidates[@]}"; do
    if [[ -f "$p" ]]; then
      printf '%s\n' "$p"
      return 0
    fi
  done
  return 1
}

EXE="$(find_exe || true)"
if [[ -z "${EXE}" ]]; then
  echo "[info] Executavel C++ nao encontrado. Compilando..."
  cmake -S . -B build
  cmake --build build --config Release
  EXE="$(find_exe || true)"
fi

if [[ -z "${EXE}" ]]; then
  echo "[erro] Build concluiu, mas AutoClickerM3Cpp.exe nao foi encontrado." >&2
  exit 1
fi

exec "./${EXE}"
