<#
.SYNOPSIS
    SushiRuntime one-script installer (Windows).

.DESCRIPTION
    Bootstraps Python + Git via winget if missing, installs the `sr` CLI, then
    runs `sr setup all`, which installs the remaining dependencies (vcpkg ports +
    toolchain), writes config.local.toml, and builds + smoke-tests.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File install.ps1 -Gpu

.EXAMPLE
    # Bare machine, no checkout yet:
    irm https://raw.githubusercontent.com/sushisystems/sushiruntime/main/install.ps1 | iex
#>
[CmdletBinding()]
param(
    [switch]$Gpu,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Info($m)  { Write-Host "[INFO] $m"  -ForegroundColor Cyan }
function Warn($m)  { Write-Host "[WARN] $m"  -ForegroundColor Yellow }
function Fail($m)  { Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

$RepoUrl = if ($env:SR_REPO_URL) { $env:SR_REPO_URL } else { "https://github.com/sushisystems/sushiruntime.git" }

# 1. winget is the bootstrap package source.
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail "winget not found. Install 'App Installer' from the Microsoft Store, then re-run."
}

# 2. Bootstrap Python + Git if missing.
function Ensure-WingetPackage($id, $cmd) {
    if (Get-Command $cmd -ErrorAction SilentlyContinue) { return }
    Info "Installing $id via winget..."
    winget install --id $id -e --accept-package-agreements --accept-source-agreements
}
Ensure-WingetPackage "Python.Python.3.12" "python"
Ensure-WingetPackage "Git.Git" "git"

# winget may have changed PATH only for new shells; refresh this session.
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path","User")

$python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $python) { Fail "python still not on PATH; open a new terminal and re-run." }

# 3. Locate the repo: a checkout containing this script, else clone it.
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

# 4. Install the `sr` CLI.
Info "Installing the sr CLI..."
python -m pip install --upgrade pip | Out-Null
python -m pip install ./cli

# 5. Run the installer pipeline.
$flags = @("setup", "all")
if ($Gpu)    { $flags += "--gpu" }
if ($DryRun) { $flags += "--dry-run" }
Info "Running: sr $($flags -join ' ')"
python -m sushiruntime @flags

Info "Done. Try: sr project test --suite functional"
