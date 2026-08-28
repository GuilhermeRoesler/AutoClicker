@echo off
setlocal
cd /d "%~dp0"

set "EXE="
for %%P in (
  "build\bin\Release\AutoClickerM3Cpp.exe"
  "build\bin\Debug\AutoClickerM3Cpp.exe"
  "build\bin\AutoClickerM3Cpp.exe"
  "build\Release\AutoClickerM3Cpp.exe"
  "build\Debug\AutoClickerM3Cpp.exe"
) do (
  if exist %%~P (
    set "EXE=%%~P"
    goto :found
  )
)

echo [info] Executavel C++ nao encontrado. Compilando com MSVC + WebView2...
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
if errorlevel 1 (
  echo [erro] cmake configure falhou. Instale Visual Studio Build Tools com workload C++.
  exit /b 1
)
cmake --build build --config Release
if errorlevel 1 (
  echo [erro] cmake build falhou.
  exit /b 1
)

for %%P in (
  "build\bin\Release\AutoClickerM3Cpp.exe"
  "build\bin\Debug\AutoClickerM3Cpp.exe"
  "build\bin\AutoClickerM3Cpp.exe"
  "build\Release\AutoClickerM3Cpp.exe"
  "build\Debug\AutoClickerM3Cpp.exe"
) do (
  if exist %%~P (
    set "EXE=%%~P"
    goto :found
  )
)

echo [erro] Build concluiu, mas AutoClickerM3Cpp.exe nao foi encontrado.
exit /b 1

:found
start "" "%EXE%"
exit /b 0
