<#
.SYNOPSIS
    SushiRuntime one-script installer (Windows).

.DESCRIPTION
    Bootstraps Python, Git, CMake, and Ninja — using winget when available,
    direct downloads otherwise (no Microsoft Store required). Then installs the
    `sr` CLI and runs `sr setup all`, which installs the lean intel-llvm +
    AdaptiveCpp SYCL toolchains (mirroring the Dockerfile). GPU/CUDA deps are
    included by default; Intel oneAPI (heavy) is opt-in via -OneApi.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File install.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File install.ps1 -NoGpu -OneApi

.EXAMPLE
    irm https://raw.githubusercontent.com/sushisystems/sushiruntime/main/install.ps1 | iex
#>
[CmdletBinding()]
param(
    [switch]$NoGpu,
    [switch]$OneApi,
    [switch]$Gpu,     # Back-compat no-op: GPU is on by default now.
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# Only Intel oneAPI needs administrator rights, and it is opt-in. Self-elevate
# (once, at a single UAC prompt) only when -OneApi is requested; the default
# intel-llvm/AdaptiveCpp install runs fine as a normal user.
if ($OneApi) {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "[INFO] Requesting administrator privileges (required for Intel oneAPI)..." -ForegroundColor Cyan
        $psArgs = [System.Collections.Generic.List[string]]@(
            "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-OneApi"
        )
        if ($NoGpu)  { $psArgs.Add("-NoGpu") }
        if ($DryRun) { $psArgs.Add("-DryRun") }
        $proc = Start-Process powershell -Verb RunAs -ArgumentList $psArgs -Wait -PassThru
        exit $proc.ExitCode
    }
}

function Info($m) { Write-Host "[INFO] $m"  -ForegroundColor Cyan }
function Warn($m) { Write-Host "[WARN] $m"  -ForegroundColor Yellow }
function Fail($m) { Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

$RepoUrl  = if ($env:SR_REPO_URL) { $env:SR_REPO_URL } else { "https://github.com/sushisystems/sushiruntime.git" }
$ToolsDir = Join-Path $env:LOCALAPPDATA "SushiRuntime\tools"
if (-not (Test-Path $ToolsDir)) { New-Item -ItemType Directory -Force $ToolsDir | Out-Null }

function Refresh-Path {
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("Path","User")   + ";" +
                $ToolsDir
}

function GitHub-LatestUrl($repo, $assetGlob) {
    $rel = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest"
    $asset = $rel.assets | Where-Object { $_.name -like $assetGlob } | Select-Object -First 1
    if (-not $asset) { Fail "No asset matching '$assetGlob' in $repo latest release." }
    return $asset.browser_download_url
}

function Download($url, $dest) {
    Info "Downloading $(Split-Path $dest -Leaf)..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    (New-Object Net.WebClient).DownloadFile($url, $dest)
}

function Ensure-Python {
    Refresh-Path
    if (Get-Command python -ErrorAction SilentlyContinue) { return }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Info "Installing Python via winget..."
        winget install --id Python.Python.3.12 -e --accept-package-agreements --accept-source-agreements --silent
        Refresh-Path
        if (Get-Command python -ErrorAction SilentlyContinue) { return }
    }

    $url  = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe"
    $dest = Join-Path $env:TEMP "python-setup.exe"
    Download $url $dest
    Info "Installing Python (user scope, prepend PATH)..."
    Start-Process -Wait -FilePath $dest -ArgumentList "/quiet","InstallAllUsers=0","PrependPath=1","Include_pip=1"
    Refresh-Path
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        Fail "Python installation failed. Open a new terminal and re-run."
    }
}

function Ensure-Git {
    Refresh-Path
    if (Get-Command git -ErrorAction SilentlyContinue) { return }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Info "Installing Git via winget..."
        winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements --silent
        Refresh-Path
        if (Get-Command git -ErrorAction SilentlyContinue) { return }
    }

    $url  = GitHub-LatestUrl "git-for-windows/git" "*64-bit.exe"
    $dest = Join-Path $env:TEMP "git-setup.exe"
    Download $url $dest
    Info "Installing Git..."
    Start-Process -Wait -FilePath $dest -ArgumentList "/VERYSILENT","/NORESTART","/NOCANCEL","/SP-","/CLOSEAPPLICATIONS","/RESTARTAPPLICATIONS","/COMPONENTS=icons,ext\reg\shellhere,assoc,assoc_sh"
    Refresh-Path
}

function Ensure-CMake {
    Refresh-Path
    if (Get-Command cmake -ErrorAction SilentlyContinue) { return }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Info "Installing CMake via winget..."
        winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements --silent
        Refresh-Path
        if (Get-Command cmake -ErrorAction SilentlyContinue) { return }
    }

    $url  = GitHub-LatestUrl "Kitware/CMake" "*windows-x86_64.msi"
    $dest = Join-Path $env:TEMP "cmake-setup.msi"
    Download $url $dest
    Info "Installing CMake..."
    Start-Process -Wait msiexec -ArgumentList "/i","$dest","/quiet","/norestart","ADD_CMAKE_TO_PATH=System"
    Refresh-Path
}

function Ensure-Ninja {
    Refresh-Path
    if (Get-Command ninja -ErrorAction SilentlyContinue) { return }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Info "Installing Ninja via winget..."
        winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements --silent
        Refresh-Path
        if (Get-Command ninja -ErrorAction SilentlyContinue) { return }
    }

    $url  = GitHub-LatestUrl "ninja-build/ninja" "ninja-win.zip"
    $zip  = Join-Path $env:TEMP "ninja-win.zip"
    Download $url $zip
    Info "Extracting Ninja to $ToolsDir..."
    Expand-Archive -Path $zip -DestinationPath $ToolsDir -Force
    Refresh-Path

    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        $env:Path += ";$ToolsDir"
    }
}

# Bootstrap tools.
Ensure-Python
Ensure-Git
Ensure-CMake
Ensure-Ninja

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { Fail "python not on PATH after install. Open a new terminal and re-run." }

# Locate or clone the repo.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { $null }
if ($ScriptDir -and (Test-Path (Join-Path $ScriptDir "CMakeLists.txt"))) {
    $RepoDir = $ScriptDir
} else {
    $RepoDir = if ($env:SR_REPO_DIR) { $env:SR_REPO_DIR } else { Join-Path $HOME "sushiruntime" }
    if (-not (Test-Path (Join-Path $RepoDir ".git"))) {
        Info "Cloning $RepoUrl -> $RepoDir"
        git clone $RepoUrl $RepoDir
    }
}
Set-Location $RepoDir
Info "Repo: $RepoDir"

# Install the sr CLI.
Info "Installing the sr CLI..."
python cli/install.py

# Run the installer pipeline. GPU is on by default ("stock"); --no-gpu skips it.
$flags = @("setup", "all")
if ($NoGpu)  { $flags += "--no-gpu" } else { $flags += "--gpu" }
if ($OneApi) { $flags += "--oneapi" }
if ($DryRun) { $flags += "--dry-run" }
Info "Running: sr $($flags -join ' ')"
$PipxBinDir = python -m pipx environment --value PIPX_BIN_DIR
$SrCmd = Join-Path $PipxBinDir "sr.exe"
if (-not (Test-Path $SrCmd)) { $SrCmd = "sr" }
& $SrCmd @flags
$srExit = $LASTEXITCODE
if ($srExit -eq 0) {
    Info "Done. Try: sr project test --suite functional"
} else {
    exit $srExit
}
