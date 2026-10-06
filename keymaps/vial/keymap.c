#include QMK_KEYBOARD_H
#include <math.h>
#include <stdlib.h>
#include "ws2812.h"
#include "via.h"

// Forward declarations for Cirque runtime API (defined in drivers/sensors/cirque_pinnacle*.c)
// These are not in the keymap include path but are resolved at link time.
void cirque_pinnacle_enable_circular_scroll(bool enable);
void cirque_pinnacle_configure_circular_scroll(uint8_t outer_ring_pct, uint8_t trigger_px, uint16_t trigger_ang, uint8_t wheel_clicks, bool left_handed);
bool cirque_pinnacle_set_adc_attenuation(uint8_t adcGain);
void cirque_pinnacle_enable_feed(bool feedEnable);
void cirque_pinnacle_enable_tap(bool enable);
void cirque_pinnacle_enable_cursor_glide(bool enable);
void cirque_pinnacle_cursor_smoothing(bool enable);
void cirque_pinnacle_set_cpi(uint16_t cpi);

#ifndef M_PI
#    define M_PI 3.14159265358979323846f
#endif

// ==========================================
// 1. LAYER ENUMERATIONS
// ==========================================
enum layers {
    _LAYER_0 = 0, // Default: Navigation / Standard Mouse & Encoders
    _LAYER_1,     // Media & Productivity (LED: User-selected color)
    _LAYER_2,     // Screensaver / Anti-Sleep Mode (LED: Rainbow Color Fade)
    _LAYER_3      // Transparent / Expansion
};

// ==========================================
// 2. CUSTOM KEYCODES
// ==========================================
enum custom_keycodes {
    SW_LAYER = SAFE_RANGE, // Roller Button: Tap -> Layer 0/1 Toggle, Long Press -> Screensaver (Layer 2)
    SCRN_SPD_DN,           // Layer 2: Decrease screensaver speed
    SCRN_SPD_UP,           // Layer 2: Increase screensaver speed
    SCRN_PAT_PREV,         // Layer 2: Previous screensaver animation pattern
    SCRN_PAT_NEXT,         // Layer 2: Next screensaver animation pattern
    TP_CIRC_TOG,           // User keycode: Toggle circular scroll on/off
    TP_SENS_CYC,           // User keycode: Cycle sensitivity (Level 1 -> 2 -> 3 -> 4)
    SCRN_TOG               // User keycode: Toggle screensaver mode
};

// ==========================================
// 3. VIAL LAYOUT OPTIONS & HARDWARE STATE
// ==========================================
// Sensitivity levels (ADC attenuation) — reversed so higher level = more sensitive:
//   index 0 = 4X attenuation (least sensitive)
//   index 1 = 3X attenuation
//   index 2 = 2X attenuation  ← default
//   index 3 = 1X attenuation  (most sensitive, no attenuation)
static const uint8_t sens_adc_values[] = { 0xC0, 0x80, 0x40, 0x00 };
#define NUM_SENS_LEVELS 4

// CPI / Pointer speed presets
static const uint16_t cpi_presets[] = { 600, 800, 1000, 1400, 1800 };
#define NUM_CPI_PRESETS 5

// Auto-Idle timeout options (in milliseconds)
static const uint32_t timeout_presets_ms[] = { 0, 60000, 180000, 300000, 600000 };

// Layer 1 LED color presets (R, G, B)
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

static const led_color_t layer1_colors[] = {
    {  0,   0,  60 }, // 0: Solid Blue (Default)
    {  0,  45,  45 }, // 1: Cyan
    {  0,  60,   0 }, // 2: Green
    { 50,   0,  50 }, // 3: Magenta / Purple
    { 40,  40,   0 }, // 4: Yellow
    { 60,  20,   0 }, // 5: Orange
    { 60,   0,   0 }, // 6: Red
    { 35,  35,  35 }, // 7: White
    {  0,   0,   0 }  // 8: LED Off
};
#define NUM_LED_COLORS 9

