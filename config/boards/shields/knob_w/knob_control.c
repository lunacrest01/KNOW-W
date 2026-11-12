#define DT_DRV_COMPAT braxon_knob_control
#include <hal/nrf_gpio.h>
#include <drivers/behavior.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/display.h>
#include <zmk/rgb_underglow.h>
#include <zmk/usb.h>
#include <zephyr/init.h>
#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <zmk/keymap.h>
#include <zmk/settings.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/pointing.h>
#include "knob_control.h"
#include "knob_power_rules.h"

#define DEFAULT_CONFIG { .presets={0,1,2,3,4}, .presses={1,0,2,2,3}, .brightness=20, \
    .rgb_on=1, .display_on=1, .timeout_minutes=10, .oled_minutes=15, .ccw={255,255,255,255,255}, \
    .cw={255,255,255,255,255}, .effect=1, .speed=3, .saturation=100, .hue=200 }
struct knob_config knob_config = DEFAULT_CONFIG;
uint8_t knob_page;
uint8_t knob_ble_cursor;
const char *const knob_modes[] = {"VOLUME", "BRIGHTNESS", "SCROLL", "H-SCROLL", "MEDIA"};
static const struct { uint32_t code; const char *label; } actions[] = {
    {0,"NONE"},{C_MUTE,"MUTE"},{LC(X),"CUT"},{C_PLAY_PAUSE,"PLAY"},
    {RET,"ENTER"},{ESC,"ESC"},{LC(C),"COPY"},{LC(V),"PASTE"},
    {LEFT,"LEFT"},{RIGHT,"RIGHT"},{UP,"UP"},{DOWN,"DOWN"},
    {PG_UP,"PG UP"},{PG_DN,"PG DN"},{HOME,"HOME"},{END,"END"},
    {C_VOL_UP,"VOL+"},{C_VOL_DN,"VOL-"},{C_BRI_UP,"DIM+"},{C_BRI_DN,"DIM-"},
    {C_PREV,"PREV"},{C_NEXT,"NEXT"},{LC(TAB),"TAB+"},{LC(LS(TAB)),"TAB-"},
    {LC(Z),"UNDO"},{LC(LS(Z)),"REDO"},{LC(EQUAL),"ZOOM+"},{LC(MINUS),"ZOOM-"},
    {LG(LC(LEFT)),"DESK-"},{LG(LC(RIGHT)),"DESK+"},
    {LG(C),"COPY"},{LG(V),"PASTE"},{LG(Z),"UNDO"},{LG(LS(Z)),"REDO"},
    {LG(EQUAL),"ZOOM+"},{LG(MINUS),"ZOOM-"},
#include "knob_catalogue.inc"
};
BUILD_ASSERT(ARRAY_SIZE(actions) + 1 == KNOB_ACTION_COUNT);
bool knob_action_valid(int action) {
    return action >= 0 && action < KNOB_ACTION_COUNT && action != KNOB_USE_PRESET;
}
static unsigned action_index(uint16_t id) { return id - (id > KNOB_USE_PRESET); }
const char *knob_action_label(uint16_t id) { return actions[action_index(id)].label; }
static int64_t pairing_until;
static int64_t connected_until;
static int64_t last_activity;
static int64_t encoder_down_at;
static bool encoder_down;
static bool powering_off;
static bool resetting;
static atomic_t input_counts[4];
unsigned knob_input_count(unsigned index) { return atomic_get(&input_counts[index]); }
static void power_policy(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(power_policy_work,power_policy);
extern int update_advertising(void);

bool knob_pairing_allowed(void) { return k_uptime_get() < pairing_until; }
bool knob_rgb_is_idle(void) { return k_uptime_get()-last_activity >= knob_config.timeout_minutes*60000LL; }
bool knob_is_idle(void) { return knob_rgb_is_idle(); }
bool knob_is_asleep(void) {
    return k_uptime_get()-last_activity >= knob_config.oled_minutes*60000LL;
}
bool knob_is_powering_off(void) { return powering_off; }
bool knob_is_resetting(void) { return resetting; }
static void power_off(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(power_off_work,power_off);
static void power_off(struct k_work *work) {
    /* Wait for the wake key to be released so System OFF does not wake immediately. */
    if (!nrf_gpio_pin_read(17)) { k_work_reschedule(&power_off_work,K_MSEC(100)); return; }
    display_blanking_on(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)));
    bt_disable();
    usb_disable();
    for (int pin=0;pin<48;pin++) { nrf_gpio_cfg_sense_set(pin,NRF_GPIO_PIN_NOSENSE); }
    nrf_gpio_cfg_sense_input(17,NRF_GPIO_PIN_PULLUP,NRF_GPIO_PIN_SENSE_LOW);
    sys_poweroff();
}
static void shutdown_hold(struct k_work *work) {
    if (!encoder_down || powering_off) { return; }
    powering_off = true;
    zmk_rgb_underglow_off();
    knob_display_refresh();
    k_work_reschedule(&power_off_work,K_MSEC(900));
}
K_WORK_DELAYABLE_DEFINE(shutdown_hold_work,shutdown_hold);
static int activity(const zmk_event_t *event) {
    bool waking = knob_rgb_is_idle() || knob_is_asleep();
    last_activity = k_uptime_get();
    if (waking) {
        k_work_reschedule(&power_policy_work,K_NO_WAIT);
        knob_display_refresh();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(knob_activity, activity);
ZMK_SUBSCRIPTION(knob_activity, zmk_position_state_changed);
ZMK_SUBSCRIPTION(knob_activity, zmk_sensor_event);

static void power_policy(struct k_work *work) {
    bool on;
    bool wanted = !powering_off && !knob_is_idle() && !KNOB_LOW_BATTERY(knob_battery_percent()) &&
        (knob_config.rgb_on || knob_pairing_allowed() || k_uptime_get() < connected_until);
    if (!zmk_rgb_underglow_get_state(&on) && on != wanted) {
        if (wanted) { zmk_rgb_underglow_on(); } else { zmk_rgb_underglow_off(); }
    }
    k_work_reschedule(&power_policy_work,knob_is_asleep() && knob_rgb_is_idle()?K_SECONDS(30):K_SECONDS(1));
}
static int power_init(void) { k_work_reschedule(&power_policy_work,K_SECONDS(1)); return 0; }
SYS_INIT(power_init,APPLICATION,99);

static void pairing_expired(struct k_work *work) {
    pairing_until = 0;
    update_advertising();
    knob_display_refresh();
}
K_WORK_DELAYABLE_DEFINE(pairing_work, pairing_expired);

int knob_pair(bool replace) {
    int selected = zmk_ble_prof_select(knob_ble_cursor);
    if (selected) { return selected; }
    if (!replace && zmk_ble_active_profile_is_connected()) {
        return zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
    }
    zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
    pairing_until = k_uptime_get() + 90000;
    if (replace) {
        zmk_ble_clear_bonds();
    }
    int err = update_advertising();
    if (err) { pairing_until = 0; return err; }
    k_work_reschedule(&power_policy_work,K_NO_WAIT);
    k_work_reschedule(&pairing_work, K_SECONDS(90));
    knob_display_refresh();
    return err;
}

static int profile_changed(const zmk_event_t *event) {
    if (knob_ble_hid_ready()) {
        pairing_until = 0;
        k_work_cancel_delayable(&pairing_work);
        connected_until = k_uptime_get() + 5000;
    }
    knob_display_refresh();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(knob_connection, profile_changed);
ZMK_SUBSCRIPTION(knob_connection, zmk_ble_active_profile_changed);

void knob_rgb_override(struct led_rgb *pixels, unsigned count) {
    bool pairing = knob_pairing_allowed();
    bool connected = k_uptime_get() < connected_until;
    for (unsigned i = 0; i < count; i++) {
        if (powering_off || knob_is_idle() || KNOB_LOW_BATTERY(knob_battery_percent())) {
            pixels[i] = (struct led_rgb){0};
        } else if (pairing) {
            pixels[i] = (struct led_rgb){.b = (k_uptime_get() / 400 % 2) ? 40 : 0};
        } else if (connected) {
            pixels[i] = (struct led_rgb){.g = 40};
        } else if (!knob_config.rgb_on) {
            pixels[i] = (struct led_rgb){0};
        } else {
            unsigned brightness = knob_config.brightness;
            unsigned phase = k_uptime_get() * knob_config.speed / 16 % 200;
            if (knob_config.effect == 4) {
                brightness = brightness * (i == phase / 100 % count ? 100 : 12) / 100;
            } else if (knob_config.effect == 5) {
                brightness = brightness * (phase < 25 || (phase >= 45 && phase < 65) ? 100 : 15) / 100;
            }
            pixels[i].r = pixels[i].r * brightness / 100;
            pixels[i].g = pixels[i].g * brightness / 100;
            pixels[i].b = pixels[i].b * brightness / 100;
        }
    }
}

int knob_config_valid(const struct knob_config *cfg) {
    if (cfg->effect >= KNOB_EFFECT_COUNT || cfg->speed < 1 || cfg->speed > 5 ||
        cfg->hue > 359 || cfg->saturation > 100) { return -EINVAL; }
    if (cfg->brightness > 100 || cfg->rgb_on > 1 || cfg->display_on > 1 ||
        cfg->timeout_minutes < 1 || cfg->timeout_minutes > 30 ||
        cfg->oled_minutes < 1 || cfg->oled_minutes > 35) { return -EINVAL; }
    for (int i = 0; i < 5; i++) {
        if (cfg->presets[i] >= 5 || !knob_action_valid(cfg->presses[i]) || !knob_action_valid(cfg->doubles[i]) ||
            (cfg->ccw[i] != KNOB_USE_PRESET && !knob_action_valid(cfg->ccw[i])) ||
            (cfg->cw[i] != KNOB_USE_PRESET && !knob_action_valid(cfg->cw[i]))) { return -EINVAL; }
    }
    return 0;
}
int knob_save_config(const struct knob_config *cfg) {
    int err = knob_config_valid(cfg);
    if (err) { return err; }
    err = settings_save_one("knob/config", cfg, sizeof(*cfg));
    if (!err) {
        bool rgb_changed = cfg->hue != knob_config.hue || cfg->saturation != knob_config.saturation ||
                           cfg->effect != knob_config.effect || cfg->speed != knob_config.speed;
        knob_config = *cfg;
        if (rgb_changed) { knob_rgb_apply(); }
        k_work_reschedule(&power_policy_work,K_NO_WAIT);
        knob_display_refresh();
    }
    return err;
}
static int load_config(const char *name, size_t len, settings_read_cb read, void *arg) {
    struct knob_config cfg = DEFAULT_CONFIG;
    struct legacy_knob_config {
        uint8_t presets[5], presses[5], brightness, rgb, display, timeout;
        uint8_t ccw[5], cw[5], effect, speed, saturation;
        uint16_t hue;
    } old = { .ccw={255,255,255,255,255}, .cw={255,255,255,255,255},
              .effect=1, .speed=3, .saturation=100, .hue=200 };
    if (strcmp(name, "config")) { return -EINVAL; }
    if (len == sizeof(cfg)) {
        if (read(arg,&cfg,len) != (int)len) { return -EINVAL; }
    } else if (len == sizeof(old) || len == offsetof(struct legacy_knob_config,ccw)) {
        if (read(arg,&old,len) != (int)len) { return -EINVAL; }
        for (int i=0;i<5;i++) {
            cfg.presets[i]=old.presets[i]; cfg.presses[i]=old.presses[i];
            cfg.ccw[i]=old.ccw[i]; cfg.cw[i]=old.cw[i];
        }
        cfg.brightness=old.brightness; cfg.rgb_on=old.rgb; cfg.display_on=old.display;
        cfg.timeout_minutes=old.timeout; cfg.oled_minutes=old.timeout+5;
        cfg.effect=old.effect; cfg.speed=old.speed; cfg.saturation=old.saturation; cfg.hue=old.hue;
    } else { return -EINVAL; }
    if (knob_config_valid(&cfg)) { return -EINVAL; }
    knob_config = cfg;
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(knob_settings, "knob", NULL, load_config, NULL, NULL);
struct knob_labels knob_labels[5];
static bool valid_text(const char *s, size_t size) {
    for (size_t i=0;i<size;i++) {
        if (!s[i]) { return true; }
        if (s[i]<32 || s[i]>126 || s[i]=='"' || s[i]=='\\') { return false; }
    }
    return false;
}
static bool valid_labels(const struct knob_labels *v) {
    return valid_text(v->name,sizeof(v->name)) && valid_text(v->left,sizeof(v->left)) &&
           valid_text(v->press,sizeof(v->press)) && valid_text(v->right,sizeof(v->right));
}
static int load_labels(const char *name,size_t len,settings_read_cb read,void *arg) {
    if (!name || name[0]<'0' || name[0]>'4' || name[1] || len!=sizeof(struct knob_labels)) { return -EINVAL; }
    struct knob_labels v;
    if (read(arg,&v,sizeof(v))!=sizeof(v) || !valid_labels(&v)) { return -EINVAL; }
    knob_labels[name[0]-'0']=v;
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(knob_text,"knob_text",NULL,load_labels,NULL,NULL);
int knob_save_labels(int layer,const struct knob_labels *v) {
    if (layer<0 || layer>=5 || !valid_labels(v)) { return -EINVAL; }
    char key[]="knob_text/0"; key[10]+=(char)layer;
    int err=settings_save_one(key,v,sizeof(*v));
    if (!err) { knob_labels[layer]=*v; knob_display_refresh(); }
    return err;
}
int knob_factory_reset(void) {
    resetting = true;
    knob_display_refresh();
    k_sleep(K_MSEC(850));
    int err = zmk_settings_erase();
    if (err) { resetting = false; knob_display_refresh(); }
    return err;
}

static struct zmk_behavior_binding held_press;
static bool press_active;
static struct zmk_behavior_binding held_turn[2];
static bool turn_active[2];
static int encoder_press(struct zmk_behavior_binding_event event, bool pressed, uint16_t action) {
    if (pressed) {
        if (!action) { return 0; }
        held_press = (struct zmk_behavior_binding){.behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)), .param1 = actions[action_index(action)].code};
        press_active = true;
    } else if (!press_active) { return 0; }
    int err = zmk_behavior_invoke_binding(&held_press, event, pressed);
    if (!pressed) { press_active = false; }
    return err;
}
static struct zmk_behavior_binding_event press_event;
K_MUTEX_DEFINE(tap_lock);
static int64_t release_at;
static void release_encoder_tap(struct k_work *work) {
    k_mutex_lock(&tap_lock,K_FOREVER);
    if (k_uptime_get() >= release_at) { encoder_press(press_event,false,0); }
    k_mutex_unlock(&tap_lock);
}
K_WORK_DELAYABLE_DEFINE(press_release_work,release_encoder_tap);
static void tap(struct zmk_behavior_binding_event event, uint16_t action) {
    if (!action) { return; }
    k_work_cancel_delayable(&press_release_work);
    encoder_press(press_event,false,0);
    press_event=event;
    if (encoder_press(event,true,action)) { encoder_press(event,false,0); return; }
    release_at=k_uptime_get()+8;
    k_work_reschedule(&press_release_work,K_MSEC(8));
}
static struct zmk_behavior_binding_event pending_event;
static int64_t click_deadline;
static uint16_t pending_single, pending_double;
static int pending_layer;
static void single_press(struct k_work *work) {
    k_mutex_lock(&tap_lock,K_FOREVER);
    if (click_deadline && k_uptime_get() >= click_deadline) {
        click_deadline=0;
        if (!powering_off && !resetting && knob_page != KNOB_BLE) { tap(pending_event,pending_single); }
    }
    k_mutex_unlock(&tap_lock);
}
K_WORK_DELAYABLE_DEFINE(single_press_work,single_press);
static void click(struct zmk_behavior_binding_event event, int layer) {
    k_mutex_lock(&tap_lock,K_FOREVER);
    int64_t now=k_uptime_get();
    if (click_deadline && now < click_deadline && layer == pending_layer) {
        click_deadline=0;
        k_work_cancel_delayable(&single_press_work);
        atomic_inc(&input_counts[3]);
        tap(event,pending_double);
    } else {
        if (click_deadline) { tap(pending_event,pending_single); click_deadline=0; }
        if (!knob_config.doubles[layer]) { tap(event,knob_config.presses[layer]); }
        else {
            pending_event=event; pending_layer=layer;
            pending_single=knob_config.presses[layer]; pending_double=knob_config.doubles[layer];
            click_deadline=now+250;
            k_work_reschedule(&single_press_work,K_MSEC(250));
        }
    }
    k_mutex_unlock(&tap_lock);
}
static int action_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (resetting) { return 0; }
    uint32_t action = binding->param1;
    if (action == 4) {
        if (encoder_down) { return 0; }
        knob_page = (knob_page + 1) % 3;
        if (knob_page == KNOB_BLE) { knob_ble_cursor=zmk_ble_active_profile_index(); }
        knob_display_refresh();
        return 0;
    }
    if (action == 1) {
        encoder_down_at = k_uptime_get();
        encoder_down = true;
        if (knob_page != KNOB_BLE) { k_work_reschedule(&shutdown_hold_work,K_MSEC(4500)); }
        return 0;
    }
    if (action == 2 || action == 3) {
        atomic_inc(&input_counts[action-2]);
        if (knob_page == KNOB_BLE) {
            if (encoder_down) { return 0; }
            knob_ble_cursor = (knob_ble_cursor + (action == 2 ? 1 : 2)) % 3;
            knob_display_refresh();
            return 0;
        }
        static const uint32_t keys[5][2] = {{C_VOL_UP,C_VOL_DN},{C_BRI_UP,C_BRI_DN},
            {SCRL_DOWN,SCRL_UP},{SCRL_RIGHT,SCRL_LEFT},{C_NEXT,C_PREV}};
        int layer = MIN(zmk_keymap_highest_layer_active(), 4);
        int mode = knob_config.presets[layer];
        int direction = action == 3;
        uint16_t custom = direction ? knob_config.ccw[layer] : knob_config.cw[layer];
        if (custom == 0) { return 0; }
        bool scroll = custom == KNOB_USE_PRESET && (mode == 2 || mode == 3);
        held_turn[direction] = (struct zmk_behavior_binding){
            .behavior_dev = scroll ? DEVICE_DT_NAME(DT_NODELABEL(knob_scroll)) : DEVICE_DT_NAME(DT_NODELABEL(kp)),
            .param1 = custom == KNOB_USE_PRESET ? keys[mode][direction] : actions[action_index(custom)].code};
        turn_active[direction] = true;
        return zmk_behavior_invoke_binding(&held_turn[direction], event, true);
    }
    return 0;
}
static int action_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (resetting) { return 0; }
    if (binding->param1 == 0) {
        return zmk_keymap_layer_to((zmk_keymap_highest_layer_active() + 1) % 5, false);
    }
    if (binding->param1 == 1) {
        atomic_inc(&input_counts[2]);
        int64_t held = k_uptime_get() - encoder_down_at;
        if (held >= 4500 && !powering_off && knob_page != KNOB_BLE) { shutdown_hold(NULL); }
        encoder_down = false;
        k_work_cancel_delayable(&shutdown_hold_work);
        if (powering_off) { return 0; }
        if (knob_page == KNOB_BLE) { return knob_pair(held >= 2000); }
        click(event,MIN(zmk_keymap_highest_layer_active(),4));
        return 0;
    }
    if (binding->param1 == 2 || binding->param1 == 3) {
        int direction = binding->param1 == 3;
        if (turn_active[direction]) {
            turn_active[direction] = false;
            return zmk_behavior_invoke_binding(&held_turn[direction],event,false);
        }
    }
    return 0;
}
static const struct behavior_driver_api api = {.binding_pressed = action_pressed, .binding_released = action_released};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
