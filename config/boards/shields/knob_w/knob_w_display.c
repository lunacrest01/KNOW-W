#include <stdio.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/keymap.h>
#include "knob_control.h"
#include "knob_assets.h"

LOG_MODULE_REGISTER(knob_display, CONFIG_ZMK_LOG_LEVEL);
FONT_ENTRY_DEFINE(knob_small, 6, 8, CFB_FONT_MONO_VPACKED, knob_font, 32, 126);
static const struct device *const oled = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static bool initialized;
static void refresh(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(display_work, refresh);
void knob_display_refresh(void) { k_work_reschedule(&display_work, K_NO_WAIT); }

static void line(int x, int y, int x2, int y2) {
    struct cfb_position a = {.x=x,.y=y}, b = {.x=x2,.y=y2};
    cfb_draw_line(oled, &a, &b);
}
static void bitmap(const uint8_t *data) {
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 128; x++) {
            if (data[(y/8)*128+x] & BIT(y%8)) {
                struct cfb_position p = {.x=x,.y=y};
                cfb_draw_point(oled, &p);
            }
        }
    }
}
static void mode_screen(void) {
    int layer = MIN(zmk_keymap_highest_layer_active(), 4);
    int mode = knob_config.presets[layer];
    char heading[22];
    const struct knob_labels *v=&knob_labels[layer];
    snprintf(heading, sizeof(heading), "%u %s", layer+1, *v->name?v->name:knob_modes[mode]);
    cfb_print(oled, heading, (128-strlen(heading)*6)/2, 0);
    line(0,9,127,9);
    line(41,12,41,31); line(85,12,85,31);
    cfb_print(oled, "<", 17,12);
    cfb_print(oled, "PRESS", 49,12);
    cfb_print(oled, ">", 105,12);
    static const char *const left[] = {"VOL-","DIM","UP","LEFT","PREV"};
    static const char *const right[] = {"VOL+","LIGHT","DOWN","RIGHT","NEXT"};
    uint16_t ccw=knob_config.ccw[layer], cw=knob_config.cw[layer];
    const char *labels[] = {
        *v->left?v->left:ccw==KNOB_USE_PRESET?left[mode]:knob_action_label(ccw),
        *v->press?v->press:knob_action_label(knob_config.presses[layer]),
        *v->right?v->right:cw==KNOB_USE_PRESET?right[mode]:knob_action_label(cw)};
    for (int i=0;i<3;i++) {
        cfb_print(oled, labels[i], i*43+(42-strlen(labels[i])*6)/2, 23);
    }
}
static void ble_screen(void) {
    int selected = knob_ble_cursor;
    for (int i=0;i<3;i++) {
        int x=i*27+4;
        bool lit = i != zmk_ble_active_profile_index() || !knob_pairing_allowed() || k_uptime_get()/400%2;
        if (lit) {
            line(x+5,1,x+5,15); line(x+5,1,x+11,5);
            line(x+11,5,x,12); line(x,4,x+11,11); line(x+11,11,x+5,15);
        }
        char number[] = {(char)('1'+i),0};
        cfb_print(oled, number, x+14,5);
        if (zmk_ble_profile_is_connected(i)) { line(x+14,15,x+19,15); }
        if (i == selected) { line(x-2,0,x-2,17); line(x-2,17,x+21,17); }
    }
    line(83,0,83,31);
    cfb_print(oled, knob_battery_cached() ? "USB" : "BAT", 91,0);
    int battery = knob_battery_percent();
    line(94,9,118,9); line(94,20,118,20);
    line(94,9,94,20); line(118,9,118,20);
    line(119,13,121,13); line(121,13,121,16); line(119,16,121,16);
    if (battery < 0) { line(103,15,110,15); }
    else {
        for (int bar=0; bar<(MIN(battery,100)+24)/25; bar++) {
            for (int y=12; y<=17; y++) { line(97+bar*5,y,100+bar*5,y); }
        }
    }
    char uptime[14];
    unsigned seconds = k_uptime_get()/1000;
    snprintf(uptime,sizeof(uptime),"%02u:%02u:%02u",seconds/3600,seconds/60%60,seconds%60);
    cfb_print(oled,uptime,5,23);
    cfb_print(oled,knob_pairing_allowed()?"PAIR":"UP",91,23);
}
static int render(void) {
    if (!device_is_ready(oled)) { return -ENODEV; }
    if (!initialized) {
        int err = cfb_framebuffer_init(oled);
        if (err) { return err; }
        initialized = true;
    }
    if (!knob_is_resetting() && !knob_is_powering_off() && (!knob_config.display_on || knob_is_asleep())) { return display_blanking_on(oled); }
    cfb_framebuffer_clear(oled,false);
    if (knob_is_resetting()) { cfb_print(oled,"FACTORY RESET",25,12); }
    else if (knob_is_powering_off()) { cfb_print(oled,"POWERING OFF",28,12); }
    else if (k_uptime_get() < 2000) { bitmap(knob_logo[0]); }
    else if (knob_is_idle()) { bitmap(knob_cat[k_uptime_get()/400%2]); }
    else if (knob_page == KNOB_MODE) { mode_screen(); }
    else if (knob_page == KNOB_BLE) { ble_screen(); }
    else { bitmap(knob_cat[k_uptime_get()/400%2]); }
    int err = cfb_framebuffer_finalize(oled);
    return err ? err : display_blanking_off(oled);
}
static void refresh(struct k_work *work) {
    int err = render();
    if (err) { LOG_WRN("OLED refresh failed: %d",err); }
    int interval = knob_is_asleep() ? 30000 :
        (knob_is_idle() || knob_page == KNOB_IDLE || knob_pairing_allowed() || k_uptime_get()<2000) ? 200 : 1000;
    k_work_reschedule(&display_work,K_MSEC(interval));
}
static int event(const zmk_event_t *event) { knob_display_refresh(); return ZMK_EV_EVENT_BUBBLE; }
ZMK_LISTENER(knob_status,event);
ZMK_SUBSCRIPTION(knob_status,zmk_layer_state_changed);
ZMK_SUBSCRIPTION(knob_status,zmk_endpoint_changed);
static int init(void) { knob_display_refresh(); return 0; }
SYS_INIT(init, APPLICATION, 99);