// Runtime configurable settings (bound to Vial Layouts tab)
static bool    tp_circ_scroll       = false; // Circular scroll enable (bit 26)
static uint8_t tp_sens_index        = 2;     // Sensitivity: Level 3 (2X default) (bits 24..25)
static uint8_t tp_cpi_index         = 2;     // CPI: 1000 CPI default (bits 21..23)
static bool    tp_smoothing         = true;  // Hardware smoothing filter (bit 20)
static bool    tp_tap_enable        = true;  // Tap-to-click (bit 19)
static bool    tp_glide_enable      = true;  // Cursor glide / inertial coasting (bit 18)
static uint8_t tp_accel_level       = 2;     // Touchpad acceleration curve (bits 15..17: 0=Linear, 1=Mild 2x, 2=Balanced 3.5x, 3=Aggressive 5x, 4=Ultra 7x)
static uint8_t tp_prec_mode         = 1;     // Touchpad precision mode (bits 13..14: 0=1.0x, 1=0.75x slow, 2=0.50x slow)
static bool    tp_circ_scroll_dir   = false; // Circular scroll direction (0=Down, 1=Up) (bit 12)
static uint8_t tp_circ_ring_width   = 1;     // Ring width (0=15%, 1=25%, 2=35%) (bits 10..11)
static uint8_t roller_accel_level   = 2;     // Scroll wheel acceleration (bits 8..9: 0=Linear, 1=Mild 4x, 2=Std 8x, 3=Aggressive 12x)
static bool    roller_invert_dir    = false; // Invert scroll wheel direction (bit 7)
static uint8_t auto_idle_timeout_idx= 0;     // Screensaver auto-idle timeout index (bits 4..6)
static uint8_t layer1_color_idx     = 0;     // Layer 1 LED color index (bits 0..3)

static uint32_t last_activity_time  = 0;     // Timestamp of last user movement/click

static void touchpad_apply(void) {
    cirque_pinnacle_enable_circular_scroll(tp_circ_scroll);

    uint8_t ring_pct = (tp_circ_ring_width == 0) ? 15 : ((tp_circ_ring_width == 1) ? 25 : 35);
    cirque_pinnacle_configure_circular_scroll(ring_pct, 10, 8192, 100, tp_circ_scroll_dir);

    cirque_pinnacle_set_adc_attenuation(sens_adc_values[tp_sens_index]);
    cirque_pinnacle_set_cpi(cpi_presets[tp_cpi_index]);
    cirque_pinnacle_cursor_smoothing(tp_smoothing);
    cirque_pinnacle_enable_tap(tp_tap_enable);
    cirque_pinnacle_enable_cursor_glide(tp_glide_enable);
    cirque_pinnacle_enable_feed(true);
}

// VIA / Vial layout options hook
// Vial GUI packs labels in big-endian order (first label = MSB, last label = LSB):
//   bits 0..3:  "Layer 1 LED Color" (4 bits, 0..8)
//   bits 4..6:  "Screensaver Auto-Timeout" (3 bits, 0..4)
//   bit 7:      "Invert Scroll Wheel Direction" (1 bit, 0..1)
//   bits 8..9:  "Scroll Wheel Acceleration" (2 bits, 0..3)
//   bits 10..11:"Circular Scroll Ring Width" (2 bits, 0..2)
//   bit 12:     "Circular Scroll Direction" (1 bit, 0..1)
//   bits 13..14:"Touchpad Precision Mode" (2 bits, 0..2)
//   bits 15..17:"Touchpad Acceleration" (3 bits, 0..4)
//   bit 18:     "Cursor Glide" (1 bit, 0..1)
//   bit 19:     "Tap to Click" (1 bit, 0..1)
//   bit 20:     "Hardware Smoothing" (1 bit, 0..1)
//   bits 21..23:"Pointer Speed / CPI" (3 bits, 0..4)
//   bits 24..25:"Touchpad Sensitivity" (2 bits, 0..3)
//   bit 26:     "Circular Scroll" (1 bit, 0..1)
void via_set_layout_options_kb(uint32_t value) {
    layer1_color_idx      = value & 0x0F;
    auto_idle_timeout_idx = (value >> 4)  & 0x07;
    roller_invert_dir     = (value >> 7)  & 0x01;
    roller_accel_level    = (value >> 8)  & 0x03;
    tp_circ_ring_width    = (value >> 10) & 0x03;
    tp_circ_scroll_dir    = (value >> 12) & 0x01;
    tp_prec_mode          = (value >> 13) & 0x03;
    tp_accel_level        = (value >> 15) & 0x07;
    tp_glide_enable       = (value >> 18) & 0x01;
    tp_tap_enable         = (value >> 19) & 0x01;
    tp_smoothing          = (value >> 20) & 0x01;
    tp_cpi_index          = (value >> 21) & 0x07;
    tp_sens_index         = (value >> 24) & 0x03;
    tp_circ_scroll        = (value >> 26) & 0x01;

    if (layer1_color_idx >= NUM_LED_COLORS)    layer1_color_idx = 0;
    if (auto_idle_timeout_idx > 4)             auto_idle_timeout_idx = 0;
    if (roller_accel_level > 3)                roller_accel_level = 2;
    if (tp_circ_ring_width > 2)                tp_circ_ring_width = 1;
    if (tp_prec_mode > 2)                      tp_prec_mode = 1;
    if (tp_accel_level > 4)                    tp_accel_level = 2;
    if (tp_cpi_index >= NUM_CPI_PRESETS)       tp_cpi_index = 2;
    if (tp_sens_index >= NUM_SENS_LEVELS)     tp_sens_index = 2;

    touchpad_apply();
}

