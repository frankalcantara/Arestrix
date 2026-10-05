import os
import subprocess
import threading
import http.server
import socketserver
import urllib.request
import zipfile
import shutil
import sys

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

def build():
    print("[Build] Compilando o backend em C++ com MSVC...")
    
    vcvars = find_vcvars()
    
    try:
        if vcvars:
            print(f"[Build] Ambiente MSVC detectado automaticamente: {vcvars}")
            # Roda nmake dentro do ambiente configurado
            cmd = f'call "{vcvars}" && nmake -nologo -f Makefile'
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        else:
            print("[Build] vswhere não encontrou o Visual Studio. Tentando nmake direto...")
            result = subprocess.run(["nmake", "-nologo", "-f", "Makefile"], capture_output=True, text=True)
            
        if result.returncode != 0:
            print("[Erro de Build] A compilação falhou:\n", result.stdout, "\n", result.stderr)
            sys.exit(1)
            
        print("[Build] Compilação concluída com sucesso!")
    except FileNotFoundError:
        print("[Erro] O compilador C++ da Microsoft não foi encontrado.")
        print("Certifique-se de que o 'Desenvolvimento para Desktop com C++' está instalado no Visual Studio.")
        sys.exit(1)

if __name__ == "__main__":
    print("[Setup] Construindo o Frontend (Vite)...")
    subprocess.run(["npm", "run", "build"], cwd=os.path.join(os.getcwd(), ".."), shell=True)
    
    setup_webui()
    build()

    print("[App] Iniciando TaskForge C++...")
    # Executa o C++ a partir da pasta raiz para que ele enxergue corretamente a pasta 'dist'.
    app_path = os.path.join(os.getcwd(), "app.exe")
    subprocess.run([app_path], cwd=os.path.join(os.getcwd(), ".."))
