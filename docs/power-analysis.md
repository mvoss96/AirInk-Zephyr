# AirInk — Power Analysis

> ## Read this first — where it stands today (2026-07-14, Matter over Thread, measured)
>
> ```
> CO2 read (1x / 5 min)   73.1 mAs   ~70 %      <- the device, essentially
> idle floor (63 uA)      ~24 mAs    ~23 %
> e-paper (1-3 refreshes)  9.1 mAs    ~9 %
> Thread radio             1.1 mAs    ~1 %
> T+RH reads (10x)         1.5 mAs    ~1 %
>                         ~105 mAs / 5 min  ->  350 uA  ->  ~118 days @ 1000 mAh
> ```
>
> **The CO₂ read is 70 % of the device**, and its 73 mAs is physics — single-shot is already the
> SCD41's lowest-duty mode. The only lever is how often it runs: every 10 min → ~187 days, every
> 15 min → ~231 days. Everything else has been chased down and is spent.
>
> **Two rig rules, both learned by publishing wrong numbers:** an attached J-Link adds **~155 µA**,
> and a power-cycle inside the measurement window puts a boot in it (an extra CO₂ read + a Thread
> re-attach) which reads as a regression that is not there. Measure a settled device, probe out.
>
> **This document is a chronology, not a reference.** The chapters below are in the order they were
> written, and the later ones correct the earlier ones — twice by disproving something stated here as
> fact. Nothing has been deleted, because being wrong in a traceable way is the point. If you only
> want the current numbers, you have just read them.

---

# Chapter 1 — the isolation campaign (superseded in its idle figures, sound in its component charges)

Autonomous measurement campaign on the Nordic PPK2 (source 3.7 V on the battery
pins) via SWD-flashed isolation firmwares (`bench/power_test.cpp`). Each consumer was
measured on its own so the numbers are attributable rather than guessed.

> **Caveat on absolute idle:** during the campaign a J-Link was attached to SWD,
> which adds a **constant ~165–183 µA** to the idle floor. All *active* charges are
> measured *above baseline* and are therefore offset-immune. For the battery model
> the **true idle (60 µA, measured earlier with no J-Link)** is used.
>
> *(Later: that 60 µA was measured on firmware whose button interrupt was silently disabled. The
> real floor is ~63 µA — see the last two chapters. The component charges in this chapter, though,
> have held up: the CO₂ read really is ~70 mAs and the panel really is ~3 mAs, which is exactly what
> the two later chapters had to rediscover the hard way.)*

---

## TL;DR

1. **The CO₂ sensor is ~everything.** One SCD41 CO₂ single-shot = **~70 mAs** (peak
   171 mA, ~5 s). The e-paper refresh is **~2–3 mAs** and the idle floor is 60 µA.
   An earlier analysis of mine wrongly blamed the display — the high-current spikes
   are the SCD41's photoacoustic pulse, not the panel.
2. **The one lever that matters is how often you measure CO₂.** 60 s → 300 s is a
   **~4× battery-life** improvement. Everything else is second-order.
3. **T+RH without CO₂ is basically free** (~0.07 mAs, ~1000× cheaper than a CO₂
   read) — so a dual cadence (CO₂ rarely, T+RH often) buys frequent temperature/
   humidity updates almost for nothing.
4. **Display, DC/DC, partial-vs-full refresh, panel deep-sleep, parallel-vs-
   sequential — all negligible.** Don't spend effort there.

---

## Measured component breakdown

| Consumer | Energy per event | Peak | Duration | Notes |
|---|---|---|---|---|
| **SCD41 CO₂ single-shot** | **~70 mAs** | 171 mA | ~5 s | photoacoustic pump/heat pulse; the dominant load |
| SCD41 **T+RH-only** (cmd 0x2196) | **0.07 mAs** | 15 mA | ~90 ms | skips CO₂; ~1000× cheaper |
| E-paper **full** refresh (busy screen) | ~3 mAs | 26 mA | ~1 s | worst case |
| E-paper **partial** refresh | ~2 mAs | 25 mA | ~0.8 s | changed pixels only |
| **Idle** (UART suspended) | 60 µA (true) | — | continuous | nRF System-ON + panel standby + LDO + sensor powered-down |
| Console **UART left active** | **+~800 µA** | — | continuous | why it's suspended between cycles (was the old 1.1 mA idle) |
| Boot (incl. panel init) | ~4.7 mAs one-time | 22 mA | ~2 s | irrelevant to battery life |

