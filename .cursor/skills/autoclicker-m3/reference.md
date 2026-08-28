# Referência — Auto Clicker M3 Pro

Documento detalhado de propósito, arquitetura e funcionamento.

---

## 1. Visão geral

O **Auto Clicker M3 Pro** é um auto-clicker desktop para Windows. A interface segue um visual escuro inspirado no Material Design 3.

O comportamento central **não** é clicar por atalho de teclado nem em coordenadas fixas. O gatilho é:

1. **Duplo-clique físico** em um botão do mouse habilitado (intervalo < 300 ms).
2. **Manter o botão pressionado** — enquanto o hold continuar, o app injeta cliques repetidos na taxa configurada (CPS).
3. **Soltar o botão** — o auto-clique para imediatamente.

---

## 2. Stack tecnológica

### Primária (Python)

| Item | Detalhe |
|------|---------|
| Linguagem | Python 3 (CI usa 3.12) |
| Interface | `customtkinter` + `tkinter` |
| Mouse (leitura e injeção) | `pynput` (`Listener` + `Controller`) |
| Empacotamento | PyInstaller (`--onefile`, `--windowed`) |
| CI/CD | GitHub Actions → release com `.exe` |

Dependências (`python/requirements.txt`): `pyinstaller`, `pynput`, `customtkinter`.

### Secundária (C++)

| Item | Detalhe |
|------|---------|
| Linguagem | C++17 |
| Interface | Win32 + GDI+ (tema escuro custom, cards/switches/slider) |
| Mouse (leitura) | `SetWindowsHookEx(WH_MOUSE_LL)` |
| Mouse (injeção) | `SendInput` |
| Build | CMake → `AutoClickerM3Cpp.exe` |
| Dependências | Nenhuma de terceiros (só libs do Windows) |

---

## 3. Estrutura do repositório

```
AutoClicker/
├── run.bat / run.sh                # Atalho → python/run.*
├── python/
│   ├── main.py                     # Versão Python (primária)
│   ├── build.py                    # Script de build PyInstaller
│   ├── requirements.txt
│   ├── run.bat                     # Windows
│   └── run.sh                      # Linux / macOS
├── cpp/
│   ├── main.cpp                    # Versão C++ (secundária, Win32)
│   ├── CMakeLists.txt
│   ├── run.bat                     # Windows (build + run)
│   └── run.sh                      # Aviso fora do Windows / MSYS
├── assets/
├── .cursor/skills/autoclicker-m3/  # Skill do agente
├── .cursor/rules/                  # Rules do projeto
├── .github/workflows/release.yml
├── python/dist/                    # Saída do build Python (não versionado)
├── python/build/                   # Cache do PyInstaller (não versionado)
└── cpp/build/                      # Cache CMake (não versionado)
```

Classes em `python/main.py` / equivalentes em `cpp/main.cpp`:

| Classe | Responsabilidade |
|--------|------------------|
| `ClickEngine` | Listener global do mouse, loop de cliques, cálculo de CPS real |
| `OverlayWindow` | HUD flutuante com CPS em tempo real |
| `AutoClickerApp` / `App` | Janela principal |


---

## 4. Arquitetura

```
┌─────────────────────────────────────────┐
│           AutoClickerApp (UI)           │
│  master switch │ CPS │ triggers │ abas  │
└────────────────────┬────────────────────┘
                     │ callbacks / estado
┌────────────────────▼────────────────────┐
│              ClickEngine                │
│  Thread: _click_loop (injeta cliques)   │
│  Thread: pynput Listener (_on_click)    │
└────────────────────┬────────────────────┘
                     │ press / release
                     ▼
              Sistema / outros apps
```

### Threads

- **Listener (`pynput`)**: captura press/release físicos do mouse em qualquer aplicação.
- **Loop de cliques (`_click_loop`)**: thread daemon que injeta `press` + `release` enquanto o hold estiver ativo.
- **UI (Tk mainloop)**: atualizações de status usam `self.after(0, ...)` (thread-safe).

---

## 5. Fluxo de ativação

1. O listener global recebe um evento de mouse.
2. Se o botão está em `active_triggers` e `master_enabled` é `True`:
   - **Press físico:** se o intervalo desde o último press for < `0.3` s → `clicking_state[btn] = True`.
   - **Release físico:** `clicking_state[btn] = False`.
