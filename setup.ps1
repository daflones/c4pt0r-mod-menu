# ============================================================================
#  C4pt0r Cheats — Full Automated Setup
#  Checks/installs: Git, VS Build Tools, ImGui
#  Downloads dependencies, builds project, outputs ready binaries
# ============================================================================

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"  # faster downloads

# ---- Config --------------------------------------------------------------
$IMGUI_VERSION  = "v1.91.8"
$IMGUI_URL      = "https://github.com/ocornut/imgui/archive/refs/tags/$IMGUI_VERSION.zip"
$REPO_URL       = "https://github.com/daflones/c4pt0r-mod-menu.git"
$PROJECT_DIR    = $PSScriptRoot
$BUILD_DIR      = Join-Path $PROJECT_DIR "build"
$CHEAT_DIR      = Join-Path $PROJECT_DIR "cheat"
$IMGUI_DEST     = Join-Path $CHEAT_DIR "deps\imgui"

# ---- Colors / Banner ----------------------------------------------------
function Write-C4 { param([string]$Text, [string]$Color = "Cyan")
    Write-Host $Text -ForegroundColor $Color
}
function Write-Ok    { param([string]$T) Write-Host "  [OK] " -ForegroundColor Green -NoNewline; Write-Host $T }
function Write-Info  { param([string]$T) Write-Host "  [..] " -ForegroundColor Cyan -NoNewline; Write-Host $T }
function Write-Warn  { param([string]$T) Write-Host "  [!!] " -ForegroundColor Yellow -NoNewline; Write-Host $T }
function Write-Err   { param([string]$T) Write-Host "  [XX] " -ForegroundColor Red -NoNewline; Write-Host $T }
function Write-Step  { param([int]$N, [string]$T)
    Write-Host ""
    Write-Host "  [$N] " -ForegroundColor Cyan -NoNewline
    Write-Host $T -ForegroundColor White
    Write-Host ("  " + "-" * 50) -ForegroundColor DarkGray
}

Write-Host ""
Write-C4 "   ██████╗██╗  ██╗██████╗ ████████╗ ██████╗ ██████╗ "
Write-C4 "  ██╔════╝██║  ██║██╔══██╗╚══██╔══╝██╔═══██╗██╔══██╗"
Write-C4 "  ██║     ███████║██████╔╝   ██║   ██║   ██║██████╔╝"
Write-C4 "  ██║     ╚════██║██╔═══╝    ██║   ██║   ██║██╔══██╗"
Write-C4 "  ╚██████╗     ██║██║        ██║   ╚██████╔╝██║  ██║"
Write-C4 "   ╚═════╝     ╚═╝╚═╝        ╚═╝    ╚═════╝ ╚═╝  ╚═╝"
Write-C4 "              C H E A T S  —  S E T U P" "White"
Write-Host ""

# ---- Check admin ---------------------------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Err "Execute como Administrador! Use o setup.bat"
    Read-Host "Pressione Enter para sair"
    exit 1
}
Write-Ok "Rodando como Administrador"

# ===========================================================================
#  STEP 1: Git
# ===========================================================================
Write-Step 1 "Verificando Git..."

$gitPath = Get-Command git -ErrorAction SilentlyContinue
if ($gitPath) {
    $gitVer = & git --version 2>$null
    Write-Ok "Git encontrado: $gitVer"
} else {
    Write-Info "Git nao encontrado. Instalando..."

    # Try winget first
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        Write-Info "Instalando via winget..."
        winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements --silent
        if ($LASTEXITCODE -eq 0) {
            Write-Ok "Git instalado via winget"
        } else {
            Write-Warn "winget falhou, baixando instalador direto..."
            $gitInstaller = Join-Path $env:TEMP "Git-Setup.exe"
            $gitUrl = "https://github.com/git-for-windows/git/releases/download/v2.47.0.windows.2/Git-2.47.0.2-64-bit.exe"
            Invoke-WebRequest -Uri $gitUrl -OutFile $gitInstaller
            Start-Process -FilePath $gitInstaller -ArgumentList "/VERYSILENT /NORESTART /NOCANCEL /SP-" -Wait
            Write-Ok "Git instalado"
            Remove-Item $gitInstaller -ErrorAction SilentlyContinue
        }
    } else {
        Write-Info "winget nao disponivel, baixando Git direto..."
        $gitInstaller = Join-Path $env:TEMP "Git-Setup.exe"
        $gitUrl = "https://github.com/git-for-windows/git/releases/download/v2.47.0.windows.2/Git-2.47.0.2-64-bit.exe"
        Invoke-WebRequest -Uri $gitUrl -OutFile $gitInstaller
        Start-Process -FilePath $gitInstaller -ArgumentList "/VERYSILENT /NORESTART /NOCANCEL /SP-" -Wait
        Write-Ok "Git instalado"
        Remove-Item $gitInstaller -ErrorAction SilentlyContinue
    }

    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("Path", "User")
}

