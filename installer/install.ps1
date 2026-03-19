param(
    [string]$SourceDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

# Check admin elevation — exit 3 per spec if not admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "Must be run as administrator"
    exit 3
}

$InstallDir = "$env:ProgramFiles\HEICconvert"
$LogDir = "$env:ProgramData\HEICconvert"
$LogFile = "$LogDir\install.log"

function Write-Log($msg) {
    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    $line = "$timestamp  $msg"
    if (!(Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
    Add-Content -Path $LogFile -Value $line
}

try {
    Write-Log "=== Install started ==="

    # Stop Explorer to release DLL locks
    Write-Log "Stopping explorer.exe"
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    # Copy ALL DLLs — vcpkg may name them heif.dll or libheif.dll, etc.
    Write-Log "Copying files to $InstallDir"
    if (!(Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null }
    Get-ChildItem "$SourceDir\*.dll" | Copy-Item -Destination "$InstallDir\" -Force

    # Register COM DLL
    Write-Log "Registering heic_wic.dll"
    $regResult = Start-Process -FilePath regsvr32 -ArgumentList '/s', "`"$InstallDir\heic_wic.dll`"" -Wait -PassThru
    if ($regResult.ExitCode -ne 0) {
        Write-Log "ERROR: regsvr32 failed with exit code $($regResult.ExitCode)"
        Start-Process explorer
        exit 1
    }

    # Restart Explorer
    Write-Log "Restarting explorer.exe"
    Start-Process explorer

    # Clear thumbnail cache
    Write-Log "Clearing thumbnail cache"
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache_*.db" -Force -ErrorAction SilentlyContinue

    Write-Log "=== Install completed successfully ==="
    exit 0
}
catch {
    Write-Log "ERROR: $($_.Exception.Message)"
    Start-Process explorer -ErrorAction SilentlyContinue
    exit 2
}
