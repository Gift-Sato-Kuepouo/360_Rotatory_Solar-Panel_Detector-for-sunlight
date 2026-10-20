/*
 * Unit tests for the 360 rotatory solar-panel tracking logic.
 * Pure host tests: no Arduino toolchain required.
 *
 * Build:
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
 *       -Ifirmware/libraries/tracking_logic \
 *       tests/test_tracking_logic.c firmware/libraries/tracking_logic/tracking_logic.c \
 *       -o test_tracking_logic
 */

#include <stdio.h>
#include <stdlib.h>

#include "tracking_logic.h"

static int failures = 0;
static int checks = 0;

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            printf("  FAIL  %s (%s:%d)\n", msg, __FILE__, __LINE__);           \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                                   \
    do {                                                                       \
        checks++;                                                              \
        if ((a) != (b)) {                                                      \
            failures++;                                                        \
            printf("  FAIL  %s: expected %d, got %d (%s:%d)\n", msg, (int)(b),  \
                   (int)(a), __FILE__, __LINE__);                              \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        int before = failures;                                                 \
        printf("RUN   %s\n", #fn);                                             \
        fn();                                                                  \
        printf(failures == before ? "  ok\n" : "  FAILED\n");                   \
    } while (0)

/* four bright, uniform quadrants -> on target, no movement */
static void test_uniform_light_holds(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {600, 600, 600, 600};
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 45, 30);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_HOLD, "uniform light must hold");
    ASSERT_EQ(a.pan_delta, 0, "no pan delta on uniform light");
    ASSERT_EQ(a.tilt_delta, 0, "no tilt delta on uniform light");
    ASSERT_EQ(st.pan_deg, 45, "pan unchanged");
    ASSERT_EQ(st.tilt_deg, 30, "tilt unchanged");
}

/* brighter on the right -> azimuth steps right (positive) */
static void test_pan_toward_right(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {300, 700, 320, 690}; /* TL TR BL BR */
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 0, 0);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_MOVE, "must move");
    ASSERT_TRUE(a.pan_delta > 0, "pan must step toward the right pair");
    ASSERT_EQ(a.error_h, (700 + 690) - (300 + 320), "error_h reported");
    ASSERT_EQ(st.pan_deg, a.pan_delta, "state follows the commanded delta");
}

/* brighter on the left -> azimuth steps left (negative) */
static void test_pan_toward_left(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {800, 200, 780, 210};
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 180, 0);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_MOVE, "must move");
    ASSERT_TRUE(a.pan_delta < 0, "pan must step toward the left pair");
}

/* sun below the panel normal (bottom pair brightest) -> incline increases */
static void test_incline_toward_low_sun(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {300, 310, 700, 690};
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 0, 20);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_MOVE, "must move");
    ASSERT_TRUE(a.tilt_delta > 0, "incline must increase toward a low sun");
    ASSERT_TRUE(a.pan_delta == 0, "horizontal error inside deadband");
}

/* sun above the panel normal (top pair brightest) -> incline decreases */
static void test_incline_toward_high_sun(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {700, 690, 300, 310};
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 0, 60);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_MOVE, "must move");
    ASSERT_TRUE(a.tilt_delta < 0, "incline must decrease toward a high sun");
}

/* error inside the deadband counts as on target (hysteresis, no hunting) */
static void test_deadband_hysteresis(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {500, 515, 505, 510}; /* pair errors of a few counts */
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 10, 10);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_HOLD, "small contrast must hold");
    ASSERT_EQ(st.pan_deg, 10, "no pan hunting");
    ASSERT_EQ(st.tilt_deg, 10, "no tilt hunting");
}