# ===========================================================================
#  STEP 2: Visual Studio Build Tools 2022
# ===========================================================================
Write-Step 2 "Verificando Visual Studio Build Tools..."

# Find vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsFound = $false
$msbuildPath = ""

if (Test-Path $vswhere) {
    $vsInstalls = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($vsInstalls) {
        $vsPath = $vsInstalls | Select-Object -First 1
        # Find MSBuild
        $msbuildCandidates = @(
            "$vsPath\MSBuild\Current\Bin\amd64\MSBuild.exe",
            "$vsPath\MSBuild\Current\Bin\MSBuild.exe"
        )
        foreach ($candidate in $msbuildCandidates) {
            if (Test-Path $candidate) {
                $msbuildPath = $candidate
                $vsFound = $true
                break
            }
        }
    }
}

if ($vsFound) {
    Write-Ok "MSBuild encontrado: $msbuildPath"
} else {
    Write-Info "Visual Studio Build Tools nao encontrado. Instalando..."
    Write-Warn "ISSO VAI DEMORAR (5-15 min). Nao feche esta janela!"
    Write-Host ""

    $vsInstaller = Join-Path $env:TEMP "vs_BuildTools.exe"
    $vsUrl = "https://aka.ms/vs/17/release/vs_BuildTools.exe"

    Write-Info "Baixando VS Build Tools 2022..."
    Invoke-WebRequest -Uri $vsUrl -OutFile $vsInstaller

    Write-Info "Instalando (C++ desktop workload + Windows SDK)..."
    $vsArgs = @(
        "--quiet", "--wait", "--norestart",
        "--add", "Microsoft.VisualStudio.Workload.VCTools",
        "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "--add", "Microsoft.VisualStudio.Component.Windows11SDK.22621",
        "--includeRecommended"
    )
    $proc = Start-Process -FilePath $vsInstaller -ArgumentList $vsArgs -Wait -PassThru
    Remove-Item $vsInstaller -ErrorAction SilentlyContinue

    if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) {
        Write-Ok "VS Build Tools instalado!"

        # Re-find MSBuild
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null | Select-Object -First 1
            $msbuildCandidates = @(
                "$vsPath\MSBuild\Current\Bin\amd64\MSBuild.exe",
                "$vsPath\MSBuild\Current\Bin\MSBuild.exe"
            )
            foreach ($candidate in $msbuildCandidates) {
                if (Test-Path $candidate) {
                    $msbuildPath = $candidate
                    $vsFound = $true
                    break
                }
            }
        }

        if (-not $vsFound) {
            Write-Err "MSBuild nao encontrado apos instalacao."
            Write-Err "Tente reiniciar o PC e rodar setup.bat de novo."
            Read-Host "Enter para sair"
            exit 1
        }
    } else {
        Write-Err "Instalacao do VS Build Tools falhou (exit code: $($proc.ExitCode))"
        Write-Err "Instale manualmente: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
        Read-Host "Enter para sair"
        exit 1
    }
}

# ===========================================================================
#  STEP 3: Git Pull (check for updates)
# ===========================================================================
Write-Step 3 "Verificando repositorio Git..."

