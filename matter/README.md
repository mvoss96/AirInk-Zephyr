# AirInk — Matter-over-Thread bring-up

Derived from the NCS v3.4.0 `samples/matter/temperature_sensor`, retargeted to the
AirInk hardware (`promicro_nrf52840/nrf52840`) and stripped of its MCUboot/OTA/DK
assumptions so it runs as a **plain single image at 0x1000, flashed via UF2** — the same
field-update story AirInk uses today. This is the Stage 2 + Stage 3 bring-up snapshot; the
eventual goal is to merge AirInk's e-paper UI in (Stage 5). See the plan and the project
memory `matter-zigbee-plan` for the full roadmap.

## Status (verified on hardware, 2026-07-13)

- **Commissioned into Home Assistant over Thread.** HA shows it as "Matter Temperature
  Sensor (32768)" with the live SCD41 reading. Both fabrics (the phone's and HA's
  multi-admin share) commit cleanly.
- Boots as an **OpenThread Sleepy End Device**, BLE-advertises for commissioning.
  Onboarding: pairing code **`3535-860-0323`**, discriminator 3840, passcode **528722**.
- Reads the **real SCD41 temperature** (i2c0, P0.22/P0.24) and publishes it to the Matter
  `TemperatureMeasurement` cluster. Humidity + CO2 clusters are Stage 4 (not yet wired).
- Footprint: FLASH ~685 KB, **RAM ~187 KB → ~70 KB free** (Debug, 40 KB CHIP heap).
- Console on **uart0 = COM9** (P1.04/P1.06), not the board's USB CDC default.

## Commissioning: the five things that must all be right

Getting this to commission took five independent fixes that mask each other -- each one
fails at a *different* stage, so fixing one just moves the failure. In order of the
commissioning flow:

1. **SPAKE2+ verifier must match the passcode** (fails at PASE: commissioner drops BLE
   right after `PASE_Pake2`). `CONFIG_CHIP_DEVICE_SPAKE2_TEST_VERIFIER` is a **static
   Kconfig default belonging to passcode 20202021** -- it is NOT recomputed when you set
   `CONFIG_CHIP_DEVICE_SPAKE2_PASSCODE`. A custom passcode with the stock verifier makes
   the device advertise one passcode while proving knowledge of another. Regenerate:
   `python modules/lib/matter/scripts/tools/spake2p/spake2p.py gen-verifier -p <passcode> -s <salt> -i 1000`
2. **The device needs a Certification Declaration** (fails at attestation:
   `Failed AttestationRequest ... err = ac`, i.e. `GetCertificationDeclaration()` returned
   nothing). The built-in example provider (`CHIP_FACTORY_DATA_NONE`) has a CD compiled in.
   Generating factory data *without* `CHIP_FACTORY_DATA_GENERATE_CD` produces DAC+PAI but
   **no CD** -- and enabling that option needs the `chip-cert` host tool, which NCS does
   not ship prebuilt.
3. **The DAC's PID must equal the device's PID** (fails at attestation: commissioner
   accepts the response, then aborts ~1 s later with `ArmFailSafe(0s)`). The example
   provider's DAC is **FFF1-8000**, so `CONFIG_CHIP_DEVICE_PRODUCT_ID` must be **32768
   (0x8000)**. The sample ships 32781 (0x800D), which mismatches. (Its CD covers
   0x8000..0x80xx, so only the DAC constrains the PID.)
4. **The controller must trust the test DCL.** Home Assistant validates attestation against
   the Distributed Compliance Ledger; with `enable_test_net_dcl = false` (the default) it
   rejects test VIDs/CDs like ours (VID 0xFFF1). Set **`enable_test_net_dcl: true`** in the
   Matter Server add-on config and restart it.
5. **The CHIP heap must fit the ScanNetworks response** (fails *after* AddNOC: the fabric is
   created, then `Adding response failed: b` (NO_MEMORY) on cluster `0x0031` cmd `0x0000`,
   the fail-safe expires and the fabric is rolled back). HA issues a Thread `ScanNetworks`
   during commissioning (phone frameworks do not -- which is why that path used to get
   through). The 10 KB default is too small: **`CONFIG_CHIP_MALLOC_SYS_HEAP_SIZE=40960`**.

Reading the device log is how you tell these apart -- each has a distinct signature, listed
above. `Failed to advertise commissionable node: 3` and the `0x1349_FC00` attribute-read
errors are benign noise; ignore them.

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
