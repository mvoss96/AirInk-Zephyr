# AirInk — Matter-over-Thread bring-up

Derived from the NCS v3.4.0 `samples/matter/temperature_sensor`, retargeted to the
AirInk hardware (`promicro_nrf52840/nrf52840`) and stripped of its MCUboot/OTA/DK
assumptions so it runs as a **plain single image at 0x1000, flashed via UF2** — the same
field-update story AirInk uses today. This is the Stage 2 + Stage 3 bring-up snapshot; the
eventual goal is to merge AirInk's e-paper UI in (Stage 5). See the plan and the project
memory `matter-zigbee-plan` for the full roadmap.

## Status (verified on hardware, 2026-07-12)

- Boots as an **OpenThread Sleepy End Device**, BLE-advertises for commissioning.
  Test onboarding: pairing code `34970112332`, discriminator 3840, passcode 20202021.
- Reads the **real SCD41 temperature** (i2c0, P0.22/P0.24) and publishes it to the Matter
  `TemperatureMeasurement` cluster. Humidity + CO2 clusters are Stage 4 (not yet wired).
- Footprint: FLASH ~685 KB, **RAM ~156 KB → ~100 KB free** (Debug). The sensor subsystem
  cost only ~256 B of RAM. This is the Stage-3 RAM gate: it passes with room for the UI.
- Console on **uart0 = COM9** (P1.04/P1.06), not the board's USB CDC default.

## What differs from the stock sample

Five sample defaults assume MCUboot / OTA / DK hardware — all turned off (they are
unnecessary for the no-MCUboot + UF2 path):

- `sysbuild.conf`: `SB_CONFIG_BOOTLOADER_NONE=y`, `SB_CONFIG_MATTER_FACTORY_DATA_GENERATE=n`,
  `SB_CONFIG_MATTER_OTA=n` (the last two are injected via `.config.sysbuild`, which is merged
  *after* `prj.conf` — so these switches must live in `sysbuild.conf`, not `prj.conf`).
- `prj.conf`: `CONFIG_CHIP_FACTORY_DATA_NONE=y` (test creds, no cert partition),
  `CONFIG_CHIP_BOOTLOADER_NONE=y` (Matter's own bootloader choice, else hard-selects
  IMG_MANAGER), `CONFIG_CHIP_NFC_ONBOARDING_PAYLOAD=n` (no nrfx NFCT on promicro),
  `CONFIG_CHIP_LIB_SHELL=n`, `CONFIG_BUILD_OUTPUT_UF2=y`, SCD41 + uart0-console configs.
- `boards/promicro_nrf52840_nrf52840.overlay`: plain-image partition map (code 0x1000/0xEB000,
  storage 0xEC000/0x8000, Adafruit bootloader 0xF4000+ untouched), the SCD41 on i2c0, the
  button, and the uart0→COM9 console remap.
- `src/app_task.cpp`: the sample's BME680 read path re-pointed at the SCD41
  (`DEVICE_DT_GET(DT_NODELABEL(scd41))`, `#ifdef CONFIG_SCD4X`).

## Build & flash (NCS v3.4.0, toolchain dcbdc366a1)

Build from the NCS workspace (see the `zephyr-build-env` memory for the full env prelude):

```
west build -b promicro_nrf52840/nrf52840 <repo>/matter -d <builddir> -p always
```

Flash via SWD (J-Link, `loadfile` preserves the Adafruit bootloader), or drag-drop the
`zephyr.uf2` over USB. Console: COM9 @ 115200.

To commission onto a real Thread network you also need a Thread Border Router and a Matter
controller (chip-tool / Apple Home / Google Home).
