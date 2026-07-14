# AirInk — Matter-over-Thread bring-up

Derived from the NCS v3.4.0 `samples/matter/temperature_sensor`, retargeted to the
AirInk hardware (`promicro_nrf52840/nrf52840`) and stripped of its MCUboot/OTA/DK
assumptions so it runs as a **plain single image at 0x1000, flashed via UF2** — the same
field-update story AirInk uses today. This is the Stage 2 + Stage 3 bring-up snapshot; the
eventual goal is to merge AirInk's e-paper UI in (Stage 5).

Matter over Thread is the only radio this device has, and the only one it is going to have.
Zigbee was carried as a fallback while the RAM budget was in doubt — Matter's ~158 KB was the
whole risk of the project — and it stopped being one the day the full UI fitted alongside it.
Nothing Zigbee is left in the tree.

## Status (verified on hardware, 2026-07-13)

- **The whole device, plus Matter.** The e-paper UI, the menu and the forced-recalibration flow
  all run exactly as in the standalone firmware -- `app::run()` is the same code -- and the same
  readings go out over Thread.
- **Commissioned into Home Assistant over Thread.** Both fabrics (the phone's and HA's
  multi-admin share) commit cleanly.
- Boots as an **OpenThread Sleepy End Device**, BLE-advertises for commissioning.
  Onboarding: pairing code **`3535-860-0323`**, discriminator 3840, passcode **528722**.
- Publishes everything the SCD41 measures, plus the battery: temperature, relative humidity,
  CO2 (ppm), a CO2-derived air quality level, and the PowerSource state.
- Footprint: FLASH ~927 KB of the 940 KB code partition, **RAM ~247 KB of 256 KB (~15 KB free)**.
  Tight, and deliberately so -- see below.
- Console on **uart0 = COM9** (P1.04/P1.06), not the board's USB CDC default.

## Threads

`main` runs the CHIP task queue and nothing else. Everything AirInk does -- the panel, the
SCD41, the button, the menu -- runs on **its own thread** (`app::run()`, see `src/app.hpp`),
because a full CO2 single-shot blocks for ~5 s and an e-paper refresh for ~2 s, and neither may
sit on the CHIP event loop while OpenThread waits to poll its parent.

Direction of travel is one-way: AirInk measures and hands each reading to Matter (the hooks in
`app::Hooks`, which take the CHIP stack lock). Matter never reaches back into the UI -- LVGL is
not thread-safe, so the network state is left in a variable (`net::set_link()`) that the loop
picks up on its next cycle.