// ==========================================
// 4. SCREENSAVER & NATURAL HUMAN SIMULATOR
// ==========================================
#define SCREENSAVER_UPDATE_INTERVAL 16 // ~60 FPS (ms)
#define NUM_PATTERNS 4

// Pattern 0: Human Simulator (Default on screensaver entry)
// Pattern 1: Pong / DVD Logo Bouncer
// Pattern 2: Smooth Circular Orbit
// Pattern 3: Lissajous Figure-8
static uint8_t screensaver_speed   = 4; // Speed scale: 1 (slow) to 10 (fast)
static uint8_t screensaver_pattern = 0; // Default: 0 (Human Simulator)

// --- Human Movement Simulation State Machine ---
enum human_jiggle_state {
    HUMAN_IDLE_WAIT,   // Mouse sits completely still (reading/thinking/idle)
    HUMAN_MOVING       // Smooth human-like curve movement towards target
};

static uint8_t  human_state            = HUMAN_IDLE_WAIT;
static uint32_t human_state_timer      = 0;
static uint32_t human_pause_duration   = 1500; // Duration of stillness in ms
static uint32_t human_move_duration    = 800;  // Duration of current stroke in ms
static float    human_cur_x            = 0.0f;
static float    human_cur_y            = 0.0f;
static float    human_start_x          = 0.0f;
static float    human_start_y          = 0.0f;
static float    human_target_x         = 0.0f;
static float    human_target_y         = 0.0f;
static float    human_ctrl_x           = 0.0f;
static float    human_ctrl_y           = 0.0f;

// Virtual Screen Boundary for Pong Bounce
#define VIRTUAL_BOX_W 960.0f
#define VIRTUAL_BOX_H 540.0f

static float screensaver_x     = 0.0f;
static float screensaver_y     = 0.0f;
static float screensaver_vx    = 3.5f;
static float screensaver_vy    = 2.5f;
static float screensaver_angle = 0.0f;

// Simple fast pseudo-random number generator for embedded
static uint32_t rng_state = 123456789;
static uint32_t pseudo_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float rand_range(float min_val, float max_val) {
    float norm = (float)(pseudo_rand() % 10000) / 10000.0f;
    return min_val + norm * (max_val - min_val);
}

