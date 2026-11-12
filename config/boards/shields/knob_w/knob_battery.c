#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zmk/usb.h>
#include "knob_control.h"

static int percent = -1;
static int millivolts = -1;
int knob_battery_percent(void) { return percent; }
int knob_battery_mv(void) { return millivolts; }
bool knob_battery_cached(void) { return zmk_usb_is_powered(); }
static void sample(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_work, sample);
static void sample(struct k_work *work) {
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(knob_battery));
    /* USB raises VDDH: it is not a battery measurement while plugged in. */
    if (!zmk_usb_is_powered() && device_is_ready(dev)) {
        struct sensor_value value;
        if (!sensor_sample_fetch(dev) && !sensor_channel_get(dev,SENSOR_CHAN_GAUGE_STATE_OF_CHARGE,&value)) {
            percent = CLAMP(value.val1,0,100);
            if (!sensor_channel_get(dev,SENSOR_CHAN_GAUGE_VOLTAGE,&value)) {
                millivolts = sensor_value_to_milli(&value);
            }
            knob_display_refresh();
        }
    }
    k_work_reschedule(&battery_work,K_SECONDS(30));
}
static int init(void) { k_work_reschedule(&battery_work,K_SECONDS(3)); return 0; }
SYS_INIT(init,APPLICATION,99);
