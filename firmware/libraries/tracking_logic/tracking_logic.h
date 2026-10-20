#ifndef TRACKING_LOGIC_H
#define TRACKING_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * quadrant LDR order used across firmware, tests and simulation:
 *   0 = TL (top-left)   1 = TR (top-right)
 *   2 = BL (bottom-left) 3 = BR (bottom-right)
 *
 * Axis conventions:
 *   pan  : azimuth in degrees, 0..359 when pan_wrap is enabled (continuous 360)
 *   tilt : incline from horizontal, 0 = flat, 90 = upright. The mechanical
 *          window is bidirectional (tilt_min can be negative) so the panel can
 *          lean past horizontal in either direction.
 */

typedef enum {
    TRACK_ACTION_HOLD = 0, /* on target (inside deadband) or already parked */
    TRACK_ACTION_MOVE,     /* pan and/or incline stepped this cycle */
    TRACK_ACTION_PARK,     /* night detected: drive to the park position */
    TRACK_ACTION_STALL     /* motion halted: commands produced no improvement */
} track_action_kind_t;

typedef struct {
    track_action_kind_t kind;
    int pan_delta;   /* signed azimuth change commanded this cycle (deg) */
    int tilt_delta;  /* signed incline change commanded this cycle (deg) */
    int error_h;     /* right-pair minus left-pair LDR counts */
    int error_v;     /* bottom-pair minus top-pair LDR counts */
    int stalled;     /* 1 while the stall latch is active */
    int night;       /* 1 while parked for the night */
} track_action_t;

typedef struct {
    int deadband;         /* LDR-count error treated as "on target" */
    int coarse_threshold; /* |error| above which coarse steps are used */
    int fine_step;        /* fine step size (deg) */
    int coarse_step;      /* coarse step size (deg) */
    int night_threshold;  /* max LDR count below which it counts as night */
    int tilt_min;         /* incline limit (deg), may be negative */
    int tilt_max;         /* incline limit (deg) */
    int pan_wrap;         /* 1 = continuous 360 deg azimuth, wraps at 360 */
    int pan_min;          /* clamped low limit when pan_wrap == 0 */
    int pan_max;          /* clamped high limit when pan_wrap == 0 */
    int park_pan;         /* night azimuth (deg) */
    int park_tilt;        /* night incline (deg), slight lean for rain runoff */
    int stall_limit;      /* unresponsive cycles before STALL latches */
    int stall_epsilon;    /* min LDR-count change per channel counting as a
                           * sensor response to a commanded move */
} track_config_t;

typedef struct {
    int pan_deg;
    int tilt_deg;
    int is_night;
    int moved_last;
    int stall_cycles;
    int stalled;
    int prev_ldr[4];
} track_state_t;

void track_config_default(track_config_t *cfg);
void track_state_init(track_state_t *st, int pan_deg, int tilt_deg);

/*
 * One sensing/actuation cycle. Reads the four quadrant LDR values (0..1023),
 * updates the state and returns the action the actuator layer must perform.
 *
 * Stall policy: a STALL latches when moves are still being commanded (light
 * gradient above the deadband) but the four LDR readings stop responding for
 * `stall_limit` consecutive cycles - i.e. the drive is blocked or the light
 * field is flat and static. The latch releases as soon as the readings move
 * again (obstruction cleared, sun moved), or immediately via track_resume().
 */
track_action_t track_update(track_state_t *st, const track_config_t *cfg,
                            const int ldr[4]);

/* Clears the stall latch, e.g. after a trained operator swaps the panel. */
void track_resume(track_state_t *st);

#ifdef __cplusplus
}
#endif
#endif /* TRACKING_LOGIC_H */
