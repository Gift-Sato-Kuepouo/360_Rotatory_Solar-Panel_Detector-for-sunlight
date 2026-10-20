# Hardware

## Source design

**[`360_degree_solar_panel_detector.fzz`](360_degree_solar_panel_detector.fzz)** — open with
[Fritzing](https://fritzing.org). It contains the breadboard prototype view and the two-layer
PCB view of the tracker electronics.

Committed export of the same design for quick viewing:

- Breadboard photo: [`../docs/media/breadboard-prototype.png`](../docs/media/breadboard-prototype.png)
- PCB photo: [`../docs/media/pcb-design.png`](../docs/media/pcb-design.png)

> The integrity test suite (`tests/test_repo_integrity.py`) unzips this file on every CI run and
> asserts the documented parts are present: **4 photocells, 4× 10 kΩ resistors, 1× 6 V solar
> panel, 2 servos** — so this README and the design file cannot silently drift apart.

## Bill of materials

### Tracker electronics

| Ref (in fzz) | Qty | Part | Notes |
|---|---|---|---|
| R1–R4 | 4 | LDR photocell (GL5528-class) | Quadrant arrangement: top-left, top-right, bottom-left, bottom-right around the panel normal |
| R5–R8 | 4 | Resistor **10 kΩ** ±5%, 400 mil | One voltage divider per LDR into A0–A3 |
| PV1 | 1 | Solar panel, 6 V (demo) | Demo harvest; field unit scales to modular 100–200 W panels |
| J1 | 1 | Servo (pan) | Azimuth drive — rotate 360° about the mast |
| J2 | 1 | Servo (incline) | Left/right incline drive |
| — | 1 | Arduino Uno or Nano | Runs [`firmware/solar_tracker`](../firmware/solar_tracker) |
| — | 1 | External 5 V servo supply | ≥2 A; common ground with the Arduino, **not** fed from its 5 V pin |

### Power train (field unit)

| Qty | Part | Notes |
|---|---|---|
| 4× | Modular solar panel (≈150 W each) | Each module is the field-swappable unit (quick-release frame) |
| 1× | PWM or MPPT charge controller | Sized to array + battery |
| 1× | Battery (≈8 kWh usable) | e.g. LiFePO₄ 48 V/160 Ah class; lead-acid viable at 50% DoD |
| 1× | DC/DC buck (battery → 5 V servo/electronics rail) | |
| 1× | Slip ring (azimuth) | Enables true continuous 360° without twisting cables |
| 1× | Inverter (optional) | Only if AC loads are required at the facility |

## Wiring

| Signal | Arduino | Notes |
|---|---|---|
| LDR TL (R1) | A0 | 10 kΩ divider |
| LDR TR (R2) | A1 | 10 kΩ divider |
| LDR BL (R3) | A2 | 10 kΩ divider |
| LDR BR (R4) | A3 | 10 kΩ divider |
| Pan servo (J1) | D9 (PWM) | External 5 V rail, shared ground |
| Incline servo (J2) | D10 (PWM) | External 5 V rail, shared ground |
| Status LED | D13 | Solid = tracking, off = night park, blink = stall latch |

Quadrant convention (panel facing you):

```text
   +--------------------+
   |  TL (A0)  TR (A1)  |      err_h = (TR + BR) - (TL + BL)   -> azimuth step
   |          |         |
   |  BL (A2)  BR (A3)  |      err_v = (BL + BR) - (TL + TR)   -> incline step
   +--------------------+
```

## Axis conventions

| Axis | Range | Drive |
|---|---|---|
| Azimuth (pan) | logical 0–359°, wraps continuously | Servo J1 via turntable gearing (demo); geared + slip-ring continuous drive (field unit) |
| Incline (tilt) | −10° … 100° (bidirectional: leans past horizontal both ways) | Servo J2 on the gimbal pivot |

## Mechanical (SolidWorks)

Modeled in SolidWorks and fabricated for the demo: mast clamp, turntable ring, servo brackets,
gimbal pivot and the **quick-release panel frame** (four fasteners + one connector — the
interface that makes the panel replaceable by a trained operator without recalibrating the
tracker body).
