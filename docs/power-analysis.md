# AirInk — Power Analysis

Autonomous measurement campaign on the Nordic PPK2 (source 3.7 V on the battery
pins) via SWD-flashed isolation firmwares (`src/power_test.cpp`). Each consumer was
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

## Methodology / what was tested

Isolation firmwares (`src/power_test.cpp`, `TEST_MODE`), each SWD-flashed and
measured on the PPK2, analysed from the raw 100 kHz CSV (`scratchpad/analyze.py`,
range-switch glitches filtered):

- **SCD41 CO₂** — `scd41::sample()` (full single-shot) in a loop, no display.
- **SCD41 T+RH-only** — raw `measure_single_shot_rht_only` (0x2196) via I²C.
- **E-paper full / partial** — 4 large DSEG7 numbers changing every second.
- **Idle floor** — console UART suspended, SoC asleep.
- **Boot** — captured across a PPK2 power-cycle.
- Cross-checked against the real app cycle (77.9 mAs) and earlier idle/DCDC/
  refresh-type experiments (all negligible).

See also the memory notes: `power-active-energy`, `power-idle-breakdown`,
`swd-flash-loop`.
