#include <zephyr/settings/settings.h>
#include <zmk/rgb_underglow.h>
#include "knob_control.h"

void knob_rgb_apply(void) {
    zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){
        .h = knob_config.hue, .s = knob_config.saturation, .b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
    });
    zmk_rgb_underglow_select_effect(knob_config.effect < 4 ? knob_config.effect : 0);
    zmk_rgb_underglow_set_spd(knob_config.speed);
}

static int rgb_start(void) {
    knob_rgb_apply();
    return knob_config.rgb_on ? zmk_rgb_underglow_on() : zmk_rgb_underglow_off();
}

SETTINGS_STATIC_HANDLER_DEFINE(knob_rgb_start, "knob_rgb", NULL, NULL, rgb_start, NULL);
