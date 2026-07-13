<#
  AirInk flash + log helper — SWD via SEGGER J-Link (no USB / no bootloader).

  Flashes the built zephyr.hex with `loadfile`, which sector-erases only the app
  region (>= 0x1000) and leaves the MBR + Adafruit bootloader intact (NOT a
  chip-erase). Then resets the core and streams the debug console.

  The repo holds two applications (see apps/): the standalone firmware and the
  Matter-over-Thread one. Pick with -App; they build into separate directories, so
  switching does not force a rebuild of the other.

  The nRF is powered by the PPK2 (battery pins) and the J-Link only debugs
  (VTREF sensed from the 3V3 rail) — so a clean current measurement can run in
  parallel. NOTE: for a *precise* idle measurement, power-cycle the board via the
  PPK2 after flashing instead of relying on this script's J-Link reset, so no
  debug-domain residual is left. Flashing + logs are unaffected.

  Usage:
    pwsh -File tools/flash.ps1                    # flash current standalone build + read COM9 20s
    pwsh -File tools/flash.ps1 -Build             # rebuild first, then flash
    pwsh -File tools/flash.ps1 -App matter -Build # the Matter firmware instead
    pwsh -File tools/flash.ps1 -LogSeconds 65     # span a full measurement cycle
    pwsh -File tools/flash.ps1 -LogPort COM12     # logs via the ESP bridge instead
#>
param(
  [ValidateSet("standalone","matter")]
  [string]$App = "standalone",
  [switch]$Build,
  [int]$LogSeconds = 20,
  [string]$LogPort = "COM9",
  [int]$Speed = 4000
)
$ErrorActionPreference = "Stop"
$root     = Split-Path $PSScriptRoot -Parent
$appDir   = Join-Path $root "apps\$App"
# Same build directory the nRF Connect VS Code extension uses (<app>/build), so the two do not
# maintain separate trees -- and so IntelliSense (.vscode/c_cpp_properties.json) finds the
# compile_commands.json whichever way you built.
$buildDir = Join-Path $appDir "build"

if ($Build) {
  $tc = "C:\ncs\toolchains\dcbdc366a1"
  $env:PATH = "$tc;$tc\mingw64\bin;$tc\bin;$tc\opt\bin;$tc\opt\bin\Scripts;$tc\opt\nanopb\generator-bin;$tc\nrfutil\bin;$tc\opt\zephyr-sdk\gnu\arm-zephyr-eabi\bin;$tc\opt\zephyr-sdk\gnu\riscv64-zephyr-elf\bin;$env:PATH"
  $env:PYTHONPATH = "$tc\opt\bin;$tc\opt\bin\Lib;$tc\opt\bin\Lib\site-packages"
  $env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
  $env:ZEPHYR_SDK_INSTALL_DIR = "$tc\opt\zephyr-sdk"
  $env:NRFUTIL_HOME = "$tc\nrfutil\home"
  $env:ZEPHYR_BASE = "C:\ncs\v3.4.0\zephyr"
  & "$tc\opt\bin\Scripts\west.exe" build -b promicro_nrf52840/nrf52840 $appDir -d $buildDir
  if ($LASTEXITCODE -ne 0) { throw "west build failed" }
}

# Under sysbuild the image lands in <builddir>/<image>/zephyr/. Find it rather than hardcode the
# image name, which is the application directory's.
$hex = Get-ChildItem -Path $buildDir -Recurse -Filter zephyr.hex -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -notmatch 'mcuboot|b0n|ipc_radio' } | Select-Object -First 1
if (-not $hex) { throw "no zephyr.hex under $buildDir (run with -Build first)" }
$hex = $hex.FullName

$jl = "C:\Program Files\SEGGER\JLink\JLink.exe"
if (-not (Test-Path $jl)) { throw "JLink.exe not found at $jl" }

$script = @"
device nRF52840_xxAA
si SWD
speed $Speed
connect
loadfile "$hex"
r
go
exit
"@
$f = Join-Path $env:TEMP "airink_flash_run.jlink"
Set-Content -Path $f -Value $script -Encoding ascii

Write-Host "Flashing $hex via J-Link (SWD, sector-erase)..."
$out = & $jl -nogui 1 -ExitOnError 1 -CommanderScript $f 2>&1 | Out-String
$out -split "`r?`n" | Where-Object { $_ -match 'Program.*Verify|O\.K\.|range affected|Cannot|Error|Failed' }
if ($out -notmatch 'O\.K\.') { throw "flash did not report O.K.:`n$out" }

Write-Host "--- reset issued; reading $LogPort for ${LogSeconds}s ---"
try {
  $p = New-Object System.IO.Ports.SerialPort($LogPort,115200,'None',8,'One')
  $p.DtrEnable=$false; $p.RtsEnable=$false; $p.ReadTimeout=500
  $p.Open()
} catch { Write-Host "log port $LogPort unavailable: $($_.Exception.Message)"; return }
for ($i=0; $i -lt ($LogSeconds*2); $i++) {
  Start-Sleep -Milliseconds 500
  try { $d = $p.ReadExisting() } catch { $d = "" }
  if ($d) { Write-Host -NoNewline $d }
}
$p.Close()
Write-Host "`n--- done ---"
