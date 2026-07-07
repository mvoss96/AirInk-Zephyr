# Host preview build for the AirInk LVGL UI.
#
# Renders each UI screen to sim/out/<name>.png and <name>.bmp without Zephyr or
# flashing. Run:
#   powershell -ExecutionPolicy Bypass -File build.ps1        (from cmd)
#   ./build.ps1                                               (from PowerShell)
# Requires a host GCC. Cross-platform (Windows/Linux/macOS) under PowerShell 7.
#   Windows: defaults to MSYS2 MinGW + NCS LVGL.
#   Linux/macOS: defaults to gcc/g++ from PATH; pass -Lvgl <path-to-lvgl>.
# LVGL is cached after the first build; -Clean forces a full rebuild.
param(
    [string]$Gcc,
    [string]$Gpp,
    [string]$Lvgl,
    [switch]$Clean   # force a full LVGL rebuild (otherwise LVGL is cached)
)
$ErrorActionPreference = "Stop"

# OS-aware defaults. $IsWindows is $null on Windows PowerShell 5.1 -> treat as Windows.
$onWindows = ($IsWindows -ne $false)
if (-not $Gcc)  { $Gcc  = if ($onWindows) { "C:\msys64\mingw64\bin\gcc.exe" } else { "gcc" } }
if (-not $Gpp)  { $Gpp  = if ($onWindows) { "C:\msys64\mingw64\bin\g++.exe" } else { "g++" } }
if (-not $Lvgl) { $Lvgl = if ($onWindows) { "C:\ncs\v3.3.0\modules\lib\gui\lvgl" }
                          else { "$HOME/ncs/v3.3.0/modules/lib/gui/lvgl" } }

$sim  = $PSScriptRoot
$root = Split-Path $sim -Parent
$src  = Join-Path $root "src"
$out  = Join-Path $sim "out"
$obj  = Join-Path $sim "build"
New-Item -ItemType Directory -Force -Path $out, $obj | Out-Null

$inc = @("-I$Lvgl", "-I$sim", "-I$src")
$def = @("-DLV_CONF_INCLUDE_SIMPLE")
$warn = @("-w")   # LVGL is noisy; we only care about our own code compiling

Write-Host "==> Compiling C++ (UI + sim harness)" -ForegroundColor Cyan
$cppSrc = @(
    (Join-Path $src "display_ui.cpp"),
    (Join-Path $sim "sim.cpp")
)
$cppObj = @()
foreach ($f in $cppSrc) {
    $o = Join-Path $obj ((Split-Path $f -Leaf) + ".o")
    & $Gpp -std=c++17 -O1 @warn @def @inc -c $f -o $o
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $f" }
    $cppObj += $o
}

# LVGL almost never changes, so compile it once into a single cached relocatable
# object (build/lvgl.o) and reuse it. Rebuild only on -Clean, if the cache is
# missing, or if lv_conf.h was touched (that re-shapes the whole LVGL build).
$lvglObj = Join-Path $obj "lvgl.o"
$conf    = Join-Path $sim "lv_conf.h"
$rebuild = $Clean -or -not (Test-Path $lvglObj) -or
           ((Get-Item $conf).LastWriteTime -gt (Get-Item $lvglObj).LastWriteTime)

if ($rebuild) {
    Write-Host "==> Compiling LVGL (cached afterwards)" -ForegroundColor Cyan
    # Exclude only backend drivers (SDL/X11/etc. pull external headers). Everything
    # else under src/ is guarded by LV_USE_* and compiles to ~nothing when disabled.
    # Regex is separator-agnostic so it also excludes on Linux (forward slashes).
    $lvglSrc = Get-ChildItem -Recurse -Path (Join-Path $Lvgl "src") -Filter *.c |
        Where-Object { $_.FullName -notmatch '[\\/]drivers[\\/]' } |
        ForEach-Object { $_.FullName }
    Write-Host "    $($lvglSrc.Count) LVGL .c files"

    # gcc @responsefile to dodge the Windows command-line length limit.
    # NB: gcc unescapes backslashes inside @files, so emit forward slashes.
    $rsp = Join-Path $obj "lvgl_sources.rsp"
    ($lvglSrc | ForEach-Object { '"' + ($_ -replace '\\','/') + '"' }) |
        Set-Content -Path $rsp -Encoding ASCII

    # -r links all the compiled units into one relocatable object we can cache.
    & $Gcc -r -O1 @warn @def @inc "@$rsp" -o $lvglObj
    if ($LASTEXITCODE -ne 0) { throw "LVGL compile failed" }
} else {
    Write-Host "==> Using cached LVGL (build.ps1 -Clean to rebuild)" -ForegroundColor DarkGray
}

Write-Host "==> Linking airink_sim.exe" -ForegroundColor Cyan
$exe = Join-Path $obj "airink_sim.exe"
& $Gcc -O1 $lvglObj @cppObj -lstdc++ -lm -o $exe
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Write-Host "==> Rendering screens" -ForegroundColor Cyan
Push-Location $out
try { & $exe } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "sim run failed" }

Write-Host "==> Done. Images in $out" -ForegroundColor Green
Get-ChildItem $out -Include *.png,*.bmp -Recurse | Select-Object Name, Length
