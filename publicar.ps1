# ============================================================================
#  C4pt0r - Script de publicacao (uso do DESENVOLVEDOR, nao vai pro cliente)
#
#  Uso:  powershell -ExecutionPolicy Bypass -File publicar.ps1 [-Version 1.0.1]
#
#  Faz:
#    1. Build Release|x64 da solucao
#    2. Monta o ZIP do cliente (so o necessario, sem nada de git/fonte)
#    3. Gera C4pt0r-Update.zip + version.txt
#    4. Faz push pro repo publico de updates (c4pt0r-mod-updates)
# ============================================================================
param(
    [string]$Version = '',
    [switch]$NoPush
)

$ErrorActionPreference = 'Stop'
$Root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $Root 'build'
$DistDir   = Join-Path $Root 'dist'
$OutDir    = Join-Path $DistDir 'out'
$StageDir  = Join-Path $OutDir 'stage'
$RepoDir   = Join-Path $DistDir 'updates-repo'
$MSBuild   = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
$UpdatesGit = 'https://github.com/daflones/c4pt0r-mod-updates.git'

# --- Versao ------------------------------------------------------------------
$verFile = Join-Path $DistDir 'version.txt'
if (-not $Version) {
    $cur = (Get-Content $verFile -Raw).Trim()
    $Version = Read-Host "Versao atual: $cur - nova versao (Enter = mesma)"
    if (-not $Version) { $Version = $cur }
}
Set-Content $verFile $Version
Write-Host "[C4pt0r] Publicando versao $Version" -ForegroundColor Cyan

# --- 1. Build -----------------------------------------------------------------
Write-Host '[1/4] Compilando Release|x64...' -ForegroundColor Yellow
& $MSBuild (Join-Path $Root 'C4pt0r-Cheats.sln') `
    -p:Configuration=Release -p:Platform=x64 `
    -p:PlatformToolset=v145 -p:WindowsTargetPlatformVersion=10.0.26100.0 `
    -m -nologo -v:m
if ($LASTEXITCODE -ne 0) { throw 'Build falhou.' }

# --- 2. Montar pacote ----------------------------------------------------------
Write-Host '[2/4] Montando pacote do cliente...' -ForegroundColor Yellow
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir | Out-Null

Copy-Item (Join-Path $BuildDir 'C4pt0r-Injector.exe') (Join-Path $StageDir 'C4pt0r-Loader.exe')
Copy-Item (Join-Path $DistDir  'Atualizar.bat')       $StageDir
Copy-Item (Join-Path $DistDir  'updater.ps1')         $StageDir
Copy-Item (Join-Path $DistDir  'LEIA-ME.txt')         $StageDir
Copy-Item $verFile (Join-Path $StageDir 'version.txt')
# ATENCAO: a DLL NAO vai no pacote — o loader baixa ela do Supabase
# depois do login. Sem licenca, ninguem tem acesso ao payload.

$clientZip = Join-Path $OutDir "C4pt0r-Mod-Menu-v$Version.zip"
$updateZip = Join-Path $OutDir 'C4pt0r-Update.zip'
foreach ($z in @($clientZip, $updateZip)) {
    if (Test-Path $z) { Remove-Item $z -Force }
    Compress-Archive -Path "$StageDir\*" -DestinationPath $z -Force
}
Write-Host "  ZIP do cliente: $clientZip" -ForegroundColor Green

# --- 3/4. Push pro repo de updates ---------------------------------------------
if ($NoPush) {
    Write-Host '[3/4] -NoPush: pulando envio pro repo de updates.' -ForegroundColor Yellow
} else {
    Write-Host '[3/4] Enviando update pro repo publico...' -ForegroundColor Yellow
    if (-not (Test-Path (Join-Path $RepoDir '.git'))) {
        if (Test-Path $RepoDir) { Remove-Item $RepoDir -Recurse -Force }
        git clone $UpdatesGit $RepoDir | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [ERRO] Nao consegui clonar $UpdatesGit" -ForegroundColor Red
            Write-Host '  Crie o repo publico "c4pt0r-mod-updates" no GitHub antes.' -ForegroundColor Red
            throw 'Repo de updates inexistente.'
        }
    }
    Copy-Item $updateZip (Join-Path $RepoDir 'C4pt0r-Update.zip') -Force
    Set-Content (Join-Path $RepoDir 'version.txt') $Version

    Push-Location $RepoDir
    git add -A
    git commit -m "release v$Version" | Out-Null
    git push origin HEAD
    $pushOk = ($LASTEXITCODE -eq 0)
    Pop-Location
    if (-not $pushOk) { throw 'Push pro repo de updates falhou.' }
    Write-Host '  Update publicado!' -ForegroundColor Green
}

# --- Upload do payload + pacote pro Supabase Storage -------------------------
if ($env:SUPABASE_SERVICE_ROLE_KEY) {
    Write-Host '[..] Subindo payload pro Supabase Storage...' -ForegroundColor Yellow
    $SU = 'https://bzthdklqvjtutkeiimpq.supabase.co'
    $SH = @{ Authorization = "Bearer $env:SUPABASE_SERVICE_ROLE_KEY"; apikey = $env:SUPABASE_SERVICE_ROLE_KEY }
    Invoke-RestMethod -Method Post -Uri "$SU/storage/v1/object/downloads/loaders/c4pt0r.dll" `
        -Headers ($SH + @{ 'Content-Type' = 'application/octet-stream'; 'x-upsert' = 'true' }) `
        -InFile (Join-Path $BuildDir 'C4pt0r-Cheat.dll') | Out-Null
    Invoke-RestMethod -Method Post -Uri "$SU/storage/v1/object/downloads/loaders/c4pt0r.zip" `
        -Headers ($SH + @{ 'Content-Type' = 'application/zip'; 'x-upsert' = 'true' }) `
        -InFile $updateZip | Out-Null
    Write-Host '  loaders/c4pt0r.dll + c4pt0r.zip atualizados no Storage.' -ForegroundColor Green
} else {
    Write-Host '[..] SUPABASE_SERVICE_ROLE_KEY nao definida — pulando upload do Storage.' -ForegroundColor Yellow
    Write-Host '     (suba manualmente: build/C4pt0r-Cheat.dll -> downloads/loaders/c4pt0r.dll)' -ForegroundColor DarkGray
}

# --- 4/4. Resumo ----------------------------------------------------------------
Write-Host '[4/4] Pronto!' -ForegroundColor Cyan
Write-Host ''
Write-Host "  Distribua apenas este arquivo aos clientes:" -ForegroundColor White
Write-Host "    $clientZip" -ForegroundColor White
Write-Host ''
