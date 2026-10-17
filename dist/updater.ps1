# ============================================================================
#  C4pt0r Mod Menu - Atualizador
#  Verifica versao remota e baixa update se houver. Sem git no lado do cliente.
# ============================================================================
$ErrorActionPreference = 'Stop'
$Host.UI.RawUI.WindowTitle = 'C4pt0r - Atualizador'

# --- Config: repositorio publico de updates (SO contem version.txt + zip) ----
$UpdateRepo  = 'https://raw.githubusercontent.com/daflones/c4pt0r-mod-updates/main'
$VersionUrl  = "$UpdateRepo/version.txt"
$PackageUrl  = "$UpdateRepo/C4pt0r-Update.zip"

$InstallDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$VersionFile = Join-Path $InstallDir 'version.txt'

function Write-Line($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }

Write-Line ''
Write-Line '  ==========================================' 'Cyan'
Write-Line '   C4PT0R MOD MENU - ATUALIZADOR' 'Cyan'
Write-Line '  ==========================================' 'Cyan'
Write-Line ''

# --- Versao local -----------------------------------------------------------
$localVer = '0.0.0'
if (Test-Path $VersionFile) { $localVer = (Get-Content $VersionFile -Raw).Trim() }
Write-Line "  Versao instalada : $localVer"

# --- Versao remota ----------------------------------------------------------
try {
    $remoteVer = (Invoke-WebRequest -Uri $VersionUrl -UseBasicParsing -TimeoutSec 15).Content.Trim()
} catch {
    Write-Line ''
    Write-Line '  [ERRO] Sem conexao com o servidor de updates.' 'Red'
    Write-Line '  Verifique sua internet e tente de novo.' 'Red'
    Read-Host "`n  Enter para sair"
    exit 1
}
Write-Line "  Versao disponivel: $remoteVer"
Write-Line ''

if ($remoteVer -eq $localVer) {
    Write-Line '  [OK] Voce ja esta na versao mais recente.' 'Green'
    Read-Host "`n  Enter para sair"
    exit 0
}

# --- Download ----------------------------------------------------------------
Write-Line "  [..] Nova versao encontrada! Baixando $remoteVer ..." 'Yellow'
$tmpZip = Join-Path $env:TEMP "c4pt0r_update_$remoteVer.zip"
$tmpDir = Join-Path $env:TEMP "c4pt0r_update_$remoteVer"

try {
    Invoke-WebRequest -Uri $PackageUrl -OutFile $tmpZip -UseBasicParsing -TimeoutSec 120
} catch {
    Write-Line '  [ERRO] Falha no download do pacote.' 'Red'
    Read-Host "`n  Enter para sair"
    exit 1
}

# --- Extracao ----------------------------------------------------------------
Write-Line '  [..] Instalando arquivos...' 'Yellow'
try {
    if (Test-Path $tmpDir) { Remove-Item $tmpDir -Recurse -Force }
    Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force

    # Se o zip veio com uma pasta-raiz dentro, desce nela
    $srcDir = $tmpDir
    $top = Get-ChildItem -Path $tmpDir -Force
    if (($top | Where-Object { -not $_.PSIsContainer }).Count -eq 0 -and $top.Count -eq 1) {
        $srcDir = $top[0].FullName
    }

    # Copia arquivos novos por cima da instalacao (cria pastas se faltarem,
    # pula arquivos em uso como o proprio updater)
    Get-ChildItem -Path $srcDir -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($srcDir.Length).TrimStart('\')
        $dst = Join-Path $InstallDir $rel
        $dstDir = Split-Path -Parent $dst
        if (-not (Test-Path $dstDir)) { New-Item -ItemType Directory -Force -Path $dstDir | Out-Null }
        try {
            Copy-Item $_.FullName $dst -Force -ErrorAction Stop
        } catch {
            Write-Line "  [..] Pulando (em uso): $rel" 'DarkYellow'
        }
    }
} catch {
    Write-Line "  [ERRO] Falha ao instalar: $_" 'Red'
    Write-Line '  Dica: feche o injetor e o FiveM antes de atualizar.' 'Red'
    Read-Host "`n  Enter para sair"
    exit 1
}

Set-Content -Path $VersionFile -Value $remoteVer
Remove-Item $tmpZip -Force -ErrorAction SilentlyContinue
Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Line ''
Write-Line "  [OK] Atualizado para a versao $remoteVer !" 'Green'
Write-Line ''
Read-Host '  Enter para sair'
exit 0
