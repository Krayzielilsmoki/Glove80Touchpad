#pragma once

// ==========================================
// 1. SPI PIN ASSIGNMENTS (Cirque Trackpad)
// ==========================================
#define SPI_SCK_PIN GP2
#define SPI_MOSI_PIN GP3
#define SPI_MISO_PIN GP4
#define POINTING_DEVICE_CS_PIN GP5

// ==========================================
// 2. CIRQUE TRACKPAD CONFIGURATION
// ==========================================
#define CIRQUE_PINNACLE_DIAMETER_MM 40
#define POINTING_DEVICE_ROTATION_270
#define CIRQUE_PINNACLE_TAP_ENABLE
#define CIRQUE_PINNACLE_CIRCULAR_SCROLL_ENABLE

// Curved Overlay (TM040040-2024-303) tuning & sensitivity
#define CIRQUE_PINNACLE_CURVED_OVERLAY
#define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_2X

#define POINTING_DEVICE_INVERT_Y
#define POINTING_DEVICE_INVERT_X

// Inertial cursor flick (glide across screen)
#define POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE

// Extended 16-bit mouse reports for smooth, high-speed movement
#define MOUSE_EXTENDED_REPORT

// ==========================================
// 3. WS2812 RGB STATUS LED CONFIGURATION
// ==========================================
#define WS2812_DI_PIN GP6
#define WS2812_LED_COUNT 1
#define WS2812_BYTE_ORDER WS2812_BYTE_ORDER_GRB

// VIA layout options EEPROM configuration
// 4 bytes for all V7 touchpad ballistics, precision mode, scroll acceleration, LED, and screensaver options
#define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 4
#define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0x025D2600

