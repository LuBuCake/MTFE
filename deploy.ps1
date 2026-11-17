# Copies the built loader (and optionally plugins) into the game folder.
# Usage: .\deploy.ps1 [-Game <game folder>] [-Plugins <dll>,<dll>...]
# Game folder: -Game, else the RE5_GAME environment variable, else Steam's default library.
param(
    [string]$Game = $(if ($env:RE5_GAME) { $env:RE5_GAME } else { "C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 5" }),
    [string[]]$Plugins = @()
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if (Get-Process re5dx9 -ErrorAction SilentlyContinue) { throw "Close the game first." }

$loader = Join-Path $root "build\windows\x86\releasedbg\dinput8.dll"
if (-not (Test-Path $loader)) { throw "Build the loader first (xmake build)." }

New-Item -ItemType Directory -Force (Join-Path $Game "MTFE\Plugins") | Out-Null
Copy-Item $loader $Game -Force
Copy-Item ($loader -replace "\.dll$", ".pdb") $Game -Force -ErrorAction SilentlyContinue
$ini = Join-Path $Game "MTFE\MTFE.ini"
if (-not (Test-Path $ini)) { Copy-Item (Join-Path $root "dist\MTFE\MTFE.ini") $ini }
Write-Host "loader -> $Game\dinput8.dll"

foreach ($p in $Plugins) {
    Copy-Item $p (Join-Path $Game "MTFE\Plugins") -Force
    Copy-Item ($p -replace "\.dll$", ".pdb") (Join-Path $Game "MTFE\Plugins") -Force -ErrorAction SilentlyContinue
    Write-Host "plugin -> MTFE\Plugins\$(Split-Path $p -Leaf)"
}
