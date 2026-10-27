<#
.SYNOPSIS
  Installs (or removes) the SDL controller shim for DmC: Devil May Cry.

.DESCRIPTION
  Copies xinput1_3.dll, dinput8.dll, dmc_sdl.ini and gamecontrollerdb.txt next to
  DMC-DevilMayCry.exe. Any existing copy of either DLL in that folder is backed up
  as <name>.dmc-backup and restored by -Uninstall.

.PARAMETER GameDir
  The game's install folder (or any parent of it). Auto-detected from Steam if omitted.

.PARAMETER Uninstall
  Remove the shim and restore any backup.

.PARAMETER Force
  Overwrite an existing dmc_sdl.ini (install) / also delete it (uninstall).

.EXAMPLE
  .\install.ps1
  .\install.ps1 -GameDir "D:\Games\DmC Devil May Cry"
  .\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [switch]$Uninstall,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$ExeName = 'DMC-DevilMayCry.exe'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Get-SteamLibraries {
    $roots = @()
    foreach ($key in 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKCU:\Software\Valve\Steam') {
        $p = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($p) { $roots += @($p.InstallPath, $p.SteamPath) | Where-Object { $_ } }
    }
    $libs = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $libs += $r
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    $libs | Select-Object -Unique
}

function Find-GameExe([string]$dir) {
    Get-ChildItem -Path $dir -Filter $ExeName -Recurse -Depth 3 -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

# --- locate the game ---------------------------------------------------------
$exe = $null
if ($GameDir) {
    $exe = Find-GameExe $GameDir
} else {
    foreach ($lib in Get-SteamLibraries) {
        $candidate = Join-Path $lib 'steamapps\common\DmC Devil May Cry'
        if (Test-Path $candidate) { $exe = Find-GameExe $candidate; if ($exe) { break } }
    }
}
if (-not $exe) { throw "Could not find $ExeName. Re-run with -GameDir <folder>." }
$Dest = $exe.DirectoryName
Write-Host "Game folder: $Dest"

$Proxies = 'xinput1_3.dll', 'dinput8.dll'

# --- uninstall ---------------------------------------------------------------
if ($Uninstall) {
    foreach ($name in $Proxies) {
        $dll = Join-Path $Dest $name
        $backup = "$dll.dmc-backup"
        if (Test-Path $dll) { Remove-Item $dll -Force; Write-Host "Removed $name" }
        if (Test-Path $backup) { Move-Item $backup $dll -Force; Write-Host "Restored original $name" }
    }
    foreach ($f in 'dmc_sdl.log', 'dmc_sdl_dinput.log') { Remove-Item (Join-Path $Dest $f) -Force -ErrorAction SilentlyContinue }
    if ($Force) { foreach ($f in 'dmc_sdl.ini', 'gamecontrollerdb.txt') { Remove-Item (Join-Path $Dest $f) -Force -ErrorAction SilentlyContinue } }
    Write-Host 'Uninstalled.'
    return
}

# --- install -----------------------------------------------------------------
function Find-Source([string]$name, [string[]]$extra) {
    foreach ($d in @($Here) + $extra) {
        $p = Join-Path $d $name
        if (Test-Path $p) { return $p }
    }
    return $null
}

foreach ($name in $Proxies) {
    $src = Find-Source $name @((Join-Path $Here '..\build\win32\Release'))
    if (-not $src) {
        if ($name -eq 'xinput1_3.dll') { throw "$name not found next to this script or in build\win32\Release." }
        Write-Warning "$name not found; skipping (pads may appear twice in-game without it)."
        continue
    }
    $dll = Join-Path $Dest $name
    $backup = "$dll.dmc-backup"
    if ((Test-Path $dll) -and -not (Test-Path $backup)) {
        Copy-Item $dll $backup
        Write-Host "Backed up existing $name -> $name.dmc-backup"
    }
    Copy-Item $src $dll -Force
    Write-Host "Installed $dll"
}

$srcIni = Find-Source 'dmc_sdl.ini' @((Join-Path $Here '..\config'))
$dstIni = Join-Path $Dest 'dmc_sdl.ini'
if ($srcIni -and ((-not (Test-Path $dstIni)) -or $Force)) { Copy-Item $srcIni $dstIni -Force; Write-Host 'Installed dmc_sdl.ini' }
elseif (Test-Path $dstIni) { Write-Host 'Kept existing dmc_sdl.ini (use -Force to overwrite)' }

$srcDb = Find-Source 'gamecontrollerdb.txt' @((Join-Path $Here '..\data'))
if ($srcDb) { Copy-Item $srcDb (Join-Path $Dest 'gamecontrollerdb.txt') -Force; Write-Host 'Installed gamecontrollerdb.txt' }

Write-Host "`nDone. Launch the game normally through Steam. To undo: .\install.ps1 -Uninstall"
