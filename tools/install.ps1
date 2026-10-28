<#
.SYNOPSIS
  Installs (or removes) the SDL controller proxies for DmC: Devil May Cry
  (or another XInput game, with -ExeName / -GameDir).

.DESCRIPTION
  Detects whether the game executable is 32-bit or 64-bit and copies the matching XInput
  proxy, dmc_sdl.ini and gamecontrollerdb.txt next to it. DirectInput gamepads are hidden
  in-process (MinHook) by the XInput proxy itself, so nothing else is installed.
  An existing copy of the XInput DLL is backed up as <name>.dmc-backup and restored by
  -Uninstall. Release zips contain x86\ and x64\ folders; a local build is also found.

.PARAMETER GameDir
  The game's install folder (or any parent of it). Auto-detected from Steam for DmC.

.PARAMETER ExeName
  Game executable to look for. Default: DMC-DevilMayCry.exe. Using another name
  requires -GameDir.

.PARAMETER XInputDll
  XInput proxy file name to install. Default: xinput1_3.dll for 32-bit games,
  xinput1_4.dll for 64-bit games. Check the game's imports with tools\inspect_imports.py;
  the proxy must match, and a build with that name must exist in the package.

.PARAMETER Uninstall
  Remove the proxies and restore any backups.

.PARAMETER Force
  Overwrite an existing dmc_sdl.ini (install) / also delete it (uninstall).

.EXAMPLE
  .\install.ps1
  .\install.ps1 -GameDir "D:\Games\DmC Devil May Cry"
  .\install.ps1 -GameDir "D:\Games\SomeGame" -ExeName SomeGame.exe -XInputDll xinput1_4.dll
  .\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [string]$ExeName = 'DMC-DevilMayCry.exe',
    [string]$XInputDll,
    [switch]$Uninstall,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
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

# Returns 'x86' or 'x64' from the PE header (IMAGE_FILE_MACHINE_I386 / AMD64).
function Get-PeArch([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Seek(0x3C, 'Begin') | Out-Null
        $pe = $br.ReadInt32()
        $fs.Seek($pe + 4, 'Begin') | Out-Null
        $machine = $br.ReadUInt16()
    } finally { $fs.Dispose() }
    switch ($machine) {
        0x14C  { return 'x86' }
        0x8664 { return 'x64' }
        default { throw ('Unsupported executable architecture 0x{0:X}' -f $machine) }
    }
}

# --- locate the game ---------------------------------------------------------
$exe = $null
if ($GameDir) {
    $exe = Find-GameExe $GameDir
} elseif ($ExeName -eq 'DMC-DevilMayCry.exe') {
    foreach ($lib in Get-SteamLibraries) {
        $candidate = Join-Path $lib 'steamapps\common\DmC Devil May Cry'
        if (Test-Path $candidate) { $exe = Find-GameExe $candidate; if ($exe) { break } }
    }
} else {
    throw 'When using -ExeName for another game you must also pass -GameDir.'
}
if (-not $exe) { throw "Could not find $ExeName. Re-run with -GameDir <folder>." }
$Dest = $exe.DirectoryName
$Arch = Get-PeArch $exe.FullName
if (-not $XInputDll) { $XInputDll = if ($Arch -eq 'x86') { 'xinput1_3.dll' } else { 'xinput1_4.dll' } }
Write-Host "Game folder: $Dest  ($Arch executable, XInput proxy: $XInputDll)"

$Proxies = @($XInputDll)

# --- uninstall ---------------------------------------------------------------
if ($Uninstall) {
    # Also clean the other well-known XInput names in case a previous install used one.
    foreach ($name in (@('xinput1_3.dll', 'xinput1_4.dll', 'xinput9_1_0.dll') | Select-Object -Unique)) {
        $dll = Join-Path $Dest $name
        $backup = "$dll.dmc-backup"
        if (Test-Path $backup) { Remove-Item $dll -Force -ErrorAction SilentlyContinue; Move-Item $backup $dll -Force; Write-Host "Restored original $name" }
        elseif (($Proxies -contains $name) -and (Test-Path $dll)) { Remove-Item $dll -Force; Write-Host "Removed $name" }
    }
    foreach ($f in 'dmc_sdl.log') { Remove-Item (Join-Path $Dest $f) -Force -ErrorAction SilentlyContinue }
    if ($Force) { foreach ($f in 'dmc_sdl.ini', 'gamecontrollerdb.txt') { Remove-Item (Join-Path $Dest $f) -Force -ErrorAction SilentlyContinue } }
    Write-Host 'Uninstalled.'
    return
}

# --- install -----------------------------------------------------------------
$buildDir = if ($Arch -eq 'x86') { 'win32' } else { 'win64' }
function Find-Source([string]$name, [string[]]$extra) {
    foreach ($d in $extra) {
        $p = Join-Path $d $name
        if (Test-Path $p) { return $p }
    }
    return $null
}
$dllSearch = @((Join-Path $Here $Arch), $Here, (Join-Path $Here "..\build\$buildDir\Release"))

foreach ($name in $Proxies) {
    $src = Find-Source $name $dllSearch
    if (-not $src) {
        throw "$name ($Arch) not found in this package or in build\$buildDir\Release. A 64-bit game may need a build with a different name: cmake -DDMC_XINPUT_DLL=<name>."

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

$srcIni = Find-Source 'dmc_sdl.ini' @($Here, (Join-Path $Here '..\config'))
$dstIni = Join-Path $Dest 'dmc_sdl.ini'
if ($srcIni -and ((-not (Test-Path $dstIni)) -or $Force)) { Copy-Item $srcIni $dstIni -Force; Write-Host 'Installed dmc_sdl.ini' }
elseif (Test-Path $dstIni) { Write-Host 'Kept existing dmc_sdl.ini (use -Force to overwrite)' }

$srcDb = Find-Source 'gamecontrollerdb.txt' @($Here, (Join-Path $Here '..\data'))
if ($srcDb) { Copy-Item $srcDb (Join-Path $Dest 'gamecontrollerdb.txt') -Force; Write-Host 'Installed gamecontrollerdb.txt' }

Write-Host "`nDone. Launch the game normally. To undo: .\install.ps1 -Uninstall"