/* larger errors take coarse steps, small errors take fine steps */
static void test_coarse_and_fine_steps(void)
{
    track_config_t cfg;
    track_state_t st;
    int ldr[4] = {100, 700, 100, 700}; /* error_h = 1200 -> coarse */
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 0, 40);
    a = track_update(&st, &cfg, ldr);
    ASSERT_EQ(a.pan_delta, cfg.coarse_step, "large error uses coarse step");

    ldr[0] = 400; ldr[2] = 400;         /* error_h = 600-400 + 600-400 = 400 */
    track_state_init(&st, 0, 40);
    a = track_update(&st, &cfg, ldr);
    /* 400 > coarse_threshold 250, still coarse; shrink further */
    ldr[1] = 520; ldr[3] = 520;         /* error_h = 240 -> fine */
    track_state_init(&st, 0, 40);
    a = track_update(&st, &cfg, ldr);
    ASSERT_EQ(a.pan_delta, cfg.fine_step, "small error uses fine step");
}

/* azimuth wraps at 360 degrees: continuous rotation */
static void test_pan_wraps_at_360(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {100, 900, 100, 900};
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 359, 45);
    a = track_update(&st, &cfg, ldr);

    ASSERT_TRUE(a.pan_delta > 0, "still stepping right");
    ASSERT_EQ(st.pan_deg, (359 + cfg.coarse_step) % 360, "azimuth wrapped");
    ASSERT_TRUE(st.pan_deg >= 0 && st.pan_deg < 360, "azimuth stays in 0..359");
}

/* when wrap is disabled the azimuth clamps to the mechanical window */
static void test_pan_clamps_without_wrap(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {100, 900, 100, 900};
    track_action_t a;

    track_config_default(&cfg);
    cfg.pan_wrap = 0;
    cfg.pan_min = 0;
    cfg.pan_max = 180;
    track_state_init(&st, 180, 45);
    a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(st.pan_deg, 180, "azimuth clamped at pan_max");
    ASSERT_EQ(a.kind, TRACK_ACTION_HOLD, "at a travel limit: hold");
    ASSERT_EQ(a.pan_delta, 0, "no phantom delta at the limit");
}

/* incline is bidirectional but bounded by tilt_min / tilt_max */
static void test_incline_limits(void)
{
    track_config_t cfg;
    track_state_t st;
    int ldr[4] = {300, 300, 800, 800}; /* drive incline up */
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 0, cfg.tilt_max);
    a = track_update(&st, &cfg, ldr);
    ASSERT_EQ(st.tilt_deg, cfg.tilt_max, "incline clamped at tilt_max");
    ASSERT_EQ(a.tilt_delta, 0, "no delta beyond the limit");

    ldr[0] = 800; ldr[1] = 800; ldr[2] = 300; ldr[3] = 300; /* drive down */
    track_state_init(&st, 0, cfg.tilt_min);
    a = track_update(&st, &cfg, ldr);
    ASSERT_EQ(st.tilt_deg, cfg.tilt_min, "incline clamped at tilt_min");
    ASSERT_EQ(a.tilt_delta, 0, "no delta beyond the limit");
}

/* dark sky parks the panel east, flat, and keeps it parked until dawn */
static void test_night_parks_and_dawn_resumes(void)
{
    track_config_t cfg;
    track_state_t st;
    const int night[4] = {20, 15, 25, 18};
    const int dawn[4] = {700, 690, 300, 310};
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 350, 60);

    a = track_update(&st, &cfg, night);
    ASSERT_EQ(a.kind, TRACK_ACTION_PARK, "first dark cycle parks");
    ASSERT_EQ(a.night, 1, "night flag set");
    ASSERT_EQ(st.pan_deg, cfg.park_pan, "parked at east azimuth");
    ASSERT_EQ(st.tilt_deg, cfg.park_tilt, "parked at runoff incline");
    ASSERT_TRUE(a.pan_delta > 0 && a.pan_delta <= 180, "shortest park path");

    a = track_update(&st, &cfg, night);
    ASSERT_EQ(a.kind, TRACK_ACTION_HOLD, "already parked: hold");
    ASSERT_EQ(a.night, 1, "night flag still set");

    a = track_update(&st, &cfg, dawn);
    ASSERT_EQ(a.night, 0, "dawn clears the night flag");
    ASSERT_TRUE(st.is_night == 0, "state leaves night mode");
    ASSERT_TRUE(a.pan_delta != 0 || a.tilt_delta != 0, "dawn movement");
}

