$toolPath = $PSScriptRoot
$permanent = $args -contains "--permanent"

Write-Host "--- SushiRuntime Windows Installer ---" -ForegroundColor Blue

if ($permanent) {
    Write-Host "[INFO] Performing permanent installation..." -ForegroundColor Yellow
    
    # 1. Update User PATH persistently
    $oldPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($oldPath -notlike "*$toolPath*") {
        $delimiter = if ($oldPath.EndsWith(";")) { "" } else { ";" }
        $newPath = $oldPath + $delimiter + $toolPath
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Host "[SUCCESS] Path added to User environment variables permanently." -ForegroundColor Green
    } else {
        Write-Host "[INFO] Path already exists in User environment variables." -ForegroundColor Cyan
    }
} else {
    Write-Host "[INFO] Temporary mode. To install permanently, run: .\install.ps1 --permanent" -ForegroundColor Gray
}

# 2. Always update CURRENT SESSION path immediately
if ($env:Path -notlike "*$toolPath*") {
    $env:Path += ";$toolPath"
    Write-Host "[SUCCESS] Path updated for the current session. You can now use 'sr'!" -ForegroundColor Green
}

Write-Host "`nTry running: sr --help" -ForegroundColor White