if ($REPO_URL -ne "" -and (Test-Path (Join-Path $PROJECT_DIR ".git"))) {
    Write-Info "Puxando atualizacoes do repositorio..."
    Push-Location $PROJECT_DIR
    try {
        $pullResult = & git pull origin main 2>&1
        Write-Ok "Repositorio atualizado: $pullResult"
    } catch {
        Write-Warn "git pull falhou (pode ser que nao tenha remote configurado)"
    }
    Pop-Location
} elseif ($REPO_URL -ne "" -and -not (Test-Path (Join-Path $PROJECT_DIR ".git"))) {
    Write-Info "Inicializando repositorio Git..."
    Push-Location $PROJECT_DIR
    & git init
    & git remote add origin $REPO_URL 2>$null
    Write-Ok "Repositorio inicializado"
    Pop-Location
} else {
    Write-Info "Nenhuma URL de repositorio configurada (edite REPO_URL no setup.ps1)"
    Write-Info "Pulando verificacao de atualizacoes..."
}

# ===========================================================================
#  STEP 4: Download ImGui
# ===========================================================================
Write-Step 4 "Verificando Dear ImGui..."

$imguiCheck = Join-Path $IMGUI_DEST "imgui.h"
if (Test-Path $imguiCheck) {
    Write-Ok "ImGui ja presente em deps/imgui/"
} else {
    Write-Info "Baixando Dear ImGui $IMGUI_VERSION..."

    $tempZip = Join-Path $env:TEMP "imgui-download.zip"
    $tempExtract = Join-Path $env:TEMP "imgui-extract"

    # Download
    Invoke-WebRequest -Uri $IMGUI_URL -OutFile $tempZip
    Write-Ok "Download completo"

    # Extract
    if (Test-Path $tempExtract) { Remove-Item $tempExtract -Recurse -Force }
    Expand-Archive -Path $tempZip -DestinationPath $tempExtract -Force

    # Find extracted folder (imgui-1.91.8 or similar)
    $extractedDir = Get-ChildItem $tempExtract -Directory | Select-Object -First 1

    # Create destination
    if (-not (Test-Path $IMGUI_DEST)) {
        New-Item -ItemType Directory -Path $IMGUI_DEST -Force | Out-Null
    }
    $backendsDir = Join-Path $IMGUI_DEST "backends"
    if (-not (Test-Path $backendsDir)) {
        New-Item -ItemType Directory -Path $backendsDir -Force | Out-Null
    }

    # Copy core files
    $coreFiles = @(
        "imgui.h", "imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp",
        "imgui_widgets.cpp", "imgui_internal.h", "imconfig.h",
        "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h"
    )
    foreach ($f in $coreFiles) {
        $src = Join-Path $extractedDir.FullName $f
        if (Test-Path $src) {
            Copy-Item $src -Destination $IMGUI_DEST -Force
        }
    }

    # Copy backend files
    $backendFiles = @(
        "imgui_impl_dx11.h", "imgui_impl_dx11.cpp",
        "imgui_impl_win32.h", "imgui_impl_win32.cpp"
    )
    foreach ($f in $backendFiles) {
        $src = Join-Path $extractedDir.FullName "backends\$f"
        if (Test-Path $src) {
            Copy-Item $src -Destination $backendsDir -Force
        }
    }

    # Cleanup
    Remove-Item $tempZip -ErrorAction SilentlyContinue
    Remove-Item $tempExtract -Recurse -ErrorAction SilentlyContinue

    Write-Ok "ImGui $IMGUI_VERSION instalado em cheat/deps/imgui/"
}

# ===========================================================================
#  STEP 5: Build
# ===========================================================================
Write-Step 5 "Compilando projeto..."

$slnPath = Join-Path $PROJECT_DIR "C4pt0r-Cheats.sln"

if (-not (Test-Path $slnPath)) {
    Write-Err "Solution file nao encontrado: $slnPath"
    Write-Err "Verifique se todos os arquivos do projeto estao presentes."
    Read-Host "Enter para sair"
    exit 1
}

# Create build directory
if (-not (Test-Path $BUILD_DIR)) {
    New-Item -ItemType Directory -Path $BUILD_DIR -Force | Out-Null
}

Write-Info "MSBuild: $msbuildPath"
Write-Info "Solution: $slnPath"
Write-Info "Compilando Release|x64..."
Write-Host ""