/* repeated commands that never reduce the error latch a STALL and stop */
static void test_stall_latches_and_stops(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {100, 900, 100, 900}; /* error never changes */
    track_action_t a;
    int i;
    int saw_stall = 0;
    int pan_after_stall = -1;

    track_config_default(&cfg);
    track_state_init(&st, 0, 45);

    for (i = 0; i < 40; i++) {
        a = track_update(&st, &cfg, ldr);
        if (a.kind == TRACK_ACTION_STALL) {
            saw_stall = 1;
            pan_after_stall = st.pan_deg;
            break;
        }
    }
    ASSERT_TRUE(saw_stall, "stall must latch");
    ASSERT_TRUE(i >= cfg.stall_limit, "stall only after stall_limit moves");
    ASSERT_EQ(a.stalled, 1, "stalled flag reported");

    for (i = 0; i < 10; i++) a = track_update(&st, &cfg, ldr);
    ASSERT_TRUE(a.stalled == 1 || a.kind == TRACK_ACTION_STALL,
                "stall latch holds");
    ASSERT_EQ(st.pan_deg, pan_after_stall, "motors commanded to stop");

    /* light situation changes (error shrinks) -> latch clears, motion resumes */
    {
        const int ldr2[4] = {200, 700, 200, 700};
        a = track_update(&st, &cfg, ldr2);
        ASSERT_EQ(a.stalled, 0, "smaller error clears the latch");
        ASSERT_EQ(a.kind, TRACK_ACTION_MOVE, "tracking resumes");
    }
}

/* a trained operator service action (panel swap) must clear the latch */
static void test_track_resume_clears_stall(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {100, 900, 100, 900};
    int i;

    track_config_default(&cfg);
    track_state_init(&st, 0, 45);
    for (i = 0; i < 40; i++) {
        track_action_t a = track_update(&st, &cfg, ldr);
        if (a.kind == TRACK_ACTION_STALL) break;
    }
    ASSERT_TRUE(st.stalled == 1, "stalled before service");

    track_resume(&st);
    ASSERT_EQ(st.stalled, 0, "service clears the stall latch");
    ASSERT_EQ(st.stall_cycles, 0, "service resets the counter");
}

/* uniform but dim light (overcast / forest shade) holds instead of hunting */
static void test_diffuse_light_holds(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {300, 312, 296, 305};
    track_action_t a;
    int i;

    track_config_default(&cfg);
    track_state_init(&st, 123, 34);
    for (i = 0; i < 20; i++) a = track_update(&st, &cfg, ldr);

    ASSERT_EQ(a.kind, TRACK_ACTION_HOLD, "diffuse light must hold");
    ASSERT_EQ(st.pan_deg, 123, "no azimuth hunting in shade");
    ASSERT_EQ(st.tilt_deg, 34, "no incline hunting in shade");
}

/* boundary: brightest quadrant exactly at the night threshold is still day */
static void test_night_threshold_boundary(void)
{
    track_config_t cfg;
    track_state_t st;
    const int ldr[4] = {90, 10, 10, 10}; /* max == night_threshold */
    track_action_t a;

    track_config_default(&cfg);
    track_state_init(&st, 12, 34);
    a = track_update(&st, &cfg, ldr);
    ASSERT_EQ(a.night, 0, "threshold is exclusive: still day");
}

int main(void)
{
    printf("360 rotatory solar-panel tracking logic - unit tests\n");
    printf("---------------------------------------------------\n");
    RUN(test_uniform_light_holds);
    RUN(test_pan_toward_right);
    RUN(test_pan_toward_left);
    RUN(test_incline_toward_low_sun);
    RUN(test_incline_toward_high_sun);
    RUN(test_deadband_hysteresis);
    RUN(test_coarse_and_fine_steps);
    RUN(test_pan_wraps_at_360);
    RUN(test_pan_clamps_without_wrap);
    RUN(test_incline_limits);
    RUN(test_night_parks_and_dawn_resumes);
    RUN(test_stall_latches_and_stops);
    RUN(test_track_resume_clears_stall);
    RUN(test_diffuse_light_holds);
    RUN(test_night_threshold_boundary);
    printf("---------------------------------------------------\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