static void screensaver_step(int8_t *dx, int8_t *dy) {
    float speed_mult = (float)screensaver_speed / 4.0f;

    switch (screensaver_pattern) {
        case 0: { // Pattern 0: Natural Human Movement Simulation (Organic & Undetectable)
            uint32_t now = timer_read32();

            if (human_state == HUMAN_IDLE_WAIT) {
                *dx = 0;
                *dy = 0;
                // Wait for random pause to finish (scaled by user screensaver speed)
                uint32_t scaled_pause = (uint32_t)((float)human_pause_duration / speed_mult);
                if (timer_elapsed32(human_state_timer) >= scaled_pause) {
                    human_state       = HUMAN_MOVING;
                    human_state_timer = now;
                    human_start_x     = human_cur_x;
                    human_start_y     = human_cur_y;

                    // Pick random destination within realistic human desk stroke distance (30 to 180 pixels)
                    float dist = rand_range(30.0f, 180.0f);
                    float ang  = rand_range(0.0f, 2.0f * (float)M_PI);
                    human_target_x = human_cur_x + dist * cosf(ang);
                    human_target_y = human_cur_y + dist * sinf(ang);

                    // Clamp to bounds so cursor doesn't drift off screen forever
                    if (human_target_x > 400.0f)  human_target_x = 400.0f;
                    if (human_target_x < -400.0f) human_target_x = -400.0f;
                    if (human_target_y > 250.0f)  human_target_y = 250.0f;
                    if (human_target_y < -250.0f) human_target_y = -250.0f;

                    // Arc control point for natural human hand curve
                    float mid_x = (human_start_x + human_target_x) * 0.5f;
                    float mid_y = (human_start_y + human_target_y) * 0.5f;
                    float perp_offset = rand_range(-25.0f, 25.0f);
                    human_ctrl_x = mid_x - sinf(ang) * perp_offset;
                    human_ctrl_y = mid_y + cosf(ang) * perp_offset;

                    // Human stroke duration: 400ms to 900ms
                    human_move_duration = (uint32_t)rand_range(400.0f, 900.0f);
                }
            } else { // HUMAN_MOVING
                uint32_t elapsed = timer_elapsed32(human_state_timer);
                uint32_t scaled_move = (uint32_t)((float)human_move_duration / speed_mult);
                if (scaled_move < 100) scaled_move = 100;

                float t = (float)elapsed / (float)scaled_move;
                if (t >= 1.0f) {
                    t = 1.0f;
                    human_state       = HUMAN_IDLE_WAIT;
                    human_state_timer = now;
                    // Next idle pause: between 1.5 seconds and 5.5 seconds
                    human_pause_duration = (uint32_t)rand_range(1500.0f, 5500.0f);
                }

                // Minimum-jerk trajectory (classic human motor control S-curve: 10t^3 - 15t^4 + 6t^5)
                float u = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);

                // Quadratic Bezier interpolation
                float one_minus_u = 1.0f - u;
                float new_x = one_minus_u * one_minus_u * human_start_x + 2.0f * one_minus_u * u * human_ctrl_x + u * u * human_target_x;
                float new_y = one_minus_u * one_minus_u * human_start_y + 2.0f * one_minus_u * u * human_ctrl_y + u * u * human_target_y;

                float step_dx = new_x - human_cur_x;
                float step_dy = new_y - human_cur_y;
                human_cur_x = new_x;
                human_cur_y = new_y;

                // Occasional subtle human micro-tremor (+/- 1 pixel noise)
                int8_t jitter_x = ((pseudo_rand() % 10) == 0) ? (int8_t)((pseudo_rand() % 3) - 1) : 0;
                int8_t jitter_y = ((pseudo_rand() % 10) == 0) ? (int8_t)((pseudo_rand() % 3) - 1) : 0;

                *dx = (int8_t)roundf(step_dx) + jitter_x;
                *dy = (int8_t)roundf(step_dy) + jitter_y;
            }
            break;
        }
        case 1: { // Pattern 1: Pong / DVD Logo Bouncer
            screensaver_x += screensaver_vx * speed_mult;
            screensaver_y += screensaver_vy * speed_mult;

            if (screensaver_x >= VIRTUAL_BOX_W) {
                screensaver_vx = -fabsf(screensaver_vx);
                screensaver_x  = VIRTUAL_BOX_W;
            } else if (screensaver_x <= -VIRTUAL_BOX_W) {
                screensaver_vx = fabsf(screensaver_vx);
                screensaver_x  = -VIRTUAL_BOX_W;
            }

            if (screensaver_y >= VIRTUAL_BOX_H) {
                screensaver_vy = -fabsf(screensaver_vy);
                screensaver_y  = VIRTUAL_BOX_H;
            } else if (screensaver_y <= -VIRTUAL_BOX_H) {
                screensaver_vy = fabsf(screensaver_vy);
                screensaver_y  = -VIRTUAL_BOX_H;
            }

            *dx = (int8_t)roundf(screensaver_vx * speed_mult);
            *dy = (int8_t)roundf(screensaver_vy * speed_mult);
            break;
        }
        case 2: { // Pattern 2: Smooth Circular Orbit
            screensaver_angle += 0.06f * speed_mult;
            if (screensaver_angle >= 2.0f * (float)M_PI) {
                screensaver_angle -= 2.0f * (float)M_PI;
            }
            float radius = 10.0f * speed_mult;
            *dx = (int8_t)roundf(-radius * sinf(screensaver_angle));
            *dy = (int8_t)roundf(radius * cosf(screensaver_angle));
            break;
        }
        case 3: { // Pattern 3: Lissajous / Figure-8
            screensaver_angle += 0.05f * speed_mult;
            if (screensaver_angle >= 2.0f * (float)M_PI) {
                screensaver_angle -= 2.0f * (float)M_PI;
            }
            float amp = 12.0f * speed_mult;
            *dx = (int8_t)roundf(amp * cosf(screensaver_angle));
            *dy = (int8_t)roundf(amp * cosf(2.0f * screensaver_angle));
            break;
        }
        default:
            *dx = 0;
            *dy = 0;
            break;
    }
}

// ==========================================
// 5. ROTARY ENCODERS & SCROLL ACCELERATION
// ==========================================
static uint32_t last_scroll_time = 0;
static int8_t   roller_accum_v   = 0;
static int8_t   roller_accum_h   = 0;

