import os
import subprocess
import threading
import http.server
import socketserver
import urllib.request
import zipfile
import shutil
import sys
import platform

IS_WINDOWS = platform.system() == "Windows"
EXE_NAME = "arestrix.exe" if IS_WINDOWS else "arestrix"

PORT = 8000
UI_DIR = "ui"

def serve():
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=UI_DIR, **kwargs)
        def log_message(self, format, *args):
            pass # Suprime os logs do servidor para manter o terminal limpo

    # Configura para reutilizar a porta e evitar erro de "Address already in use"
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        print(f"[Python] Servidor HTTP rodando na porta {PORT} (Diretório: {UI_DIR}/)...")
        httpd.serve_forever()

def setup_webui():
    if not IS_WINDOWS:
        # No Linux a lib estatica (vendor/webui/linux) ja vem no repositorio; o Makefile.linux baixa se faltar
        return
    if not os.path.exists("vendor/webui/include"):
        print("[Setup] Baixando biblioteca WebUI (C++) para MSVC...")
        os.makedirs("vendor", exist_ok=True)
        # Usa uma release estática e adiciona header pra evitar bloqueio
        url = "https://github.com/webui-dev/webui/releases/download/2.4.2/webui-windows-msvc-x64.zip"
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        
        with urllib.request.urlopen(req) as response, open("vendor/webui.zip", 'wb') as out_file:
            shutil.copyfileobj(response, out_file)
        
        with zipfile.ZipFile("vendor/webui.zip", 'r') as zip_ref:
            zip_ref.extractall("vendor/webui_temp")
            
        os.remove("vendor/webui.zip")
        
        # O zip do WebUI 2.4.2 geralmente contém uma pasta principal
        extracted_folder = os.path.join("vendor", "webui_temp", os.listdir("vendor/webui_temp")[0])
        shutil.move(extracted_folder, "vendor/webui")
        shutil.rmtree(os.path.join("vendor", "webui_temp"))
        print("[Setup] WebUI baixado e extraído com sucesso.")

def find_vcvars():
    # Usa a ferramenta oficial da Microsoft (vswhere) para achar onde o VS está instalado
    vswhere_path = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if not os.path.exists(vswhere_path):
        return None
        
    result = subprocess.run([
        vswhere_path, 
        "-latest", 
        "-products", "*", 
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", 
        "-property", "installationPath"
    ], capture_output=True, text=True)
    
    vs_path = result.stdout.strip()
    if not vs_path:
        return None
        
    vcvars = os.path.join(vs_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
    if os.path.exists(vcvars):
        return vcvars
    return None

def build_linux():
    print("[Build] Compilando o backend em C++ com GCC (Makefile.linux)...")
    std = os.environ.get("ARESTRIX_STD", "c++23")
    try:
        result = subprocess.run(["make", "-k", "-f", "Makefile.linux", f"STD={std}"], capture_output=True, text=True)
    except FileNotFoundError:
        print("[Erro] 'make'/'g++' nao encontrados. Instale: sudo apt install build-essential curl unzip")
        sys.exit(1)
    if result.returncode != 0:
        print("[Erro de Build] A compilação falhou:\n", result.stdout, "\n", result.stderr)
        sys.exit(1)
    print("[Build] Compilação concluída com sucesso!")
    sync_dist()

def sync_dist():
    # Sincroniza dist para dentro de CppCore se existir
    root_dist = os.path.join(os.getcwd(), "..", "dist")
    local_dist = os.path.join(os.getcwd(), "dist")
    if os.path.exists(root_dist):
        if os.path.exists(local_dist):
            shutil.rmtree(local_dist)
        shutil.copytree(root_dist, local_dist)

def build():
    if not IS_WINDOWS:
        return build_linux()
    print("[Build] Compilando o backend em C++ com MSVC...")
    
    vcvars = find_vcvars()
    
    try:
        if vcvars:
            print(f"[Build] Ambiente MSVC detectado automaticamente: {vcvars}")
            # Roda nmake dentro do ambiente configurado
            std = os.environ.get("ARESTRIX_STD_MSVC", "/std:c++latest")
            cmd = f'call "{vcvars}" && nmake -nologo -f Makefile STD={std}'
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        else:
            print("[Build] vswhere não encontrou o Visual Studio. Tentando nmake direto...")
            result = subprocess.run(["nmake", "-nologo", "-f", "Makefile"], capture_output=True, text=True)
            
        if result.returncode != 0:
            print("[Erro de Build] A compilação falhou:\n", result.stdout, "\n", result.stderr)
            sys.exit(1)
            
        print("[Build] Compilação concluída com sucesso!")
        
        # Sincroniza dist para dentro de CppCore se existir
        root_dist = os.path.join(os.getcwd(), "..", "dist")
        local_dist = os.path.join(os.getcwd(), "dist")
        if os.path.exists(root_dist):
            if os.path.exists(local_dist):
                shutil.rmtree(local_dist)
            shutil.copytree(root_dist, local_dist)

        # Copia arestrix.exe para a raiz do projeto e mantém compatibilidade com app.exe
        target_exe = os.path.join(os.getcwd(), "arestrix.exe")
        root_exe = os.path.join(os.getcwd(), "..", "arestrix.exe")
        compat_exe = os.path.join(os.getcwd(), "app.exe")
        if os.path.exists(target_exe):
            shutil.copyfile(target_exe, root_exe)
            shutil.copyfile(target_exe, compat_exe)

    except FileNotFoundError:
        print("[Erro] O compilador C++ da Microsoft não foi encontrado.")
        print("Certifique-se de que o 'Desenvolvimento para Desktop com C++' está instalado no Visual Studio.")
        sys.exit(1)

if __name__ == "__main__":
    print("[Setup] Construindo o Frontend (Vite)...")
    subprocess.run(["npm", "run", "build"], cwd=os.path.join(os.getcwd(), ".."), shell=IS_WINDOWS)
    
    setup_webui()
    build()

    print("[App] Iniciando Arestrix C++...")
    app_path = os.path.join(os.getcwd(), EXE_NAME)
    subprocess.run([app_path], cwd=os.path.join(os.getcwd(), ".."))
