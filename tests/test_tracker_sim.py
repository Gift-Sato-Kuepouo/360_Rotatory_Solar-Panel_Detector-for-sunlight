"""Behavioural tests for the Python port of the tracking logic + day simulation.

The same scenarios are asserted in C by tests/test_tracking_logic.c; this
suite keeps both implementations honest and validates the closed-loop energy
gain over a fixed panel.
"""

import pytest

from software.tracker_sim import (
    ACTION_NAMES,
    HOLD,
    MOVE,
    PARK,
    STALL,
    TrackerConfig,
    TrackerState,
    Action,
    ldr_readings,
    simulate_day,
    sun_position,
    track_resume,
    track_update,
)


def update(state, ldr, cfg=None):
    return track_update(state, cfg or TrackerConfig(), ldr)


def test_uniform_light_holds():
    st = TrackerState(45, 30)
    act = update(st, [600, 600, 600, 600])
    assert act.kind == HOLD
    assert act.pan_delta == 0 and act.tilt_delta == 0
    assert (st.pan_deg, st.tilt_deg) == (45, 30)


def test_pan_steps_toward_brighter_side():
    st = TrackerState(0, 0)
    act = update(st, [300, 700, 320, 690])
    assert act.kind == MOVE
    assert act.pan_delta > 0
    assert act.error_h == (700 + 690) - (300 + 320)

    st = TrackerState(180, 0)
    act = update(st, [800, 200, 780, 210])
    assert act.pan_delta < 0


def test_incline_follows_sun_height():
    st = TrackerState(0, 20)
    act = update(st, [300, 310, 700, 690])  # low sun -> incline up
    assert act.kind == MOVE
    assert act.tilt_delta > 0
    assert act.pan_delta == 0

    st = TrackerState(0, 60)
    act = update(st, [700, 690, 300, 310])  # high sun -> incline down
    assert act.tilt_delta < 0


def test_deadband_prevents_hunting():
    st = TrackerState(10, 10)
    act = update(st, [500, 515, 505, 510])
    assert act.kind == HOLD
    assert (st.pan_deg, st.tilt_deg) == (10, 10)


def test_coarse_and_fine_steps():
    cfg = TrackerConfig()
    st = TrackerState(0, 40)
    act = update(st, [100, 700, 100, 700], cfg)
    assert act.pan_delta == cfg.coarse_step

    st = TrackerState(0, 40)
    act = update(st, [400, 520, 400, 520], cfg)  # error_h = 240
    assert act.pan_delta == cfg.fine_step


def test_azimuth_wraps_at_360():
    st = TrackerState(359, 45)
    act = update(st, [100, 900, 100, 900])
    assert act.pan_delta > 0
    assert st.pan_deg == (359 + TrackerConfig().coarse_step) % 360


def test_azimuth_clamps_without_wrap():
    cfg = TrackerConfig(pan_wrap=0, pan_min=0, pan_max=180)
    st = TrackerState(180, 45)
    act = update(st, [100, 900, 100, 900], cfg)
    assert st.pan_deg == 180
    assert act.kind == HOLD
    assert act.pan_delta == 0


def test_incline_limits_are_bidirectional():
    cfg = TrackerConfig()
    st = TrackerState(0, cfg.tilt_max)
    act = update(st, [300, 300, 800, 800], cfg)
    assert st.tilt_deg == cfg.tilt_max and act.tilt_delta == 0

    st = TrackerState(0, cfg.tilt_min)
    act = update(st, [800, 800, 300, 300], cfg)
    assert st.tilt_deg == cfg.tilt_min and act.tilt_delta == 0


