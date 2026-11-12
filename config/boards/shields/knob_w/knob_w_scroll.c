#define DT_DRV_COMPAT braxon_knob_scroll
#include <drivers/behavior.h>
#include <zephyr/input/input.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/pointing.h>

static int scroll_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    int16_t x = MOVE_X_DECODE(binding->param1);
    int16_t y = MOVE_Y_DECODE(binding->param1);
    /* Report a discrete step; time-based mouse-key taps can lose slow detents. */
    if (x) {
        return input_report_rel(dev, INPUT_REL_HWHEEL, x, true, K_MSEC(10));
    }
    if (y) {
        return input_report_rel(dev, INPUT_REL_WHEEL, y, true, K_MSEC(10));
    }
    return 0;
}

static int scroll_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return 0;
}

static const struct behavior_driver_api scroll_api = {
    .binding_pressed = scroll_pressed,
    .binding_released = scroll_released,
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &scroll_api);
