# Auto Clicker M3 Pro

Auto-clicker para Windows com interface escura estilo Material Design 3.

![Auto Clicker M3 Pro](assets/demo.png)

## Como funciona

1. Faça um **duplo-clique** em um botão do mouse habilitado
2. **Mantenha pressionado** — o app injeta cliques na taxa configurada (CPS)
3. **Solte** o botão para parar

Não usa atalhos de teclado nem coordenadas fixas: o gatilho é o próprio botão do mouse.

## Recursos

- Velocidade de **1 a 100 CPS**
- **Modo humanizado** (intervalos aleatórios)
- Gatilhos: esquerdo, direito, meio, X1 e X2
- **Overlay** com CPS real em tempo real
- Aba de teste com efeito de ripples

## Estrutura

```
AutoClicker/
├── run.bat / run.sh   # atalho → versão C++
├── cpp/               # versão primária (Win32)
├── python/            # versão secundária (CustomTkinter + pynput)
└── assets/
```

## Executar

Na raiz (C++ por padrão, Windows):

```powershell
.\run.bat          # Windows
./run.sh           # Windows (MinGW/MSYS); Linux/macOS: use Python
```

Direto em cada versão:

```powershell
.\cpp\run.bat      # Windows; compila se o .exe nao existir
.\python\run.bat
```

```bash
./cpp/run.sh       # so em ambiente Windows (MinGW/MSYS)
./python/run.sh    # Linux / macOS / Windows
```

## C++ (primária)

Win32, sem dependências externas — mesmo modelo de duplo-clique + hold.

```powershell
cmake -S cpp -B cpp/build -G "MinGW Makefiles"
cmake --build cpp/build
.\cpp\build\bin\AutoClickerM3Cpp.exe
```

Releases prontas também estão disponíveis na [página de Releases](https://github.com/GuilhermeRoesler/AutoClicker/releases) do GitHub — incluindo `windows-optimized.exe` (build C++ nativo).

## Python (secundária)

Requisitos: Windows · Python 3.12+

```powershell
git clone https://github.com/GuilhermeRoesler/AutoClicker.git
cd AutoClicker/python
python -m venv venv
.\venv\Scripts\Activate.ps1
pip install -r requirements.txt
python main.py
```

### Build do executável

```powershell
cd python
python build.py
```

O arquivo sai em `python/dist/AutoClickerM3.exe`.

## Stack

- **Primária:** C++17 · Win32 + GDI+ · CMake
- **Secundária:** Python · CustomTkinter · pynput · PyInstaller

## Licença

Uso pessoal. Consulte o repositório para detalhes de distribuição.