bool encoder_update_user(uint8_t index, bool clockwise) {
    return true;
}

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_LAYER_0] = {
        // Encoder 0 (EC11): Volume Down, Volume Up
        ENCODER_CCW_CW(KC_VOLD, KC_VOLU),
        // Encoder 1 (EVQWGD001 Roller): Scroll Down, Scroll Up
        ENCODER_CCW_CW(MS_WHLD, MS_WHLU)
    },
    [_LAYER_1] = {
        // Encoder 0 (EC11): Video Seek 5s Back (Left Arrow), Seek 5s Forward (Right Arrow)
        ENCODER_CCW_CW(KC_LEFT, KC_RIGHT),
        // Encoder 1 (EVQWGD001 Roller): Horizontal Scroll Left, Right
        ENCODER_CCW_CW(MS_WHLL, MS_WHLR)
    },
    [_LAYER_2] = {
        // Encoder 0 (EC11): Screensaver Speed Down, Speed Up
        ENCODER_CCW_CW(SCRN_SPD_DN, SCRN_SPD_UP),
        // Encoder 1 (EVQWGD001 Roller): Screensaver Pattern Previous, Next
        ENCODER_CCW_CW(SCRN_PAT_PREV, SCRN_PAT_NEXT)
    },
    [_LAYER_3] = {
        ENCODER_CCW_CW(KC_TRNS, KC_TRNS),
        ENCODER_CCW_CW(KC_TRNS, KC_TRNS)
    }
};
#endif

// ==========================================
// 6. PUSH-BUTTON SWITCH MAPPINGS (4 Layers)
// ==========================================
// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_LAYER_0] = LAYOUT(
        KC_MUTE,    // EC11 Button (GP20): Mute
        SW_LAYER    // EVQWGD001 Button (GP29): Tap -> Layer 1, Long-press -> Screensaver (Layer 2)
    ),
    [_LAYER_1] = LAYOUT(
        KC_MPLY,    // EC11 Button (GP20): Media Play / Pause Video
        SW_LAYER    // EVQWGD001 Button (GP29): Tap -> Layer 0, Long-press -> Screensaver (Layer 2)
    ),
    [_LAYER_2] = LAYOUT(
        KC_TRNS,    // EC11 Button (GP20): Exit Screensaver (wake to Layer 0)
        SW_LAYER    // EVQWGD001 Button (GP29): Exit Screensaver (wake to Layer 0)
    ),
    [_LAYER_3] = LAYOUT(
        KC_TRNS,
        KC_TRNS
    )
};
// clang-format on

// ==========================================
// 7. BUTTON TIMING & TAP / LONG-PRESS LOGIC
// ==========================================
static uint32_t roller_press_time   = 0;
static bool     roller_long_pressed = false;

static void enter_screensaver(void) {
    screensaver_pattern   = 0; // Always start in Human Simulator (Pattern 0)
    human_state           = HUMAN_IDLE_WAIT;
    human_state_timer     = timer_read32();
    human_pause_duration  = 1200;
    human_cur_x           = 0.0f;
    human_cur_y           = 0.0f;
    layer_move(_LAYER_2);
}

