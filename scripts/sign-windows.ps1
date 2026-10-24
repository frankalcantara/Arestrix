# Assinatura Authenticode dos binários do Arestrix (Windows).
#
# Requer um certificado de assinatura de código. Nenhum segredo é gravado no repositório:
# use ARESTRIX_SIGN_PFX + ARESTRIX_SIGN_PFX_PASSWORD ou ARESTRIX_SIGN_THUMBPRINT.
#
# Exemplos:
#   $env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
#   $env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
#   pwsh scripts/sign-windows.ps1
#
#   $env:ARESTRIX_SIGN_THUMBPRINT = "AB12..."
#   pwsh scripts/sign-windows.ps1 -RuntimePath "$env:LOCALAPPDATA\Arestrix\runtime"
#
# Observação: o manifesto do llama.cpp verifica o SHA-256 do arquivo compactado baixado.
# Assinar o llama-server.exe extraído é opcional e não altera essa verificação.

param(
    [string[]]$Path = @("build\arestrix.exe", "build\arestrix-cli.exe", "build\arestrix-gateway.exe"),
    [string]$RuntimePath = "",
    [string]$PfxPath = $env:ARESTRIX_SIGN_PFX,
    [string]$PfxPassword = $env:ARESTRIX_SIGN_PFX_PASSWORD,
    [string]$Thumbprint = $env:ARESTRIX_SIGN_THUMBPRINT,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $kits = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($kit in $kits) {
        $candidate = Join-Path $kit.FullName "x64\signtool.exe"
        if (Test-Path $candidate) { return $candidate }
    }

    throw "signtool.exe não encontrado. Instale o Windows SDK (Signing Tools)."
}

$signTool = Find-SignTool
$files = @()
foreach ($item in $Path) {
    if (Test-Path $item) {
        $files += (Resolve-Path $item).Path
    } else {
        Write-Warning "Ignorando arquivo inexistente: $item"
    }
}

if ($RuntimePath) {
    if (Test-Path $RuntimePath) {
        $files += Get-ChildItem $RuntimePath -Recurse -File -Include "llama-server.exe", "*.dll" |
            Select-Object -ExpandProperty FullName
    } else {
        Write-Warning "RuntimePath inexistente: $RuntimePath"
    }
}

if ($files.Count -eq 0) { throw "Nenhum arquivo para assinar." }

if ($Thumbprint) {
    $certArgs = @("/sha1", $Thumbprint, "/s", "My")
} elseif ($PfxPath) {
    if (-not (Test-Path $PfxPath)) { throw "PFX não encontrado: $PfxPath" }
    $certArgs = @("/f", (Resolve-Path $PfxPath).Path)
    if ($PfxPassword) { $certArgs += @("/p", $PfxPassword) }
} else {
    throw "Defina ARESTRIX_SIGN_PFX/ARESTRIX_SIGN_PFX_PASSWORD ou ARESTRIX_SIGN_THUMBPRINT."
}

foreach ($file in $files) {
    Write-Host "Assinando $file"
    & $signTool sign /fd SHA256 /tr $TimestampUrl /td SHA256 @certArgs $file
    if ($LASTEXITCODE -ne 0) { throw "Falha ao assinar $file (código $LASTEXITCODE)." }
    & $signTool verify /pa /v $file
    if ($LASTEXITCODE -ne 0) { throw "Falha ao verificar a assinatura de $file." }
}

Write-Host "Assinatura concluída: $($files.Count) arquivo(s)."