**One full measurement cycle as shipped** (CO₂ + battery ADC + display refresh) ≈
**78 mAs**: SCD41 ~70 + display ~3 + I²C/CPU/ADC overhead ~5. Validated against the
real app (measured burst 77.9 mAs).

---

## Where the energy goes (per 60 s cycle, as shipped)

```
SCD41 CO₂ read   70 mAs   ~86 %   ####################################
overhead/ADC      5 mAs    ~6 %   ###
display refresh   3 mAs    ~4 %   ##
idle (60 s×60µA)  3.6 mAs  ~4 %   ##
                 -----
total            ~82 mAs  -> avg 1.36 mA -> ~31 days @ 1000 mAh
```

---

## Battery-life model

`avg_current = 60 µA + (per-cycle active charge) / interval`, then
`life = capacity / avg_current`. For the shipped "CO₂ + display every interval"
(78 mAs/cycle):

| CO₂ interval | avg current | 500 mAh | 1000 mAh | 2000 mAh |
|---|---|---|---|---|
| 60 s (today) | 1.36 mA | 15 d | **31 d** | 61 d |
| 120 s | 0.71 mA | 29 d | 59 d | 117 d |
| 180 s | 0.49 mA | 42 d | 85 d | 169 d |
| **300 s (5 min)** | 0.32 mA | 65 d | **130 d** | 260 d |
| 600 s (10 min) | 0.19 mA | 110 d | 219 d | 438 d |
| 900 s (15 min) | 0.15 mA | 142 d | 283 d | 567 d |

*(True 60 µA idle, no J-Link. Boot is one-time and excluded.)*

**Dual cadence** (CO₂ every 300 s **+** a T+RH-only display update every 60 s):
per 300 s = 78 (CO₂ cycle) + 4×~3 (T+RH: 0.07 sensor + ~2 partial refresh + overhead)
+ 18 (idle) ≈ 108 mAs → **~0.36 mA → ~116 d @ 1000 mAh**. I.e. you keep 60 s
temperature/humidity freshness for the price of ~14 days vs CO₂-only-every-5-min.
The cost is the *display refreshes*, not the T+RH reads.

---

## Optimization levers, ranked

| Lever | Impact | Effort | Verdict |
|---|---|---|---|
| **CO₂ interval 60 s → 300 s** | **~4× life** | trivial (one constant) | **do this** — biggest lever by far |
| **Dual cadence** (T+RH often, CO₂ rare) | frequent T+RH ~for free | small | do it *if* you want live T+RH on screen |
| UART idle-suspend | 1.1 mA → 60 µA | done | ✅ already shipped |
| Sensor-rail (ext_3v3 P0.13) gating in idle | maybe −20–40 µA idle | medium, re-init cost | marginal (idle is only ~4 % of budget) |
| SAADC suspend | negligible | small | skip |
| DC/DC (REG1) | ~0 now | done (committed) | keep — pays off once radio draws mA |
| Display: partial/full, deep-sleep, windowing | negligible (~2–3 mAs) | — | **don't bother** |
| Sensor read parallel vs sequential | <1 % | — | only for responsiveness |

There is **no way to make a CO₂ read cheaper** — 70 mAs is the SCD41 physics
(single-shot is already the lowest-duty mode). The only CO₂ lever is frequency.

---

## Recommended "optimal" setup

1. **CO₂ every 180–300 s.** Air quality moves slowly; 60 s is overkill. → 3–5×
   the battery life. This is the whole game.
2. **Optional dual cadence:** if you want temperature/humidity to feel "live," do a
   T+RH-only read + partial display update every 30–60 s and a full CO₂ read every
   300 s. Nearly free on the sensor; the only cost is the extra display refreshes.
3. **Keep the UART idle-suspend** (already in) — it's the single change that took
   idle from 1.1 mA to 60 µA.
