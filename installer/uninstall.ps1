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
    Write-Log "=== Uninstall started ==="

    # Stop Explorer
    Write-Log "Stopping explorer.exe"
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    # Unregister COM DLL
    if (Test-Path "$InstallDir\heic_wic.dll") {
        Write-Log "Unregistering heic_wic.dll"
        Start-Process -FilePath regsvr32 -ArgumentList '/u', '/s', "`"$InstallDir\heic_wic.dll`"" -Wait
    }

    # Remove files
    Write-Log "Removing $InstallDir"
    try {
        Remove-Item $InstallDir -Recurse -Force
    }
    catch {
        Write-Log "WARN: Could not remove $InstallDir (may be locked). Scheduling removal on reboot."
        Add-Type @"
            using System;
            using System.Runtime.InteropServices;
            public class FileOps {
                [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
                public static extern bool MoveFileEx(string lpExistingFileName, string lpNewFileName, int dwFlags);
            }
"@
        Get-ChildItem $InstallDir -File | ForEach-Object {
            [FileOps]::MoveFileEx($_.FullName, $null, 4) | Out-Null
        }
        [FileOps]::MoveFileEx($InstallDir, $null, 4) | Out-Null
    }

    # Restart Explorer
    Write-Log "Restarting explorer.exe"
    Start-Process explorer

    # Clear thumbnail cache
    Write-Log "Clearing thumbnail cache"
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache_*.db" -Force -ErrorAction SilentlyContinue

    Write-Log "=== Uninstall completed ==="
    exit 0
}
catch {
    Write-Log "ERROR: $($_.Exception.Message)"
    Start-Process explorer -ErrorAction SilentlyContinue
    exit 1
}