# Build
$buildArgs = @(
    "`"$slnPath`"",
    "/p:Configuration=Release",
    "/p:Platform=x64",
    "/m",           # parallel build
    "/v:minimal",   # minimal verbosity
    "/nologo"
)

$buildResult = & $msbuildPath $slnPath /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo 2>&1
$buildExit = $LASTEXITCODE

Write-Host ""

if ($buildExit -eq 0) {
    Write-Ok "Build concluido com sucesso!"
} else {
    Write-Err "Build falhou! Verifique os erros acima."
    Write-Host ""
    Write-Host ($buildResult | Out-String) -ForegroundColor DarkGray
    Write-Host ""
    Write-Warn "Erros comuns:"
    Write-Warn "  - Windows SDK nao encontrado → reinstale Build Tools"
    Write-Warn "  - ImGui headers nao encontrados → rode setup.bat de novo"
    Read-Host "Enter para sair"
    exit 1
}

# ===========================================================================
#  STEP 6: Verify output
# ===========================================================================
Write-Step 6 "Verificando binarios..."

$injectorExe = Join-Path $BUILD_DIR "C4pt0r-Injector.exe"
$cheatDll    = Join-Path $BUILD_DIR "C4pt0r-Cheat.dll"

$allGood = $true

if (Test-Path $injectorExe) {
    $size = (Get-Item $injectorExe).Length / 1KB
    Write-Ok "Injector: C4pt0r-Injector.exe ($([math]::Round($size))KB)"
} else {
    # Try alternate output paths
    $altPaths = @(
        (Join-Path $PROJECT_DIR "injector\Release\C4pt0r-Injector.exe"),
        (Join-Path $PROJECT_DIR "x64\Release\C4pt0r-Injector.exe")
    )
    $found = $false
    foreach ($alt in $altPaths) {
        if (Test-Path $alt) {
            Copy-Item $alt -Destination $BUILD_DIR -Force
            Write-Ok "Injector copiado para build/"
            $found = $true
            break
        }
    }
    if (-not $found) {
        Write-Err "C4pt0r-Injector.exe nao encontrado"
        $allGood = $false
    }
}

if (Test-Path $cheatDll) {
    $size = (Get-Item $cheatDll).Length / 1KB
    Write-Ok "Cheat DLL: C4pt0r-Cheat.dll ($([math]::Round($size))KB)"
} else {
    $altPaths = @(
        (Join-Path $PROJECT_DIR "cheat\Release\C4pt0r-Cheat.dll"),
        (Join-Path $PROJECT_DIR "x64\Release\C4pt0r-Cheat.dll")
    )
    $found = $false
    foreach ($alt in $altPaths) {
        if (Test-Path $alt) {
            Copy-Item $alt -Destination $BUILD_DIR -Force
            Write-Ok "Cheat DLL copiada para build/"
            $found = $true
            break
        }
    }
    if (-not $found) {
        Write-Err "C4pt0r-Cheat.dll nao encontrado"
        $allGood = $false
    }
}

# ===========================================================================
#  Done
# ===========================================================================
Write-Host ""
Write-Host "  ========================================" -ForegroundColor DarkGray

if ($allGood) {
    Write-Host ""
    Write-C4 "  BUILD COMPLETO!" "Green"
    Write-Host ""
    Write-Host "  Seus arquivos estao em:" -ForegroundColor White
    Write-C4 "    $BUILD_DIR"
    Write-Host ""
    Write-Host "  Como usar:" -ForegroundColor White
    Write-Host "    1. Abra o FiveM e entre num servidor" -ForegroundColor Gray
    Write-Host "    2. Execute C4pt0r-Injector.exe como Admin" -ForegroundColor Gray
    Write-Host "    3. Clique INJETAR" -ForegroundColor Gray
    Write-Host "    4. F9 para abrir o menu no jogo" -ForegroundColor Gray
    Write-Host ""

    # Open build folder
    Write-Info "Abrindo pasta build..."
    Start-Process explorer.exe $BUILD_DIR
} else {
    Write-Host ""
    Write-Err "Alguns arquivos nao foram gerados. Verifique os erros acima."
}

Write-Host ""