4. **Leave the display alone.** Refresh whenever a value changes; it's ~3 mAs.
5. **Keep DC/DC enabled** for the coming radio work.
6. *(Optional, later)* gate the ext_3v3 sensor/panel rail in idle for another
   ~20–40 µA — only worth it once the interval is long and idle dominates.

**Expected result:** at CO₂ every 300 s with a ~1000 mAh cell, **~130 days**
(vs ~31 days today) — an ~4× improvement from a one-line change, with the sensor
cadence as the single knob to trade battery life against CO₂ freshness.

---

## Idle deep-dive (can we lower the 60 µA?)

Attributed by toggling each consumer in the idle harness (deltas are J-Link-offset
immune; the ~170 µA J-Link offset dominates the absolute reading so only deltas are
trusted):

| idle config | measured (w/ J-Link) | delta |
|---|---|---|
| baseline (panel standby) | 243 µA | — |
| SAADC suspended | 243 µA | **0 µA** — the nrfx driver already fully disables the SAADC after each read |
| panel deep-sleep (DSM) | 222 µA | **−21 µA** — panel controller standby |
| panel DSM + rail gated | 177 µA | **−45 µA more** — ext_3v3 LDO quiescent + SCD41 |
| **real app, idle gap** | **222 µA** | app already DSMs the panel ✓ |

