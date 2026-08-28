import sys

import PyInstaller.__main__

APP_NAME = "AutoClickerM3"

# Backends do pynput são carregados dinamicamente e o PyInstaller
# não os detecta sozinho — sem isso o binário Linux falha com
# "No module named 'pynput.keyboard._xorg'".
PYNPUT_HIDDEN_IMPORTS = {
    "linux": (
        "pynput.keyboard._xorg",
        "pynput.mouse._xorg",
        "Xlib",
        "Xlib.display",
        "Xlib.ext",
        "Xlib.ext.xtest",
    ),
    "darwin": (
        "pynput.keyboard._darwin",
        "pynput.mouse._darwin",
    ),
    "win32": (
        "pynput.keyboard._win32",
        "pynput.mouse._win32",
    ),
}


def platform_key() -> str:
    if sys.platform.startswith("linux"):
        return "linux"
    if sys.platform == "darwin":
        return "darwin"
    return "win32"


def build_args() -> list[str]:
    args = [
        "main.py",
        "--onefile",
        f"--name={APP_NAME}",
        "--collect-all=customtkinter",
        "--collect-all=pynput",
        "--noconfirm",
    ]

    for module in PYNPUT_HIDDEN_IMPORTS[platform_key()]:
        args.append(f"--hidden-import={module}")

    if platform_key() == "linux":
        args.append("--collect-all=Xlib")

    # GUI sem console (no Linux o PyInstaller ignora --windowed)
    if platform_key() in ("win32", "darwin"):
        args.append("--windowed")

    return args


def output_path() -> str:
    if platform_key() == "win32":
        return f"dist/{APP_NAME}.exe"
    return f"dist/{APP_NAME}"


def main() -> None:
    print("Iniciando a compilação do Auto Clicker M3 Pro...")

    PyInstaller.__main__.run(build_args())

    print("\nCompilação concluída!")
    print(f"Executável em: {output_path()}")


if __name__ == "__main__":
    main()
