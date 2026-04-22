$toolPath = $PSScriptRoot
Write-Host "[INFO] Removing SushiRuntime Manager Path: $toolPath" -ForegroundColor Blue

# 1. Update User PATH persistently
$oldPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($oldPath -like "*$toolPath*") {
    # Split by semicolon, filter out the toolPath, and join back
    $newPath = ($oldPath.Split(';') | Where-Object { $_ -and $_.Trim() -ne $toolPath }) -join ';'
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "[SUCCESS] Path removed from User environment variables permanently." -ForegroundColor Green
} else {
    Write-Host "[INFO] Path not found in User environment variables." -ForegroundColor Cyan
}

# 2. Update CURRENT SESSION path
$env:Path = ($env:Path.Split(';') | Where-Object { $_ -and $_.Trim() -ne $toolPath }) -join ';'
Write-Host "[SUCCESS] Path removed from the current session." -ForegroundColor Green
