import PyInstaller.__main__
import os

print("Iniciando a compilação do Auto Clicker M3 Pro...")

# Instala o pyinstaller caso você ainda não o tenha feito:
# pip install pyinstaller

PyInstaller.__main__.run([
    'main.py',                 # Arquivo principal
    '--onefile',                         # Empacota tudo em apenas um arquivo .exe
    '--windowed',                        # Impede que a tela preta do console abra junto (modo UI)
    '--name=AutoClickerM3',              # Nome do arquivo de saída
    '--collect-all=customtkinter',       # Garante que o tema visual seja levado no .exe
    '--noconfirm',                       # Sobrescreve pastas antigas sem perguntar
])

print("\nCompilação concluída!")
print("O seu executável (AutoClickerM3.exe) pode ser encontrado dentro da pasta 'dist'.")