**Breakdown of the true ~60 µA idle:** ext_3v3 **LDO quiescent ~40–45 µA** (dominant)
+ panel ~5 µA (DSM'd) + SCD41 ~0.5 µA + nRF System-ON idle + LFCLK ~7 µA.

**Conclusion: idle is already near firmware-optimal at the 30 s cadence.** The panel
is deep-slept (the app does this, verified). The SAADC draws nothing. The nRF is in
the right sleep mode. The remaining ~45 µA is the **ext_3v3 LDO quiescent** — a
hardware property that firmware can only remove by **gating the rail**, and:

- Gating naively **back-powers the unpowered panel** through driven GPIOs (measured
  **10 mA**!). It requires parking every panel/sensor line (CS/DC/RST/SCK/MOSI low,
  I²C/BUSY as inputs) first.
- Even done cleanly, at the 30 s cadence it is **net-negative**: rail-off for ~29 s
  saves 45 µA×29 s ≈ 1.3 mAs, but the panel loses its RAM/config so the next update
  needs a re-init + full refresh (~3–5 mAs). The re-init costs more than the idle
  saves.

**When rail-gating *would* pay off:** only with long idle gaps (CO₂-only, no 30 s
T+RH ticks) *or* combined with display-refresh quantization (below). A hardware
lower-quiescent LDO/load-switch is the clean fix but is out of firmware scope.

**Bigger lever than idle — IMPLEMENTED:** at the dual-cadence budget the T+RH
display refreshes cost more than idle. `set_sensor` now dedups on the *displayed*
value (CO2 25 ppm / T 0.5 °C / RH 1 %) and the battery is EMA-smoothed + only shown
on the 5-min CO2 tick, so a T+RH tick only refreshes when the shown value actually
changes. **Measured: a stable T+RH tick dropped from ~2.96 mAs to ~0.16 mAs (~18×).**
Battery life (stable air): CO2-5min/T+RH-30s ~135 d (was 106); CO2-10min/T+RH-30s
**~223 d** (was 151) @ 1000 mAh. Real life sits between the "every tick refreshes"
and "stable" bounds depending on how often T/RH cross a step.

## The interactive screens (menu, calibration)

Measured with `bench/ui_power.cpp` (`-DAPP_ENTRY=ui_power`), PPK2 source 3815 mV,
power-cycled before each capture. The button cannot be pressed by a script, so the
harness walks into each state directly.

| What | Energy | Notes |
|---|---|---|
| E-paper **full** refresh | **3.9 mAs** | 0.95 s, peak 25 mA |
| E-paper **partial** refresh | **3.0 mAs** | 0.85 s, peak 23–25 mA |
| One **CO₂ recalibration** (3 min) | **2588 mAs** ≈ 0.72 mAh | avg 14.3 mA over 181 s |

**Partial vs. full refresh is a 0.9 mAs difference — not an energy lever.** The menu and
the calibration steps use partial refreshes because a full one is a ~1 s black flash,
which makes navigation feel broken. Energy did not enter into it. (A first reading of a
"60.7 mAs full refresh" was a misattribution: that burst is the SCD41's first periodic
measurement, which happens to land right after the boot splash.)

**A recalibration is dominated by the sensor, not the screen.** In periodic mode the
SCD41 measures every ~4.9 s: 36 bursts of ~50 mA for ~1.2 s, on a 3.4 mA baseline. The
twelve countdown redraws add ~36 mAs — 1.4 % of the total. Redrawing every second
instead would cost ~540 mAs, which is why the countdown ticks every 15 s.

Against a 2000 mAh cell a calibration costs **0.036 %**, about as much as half an hour of
normal operation. It is affordable and does not need optimising.

> **Caveat.** Both harnesses idle at ~236 µA rather than the firmware's 60 µA. They do
> not reproduce `main()`'s init/suspend sequence — `MODE_MENU_IDLE` never calls
> `scd41::init()`, so the sensor never receives its `power_down`. Do not read an idle
> floor out of these captures.

---

## Methodology / what was tested

Isolation firmwares (`bench/power_test.cpp`, `TEST_MODE`; build with
`-DAPP_ENTRY=power_test`), each SWD-flashed and measured on the PPK2, analysed from
the raw 100 kHz CSV (`scratchpad/analyze.py`, range-switch glitches filtered):

- **SCD41 CO₂** — `scd41::sample()` (full single-shot) in a loop, no display.
- **SCD41 T+RH-only** — raw `measure_single_shot_rht_only` (0x2196) via I²C.
- **E-paper full / partial** — 4 large DSEG7 numbers changing every second.
- **Idle floor** — console UART suspended, SoC asleep.
- **Boot** — captured across a PPK2 power-cycle.
- Cross-checked against the real app cycle (77.9 mAs) and earlier idle/DCDC/
  refresh-type experiments (all negligible).

See also the memory notes: `power-active-energy`, `power-idle-breakdown`,
`swd-flash-loop`.

---

# Matter over Thread (2026-07-13)

The same device, plus a Thread radio. Measured on the PPK2 at 3.7 V (source mode, no USB),
across a 300 s window — ten measurement cycles, one of them a full CO₂ read.

## You cannot measure a device that is talking

The first number was 1.3 mA average, 620 µA idle. That number is nearly worthless: it is
mostly the console. A live console UARTE holds the HFCLK on, and the Matter build is the loud
one — CHIP logs every message it sends and receives.

Rebuilt with the logging off (the fragment now at `conf/release.conf`), which also hands the console
back to the loop so it powers the UART down between cycles:

| | logging on | logging off |
|---|---|---|
| average | 1.3 mA | **525 µA** |
| idle floor | 620 µA | **~230 µA** |

**Over half the current was the log.** Every number below is from the quiet build.

## Where the charge goes

157.6 mAs over 300 s, i.e. an average of **525 µA**:

| | share of time | charge | share of charge |
|---|---|---|---|
| standing current (~230 µA) | 97 % | 69 mAs | **44 %** |
| e-paper refresh | 0.2 % | 59 mAs | **38 %** |
| sensor + CPU (incl. one 5 s CO₂ read) | 2.2 % | 27 mAs | 17 % |
| Thread radio | 0.7 % | 2.6 mAs | **2 %** |

**The radio is not the problem.** The SED polls its parent every 5 s — two bursts of ~50 ms,
peaking around 11 mA — and the whole of that costs 2 % of the budget. The Thread stack is
cheap; what it brought with it is not.

## The standing current is not Matter's — and it is not 60 µA any more

I first wrote that Matter added ~170 µA of standing current, comparing against the 60 µA idle
figure from the analysis above. That was wrong, and the way it was wrong is worth recording.

Measured on the same rig, same session, both freshly power-cycled:

| | floor (5th pct) | median |
|---|---|---|
| standalone firmware | 218.5 µA | 230.9 µA |
| Matter, no logging | 218.5 µA | 230.5 µA |
| Matter, no logging, **no BLE** | 218.1 µA | 230.5 µA |

**All three are identical.** Matter adds nothing to the standing current, and neither does the
BLE controller — that experiment (CONFIG_BT=n, still commissioned, still on Thread) was run
precisely to blame it, and it exonerated it.

The standing current is also perfectly flat: sampled at 100 kHz, the 3.6 s between two Thread
polls sits at 236–239 µA with no structure at all. It is a DC draw, not the average of many
small wakeups — which also means **the 5 s poll interval is not the lever**. Lengthening it
would trim the 2 % the radio costs, not the 44 % the floor costs.

So the standing current is the *device's*, and it is ~230 µA rather than the 60 µA this document
claims above. Either that number regressed, or it was measured under conditions that do not hold
today. Note the caveat already written above about the bench harnesses idling at ~236 µA — the
same number — because they never power the SCD41 down. But the firmware does call
`scd41::power_down()` after every read, and it still measures 230 µA.

**Prime suspect: the e-paper panel is never put to sleep.** The vendored ssd16xx driver
implements `PM_DEVICE_ACTION_SUSPEND` — it sends the panel's deep-sleep command — and *nothing in
the firmware ever calls it*. An SSD1683 left in normal mode after a refresh draws on this order.
Not yet proven; it is the next experiment.

> **A warning about how not to test this.** Reading the nRF's `HFCLKSTAT` over SWD to see whether
> the 64 MHz clock was stuck on returned "running" — for the standalone firmware too, which idles
> at the same current either way. Attaching the debugger powers the debug domain and starts the
> HFCLK. The register read measures the J-Link, not the device. Peripheral `ENABLE` registers are
> still meaningful (UARTE0 and TWIM0 read 0, so the console suspend and the sensor power-down both
> do work); clock and power state, over SWD, are not.

## The levers, in order

1. **The standing current: ~230 µA, in both builds.** 97 % of the time, 44 % of the charge. Not
   Matter's, not BLE's, not the poll interval's. Chase the panel.
2. **The e-paper refresh: 38 % of the charge in 0.2 % of the time.** Unchanged from the analysis
   above, where it was also the dominant active cost. The lever remains refresh *frequency*, not
   refresh type.
3. **Matter itself: 2 %.** The radio is not the problem.

## Reproducing

```
west build -b promicro_nrf52840/nrf52840 apps/matter -d <dir>
```

Measure the ordinary, talking build — that is the firmware that ships. (The chapters above muted it
first, which was necessary *then* and is not any more; see below. The mute switch still exists as
`conf/release.conf`, opt-in via `-Dmatter_EXTRA_CONF_FILE=<abs>/conf/release.conf`, but it is worth
about 7 µA now, not the ~650 µA it looked like here.)

Flash it, then **power-cycle via the PPK2** rather than resetting over SWD, or the debug domain
stays powered and the idle floor is a fiction. And unplug the J-Link: **an attached probe adds
~155 µA** all by itself, which is more than the idle floor it is supposed to help you read.

---

# The console was never the price of having a console (2026-07-13)

The measurements above kept saying "the logging costs ~650 µA, so the Matter build cannot afford a
console". That framing was wrong, and the question that broke it was: *why can't we have UART
without the penalty?*

An enabled UARTE holds the HFCLK on. That is the cost — **enabled**, not **transmitting**. It does
not matter whether anything is listening; the shipped device pays it too. The standalone firmware
worked around it by suspending the console by hand around its sleep, which is why it idled at 60 µA
and the Matter build (where CHIP logs *while* we sleep) could not.

Zephyr already has the right mechanism: **device runtime PM** (`CONFIG_PM_DEVICE_RUNTIME`). The
nrfx UARTE driver calls `pm_device_runtime_put_async()` when its TX stops, so the peripheral is
enabled for the microseconds of a line and suspended in between.

It asserted on the first try — `ASSERTION FAIL @ nrfx_twi.c:312`. The reason is worth writing down:

> **The promicro board DTS downgrades i2c0 and spi2 to the legacy `nordic,nrf-twi` and
> `nordic,nrf-spi`.** Those are the only nRF serial drivers that do *not* participate in device
> runtime PM — they never call `pm_device_runtime_get()`, so they touch a suspended peripheral and
> assert. `dts/airink_hw.dtsi` now puts both back on the SoC's own defaults, TWIM and SPIM, which
> are DMA-based and do the get/put. (Which also means the 15 KB framebuffer flush and every sensor
> transaction had been going through the CPU byte by byte.)

## Result: full logging, and the low floor

| | idle floor (probe-corrected) |
|---|---|
| Matter, full CHIP logging, legacy drivers | ~705 µA |
| Matter, full CHIP logging, **runtime PM + DMA** | **~60 µA** |
| Matter, logging off entirely (`conf/release.conf`) | 55 µA |

**Logging now costs about 7 µA** — against a 343 µA shipping average, about 2 %. So the mute switch
is no longer a measurement crutch (measure the talking firmware; it is the honest number), and the
shipped firmware keeps its console. It survives as `conf/release.conf`, an opt-in fragment for a
release image where nobody is reading the console anyway and 7 µA is still 7 µA.

`app.cpp`'s hand-rolled console suspend is deleted. The driver does it per line, in every build.

## The shipping profile

Matter over Thread, full logging, 300 s, one CO₂ read, one panel refresh. Probe-corrected
average **~343 µA** (149.3 mAs / 5 min):

| | share of time | charge |
|---|---|---|
| **e-paper refresh** | 0.2 % | **39 %** |
| idle floor (~60 µA) | 92 % | 41 % |
| **sensor + CPU** (the 5 s CO₂ read) | 1.8 % | **14 %** |
| Thread radio | 0.8 % | 3 % |

**~122 days on a 1000 mAh cell, ~243 on 2000 mAh.**

The remaining levers are the two the device chooses for itself: how often it refreshes the panel,
and how often it reads CO₂. Neither is a bug.

> **Rig warning, learned the hard way.** An attached J-Link adds **~155 µA** to the measured floor
> (measured: identical firmware, probe in 859.5 µA vs probe out 699.8 µA). Every idle figure taken
> with the probe attached is that much too high — which is exactly how a 60 µA floor was mistaken
> for 230 µA and blamed first on Matter and then on the panel. Unplug SWD for any absolute idle
> number; the offset is only good enough for A/B iteration.

---

# The floor above was too good, because the button was broken (2026-07-13)

`CONFIG_PM_DEVICE_RUNTIME` bought the console back for 7 µA. It also, silently, switched the button
off: `gpio-keys` calls `pm_device_runtime_enable()` in its own init, which *suspends* the device on
the spot, and its suspend action is `gpio_pin_interrupt_configure_dt(GPIO_INT_DISABLE)`. Nobody ever
asked for it back. See the fix in `button::init()`.

So every idle figure in the chapter above was measured on a device that could not be woken by its
one button. Asking for the button back costs current, and now that is measured too:

| firmware | idle floor (5 %) | mean while idle | charge / 5 min |
|---|---|---|---|
| the chapter above (button off, no VBUS watch) | 215.4 µA | 226.5 µA | 149.3 mAs |
| button interrupt back on | 218.1 µA | 238.4 µA | 156.4 mAs |
| button + VBUS wake (shipping) | 218.1 µA | 238.0 µA | 151.7 mAs |

*(rig figures, J-Link attached: subtract ~155 µA for the absolute number.)*

**The button costs ~12 µA** — a GPIOTE channel in event mode. That is not a regression to fix; it is
the price of the device responding to a press at all, and it was always meant to be paid. The honest
idle floor is **~72 µA**, not ~60, and the shipping average **~355 µA**: **~118 days** on a
1000 mAh cell rather than 122.

**The VBUS wake costs nothing** (−0.4 µA, i.e. noise). The charging bolt used to hang on the panel
for up to 30 s after the cable was pulled -- charging is read from the raw voltages and was never
stale, but the loop only looked once per cycle. It now waits on the SoC's own USBDETECTED/USBREMOVED
alongside the button (`k_poll`, `battery::watch_supply`), so the panel reacts within one refresh.
Polling for it would have cost current every cycle for an event that happens twice a week; the POWER
peripheral's VBUS comparator is always on anyway and asks for nothing.

> Measure a *settled* device. The first run of this comparison put a power-cycle inside the window
> and the boot's extra CO₂ read and Thread re-attach showed up as +60 µA of "regression" that was
> not there. The e-paper charge (58.4 → 58.9 mAs across all three runs) is the tell: same number of
> refreshes, so any difference in the mean has to come from somewhere else.

---

# Where the energy actually goes, by event and not by threshold (2026-07-14)

Twice now this document has blamed the e-paper panel, and twice it was wrong. The mistake both times
was to bucket the samples *by current* and name the buckets: the ">12 mA" band looks like a display
refresh until you notice it carries 58 mAs in 0.6 s, which is 97 mA average -- and the panel draws
26 mA. It is the SCD41's photoacoustic pulse (peak 145 mA), which the earlier component measurements
already said costs ~70 mAs.

Bucket by *event* -- find the bursts, measure each one, match it against the known cadence -- and it
is unambiguous (300 s, settled, probe-corrected, LIT):

```
CO2 read (1x / 5 min)   73.1 mAs   ~70 %   6.7 s, peak 145 mA
idle floor (63 uA)      ~24 mAs    ~23 %   97 % of the time
e-paper (3 refreshes)    9.1 mAs    ~9 %   1.6 s, peak 24 mA, ~3 mAs each
Thread radio             1.1 mAs    ~1 %
T+RH reads (10x)         1.5 mAs    ~1 %   90 ms, peak 15 mA
                        --------
                        ~105 mAs / 5 min -> 350 uA -> ~118 days @ 1000 mAh
```

**The CO₂ read is 70 % of the device.** Nothing else is worth optimising, and its 73 mAs is physics
(single-shot is already the SCD41's lowest-duty mode). The only lever is how often it runs:
every 10 min → ~187 days; every 15 min → ~231 days.

Note the panel is *already* frugal: `ui::refresh()` only paints when a displayed value changed, so a
still room refreshes once in five minutes and a moving one three times. That variance (±6 mAs) is
larger than most of the things one might try to optimise -- which is why a single 5-minute window
cannot resolve a 4 mAs change, and event attribution can.

## The Thread radio: 5 s → 15 s → LIT, and it was never the problem

The device polls its router to ask "anything for me?". Measured: 11 mA a poll.

`CONFIG_CHIP_ICD_SLOW_POLL_INTERVAL` is 300 s and never took effect. A LIT-capable ICD must operate
in **SIT** mode until some client registers itself through the ICD Management cluster, and in SIT the
poll is capped by `CHIP_ICD_SIT_SLOW_POLL_LIMIT`. The sample ships 5 s; the spec ceiling (and NCS's
own default) is 15 s. Raising it is free for a device that only ever reports -- the only cost is that
an inbound command may wait up to 15 s.

| | polls / 5 min | radio charge | interval |
|---|---|---|---|
| SIT, 5 s limit (as shipped by the sample) | 64 | 6.8 mAs | 5.0 s |
| SIT, 15 s limit | 26 | 2.7 mAs | 15.0 s |
| **LIT** (Home Assistant registered as an ICD client) | 9 | **1.1 mAs** | no idle poll at all |

In LIT the remaining bursts are not polls: they are the readings being reported, which is the job.

**And the average did not move** — 353.5 → 352.7 µA, 118 days either way. The radio was ~6 % of the
budget at its worst and is ~1 % now. Both changes are right (less radio is less radio, and it is
kinder to the Thread router), but together they buy about five days. Anyone reading this looking for
battery life should stop here and go change the CO₂ interval.

> Home Assistant offers the SIT/LIT switch because our ICD Management cluster advertises the LITS
> feature bit. Commercial sensors (an IKEA Vallhorn, say) are SIT-only and offer no such control:
> LIT additionally requires the Check-In protocol and a User Active Mode Trigger. We have all of it
> because the NCS sample's defaults gave it to us, not because we chose it.
