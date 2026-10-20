#include "tracking_logic.h"

static int abs_i(int v) { return v < 0 ? -v : v; }

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int max4(const int ldr[4])
{
    int m = ldr[0];
    if (ldr[1] > m) m = ldr[1];
    if (ldr[2] > m) m = ldr[2];
    if (ldr[3] > m) m = ldr[3];
    return m;
}

static int normalise_pan(const track_config_t *cfg, int pan)
{
    if (cfg->pan_wrap) {
        pan %= 360;
        if (pan < 0) pan += 360;
        return pan;
    }
    return clamp_i(pan, cfg->pan_min, cfg->pan_max);
}

/* Shortest signed azimuth change from -> to (handles 360 deg wrap). */
static int pan_delta_to(const track_config_t *cfg, int from, int to)
{
    int d;
    if (!cfg->pan_wrap) return to - from;
    d = (to - from) % 360;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
}

/* Did at least one quadrant react since the previous sampling cycle? */
static int ldr_responded(const track_state_t *st, const int ldr[4],
                         int epsilon)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (abs_i(ldr[i] - st->prev_ldr[i]) >= epsilon) return 1;
    }
    return 0;
}

static void remember_ldr(track_state_t *st, const int ldr[4])
{
    int i;
    for (i = 0; i < 4; i++) st->prev_ldr[i] = ldr[i];
}

void track_config_default(track_config_t *cfg)
{
    cfg->deadband = 40;
    cfg->coarse_threshold = 250;
    cfg->fine_step = 2;
    cfg->coarse_step = 5;
    cfg->night_threshold = 90;
    cfg->tilt_min = -10;  /* bidirectional incline: past horizontal both ways */
    cfg->tilt_max = 100;  /* slight over-travel past upright */
    cfg->pan_wrap = 1;    /* continuous 360 degree azimuth */
    cfg->pan_min = 0;
    cfg->pan_max = 180;   /* only used when pan_wrap == 0 (demo gimbal) */
    cfg->park_pan = 90;   /* east: meet the sunrise */
    cfg->park_tilt = 5;   /* slight lean so rain runs off the glass */
    cfg->stall_limit = 6;
    cfg->stall_epsilon = 3;
}

void track_state_init(track_state_t *st, int pan_deg, int tilt_deg)
{
    int i;
    st->pan_deg = pan_deg;
    st->tilt_deg = tilt_deg;
    st->is_night = 0;
    st->moved_last = 0;
    st->stall_cycles = 0;
    st->stalled = 0;
    for (i = 0; i < 4; i++) st->prev_ldr[i] = -1;
}

void track_resume(track_state_t *st)
{
    st->stalled = 0;
    st->stall_cycles = 0;
    st->moved_last = 0;
}

track_action_t track_update(track_state_t *st, const track_config_t *cfg,
                            const int ldr[4])
{
    track_action_t act;
    int err_h, err_v, maxldr, responded;

    act.kind = TRACK_ACTION_HOLD;
    act.pan_delta = 0;
    act.tilt_delta = 0;
    act.error_h = 0;
    act.error_v = 0;
    act.stalled = 0;
    act.night = 0;

    responded = ldr_responded(st, ldr, cfg->stall_epsilon);
    remember_ldr(st, ldr);

    err_h = (ldr[1] + ldr[3]) - (ldr[0] + ldr[2]); /* right pair - left pair */
    err_v = (ldr[2] + ldr[3]) - (ldr[0] + ldr[1]); /* bottom pair - top pair */
    maxldr = max4(ldr);
    act.error_h = err_h;
    act.error_v = err_v;

    /* --- night: park once, stay parked until dawn ---------------------- */
    if (maxldr < cfg->night_threshold) {
        st->stall_cycles = 0;
        st->stalled = 0;
        st->moved_last = 0;
        act.night = 1;
        if (!st->is_night) {
            act.kind = TRACK_ACTION_PARK;
            act.pan_delta = pan_delta_to(cfg, st->pan_deg, cfg->park_pan);
            act.tilt_delta = cfg->park_tilt - st->tilt_deg;
            st->pan_deg = normalise_pan(cfg, cfg->park_pan);
            st->tilt_deg = clamp_i(cfg->park_tilt, cfg->tilt_min, cfg->tilt_max);
            st->is_night = 1;
        }
        return act;
    }
    st->is_night = 0;

    /* --- stall latch: blocked drive or flat static light --------------- */
    if (st->stalled) {
        if (responded) {
            track_resume(st); /* light moved or the obstruction cleared */
        } else {
            act.kind = TRACK_ACTION_STALL;
            act.stalled = 1;
            return act;
        }
    }

    if (st->moved_last && !responded) {
        st->stall_cycles++;
        if (st->stall_cycles >= cfg->stall_limit) {
            st->stall_cycles = 0;
            st->moved_last = 0;
            st->stalled = 1;
            act.kind = TRACK_ACTION_STALL;
            act.stalled = 1;
            return act;
        }
    } else {
        st->stall_cycles = 0;
    }

    /* --- diffuse light / on target: hold instead of hunting ------------ */
    if (abs_i(err_h) <= cfg->deadband && abs_i(err_v) <= cfg->deadband) {
        st->moved_last = 0;
        return act; /* HOLD */
    }

    /* --- step toward the brightest quadrant ---------------------------- */
    if (abs_i(err_h) > cfg->deadband) {
        int step = (abs_i(err_h) > cfg->coarse_threshold) ? cfg->coarse_step
                                                          : cfg->fine_step;
        int delta = (err_h > 0) ? step : -step;
        int before = st->pan_deg;
        st->pan_deg = normalise_pan(cfg, st->pan_deg + delta);
        act.pan_delta = st->pan_deg - before;
        if (cfg->pan_wrap) { /* report the short way around the circle */
            if (act.pan_delta > 180) act.pan_delta -= 360;
            if (act.pan_delta < -180) act.pan_delta += 360;
        }
    }
    if (abs_i(err_v) > cfg->deadband) {
        int step = (abs_i(err_v) > cfg->coarse_threshold) ? cfg->coarse_step
                                                          : cfg->fine_step;
        int delta = (err_v > 0) ? step : -step;
        int target = clamp_i(st->tilt_deg + delta, cfg->tilt_min, cfg->tilt_max);
        act.tilt_delta = target - st->tilt_deg;
        st->tilt_deg = target;
    }

    if (act.pan_delta != 0 || act.tilt_delta != 0) {
        act.kind = TRACK_ACTION_MOVE;
        st->moved_last = 1;
    } else {
        act.kind = TRACK_ACTION_HOLD; /* driven into a travel limit */
        st->moved_last = 0;
    }
    return act;
}
