# Instaladores (Inno Setup)

Scripts para gerar `setup.exe` das builds Windows (Python e C++).

## Pré-requisito

[Inno Setup 6](https://jrsoftware.org/isinfo.php) com o compilador `ISCC.exe`.

## Build local

1. Gere o executável portable (PyInstaller ou CMake).
2. Coloque-o em `dist/staging-python/AutoClickerM3.exe` ou `dist/staging-cpp/AutoClickerM3.exe`.
3. Compile o instalador:

```powershell
# Python
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\python.iss /DMyAppVersion=1.0.0

# C++
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\cpp.iss /DMyAppVersion=1.0.0
```

Saída em `dist/`:

- `AutoClickerM3-windows-setup.exe`
- `AutoClickerM3-windows-optimized-setup.exe`

No CI (`release.yml`) isso roda automaticamente nas jobs Windows.
