# Host unit tests for AirInk's pure logic (EMA filter, Li-Ion curve, display rounding).
#
#   ./run.ps1                       run everything
#   ./run.ps1 --list-test-cases     what is there
#   ./run.ps1 -tc="Ema:*"           run one group
#   ./run.ps1 -s                    show successful assertions too
#
# Requires a host GCC, same as sim/build.ps1. Nothing here touches Zephyr, LVGL or the
# board -- for those see sim/ (renders the UI on the PC) and bench/ (firmware for the
# power bench). Exits non-zero when a check fails, so it can gate a commit.
#
# doctest is vendored in third_party/ and included with -isystem, so -Werror applies to
# our code and not to a 300 KB header we do not maintain.
param(
    [string]$Gpp,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$TestArgs
)
$ErrorActionPreference = "Stop"

$onWindows = ($IsWindows -ne $false)
if (-not $Gpp) { $Gpp = if ($onWindows) { "C:\msys64\mingw64\bin\g++.exe" } else { "g++" } }
if (-not (Get-Command $Gpp -ErrorAction SilentlyContinue)) {
    throw "g++ not found at '$Gpp'. Pass -Gpp <path-to-g++>."
}

$here = $PSScriptRoot
$root = Split-Path $here -Parent
$out = Join-Path $here "out"
New-Item -ItemType Directory -Force $out | Out-Null

$exe = Join-Path $out "tests.exe"
$sources = Get-ChildItem -Path $here -Filter "*.cpp" | ForEach-Object { $_.FullName }

Write-Host "==> Compiling $($sources.Count) test files"
& $Gpp -std=c++17 -Wall -Wextra -Werror -O1 `
    -I"$root\src" -isystem "$here\third_party" `
    $sources -o $exe
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

Write-Host "==> Running`n"
& $exe @TestArgs
$rc = $LASTEXITCODE
if ($rc -ne 0) { Write-Host "`nTESTS FAILED" -ForegroundColor Red }
else { Write-Host "`nOK" -ForegroundColor Green }
exit $rc
