"""Closed-loop daylight simulation for the 360 rotatory solar panel.

`track_update` is a line-for-line port of
``firmware/libraries/tracking_logic/tracking_logic.c`` so the behaviour that
GitHub Actions compiles and unit-tests in C is the same behaviour this module
simulates. The simulation drives a synthetic equatorial day (east -> south ->
west sun path) against a fixed 10-degree panel and reports the harvested
energy ratio.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

HOLD, MOVE, PARK, STALL = 0, 1, 2, 3
ACTION_NAMES = {HOLD: "HOLD", MOVE: "MOVE", PARK: "PARK", STALL: "STALL"}

# Quadrant LDR order, same as the C header.
TL, TR, BL, BR = 0, 1, 2, 3


@dataclass
class TrackerConfig:
    deadband: int = 40
    coarse_threshold: int = 250
    fine_step: int = 2
    coarse_step: int = 5
    night_threshold: int = 90
    tilt_min: int = -10
    tilt_max: int = 100
    pan_wrap: int = 1
    pan_min: int = 0
    pan_max: int = 180
    park_pan: int = 90
    park_tilt: int = 5
    stall_limit: int = 6
    stall_epsilon: int = 3


@dataclass
class TrackerState:
    pan_deg: int = 0
    tilt_deg: int = 0
    is_night: int = 0
    moved_last: int = 0
    stall_cycles: int = 0
    stalled: int = 0
    prev_ldr: list | None = None

    def __post_init__(self):
        if self.prev_ldr is None:
            self.prev_ldr = [-1, -1, -1, -1]


@dataclass
class Action:
    kind: int = HOLD
    pan_delta: int = 0
    tilt_delta: int = 0
    error_h: int = 0
    error_v: int = 0
    stalled: int = 0
    night: int = 0


def track_resume(state: TrackerState) -> None:
    state.stalled = 0
    state.stall_cycles = 0
    state.moved_last = 0


def _normalise_pan(cfg: TrackerConfig, pan: int) -> int:
    if cfg.pan_wrap:
        return pan % 360
    return max(cfg.pan_min, min(cfg.pan_max, pan))


def track_update(state: TrackerState, cfg: TrackerConfig, ldr: list[int]) -> Action:
    act = Action()
    responded = any(
        abs(ldr[i] - state.prev_ldr[i]) >= cfg.stall_epsilon for i in range(4)
    )
    state.prev_ldr = list(ldr)

    err_h = (ldr[TR] + ldr[BR]) - (ldr[TL] + ldr[BL])
    err_v = (ldr[BL] + ldr[BR]) - (ldr[TL] + ldr[TR])
    act.error_h, act.error_v = err_h, err_v

    if max(ldr) < cfg.night_threshold:
        state.stall_cycles = 0
        state.stalled = 0
        state.moved_last = 0
        act.night = 1
        if not state.is_night:
            act.kind = PARK
            if cfg.pan_wrap:
                delta = (cfg.park_pan - state.pan_deg) % 360
                if delta > 180:
                    delta -= 360
            else:
                delta = cfg.park_pan - state.pan_deg
            act.pan_delta = delta
            act.tilt_delta = cfg.park_tilt - state.tilt_deg
            state.pan_deg = _normalise_pan(cfg, cfg.park_pan)
            state.tilt_deg = max(cfg.tilt_min, min(cfg.tilt_max, cfg.park_tilt))
            state.is_night = 1
        return act
    state.is_night = 0

    if state.stalled:
        if responded:
            track_resume(state)
        else:
            act.kind = STALL
            act.stalled = 1
            return act

    if state.moved_last and not responded:
        state.stall_cycles += 1
        if state.stall_cycles >= cfg.stall_limit:
            state.stall_cycles = 0
            state.moved_last = 0
            state.stalled = 1
            act.kind = STALL
            act.stalled = 1
            return act
    else:
        state.stall_cycles = 0

    if abs(err_h) <= cfg.deadband and abs(err_v) <= cfg.deadband:
        state.moved_last = 0
        return act

    if abs(err_h) > cfg.deadband:
        step = cfg.coarse_step if abs(err_h) > cfg.coarse_threshold else cfg.fine_step
        delta = step if err_h > 0 else -step
        before = state.pan_deg
        state.pan_deg = _normalise_pan(cfg, state.pan_deg + delta)
        act.pan_delta = state.pan_deg - before
        if cfg.pan_wrap:
            if act.pan_delta > 180:
                act.pan_delta -= 360
            if act.pan_delta < -180:
                act.pan_delta += 360
    if abs(err_v) > cfg.deadband:
        step = cfg.coarse_step if abs(err_v) > cfg.coarse_threshold else cfg.fine_step
        delta = step if err_v > 0 else -step
        target = max(cfg.tilt_min, min(cfg.tilt_max, state.tilt_deg + delta))
        act.tilt_delta = target - state.tilt_deg
        state.tilt_deg = target

    if act.pan_delta or act.tilt_delta:
        act.kind = MOVE
        state.moved_last = 1
    else:
        act.kind = HOLD
        state.moved_last = 0
    return act


# --------------------------------------------------------------------------
# Sun and panel geometry (degrees, azimuth measured clockwise from north)
# --------------------------------------------------------------------------

def sun_position(t_hours: float, zenith_elev: float = 86.0) -> tuple[float, float]:
    """Synthetic equatorial day: rises due east, peaks south, sets due west."""
    phase = (t_hours - 6.0) / 12.0
    elev = max(0.0, zenith_elev * math.sin(math.pi * phase))
    az = 90.0 + 180.0 * phase
    return az, elev


def _vec_sun(az_deg: float, el_deg: float) -> tuple[float, float, float]:
    az, el = math.radians(az_deg), math.radians(el_deg)
    return math.cos(el) * math.sin(az), math.cos(el) * math.cos(az), math.sin(el)


def panel_normal(pan_deg: float, tilt_deg: float) -> tuple[float, float, float]:
    """Panel normal. tilt = incline from horizontal (0 flat, 90 upright).

    The incline is symmetric about flat: +-tilt lean past horizontal in
    either direction without flipping the facing azimuth.
    """
    az = math.radians(pan_deg)
    th = math.radians(abs(tilt_deg))
    return (math.sin(az) * math.sin(th), math.cos(az) * math.sin(th), math.cos(th))


def ldr_readings(
    pan_deg: float,
    tilt_deg: float,
    sun_az: float,
    sun_el: float,
    ambient: float = 40.0,
    gain: float = 950.0,
    spread: float = 0.3,
) -> list[int]:
    """Four quadrant LDR readings (0..1023) around the panel normal."""
    n = panel_normal(pan_deg, tilt_deg)
    az = math.radians(pan_deg)
    th = math.radians(abs(tilt_deg))
    f = (math.sin(az), math.cos(az), 0.0)
    h = (math.cos(az), -math.sin(az), 0.0)
    g = (f[0] * math.cos(th), f[1] * math.cos(th), -math.sin(th))

    s = _vec_sun(sun_az, sun_el)
    offsets = (
        (-h[0] - g[0], -h[1] - g[1], -g[2]),  # TL
        (h[0] - g[0], h[1] - g[1], -g[2]),    # TR
        (-h[0] + g[0], -h[1] + g[1], g[2]),   # BL
        (h[0] + g[0], h[1] + g[1], g[2]),     # BR
    )

    out = []
    for dx, dy, dz in offsets:
        d = (n[0] + spread * dx, n[1] + spread * dy, n[2] + spread * dz)
        norm = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2) or 1.0
        cos_i = (d[0] * s[0] + d[1] * s[1] + d[2] * s[2]) / norm
        value = ambient + gain * max(0.0, cos_i) ** 2
        out.append(int(max(0, min(1023, round(value)))))
    return out


def capture(pan_deg: float, tilt_deg: float, sun_az: float, sun_el: float) -> float:
    """Instantaneous irradiance capture (W/m2 for a 1000 W/m2 reference sun)."""
    n = panel_normal(pan_deg, tilt_deg)
    s = _vec_sun(sun_az, sun_el)
    cos_i = n[0] * s[0] + n[1] * s[1] + n[2] * s[2]
    return 1000.0 * max(0.0, cos_i)


def simulate_day(
    cfg: TrackerConfig | None = None,
    start_pan: int = 90,
    start_tilt: int = 5,
    ambient: float = 40.0,
    gain: float = 950.0,
    diffuse: bool = False,
    dt_min: float = 1.0,
    start_hour: float = 6.0,
    end_hour: float = 18.0,
    fixed_pan: float = 180.0,
    fixed_tilt: float = 10.0,
) -> dict:
    """Run one closed-loop day. Returns energy totals and movement stats."""
    cfg = cfg or TrackerConfig()
    state = TrackerState(start_pan, start_tilt)
    if diffuse:
        ambient, gain = 900.0, 20.0

    steps = int((end_hour - start_hour) * 60 / dt_min)
    tracker_wh = fixed_wh = ideal_wh = 0.0
    moves = holds = stalls = parks = 0

    for i in range(steps):
        t = start_hour + i * dt_min / 60.0
        sun_az, sun_el = sun_position(t)
        ldr = ldr_readings(state.pan_deg, state.tilt_deg, sun_az, sun_el,
                           ambient=ambient, gain=gain)
        act = track_update(state, cfg, ldr)
        moves += act.kind == MOVE
        holds += act.kind == HOLD
        stalls += act.kind == STALL
        parks += act.kind == PARK
        hours = dt_min / 60.0
        tracker_wh += capture(state.pan_deg, state.tilt_deg, sun_az, sun_el) * hours
        fixed_wh += capture(fixed_pan, fixed_tilt, sun_az, sun_el) * hours
        ideal_wh += capture(sun_az, 90.0 - sun_el, sun_az, sun_el) * hours

    return {
        "tracker_wh_m2": tracker_wh,
        "fixed_wh_m2": fixed_wh,
        "ideal_wh_m2": ideal_wh,
        "gain_ratio": tracker_wh / fixed_wh if fixed_wh else float("inf"),
        "capture_ratio": tracker_wh / ideal_wh if ideal_wh else 0.0,
        "moves": moves,
        "holds": holds,
        "stalls": stalls,
        "parks": parks,
        "steps": steps,
        "final_pan": state.pan_deg,
        "final_tilt": state.tilt_deg,
        "night": state.is_night,
        "stalled": state.stalled,
    }


def main() -> None:
    r = simulate_day()
    print("360 rotatory solar-panel tracker - simulated day")
    print("------------------------------------------------")
    print(f"steps (1 min)          : {r['steps']}")
    print(f"tracker harvest        : {r['tracker_wh_m2']:.2f} Wh/m2")
    print(f"fixed 10-deg panel     : {r['fixed_wh_m2']:.2f} Wh/m2")
    print(f"gain over fixed        : {(r['gain_ratio'] - 1) * 100:.1f} %")
    print(f"vs perfect sun-tracking: {r['capture_ratio'] * 100:.1f} %")
    print(f"move/hold/park/stall   : {r['moves']}/{r['holds']}/{r['parks']}/{r['stalls']}")
    print(f"final azimuth/incline  : {r['final_pan']} deg / {r['final_tilt']} deg")


if __name__ == "__main__":
    main()
