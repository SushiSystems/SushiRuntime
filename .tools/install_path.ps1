$toolPath = $PSScriptRoot
Write-Host "[INFO] SushiRuntime Manager Path: $toolPath" -ForegroundColor Blue

# 1. Update User PATH persistently
$oldPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($oldPath -notlike "*$toolPath*") {
    $newPath = $oldPath + ";" + $toolPath
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "[SUCCESS] Path added to User environment variables permanently." -ForegroundColor Green
} else {
    Write-Host "[INFO] Path already exists in User environment variables." -ForegroundColor Cyan
}

# 2. Update CURRENT SESSION path immediately
$env:Path += ";$toolPath"
Write-Host "[SUCCESS] Path updated for the current session. You can now use 'sr'!" -ForegroundColor Green

Write-Host "`nTry running: sr --help" -ForegroundColor Yellow
