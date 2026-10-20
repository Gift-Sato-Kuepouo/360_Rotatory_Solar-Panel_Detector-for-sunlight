/*
 * 360 Rotatory Solar-Panel Detector for Sunlight - firmware
 * ----------------------------------------------------------
 * Dual-axis tracker:
 *   Axis 1 - azimuth (pan) : continuous 360 deg rotation about the mast
 *   Axis 2 - incline (tilt): leans the panel left/right, -10..100 deg window
 *
 * Sensing: four quadrant LDRs (TL/TR/BL/BR) as 10k dividers into A0..A3,
 *          matching the Fritzing design in hardware/.
 * Actuators: pan servo on D9, incline servo on D10 (J1/J2 in the Fritzing file).
 *
 * The full tracking algorithm lives in the portable `tracking_logic` library
 * (firmware/libraries/tracking_logic) so the exact code that runs on the
 * Arduino is unit-tested on every push by GitHub Actions.
 *
 * Serial commands (115200 baud):
 *   c  clear the stall latch (run after swapping a damaged panel)
 *   s  print current state
 *   h  print help
 */

#include <Servo.h>
#include <tracking_logic.h>

/* ------------------------------- pin map -------------------------------- */
static const uint8_t LDR_PIN[4] = {A0, A1, A2, A3}; /* TL, TR, BL, BR */
static const uint8_t PAN_SERVO_PIN = 9;             /* J1 - azimuth drive  */
static const uint8_t TILT_SERVO_PIN = 10;           /* J2 - incline drive  */
static const uint8_t STATUS_LED_PIN = LED_BUILTIN;

/* The demo gimbal maps the logical 0..359 deg azimuth onto the pan servo's
 * 0..180 deg travel (turntable gearing). The field build replaces the pan
 * servo with a geared slip-ring drive for true continuous rotation; this
 * mapping is the only line that changes. */
static const int PAN_SERVO_MIN_DEG = 0;
static const int PAN_SERVO_MAX_DEG = 180;

static const int TILT_SERVO_MIN_DEG = 0;
static const int TILT_SERVO_MAX_DEG = 180;

static const unsigned long SAMPLE_INTERVAL_MS = 200;
static const uint8_t LDR_OVERSAMPLE = 8;

static Servo panServo;
static Servo tiltServo;
static track_config_t cfg;
static track_state_t st;
static unsigned long lastSampleMs = 0;

/* ------------------------------ sensing --------------------------------- */
static int readLdr(uint8_t pin)
{
    long sum = 0;
    for (uint8_t i = 0; i < LDR_OVERSAMPLE; i++) {
        sum += analogRead(pin);
        delayMicroseconds(200);
    }
    return (int)(sum / LDR_OVERSAMPLE);
}

static void readQuadrants(int ldr[4])
{
    for (uint8_t i = 0; i < 4; i++) ldr[i] = readLdr(LDR_PIN[i]);
}

/* ------------------------------ actuators -------------------------------- */
static int panToServo(int deg)
{
    long v = map(deg, 0, 359, PAN_SERVO_MIN_DEG, PAN_SERVO_MAX_DEG);
    if (v < 0) v = 0;
    if (v > 180) v = 180;
    return (int)v;
}

static int tiltToServo(int deg)
{
    long v = map(deg, cfg.tilt_min, cfg.tilt_max, TILT_SERVO_MIN_DEG,
                 TILT_SERVO_MAX_DEG);
    if (v < 0) v = 0;
    if (v > 180) v = 180;
    return (int)v;
}

static void applyState(void)
{
    panServo.write(panToServo(st.pan_deg));
    tiltServo.write(tiltToServo(st.tilt_deg));
}

/* ------------------------------ telemetry -------------------------------- */
static const char *actionName(track_action_kind_t k)
{
    switch (k) {
    case TRACK_ACTION_HOLD:  return "HOLD";
    case TRACK_ACTION_MOVE:  return "MOVE";
    case TRACK_ACTION_PARK:  return "PARK";
    case TRACK_ACTION_STALL: return "STALL";
    }
    return "?";
}

static void printTelemetry(const track_action_t *act, const int ldr[4])
{
    Serial.print(millis());
    Serial.print(F(",pan="));   Serial.print(st.pan_deg);
    Serial.print(F(",tilt="));  Serial.print(st.tilt_deg);
    Serial.print(F(",ldr="));
    for (uint8_t i = 0; i < 4; i++) {
        Serial.print(ldr[i]);
        Serial.print(i < 3 ? '/' : ' ');
    }
    Serial.print(F(",eh="));   Serial.print(act->error_h);
    Serial.print(F(",ev="));   Serial.print(act->error_v);
    Serial.print(F(",act="));  Serial.print(actionName(act->kind));
    if (act->stalled) Serial.print(F(",STALLED"));
    if (act->night)   Serial.print(F(",NIGHT"));
    Serial.println();
}

static void printState(void)
{
    Serial.print(F("pan="));  Serial.print(st.pan_deg);
    Serial.print(F(" tilt=")); Serial.print(st.tilt_deg);
    Serial.print(F(" night=")); Serial.print(st.is_night);
    Serial.print(F(" stalled=")); Serial.println(st.stalled);
}

static void printHelp(void)
{
    Serial.println(F("c = clear stall latch (after panel swap)"));
    Serial.println(F("s = print state"));
    Serial.println(F("h = help"));
}

static void handleSerial(void)
{
    while (Serial.available() > 0) {
        switch (Serial.read()) {
        case 'c':
        case 'C':
            track_resume(&st);
            Serial.println(F("stall latch cleared - tracking resumed"));
            break;
        case 's':
        case 'S':
            printState();
            break;
        default:
            printHelp();
            break;
        }
    }
}

/* ------------------------------ main loop -------------------------------- */
void setup(void)
{
    Serial.begin(115200);
    track_config_default(&cfg);
    track_state_init(&st, cfg.park_pan, cfg.park_tilt);

    pinMode(STATUS_LED_PIN, OUTPUT);
    panServo.attach(PAN_SERVO_PIN);
    tiltServo.attach(TILT_SERVO_PIN);
    applyState();

    Serial.println(F("360 rotatory solar-panel tracker ready"));
    printHelp();
}

void loop(void)
{
    unsigned long now = millis();
    if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
        handleSerial();
        return;
    }
    lastSampleMs = now;

    int ldr[4];
    readQuadrants(ldr);

    track_action_t act = track_update(&st, &cfg, ldr);
    applyState();

    if (act.night) {
        digitalWrite(STATUS_LED_PIN, LOW);
    } else if (act.stalled) {
        digitalWrite(STATUS_LED_PIN, (now / 250) % 2); /* blink = stalled */
    } else {
        digitalWrite(STATUS_LED_PIN, HIGH);
    }

    printTelemetry(&act, ldr);
    handleSerial();
}