**`CONFIG_DK_LIBRARY=n`.** The DK library drives a development kit's buttons and LEDs; we have
one button and no status LEDs, and that button already belongs to Zephyr's gpio-keys +
input-longpress drivers. Two owners on one pin is not a style question -- the DK library takes
raw GPIO callbacks and *disables the pin interrupt* while it debounces, which is the same
interrupt gpio-keys is using. (Turning it off also retires the sample's "LED index out of the
range" boot error.)

## RAM

RAM, not flash, is what this build is short of, and every number below was measured rather than
guessed. The merge overflowed by 34 KB on the first link. What paid for it:

| | |
|---|---|
| `clip_buf` in the vendored ssd16xx driver | **−20 KB** |
| `CONFIG_LV_Z_MEM_POOL_SIZE` 48 KB → 24 KB | −24 KB |
| `CONFIG_MAIN_STACK_SIZE` 6 KB → 3 KB | −3 KB |
| `CONFIG_CHIP_TASK_STACK_SIZE` 8 KB → 6 KB | −2 KB |

The first one was not a trim but a bug: `clip_buf` is a page-compaction scratch buffer sized for
the largest panel the driver supports, and it is only ever used in the **portrait** orientations.
AirInk is landscape, so 20 KB of BSS sat there untouched -- in the standalone firmware too. It is
now compiled only when a configured panel could need it.

The LVGL pool is trimmed to its measured peak plus headroom: `ui::log_pool()` reports
`peak 17644 of 24576 B` at boot, when every view is resident. If you add a view or a bigger font,
**read that line** -- running the pool dry is an MPU fault in `lv_draw_label`, not a graceful
failure.

`CONFIG_CHIP_MALLOC_SYS_HEAP_SIZE` stays at 40 KB. It looks like an obvious 8 KB to reclaim; it
is not (see commissioning trap 5 below).

## Data model (src/default_zap/airink.zap)

| Endpoint | Device type | Clusters | Source |
|---|---|---|---|
| 0 | Root Node | PowerSource, ICD Management, Thread/General Diagnostics, … | `battery::read()` |
| 1 | Air Quality Sensor | AirQuality, CarbonDioxideConcentration, TemperatureMeasurement, RelativeHumidityMeasurement | SCD41 |

**One physical sensor, one endpoint.** The Air Quality Sensor device type (0x002C) may carry
temperature and humidity alongside the CO2 clusters, so everything the SCD41 measures lives on
endpoint 1. Splitting them across a temperature, a humidity and an air quality endpoint also
works — but each sensor device type *mandates* its own Identify cluster, and a controller then
shows three "Identify" entities for one device.

Derived from NCS's **`matter_weather_station`** ZAP (not `temperature_sensor`): it is NCS-native,
so the root endpoint carries the ICD/Thread clusters an SED build needs, and it already has
PowerSource and a humidity cluster to lift. Deltas:

- **OTA dropped.** The stock ZAP carries the OTA Software Update Requestor cluster, but we build
  without OTA (`SB_CONFIG_MATTER_OTA=n`), so nothing ever initialises its attributes — they sit
  at the ZAP defaults `UpdatePossible=true` / `UpdateState=kUnknown` forever. Home Assistant's
  update entity treats "not kIdle" as *installing*, so the device advertised a phantom firmware
  install that could never finish.
- **ICD Management swapped** for `temperature_sensor`'s full cluster. The weather station is a
  plain SIT ICD and ships a cut-down one; our Kconfig enables the LIT/CIP/UAT features, so a
  controller reading `MaximumCheckInBackOff` was getting an error.
- **`BatVoltage` and `BatTimeRemaining` dropped.** `battery.cpp` deliberately does not expose the
  cell voltage and we do not estimate runtime, so both would report a permanent 0.

## Editing the data model

NCS compiles `src/default_zap/zap-generated/` **exactly as checked in** (`BYPASS_IDL`), so
editing `airink.zap` alone changes nothing. After every ZAP edit run `tools/zap_regen.ps1` and
commit the regenerated `zap-generated/` + `airink.matter`. It needs the ZAP tool, which NCS
does not ship -- see the header of that script.

Two traps worth knowing, both of which cost real debugging time here:

- **Cluster instances must be initialised *after* `StartServer()`.** AirQuality and the
  concentration clusters are not ember-backed; they are `AttributeAccessInterface` instances,
  and their `Init()` calls `emberAfContainsServer()`, which only works once `Server::Init()`
  has run `emberAfEndpointConfigure()`. `AirQuality::Instance::Init()` checks this with
  `VerifyOrDie` -- initialise it too early and the board aborts at boot. The concentration
  instance only returns an error, and would then silently never publish.
- **Every code-driven cluster needs one instance per endpoint.** Identify is mandatory for each
  sensor device type, so it exists on endpoints 1, 2 and 3. Instantiating it for only one leaves
  the others' attribute reads failing during a controller's interview.

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
west build -b promicro_nrf52840/nrf52840 <repo>/apps/matter -d <builddir> -p always
```

Flash via SWD (J-Link, `loadfile` preserves the Adafruit bootloader), or drag-drop the
`zephyr.uf2` over USB. Console: COM9 @ 115200.

To commission onto a real Thread network you also need a Thread Border Router and a Matter
controller (chip-tool / Apple Home / Google Home).
