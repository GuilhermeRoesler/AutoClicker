import sys

import PyInstaller.__main__

APP_NAME = "AutoClickerM3"


def build_args() -> list[str]:
    args = [
        "main.py",
        "--onefile",
        f"--name={APP_NAME}",
        "--collect-all=customtkinter",
        "--noconfirm",
    ]

    # GUI sem console (ignorado no Linux pelo PyInstaller)
    if sys.platform in ("win32", "darwin"):
        args.append("--windowed")

    return args


def output_path() -> str:
    if sys.platform == "win32":
        return f"dist/{APP_NAME}.exe"
    return f"dist/{APP_NAME}"


def main() -> None:
    print("Iniciando a compilação do Auto Clicker M3 Pro...")

    PyInstaller.__main__.run(build_args())

    print("\nCompilação concluída!")
    print(f"Executável em: {output_path()}")


if __name__ == "__main__":
    main()
