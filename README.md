# AirInk

A CO2, temperature and humidity monitor on a 4.2" e-paper panel — nRF52840 (promicro /
nice!nano), SCD41 sensor, battery powered. Optionally a Matter-over-Thread device.

## Layout

There are **two applications** and one device. The applications differ in how the device talks to
the world; the device itself — sensor, panel, menu, button, measurement loop — is one body of code
that both of them build.

```
apps/
  standalone/   The firmware. No radio: it measures, it draws, it sleeps.
  matter/       The same firmware, plus Matter over Thread. Its own main(), which brings the
                CHIP stack up and then runs the device on a thread of its own.
src/            The device. Both applications compile this.
dts/            The hardware (airink_hw.dtsi). Both applications include it.
conf/           The board's drivers (airink_hw.conf). Both applications merge it.
cmake/          The shared source lists.
sim/            A host program (not a Zephyr app): renders the UI views to PNG with plain gcc, so
                layout can be checked without flashing.
tools/          flash.ps1 (SWD via J-Link), and the ZAP regeneration script under apps/matter.
docs/           Power analysis.
```

A Zephyr application is just a directory holding `CMakeLists.txt` + `prj.conf`, and it is what you
pass to `west build`. Nothing else in the tree is one — which is why `apps/` exists: before, the
repo root was itself an application, and that was invisible.

## Building

```
west build -b promicro_nrf52840/nrf52840 apps/standalone -d build/standalone
west build -b promicro_nrf52840/nrf52840 apps/matter     -d build/matter
```

Or, with the flash helper (J-Link over SWD, then streams the console):

```
pwsh -File tools/flash.ps1 -Build                # standalone
pwsh -File tools/flash.ps1 -App matter -Build    # Matter
```

Build for the **plain** board target, never `promicro_nrf52840/nrf52840/uf2`: the plain target links
at 0x1000, where the Adafruit UF2 bootloader expects a SoftDevice-less app. The `/uf2` variant links
at 0x26000 and will not boot. Both builds also emit a `.uf2` for drag-drop flashing.

In VS Code (nRF Connect), add both `apps/standalone` and `apps/matter` as applications; each gets
its own build configuration. `.vscode/c_cpp_properties.json` has an IntelliSense configuration for
each — pick one in the status bar.

Day to day: do UI and sensor work in the standalone build, which compiles in seconds. The Matter
build takes minutes, and the code is the same.

## How the two fit together

`src/app.cpp` is the device: measure, draw, wait for the button, repeat. The standalone application
runs it as `main()`. The Matter application runs it on a dedicated thread, next to the CHIP event
loop and OpenThread — because a CO2 read blocks for ~5 s and an e-paper refresh for ~2 s, and
neither may sit on the event loop while OpenThread waits to poll its parent.

The coupling is one-way and deliberately thin. `app.cpp` knows nothing about Matter: it calls two
optional function pointers (`app::Hooks`) after each cycle, which the Matter application installs
and the standalone one leaves null. In the other direction, Matter never touches the UI — LVGL is
not thread-safe — so it drops the radio state with `app::set_link()` and the loop picks it up on its
next pass.

The one exception is `#ifndef CONFIG_CHIP` in `app.cpp`, guarding the console power-down: the
standalone build suspends the console UART while it sleeps (~1 mA versus a 60 µA idle floor), and
the Matter build must not, because CHIP and OpenThread log while we wait. Those two lines are the
only place the device code knows which application is building it.

See `apps/matter/README.md` for the Matter data model, the commissioning traps, and the RAM budget.