3. Enquanto `clicking_state[btn]` e o trigger estiverem ativos, `_click_loop`:
   - `mouse.press(btn)` → sleep ~10–20 ms → `mouse.release(btn)`;
   - aguarda `1 / cps` (ou variação humanizada).
4. Cliques injetados são filtrados (anti-feedback).

### Uso

1. Abrir o app; switch **LIGADO**.
2. Ajustar CPS e botões-gatilho.
3. No app alvo: **duplo-clique + hold**.
4. Soltar para parar.
5. (Opcional) Overlay e/ou aba de ripples.

Não há hotkeys de teclado.

---

## 6. Interface

Janela: **500×750**, não redimensionável, tema escuro.

### Header

- Título **Auto Clicker**
- Switch mestre **LIGADO / DESLIGADO**

### Aba Configurações

| Controle | Descrição | Padrão |
|----------|-----------|--------|
| Slider CPS | 1–100 CPS | 12 |
| Modo humanizado | Intervalo 70%–130% do base | Off |
| Botão Esquerdo | Gatilho | On |
| Direito / Meio / X1 / X2 | Gatilho | Off |
| Overlay | HUD de CPS real | Off |

### Aba Teste (Ripples)

Canvas com ondas por clique. Bindings Tk: Button-1/2/3 (sem X1/X2).

### Status

| Valor | Significado |
|-------|-------------|
| `INATIVO` | Motor ligado, sem hold |
| `ATIVO` | Auto-clique em andamento |
| `DESLIGADO GERAL` | Switch mestre off |

### Overlay

Sem borda, topmost, canto superior direito; verde `#00E676` quando ativo; refresh 100 ms.

---

## 7. Motor (`ClickEngine`)

### Anti-feedback

Flags por botão: `ignore_next_press`, `ignore_next_release`. Marcadas antes de cada injeção; o listener ignora só o próximo evento daquele tipo.

### Timing

| Situação | Comportamento |
|----------|---------------|
| Press → release injetado | `random.uniform(0.01, 0.02)` s |
| Entre ciclos | `1/cps`, ou ±30% se humanizado; compensação ~25 ms |
| Idle | sleep `0.01` s |
| Master off | sleep `0.05` s |

### CPS real

`deque(maxlen=200)` de timestamps; `get_real_cps()` conta eventos no último segundo.

### Threshold

`double_click_threshold = 0.3` s (hardcoded).

---

## 8. Persistência

Tudo em memória. Hardcoded: tema Dark+blue, threshold 0.3 s, janela 500×750, overlay em `screenwidth - 250`, `y = 50`.

---

## 9. Executar e compilar

### Run scripts

| Script | Plataforma | Comportamento |
|--------|------------|---------------|
| `run.bat` / `run.sh` (raiz) | Win / Linux / macOS | Encaminha para `python/run.*` |
| `python/run.bat` / `python/run.sh` | Win / Linux / macOS | Roda `main.py` (usa `venv` se existir) |
| `cpp/run.bat` | Windows | Compila se preciso e abre o `.exe` |
| `cpp/run.sh` | Windows (MSYS/MinGW) | Idem; fora do Windows encerra com aviso |

### Python — desenvolvimento

```powershell
cd python
python -m venv venv
.\venv\Scripts\Activate.ps1
pip install -r requirements.txt
python main.py
```

### Python — build

```powershell
cd python
python build.py
# → python/dist/AutoClickerM3.exe
```

Flags: `--onefile`, `--windowed`, `--name=AutoClickerM3`, `--collect-all=customtkinter`, `--noconfirm`.

### C++ — build (MinGW)

```powershell
cmake -S cpp -B cpp/build -G "MinGW Makefiles"
cmake --build cpp/build
# → cpp/build/bin/AutoClickerM3Cpp.exe
```

### Release (CI)

`.github/workflows/release.yml` — tag `v*` ou `workflow_dispatch`; build a partir de `python/`; publica o `.exe` **Python**. A versão C++ ainda não entra no release automatizado.

---

## 10. Limitações

- Sem persistência, hotkeys de teclado ou clique em coordenadas fixas
- Listener global (permissões / antivírus no Windows)
- Ripples não cobrem X1/X2
- UI só em português
- Sem testes automatizados, ícone próprio ou config file externo
