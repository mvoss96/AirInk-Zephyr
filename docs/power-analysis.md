# AirInk — Power Analysis

Autonomous measurement campaign on the Nordic PPK2 (source 3.7 V on the battery
pins) via SWD-flashed isolation firmwares (`bench/power_test.cpp`). Each consumer was
measured on its own so the numbers are attributable rather than guessed.

> **Caveat on absolute idle:** during the campaign a J-Link was attached to SWD,
> which adds a **constant ~165–183 µA** to the idle floor. All *active* charges are
> measured *above baseline* and are therefore offset-immune. For the battery model
> the **true idle (60 µA, measured earlier with no J-Link)** is used.

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

Rebuilt with the logging off (`apps/matter/power.conf`), which also hands the console back to
the loop so it powers the UART down between cycles:

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

## The two levers, in order

1. **The standing current: ~230 µA, against the standalone firmware's 60 µA.** The device
   spends 97 % of its life here, and it is 44 % of the charge. Something in the Matter build
   keeps a high-power domain alive between polls — this has **not** been decomposed yet, and
   until it is, every other optimisation is rearranging deck chairs. Suspects: the ICD poll
   configuration, the HFCLK not being released between radio wakeups, a peripheral (SAADC,
   TWIM) left enabled.
2. **The e-paper refresh: 38 % of the charge in 0.2 % of the time.** Unchanged from the
   standalone analysis, where it was also the dominant active cost. The lever remains refresh
   *frequency*, not refresh type.

## Reproducing

```
west build -b promicro_nrf52840/nrf52840 apps/matter -d <dir> \
    -- -Dmatter_EXTRA_CONF_FILE=<abs>/apps/matter/power.conf
```

Flash it, then **power-cycle via the PPK2** rather than resetting over SWD, or the debug domain
stays powered and the idle floor is a fiction. The Zephyr boot banner still comes out (it goes
through `printk`, not the logging subsystem), which is how you know the board came up at all.
