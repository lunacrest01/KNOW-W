#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

enum { KNOB_MODE, KNOB_BLE, KNOB_IDLE };
struct knob_config {
    uint8_t presets[5];
    uint16_t presses[5], doubles[5], ccw[5], cw[5];
    uint8_t brightness;
    uint8_t rgb_on;
    uint8_t display_on;
    uint8_t timeout_minutes;
    uint8_t oled_minutes;
    uint8_t effect, speed, saturation;
    uint16_t hue;
};
extern struct knob_config knob_config;
extern uint8_t knob_page;
extern uint8_t knob_ble_cursor;
struct knob_labels { char name[17], left[7], press[7], right[7]; };
extern struct knob_labels knob_labels[5];
int knob_save_labels(int layer, const struct knob_labels *labels);
extern const char *const knob_modes[5];
enum { KNOB_USE_PRESET = 255, KNOB_ACTION_COUNT = 283, KNOB_EFFECT_COUNT = 6 };
bool knob_action_valid(int action);
const char *knob_action_label(uint16_t action);
void knob_rgb_apply(void);
bool knob_pairing_allowed(void);
bool knob_ble_hid_ready(void);
bool knob_is_idle(void);
bool knob_is_asleep(void);
bool knob_is_powering_off(void);
bool knob_is_resetting(void);
bool knob_rgb_is_idle(void);
unsigned knob_input_count(unsigned index);
int knob_pair(bool replace);
void knob_display_refresh(void);
void knob_rgb_override(struct led_rgb *pixels, unsigned count);
int knob_save_config(const struct knob_config *config);
int knob_factory_reset(void);
int knob_config_valid(const struct knob_config *config);
int knob_battery_percent(void);
int knob_battery_mv(void);
bool knob_battery_cached(void);