def test_night_parks_then_dawn_resumes():
    cfg = TrackerConfig()
    st = TrackerState(350, 60)

    act = update(st, [20, 15, 25, 18], cfg)
    assert act.kind == PARK and act.night == 1
    assert (st.pan_deg, st.tilt_deg) == (cfg.park_pan, cfg.park_tilt)
    assert 0 < act.pan_delta <= 180  # shortest way around the circle

    act = update(st, [20, 15, 25, 18], cfg)
    assert act.kind == HOLD and act.night == 1

    act = update(st, [700, 690, 300, 310], cfg)
    assert act.night == 0 and st.is_night == 0
    assert act.pan_delta or act.tilt_delta


def test_night_threshold_is_exclusive():
    st = TrackerState(12, 34)
    act = update(st, [90, 10, 10, 10])  # max == night_threshold
    assert act.night == 0


def test_stall_latches_holds_and_recovers():
    cfg = TrackerConfig()
    st = TrackerState(0, 45)
    ldr = [100, 900, 100, 900]
    kinds = []
    for _ in range(40):
        act = update(st, ldr, cfg)
        kinds.append(act.kind)
        if act.kind == STALL:
            break
    assert STALL in kinds
    assert kinds.index(STALL) >= cfg.stall_limit
    assert st.stalled == 1

    pan_when_stalled = st.pan_deg
    for _ in range(10):
        act = update(st, ldr, cfg)
    assert st.pan_deg == pan_when_stalled  # motors commanded to stop
    assert act.stalled == 1 or act.kind == STALL

    # obstruction clears / light shifts -> latch releases, tracking resumes
    act = update(st, [200, 700, 200, 700], cfg)
    assert act.stalled == 0
    assert act.kind == MOVE


def test_operator_resume_after_panel_swap():
    cfg = TrackerConfig()
    st = TrackerState(0, 45)
    for _ in range(40):
        act = update(st, [100, 900, 100, 900], cfg)
        if act.kind == STALL:
            break
    assert st.stalled == 1
    track_resume(st)
    assert st.stalled == 0 and st.stall_cycles == 0


def test_diffuse_forest_light_holds_position():
    st = TrackerState(123, 34)
    for _ in range(20):
        act = update(st, [300, 312, 296, 305])
    assert act.kind == HOLD
    assert (st.pan_deg, st.tilt_deg) == (123, 34)


# --------------------------------------------------------------------------
# closed-loop simulation
# --------------------------------------------------------------------------

def test_simulated_day_beats_fixed_panel():
    r = simulate_day()
    # published dual-axis gains cluster around +20..+32 % (see docs/research.md)
    assert r["gain_ratio"] >= 1.20, r
    assert r["capture_ratio"] >= 0.90, r
    assert r["stalled"] == 0, r
    assert r["stalls"] <= 5, r
    assert r["moves"] + r["holds"] + r["stalls"] + r["parks"] == r["steps"], r
    assert r["moves"] >= 100, r  # it really does track all day


def test_tracker_keeps_facing_east_to_west():
    r = simulate_day()
    # sun sweeps 90 -> 270 deg; the azimuth must follow the same order
    assert 90 <= r["final_pan"] <= 275, r


def test_diffuse_day_stays_put():
    r = simulate_day(diffuse=True)
    assert r["moves"] <= r["steps"] * 0.05, r
    assert r["stalls"] == 0, r


def test_ldr_readings_track_sun_across_sky():
    # dawn: sun in the east, panel parked facing east -> bottom pair hottest
    sun_az, sun_el = sun_position(7.0)
    ldr = ldr_readings(90, 5, sun_az, sun_el)
    assert max(ldr[2], max(ldr[0], ldr[1], ldr[3])) > ldr[0]
    assert ldr[2] + ldr[3] > ldr[0] + ldr[1]  # bottom brighter than top

    # midday: sun high in the south, panel south, incline raised -> top pair hot
    sun_az, sun_el = sun_position(12.0)
    ldr = ldr_readings(180, 60, sun_az, sun_el)
    assert ldr[0] + ldr[1] > ldr[2] + ldr[3]


def test_action_names_are_printable():
    for kind, name in ACTION_NAMES.items():
        assert isinstance(kind, int) and name.isalpha()