void matrix_scan_user(void) {
    // Check if roller button is held for >= 400ms to trigger Screensaver
    if (roller_press_time != 0 && !roller_long_pressed) {
        if (timer_elapsed32(roller_press_time) >= 400) {
            roller_long_pressed = true;
            enter_screensaver();
        }
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    last_activity_time = timer_read32();

    // 1. SCREENSAVER MODE (Layer 2): DIRECT ENCODER INTERCEPT
    if (get_highest_layer(layer_state) == _LAYER_2) {
        if (IS_ENCODEREVENT(record->event)) {
            if (record->event.pressed) {
                bool clockwise = (record->event.key.row == KEYLOC_ENCODER_CW);
                if (record->event.key.col == 1) { // EVQWGD001 Roller (Scroll Wheel): Cycle patterns
                    if (clockwise) {
                        screensaver_pattern = (screensaver_pattern + 1) % NUM_PATTERNS;
                    } else {
                        screensaver_pattern = (screensaver_pattern == 0) ? (NUM_PATTERNS - 1) : (screensaver_pattern - 1);
                    }
                    if (screensaver_pattern == 0) {
                        human_state          = HUMAN_IDLE_WAIT;
                        human_state_timer    = timer_read32();
                        human_pause_duration = 1500;
                        human_cur_x          = 0.0f;
                        human_cur_y          = 0.0f;
                    }
                } else if (record->event.key.col == 0) { // EC11 Knob: Adjust speed
                    if (clockwise) {
                        if (screensaver_speed < 10) screensaver_speed++;
                    } else {
                        if (screensaver_speed > 1) screensaver_speed--;
                    }
                }
            }
            return false; // Intercepted completely: do not send scroll or keys to host
        }

        // Any physical button press wakes up from Screensaver to Layer 0
        if (record->event.pressed) {
            layer_move(_LAYER_0);
            return false;
        }
    }

    // 2. EVQWGD001 Roller (Encoder index 1) in Normal Layers: Acceleration & Inversion
    if (IS_ENCODEREVENT(record->event) && record->event.key.col == 1) {
        if (keycode == MS_WHLU || keycode == MS_WHLD || keycode == MS_WHLL || keycode == MS_WHLR) {
            if (record->event.pressed) {
                uint32_t now      = timer_read32();
                uint32_t interval = timer_elapsed32(last_scroll_time);
                last_scroll_time  = now;

                int8_t mult = 1;
                switch (roller_accel_level) {
                    case 1: // Mild (up to 4x)
                        if (interval < 40) mult = 4;
                        else if (interval < 80) mult = 2;
                        break;
                    case 2: // Standard (up to 8x - Recommended)
                        if (interval < 35) mult = 8;
                        else if (interval < 65) mult = 4;
                        else if (interval < 100) mult = 2;
                        break;
                    case 3: // Aggressive (up to 12x)
                        if (interval < 30) mult = 12;
                        else if (interval < 50) mult = 8;
                        else if (interval < 75) mult = 4;
                        else if (interval < 110) mult = 2;
                        break;
                    default: // 0: Disabled (linear 1x)
                        mult = 1;
                        break;
                }

                bool is_up_or_right = (keycode == MS_WHLU || keycode == MS_WHLR);
                if (roller_invert_dir) {
                    is_up_or_right = !is_up_or_right;
                }

                if (keycode == MS_WHLL || keycode == MS_WHLR) {
                    roller_accum_h += is_up_or_right ? mult : -mult;
                } else {
                    roller_accum_v += is_up_or_right ? mult : -mult;
                }
            }
            return false; // Handled directly in mouse report
        }

        return true; // Let user-remapped keycodes run normally
    }

    switch (keycode) {
        case SW_LAYER:
            if (record->event.pressed) {
                roller_press_time   = timer_read32();
                roller_long_pressed = false;
            } else {
                if (!roller_long_pressed && roller_press_time != 0) {
                    // Short tap: toggle between Layer 0 and Layer 1
                    if (get_highest_layer(layer_state) == _LAYER_0) {
                        layer_move(_LAYER_1);
                    } else {
                        layer_move(_LAYER_0);
                    }
                }
                roller_press_time   = 0;
                roller_long_pressed = false;
            }
            return false;

        case SCRN_TOG:
            if (record->event.pressed) {
                if (get_highest_layer(layer_state) == _LAYER_2) {
                    layer_move(_LAYER_0);
                } else {
                    enter_screensaver();
                }
            }
            return false;

        case SCRN_SPD_DN:
            if (record->event.pressed) {
                if (screensaver_speed > 1) screensaver_speed--;
            }
            return false;

        case SCRN_SPD_UP:
            if (record->event.pressed) {
                if (screensaver_speed < 10) screensaver_speed++;
            }
            return false;

        case SCRN_PAT_PREV:
            if (record->event.pressed) {
                if (screensaver_pattern == 0) {
                    screensaver_pattern = NUM_PATTERNS - 1;
                } else {
                    screensaver_pattern--;
                }
            }
            return false;

        case SCRN_PAT_NEXT:
            if (record->event.pressed) {
                screensaver_pattern = (screensaver_pattern + 1) % NUM_PATTERNS;
            }
            return false;

        case TP_CIRC_TOG:
            if (record->event.pressed) {
                tp_circ_scroll = !tp_circ_scroll;
                touchpad_apply();
            }
            return false;

        case TP_SENS_CYC:
            if (record->event.pressed) {
                tp_sens_index = (tp_sens_index + 1) % NUM_SENS_LEVELS;
                touchpad_apply();
            }
            return false;

        default:
            return true;
    }
}

// ==========================================
// 8. POINTING DEVICE INTERCEPT & DYNAMIC BALLISTICS
// ==========================================
report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (mouse_report.x != 0 || mouse_report.y != 0 || mouse_report.buttons != 0) {
        last_activity_time = timer_read32();
    }

    if (get_highest_layer(layer_state) == _LAYER_2) {
        // In Screensaver mode: deliberate touch (> 3 counts) or click wakes up to Layer 0
        if (abs(mouse_report.x) > 3 || abs(mouse_report.y) > 3 || mouse_report.buttons != 0) {
            layer_move(_LAYER_0);
            return mouse_report;
        }

        // Generate screensaver motion
        static uint32_t last_screensaver_tick = 0;
        if (timer_elapsed32(last_screensaver_tick) >= SCREENSAVER_UPDATE_INTERVAL) {
            last_screensaver_tick = timer_read32();
            int8_t dx = 0;
            int8_t dy = 0;
            screensaver_step(&dx, &dy);
            mouse_report.x = dx;
            mouse_report.y = dy;
        }
        return mouse_report;
    }

    // Dynamic Touchpad Ballistics & Variable Speed (Excel micro-precision + high-speed flick)
    if (mouse_report.x != 0 || mouse_report.y != 0) {
        int16_t rx = mouse_report.x;
        int16_t ry = mouse_report.y;
        float dist = sqrtf((float)rx * (float)rx + (float)ry * (float)ry);

        if (dist > 0.001f) {
            // 1. Base Precision dampening factor for slow movements (Excel cell border, fine targeting)
            float base_mult = 1.0f;
            if (tp_prec_mode == 1) { // Fine Control (0.75x slow)
                if (dist <= 1.5f) {
                    base_mult = 0.75f;
                } else if (dist <= 3.5f) {
                    base_mult = 0.75f + (dist - 1.5f) * (0.25f / 2.0f);
                }
            } else if (tp_prec_mode == 2) { // Microscopic Precision (0.50x slow)
                if (dist <= 1.5f) {
                    base_mult = 0.50f;
                } else if (dist <= 3.5f) {
                    base_mult = 0.50f + (dist - 1.5f) * (0.50f / 2.0f);
                }
            }

            // 2. High-speed flick acceleration curve
            float mult = base_mult;
            if (tp_accel_level > 0 && dist > 3.5f) {
                float excess = dist - 3.5f;
                float k = 0.0f;
                float max_mult = 1.0f;

                switch (tp_accel_level) {
                    case 1: // Mild (up to 2x)
                        k = 0.035f;
                        max_mult = 2.0f;
                        break;
                    case 2: // Balanced (up to 3.5x - Recommended)
                        k = 0.075f;
                        max_mult = 3.5f;
                        break;
                    case 3: // Aggressive (up to 5x)
                        k = 0.120f;
                        max_mult = 5.0f;
                        break;
                    case 4: // Ultra Curve (up to 7x)
                        k = 0.180f;
                        max_mult = 7.0f;
                        break;
                    default:
                        break;
                }

                mult = 1.0f + k * excess;
                if (mult > max_mult) mult = max_mult;
            }

            // Sub-pixel fractional accumulator for silky smooth movements with zero lost counts
            static float tp_accum_x = 0.0f;
            static float tp_accum_y = 0.0f;

            float scaled_x = (float)rx * mult + tp_accum_x;
            float scaled_y = (float)ry * mult + tp_accum_y;

            int16_t out_x = (int16_t)roundf(scaled_x);
            int16_t out_y = (int16_t)roundf(scaled_y);

            tp_accum_x = scaled_x - (float)out_x;
            tp_accum_y = scaled_y - (float)out_y;

            mouse_report.x = out_x;
            mouse_report.y = out_y;
        }
    }

    // Apply roller scroll accumulation with clamping to int8_t bounds
    if (roller_accum_v != 0) {
        int16_t v = (int16_t)mouse_report.v + roller_accum_v;
        if (v > 127) v = 127;
        if (v < -127) v = -127;
        mouse_report.v = (int8_t)v;
        roller_accum_v = 0;
    }
    if (roller_accum_h != 0) {
        int16_t h = (int16_t)mouse_report.h + roller_accum_h;
        if (h > 127) h = 127;
        if (h < -127) h = -127;
        mouse_report.h = (int8_t)h;
        roller_accum_h = 0;
    }

    return mouse_report;
}

