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
if (-not $Gcc) { $Gcc = if ($onWindows) { "C:\msys64\mingw64\bin\gcc.exe" } else { "gcc" } }
if (-not $Gpp) { $Gpp = if ($onWindows) { "C:\msys64\mingw64\bin\g++.exe" } else { "g++" } }
if (-not $Lvgl) {
    $Lvgl = if ($onWindows) { "C:\ncs\v3.4.0\modules\lib\gui\lvgl" }
    else { "$HOME/ncs/v3.4.0/modules/lib/gui/lvgl" }
}

$sim = $PSScriptRoot
$root = Split-Path $sim -Parent
$src = Join-Path $root "src"
$out = Join-Path $sim "out"
$obj = Join-Path $sim "build"
New-Item -ItemType Directory -Force -Path $out, $obj | Out-Null

$inc = @("-I$Lvgl", "-I$sim", "-I$src")
$def = @("-DLV_CONF_INCLUDE_SIMPLE")
$warn = @("-w")   # LVGL is noisy; we only care about our own code compiling

Write-Host "==> Compiling C++ (UI + sim harness)" -ForegroundColor Cyan
$cppSrc = @(
    (Join-Path $src "ui/display_ui.cpp"),
    (Join-Path $sim "ui_platform_sim.cpp"),
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
$conf = Join-Path $sim "lv_conf.h"
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
    ($lvglSrc | ForEach-Object { '"' + ($_ -replace '\\', '/') + '"' }) |
    Set-Content -Path $rsp -Encoding ASCII

    # -r links all the compiled units into one relocatable object we can cache.
    & $Gcc -r -O1 @warn @def @inc "@$rsp" -o $lvglObj
    if ($LASTEXITCODE -ne 0) { throw "LVGL compile failed" }
}
else {
    Write-Host "==> Using cached LVGL (build.ps1 -Clean to rebuild)" -ForegroundColor DarkGray
}

# UI fonts live in src/fonts/ (single source of truth, shared with the firmware)
# -> cached fonts.o, rebuilt when any of them changes.
$fontObj = Join-Path $obj "fonts.o"
$fontSrc = @(Get-ChildItem -Path (Join-Path $src "fonts") -Filter *.c -ErrorAction SilentlyContinue)
if ($fontSrc.Count -gt 0) {
    $newest = ($fontSrc | Measure-Object -Property LastWriteTime -Maximum).Maximum
    if ($Clean -or -not (Test-Path $fontObj) -or $newest -gt (Get-Item $fontObj).LastWriteTime) {
        Write-Host "==> Compiling $($fontSrc.Count) UI fonts (cached)" -ForegroundColor Cyan
        $frsp = Join-Path $obj "fonts.rsp"
        ($fontSrc | ForEach-Object { '"' + ($_.FullName -replace '\\', '/') + '"' }) |
        Set-Content -Path $frsp -Encoding ASCII
        & $Gcc -r -O1 @warn @def @inc "@$frsp" -o $fontObj
        if ($LASTEXITCODE -ne 0) { throw "font compile failed" }
    }
}

Write-Host "==> Linking airink_sim.exe" -ForegroundColor Cyan
$exe = Join-Path $obj "airink_sim.exe"
$linkObjs = @($lvglObj)
if (Test-Path $fontObj) { $linkObjs += $fontObj }
$linkObjs += $cppObj
& $Gcc -O1 @linkObjs -lstdc++ -lm -o $exe
if ($LASTEXITCODE -ne 0) { throw "link failed" }

# Once per application (see apps/). The two do not draw the same device -- the Matter build names
# itself on the splash and has two extra menu rows with screens behind them -- and a screen only one
# build has is exactly the screen a mockup is needed for.
foreach ($variant in @("standalone", "matter")) {
    $dir = Join-Path $out $variant
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Write-Host "==> Rendering screens: $variant" -ForegroundColor Cyan
    Push-Location $dir
    try { & $exe $variant } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "sim run failed for $variant" }
}

Write-Host "==> Done. Images in $out" -ForegroundColor Green
Get-ChildItem $out -Include *.png -Recurse | ForEach-Object {
    "{0,-12} {1}" -f $_.Directory.Name, $_.Name
}
