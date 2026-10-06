#include QMK_KEYBOARD_H
#include <math.h>
#include "ws2812.h"

#ifndef M_PI
#    define M_PI 3.14159265358979323846f
#endif

// ==========================================
// 1. LAYER ENUMERATIONS
// ==========================================
enum layers {
    _LAYER_0 = 0, // Default: Navigation / Standard Mouse & Encoders (LED: Off)
    _LAYER_1,     // Media & Productivity (LED: Solid Blue)
    _LAYER_2,     // Screensaver Mode (LED: Rainbow Color Fade)
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
    SCRN_PAT_NEXT          // Layer 2: Next screensaver animation pattern
};

// ==========================================
// 3. SCREENSAVER ENGINE STATE
// ==========================================
#define SCREENSAVER_UPDATE_INTERVAL 16 // ~60 FPS (ms)
#define NUM_PATTERNS 3

static uint8_t screensaver_speed   = 4; // Speed scale: 1 (slow) to 10 (fast)
static uint8_t screensaver_pattern = 0; // 0: Pong/DVD Bounce, 1: Circle Orbit, 2: Lissajous Figure-8

// Physics / Coordinate State
static float screensaver_x     = 0.0f;
static float screensaver_y     = 0.0f;
static float screensaver_vx    = 3.5f;
static float screensaver_vy    = 2.5f;
static float screensaver_angle = 0.0f;

// Virtual Screen Boundary for Pong Bounce
#define VIRTUAL_BOX_W 960.0f
#define VIRTUAL_BOX_H 540.0f

static void screensaver_step(int8_t *dx, int8_t *dy) {
    float speed_mult = (float)screensaver_speed / 4.0f;

    switch (screensaver_pattern) {
        case 0: { // Pattern 0: Pong / DVD Logo Bouncer
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
        case 1: { // Pattern 1: Smooth Circular Orbit
            screensaver_angle += 0.06f * speed_mult;
            if (screensaver_angle >= 2.0f * (float)M_PI) {
                screensaver_angle -= 2.0f * (float)M_PI;
            }
            float radius = 10.0f * speed_mult;
            *dx = (int8_t)roundf(-radius * sinf(screensaver_angle));
            *dy = (int8_t)roundf(radius * cosf(screensaver_angle));
            break;
        }
        case 2: { // Pattern 2: Lissajous / Figure-8
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
// 4. ROTARY ENCODER MAPPINGS (4 Layers)
// ==========================================
#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_LAYER_0] = {
        // Encoder 1 (EC11): Volume Down, Volume Up
        ENCODER_CCW_CW(KC_VOLD, KC_VOLU),

        // Encoder 2 (EVQWGD001 Roller): Scroll Down, Scroll Up
        ENCODER_CCW_CW(MS_WHLD, MS_WHLU)
    },
    [_LAYER_1] = {
        // Encoder 1 (EC11): Media Track Previous, Next
        ENCODER_CCW_CW(KC_MPRV, KC_MNXT),

        // Encoder 2 (EVQWGD001 Roller): Horizontal Scroll Left, Right
        ENCODER_CCW_CW(MS_WHLL, MS_WHLR)
    },
    [_LAYER_2] = {
        // Encoder 1 (EC11): Screensaver Speed Down, Speed Up
        ENCODER_CCW_CW(SCRN_SPD_DN, SCRN_SPD_UP),

        // Encoder 2 (EVQWGD001 Roller): Animation Pattern Previous, Next
        ENCODER_CCW_CW(SCRN_PAT_PREV, SCRN_PAT_NEXT)
    },
    [_LAYER_3] = {
        ENCODER_CCW_CW(KC_TRNS, KC_TRNS),
        ENCODER_CCW_CW(KC_TRNS, KC_TRNS)
    }
};
#endif

// ==========================================
// 5. PUSH-BUTTON SWITCH MAPPINGS (4 Layers)
// ==========================================
// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_LAYER_0] = LAYOUT(
        KC_MUTE,    // EC11 Button (GP20): Mute
        SW_LAYER    // EVQWGD001 Button (GP29): Tap -> Layer 1, Long-press -> Screensaver (Layer 2)
    ),
    [_LAYER_1] = LAYOUT(
        KC_MPLY,    // EC11 Button (GP20): Play/Pause
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
// 6. BUTTON TIMING & TAP / LONG-PRESS LOGIC
// ==========================================
static uint32_t roller_press_time   = 0;
static bool     roller_long_pressed = false;

void matrix_scan_user(void) {
    // Check if roller button is held for >= 400ms to trigger Screensaver
    if (roller_press_time != 0 && !roller_long_pressed) {
        if (timer_elapsed32(roller_press_time) >= 400) {
            roller_long_pressed = true;
            layer_move(_LAYER_2); // Switch to Screensaver Mode
        }
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // If currently in Screensaver mode (Layer 2)
    if (get_highest_layer(layer_state) == _LAYER_2) {
        if (record->event.pressed) {
            switch (keycode) {
                case SCRN_SPD_DN:
                case SCRN_SPD_UP:
                case SCRN_PAT_PREV:
                case SCRN_PAT_NEXT:
                    break; // Allow encoder adjustment keys to execute
                default:
                    // Any physical button press wakes up from Screensaver to Layer 0
                    layer_move(_LAYER_0);
                    return false;
            }
        }
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

        case SCRN_SPD_DN:
            if (record->event.pressed) {
                if (screensaver_speed > 1) {
                    screensaver_speed--;
                }
            }
            return false;

        case SCRN_SPD_UP:
            if (record->event.pressed) {
                if (screensaver_speed < 10) {
                    screensaver_speed++;
                }
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

        default:
            return true;
    }
}

// ==========================================
// 7. POINTING DEVICE INTERCEPT & TOUCH-TO-WAKE
// ==========================================
report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (get_highest_layer(layer_state) == _LAYER_2) {
        // If trackpad is physically touched or buttons clicked, wake up to Layer 0
        if (mouse_report.x != 0 || mouse_report.y != 0 || mouse_report.buttons != 0) {
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
    }
    return mouse_report;
}

// ==========================================
// 8. WS2812 RGB STATUS LED MANAGEMENT
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
            // Layer 1 (Media): Solid Blue at medium/low brightness
            ws2812_set_color(0, 0, 0, 60);
            ws2812_flush();
        } else {
            // Layer 0 (Default) / Other: LED is OFF
            ws2812_set_color(0, 0, 0, 0);
            ws2812_flush();
        }
    }
}

// ==========================================
// 9. HARDWARE INITIALIZATION
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
}