// ==========================================
// 9. WS2812 RGB STATUS LED MANAGEMENT & AUTO-IDLE
// ==========================================
static void set_led_hsv(uint8_t h, uint8_t s, uint8_t v) {
    uint8_t r, g, b;
    if (s == 0) {
        r = g = b = v;
    } else {
        uint8_t region    = h / 43;
        uint8_t remainder = (h - (region * 43)) * 6;
        uint8_t p         = (v * (255 - s)) >> 8;
        uint8_t q         = (v * (255 - ((s * remainder) >> 8))) >> 8;
        uint8_t t         = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

        switch (region) {
            case 0:  r = v; g = t; b = p; break;
            case 1:  r = q; g = v; b = p; break;
            case 2:  r = p; g = v; b = t; break;
            case 3:  r = p; g = q; b = v; break;
            case 4:  r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }
    ws2812_set_color(0, r, g, b);
    ws2812_flush();
}

static uint8_t  last_layer_rendered = 255;
static uint8_t  led_fade_hue        = 0;
static uint32_t last_led_tick       = 0;

void housekeeping_task_user(void) {
    uint8_t current_layer = get_highest_layer(layer_state);

    // Auto-Idle Timeout check: trigger screensaver if idle duration exceeded
    if (auto_idle_timeout_idx > 0 && current_layer != _LAYER_2) {
        uint32_t timeout_ms = timeout_presets_ms[auto_idle_timeout_idx];
        if (timeout_ms > 0 && timer_elapsed32(last_activity_time) >= timeout_ms) {
            enter_screensaver();
            return;
        }
    }

    if (current_layer == _LAYER_2) {
        // Layer 2 (Screensaver Mode): Smooth rainbow color fade at medium/low brightness (v=60)
        if (timer_elapsed32(last_led_tick) >= 20) { // 50 Hz update
            last_led_tick = timer_read32();
            led_fade_hue++;
            set_led_hsv(led_fade_hue, 255, 60);
            last_layer_rendered = _LAYER_2;
        }
    } else if (current_layer != last_layer_rendered) {
        // Static layer colors
        last_layer_rendered = current_layer;
        if (current_layer == _LAYER_1) {
            // Layer 1 (Media): User-selected color from Layouts tab
            led_color_t col = layer1_colors[layer1_color_idx];
            ws2812_set_color(0, col.r, col.g, col.b);
            ws2812_flush();
        } else {
            // Layer 0 (Default) / Other: LED is OFF
            ws2812_set_color(0, 0, 0, 0);
            ws2812_flush();
        }
    }
}

// ==========================================
// 10. HARDWARE INITIALIZATION & EEPROM SELF-REPAIR
// ==========================================
void keyboard_pre_init_user(void) {
    // Set GP28 as Output LOW to serve as GND reference for EVQWGD001 SW2
    gpio_set_pin_output(GP28);
    gpio_write_pin_low(GP28);

    // Set GP22 (NC / shield pin) safely to input with pull-up
    gpio_set_pin_input_high(GP22);
}

void keyboard_post_init_user(void) {
    ws2812_init();
    ws2812_set_color(0, 0, 0, 0);
    ws2812_flush();

    last_activity_time = timer_read32();

    uint32_t val = via_get_layout_options();
    // Validate EEPROM layout options (bits 0..26 used, bits 27+ must be 0)
    bool invalid = false;
    if ((val & 0x0F) >= NUM_LED_COLORS)            invalid = true;
    if (((val >> 4)  & 0x07) > 4)                  invalid = true;
    if (((val >> 8)  & 0x03) > 3)                  invalid = true;
    if (((val >> 10) & 0x03) > 2)                  invalid = true;
    if (((val >> 13) & 0x03) > 2)                  invalid = true; // tp precision mode (0..2)
    if (((val >> 15) & 0x07) > 4)                  invalid = true; // tp accel level (0..4)
    if (((val >> 21) & 0x07) >= NUM_CPI_PRESETS)   invalid = true;
    if (((val >> 24) & 0x03) >= NUM_SENS_LEVELS)  invalid = true;
    if ((val >> 27) != 0)                          invalid = true;

    if (invalid) {
        // Reset to clean V7 default
        val = VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT;
        via_set_layout_options(val);
    }

    via_set_layout_options_kb(val);

#if defined(ENCODER_MAP_ENABLE)
    // Dynamic keymap self-repair & upgrade:
    if (dynamic_keymap_get_encoder(0, 0, false) == KC_NO) {
        dynamic_keymap_set_encoder(0, 0, false, KC_VOLD);
        dynamic_keymap_set_encoder(0, 0, true, KC_VOLU);
    }
    if (dynamic_keymap_get_encoder(0, 1, false) == KC_NO) {
        dynamic_keymap_set_encoder(0, 1, false, MS_WHLD);
        dynamic_keymap_set_encoder(0, 1, true, MS_WHLU);
    }

    // Upgrade Layer 1 EC11 knob to YouTube seek (KC_LEFT / KC_RIGHT) if unset or previous KC_MPRV
    uint16_t l1_e0 = dynamic_keymap_get_encoder(1, 0, false);
    if (l1_e0 == KC_NO || l1_e0 == KC_MPRV) {
        dynamic_keymap_set_encoder(1, 0, false, KC_LEFT);
        dynamic_keymap_set_encoder(1, 0, true, KC_RIGHT);
    }
    if (dynamic_keymap_get_encoder(1, 1, false) == KC_NO) {
        dynamic_keymap_set_encoder(1, 1, false, MS_WHLL);
        dynamic_keymap_set_encoder(1, 1, true, MS_WHLR);
    }

    if (dynamic_keymap_get_encoder(2, 0, false) == KC_NO) {
        dynamic_keymap_set_encoder(2, 0, false, SCRN_SPD_DN);
        dynamic_keymap_set_encoder(2, 0, true, SCRN_SPD_UP);
    }
    if (dynamic_keymap_get_encoder(2, 1, false) == KC_NO) {
        dynamic_keymap_set_encoder(2, 1, false, SCRN_PAT_PREV);
        dynamic_keymap_set_encoder(2, 1, true, SCRN_PAT_NEXT);
    }
#endif
}

