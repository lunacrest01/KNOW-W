#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/reboot.h>
#include <cmsis_core.h>
#include <hal/nrf_power.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/ble.h>
#include <zmk/keymap.h>
#include "knob_control.h"

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(board_cdc_acm_uart));
K_SEM_DEFINE(rx_ready,0,1);
static void ready(const struct device *dev,void *arg) {
    if (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uart_irq_rx_disable(dev); k_sem_give(&rx_ready);
    }
}
struct request { int id; char *cmd; int layer; int preset; int press; int brightness; int rgb; int display; int timeout; char *name,*left,*click,*right; int ccw,cw,hue,saturation,effect,speed,double_press,oled_timeout; };
static const struct json_obj_descr fields[] = {
    JSON_OBJ_DESCR_PRIM(struct request,id,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,cmd,JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct request,layer,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,preset,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,press,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,brightness,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,rgb,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,display,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,timeout,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,name,JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct request,left,JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct request,click,JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct request,right,JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct request,ccw,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,cw,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,hue,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,saturation,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,effect,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,speed,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,double_press,JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct request,oled_timeout,JSON_TOK_NUMBER),
};
#define RPC_LINE 256
struct ble_request { struct bt_conn *conn; char line[RPC_LINE]; };
K_MSGQ_DEFINE(ble_requests,sizeof(struct ble_request),2,4);
K_MUTEX_DEFINE(ble_frames_lock);
static struct { struct ble_request req; size_t len; uint32_t last; } frames[CONFIG_BT_MAX_CONN];
static struct bt_conn *reply_conn;
static bool allowed(struct bt_conn *conn) {
    struct bt_conn_info info;
    return !bt_conn_get_info(conn,&info) && info.state==BT_CONN_STATE_CONNECTED &&
           bt_conn_get_security(conn)>=BT_SECURITY_L2 && !zmk_ble_active_profile_is_open() &&
           !bt_addr_le_cmp(bt_conn_get_dst(conn),zmk_ble_active_profile_addr());
}
static ssize_t receive(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *data,
                       uint16_t len,uint16_t offset,uint8_t flags) {
    if (!allowed(conn)) { return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION); }
    if (offset || (flags & BT_GATT_WRITE_FLAG_PREPARE)) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
    int index=bt_conn_index(conn);
    const char *bytes=data;
    ssize_t result=len;
    k_mutex_lock(&ble_frames_lock,K_FOREVER);
    if (k_uptime_get_32()-frames[index].last>2000) { frames[index].len=0; }
    frames[index].last=k_uptime_get_32();
    for (uint16_t i=0;i<len;i++) {
        size_t n=frames[index].len;
        if (bytes[i]=='\n') {
            if (!n) { continue; }
            frames[index].req.line[n]=0;
            frames[index].req.conn=bt_conn_ref(conn);
            frames[index].len=0;
            if (k_msgq_put(&ble_requests,&frames[index].req,K_NO_WAIT)) {
                bt_conn_unref(conn);
                result=BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
                break;
            }
            k_sem_give(&rx_ready);
        } else if (n+1>=RPC_LINE) {
            frames[index].len=0;
            result=BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
            break;
        } else {
            frames[index].req.line[frames[index].len++]=bytes[i];
        }
    }
    k_mutex_unlock(&ble_frames_lock);
    return result;
}
static void rpc_disconnected(struct bt_conn *conn,uint8_t reason) {
    k_mutex_lock(&ble_frames_lock,K_FOREVER);
    frames[bt_conn_index(conn)].len=0;
    k_mutex_unlock(&ble_frames_lock);
}
BT_CONN_CB_DEFINE(knob_rpc_callbacks) = {.disconnected=rpc_disconnected};
#define NUS_UUID(n) BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(n,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e))
BT_GATT_SERVICE_DEFINE(knob_rpc_service,
    BT_GATT_PRIMARY_SERVICE(NUS_UUID(0x6e400001)),
    BT_GATT_CHARACTERISTIC(NUS_UUID(0x6e400003), BT_GATT_CHRC_NOTIFY, 0, NULL, NULL, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_CHARACTERISTIC(NUS_UUID(0x6e400002), BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, receive, NULL));
static void send(const char *text) {
    if (!reply_conn) { while (*text) { uart_poll_out(uart,*text++); } return; }
    size_t remaining=strlen(text);
    while (remaining && allowed(reply_conn)) {
        size_t count=MIN(remaining,20);
        if (bt_gatt_notify(reply_conn,&knob_rpc_service.attrs[2],text,count)) { return; }
        remaining-=count;
        text+=count;
    }
}
extern void knob_ble_diag(int values[6]);
extern void knob_ble_bonds(char out[][BT_ADDR_LE_STR_LEN], int flags[], int count);
extern int knob_ble_advertising(void);
extern void knob_fault_read(uint32_t *reason,uint32_t *seconds);
extern uint32_t knob_fault_thread(void);
extern uint32_t knob_fault_pc(void);
extern uint32_t knob_fault_lr(void);
static void handle(char *line) {
    struct request req = {0};
    int64_t found = json_obj_parse(line,strlen(line),fields,ARRAY_SIZE(fields),&req);
    if (found < 0 || (found & 3) != 3 || req.id < 0) { send("{\"id\":0,\"ok\":false,\"error\":\"Invalid request\"}\n"); return; }
    struct knob_config cfg = knob_config;
    int err=0;
    bool reset=false;
    bool boot=false;
    if (!strcmp(req.cmd,"get_stack_diag")) {
#if IS_ENABLED(CONFIG_INIT_STACKS)
        extern struct k_thread prio_recv_thread_data,recv_thread_data;
        size_t priority_free=0,receive_free=0;
        int priority_err=k_thread_stack_space_get(&prio_recv_thread_data,&priority_free);
        int receive_err=k_thread_stack_space_get(&recv_thread_data,&receive_free);
        char msg[200];
        snprintf(msg,sizeof(msg),"{\"id\":%d,\"ok\":%s,\"controller_priority_free\":%u,\"controller_receive_free\":%u}\n",
            req.id,!priority_err&&!receive_err?"true":"false",(unsigned)priority_free,(unsigned)receive_free);
        send(msg);
#else
        char msg[100];
        snprintf(msg,sizeof(msg),"{\"id\":%d,\"ok\":false,\"error\":\"Stack watermarking disabled\"}\n",req.id);
        send(msg);
#endif
        return;
    }
    if (!strcmp(req.cmd,"get_fault_diag")) {
        uint32_t reason,seconds;
        char msg[240]; knob_fault_read(&reason,&seconds);
        snprintf(msg,sizeof(msg),"{\"id\":%d,\"ok\":true,\"fault\":%s,\"reason\":%u,\"at_uptime\":%u,\"uptime\":%u,\"thread\":%u,\"pc\":%u,\"lr\":%u}\n",
            req.id,reason==UINT32_MAX?"false":"true",reason,seconds,(unsigned)(k_uptime_get()/1000),knob_fault_thread(),knob_fault_pc(),knob_fault_lr());
        send(msg); return;
    }
    if (!strcmp(req.cmd,"get_ble_diag")) {
        int v[6]; char msg[320];
        knob_ble_diag(v);
        snprintf(msg,sizeof(msg),"{\"id\":%d,\"ok\":true,\"profile\":%d,\"pairing\":%s,\"links\":%d,\"link_error\":%d,\"auth_requests\":%d,\"pair_error\":%d,\"security_error\":%d,\"disconnect_reason\":%d,\"bonded\":[%d,%d,%d]}\n",req.id,zmk_ble_active_profile_index()+1,knob_pairing_allowed()?"true":"false",v[0],v[1],v[2],v[3],v[4],v[5],!zmk_ble_profile_is_open(0),!zmk_ble_profile_is_open(1),!zmk_ble_profile_is_open(2));
        send(msg); return;
    }
    if (!strcmp(req.cmd,"get_bonds")) {
        char addr[3][BT_ADDR_LE_STR_LEN]; int flags[3]; char msg[460];
        knob_ble_bonds(addr,flags,3);
        int adv=knob_ble_advertising();
        snprintf(msg,sizeof(msg),"{\"id\":%d,\"ok\":true,\"adv\":%d,\"filtered\":%d,\"pairing\":%s,"
            "\"slots\":[{\"addr\":\"%s\",\"bonded\":%s,\"connected\":%s},"
            "{\"addr\":\"%s\",\"bonded\":%s,\"connected\":%s},"
            "{\"addr\":\"%s\",\"bonded\":%s,\"connected\":%s}]}\n",
            req.id,adv>>1,adv&1,knob_pairing_allowed()?"true":"false",
            addr[0],flags[0]&1?"true":"false",flags[0]&2?"true":"false",
            addr[1],flags[1]&1?"true":"false",flags[1]&2?"true":"false",
            addr[2],flags[2]&1?"true":"false",flags[2]&2?"true":"false");
        send(msg); return;
    }
    if (!strcmp(req.cmd,"get_labels") || !strcmp(req.cmd,"set_labels")) {
        if (!(found & BIT(2)) || req.layer<0 || req.layer>=5) { err=-EINVAL; }
        else if (!strcmp(req.cmd,"set_labels")) {
            struct knob_labels v={0};
            if (!req.name || !req.left || !req.click || !req.right ||
                strlen(req.name)>=sizeof(v.name) || strlen(req.left)>=sizeof(v.left) ||
                strlen(req.click)>=sizeof(v.press) || strlen(req.right)>=sizeof(v.right)) { err=-EINVAL; }
            else {
                strcpy(v.name,req.name); strcpy(v.left,req.left); strcpy(v.press,req.click); strcpy(v.right,req.right);
                err=knob_save_labels(req.layer,&v);
            }
        }
        if (!err) {
            const struct knob_labels *v=&knob_labels[req.layer];
            char result[160];
            snprintf(result,sizeof(result),"{\"id\":%d,\"ok\":true,\"name\":\"%s\",\"left\":\"%s\",\"click\":\"%s\",\"right\":\"%s\"}\n",req.id,v->name,v->left,v->press,v->right);
            send(result); return;
        }
    } else if (!strcmp(req.cmd,"set_knob")) {
        if ((found & 28)!=28 || req.layer<0 || req.layer>4 || req.preset<0 || req.preset>4 ||
            !knob_action_valid(req.press) ||
            ((found & BIT(13)) && req.ccw!=KNOB_USE_PRESET && !knob_action_valid(req.ccw)) ||
            ((found & BIT(14)) && req.cw!=KNOB_USE_PRESET && !knob_action_valid(req.cw)) ||
            ((found & BIT(19)) && !knob_action_valid(req.double_press))) { err=-EINVAL; }
        else {
            cfg.presets[req.layer]=req.preset; cfg.presses[req.layer]=req.press;
            if (found & BIT(13)) { cfg.ccw[req.layer]=req.ccw; }
            if (found & BIT(14)) { cfg.cw[req.layer]=req.cw; }
            if (found & BIT(19)) { cfg.doubles[req.layer]=req.double_press; }
            err=knob_save_config(&cfg);
        }
    } else if (!strcmp(req.cmd,"set_knob_rgb")) {
        const uint32_t mask=BIT(5)|BIT(6)|BIT(15)|BIT(16)|BIT(17)|BIT(18);
        if ((found & mask)!=mask || req.brightness<0 || req.brightness>100 || req.rgb<0 ||
            req.rgb>1 || req.hue<0 || req.hue>359 || req.saturation<0 || req.saturation>100 ||
            req.effect<0 || req.effect>=KNOB_EFFECT_COUNT || req.speed<1 || req.speed>5 ||
            ((found & BIT(8)) && (req.timeout<1 || req.timeout>30))) { err=-EINVAL; }
        else {
            cfg.brightness=req.brightness; cfg.rgb_on=req.rgb; cfg.hue=req.hue;
            cfg.saturation=req.saturation; cfg.effect=req.effect; cfg.speed=req.speed;
            if (found & BIT(8)) { cfg.timeout_minutes=req.timeout; }
            err=knob_save_config(&cfg);
        }
    } else if (!strcmp(req.cmd,"set_knob_power")) {
        if ((found & 480)!=480 || req.brightness<0 || req.brightness>100 || req.rgb<0 || req.rgb>1 || req.display<0 || req.display>1 || req.timeout<1 || req.timeout>30) { err=-EINVAL; }
        else if ((found & BIT(20)) && (req.oled_timeout<1 || req.oled_timeout>35)) { err=-EINVAL; }
        else {
            cfg.brightness=req.brightness; cfg.rgb_on=req.rgb; cfg.display_on=req.display;
            cfg.timeout_minutes=req.timeout;
            if (found & BIT(20)) { cfg.oled_minutes=req.oled_timeout; }
            err=knob_save_config(&cfg);
        }
    } else if (!strcmp(req.cmd,"select_layer")) {
        if (!(found & BIT(2)) || req.layer<0 || req.layer>=5) { err=-EINVAL; }
        else { err=zmk_keymap_layer_to(req.layer,false); }
    } else if (!strcmp(req.cmd,"ble_start_pairing")) {
        if (!IS_ENABLED(CONFIG_LOG) || reply_conn) { err=-ENOTSUP; }
        else if (!(found & BIT(2)) || req.layer<0 || req.layer>=3) { err=-EINVAL; }
        else {
            knob_ble_cursor=req.layer;
            knob_page=KNOB_BLE;
            err=knob_pair(false);
        }
    } else if (!strcmp(req.cmd,"hard_reset")) { err=knob_factory_reset(); reset=!err; }
    else if (!strcmp(req.cmd,"reset_bootloader")) { err=knob_factory_reset(); reset=!err; boot=!err; }
    else if (!strcmp(req.cmd,"bootloader")) { reset=true; boot=true; }
    else if (strcmp(req.cmd,"ping") && strcmp(req.cmd,"get_config") && strcmp(req.cmd,"get_status")) { err=-ENOTSUP; }
    char reply[850];
    if (err) { snprintf(reply,sizeof(reply),"{\"id\":%d,\"ok\":false,\"error\":\"Invalid command or save failed (%d)\"}\n",req.id,err); }
    else {
        cfg=knob_config;
        char battery[8]="null";
        char voltage[12]="null";
        if (knob_battery_percent() >= 0) { snprintf(battery,sizeof(battery),"%d",knob_battery_percent()); }
        if (knob_battery_mv() >= 0) { snprintf(voltage,sizeof(voltage),"%d",knob_battery_mv()); }
        snprintf(reply,sizeof(reply),
            "{\"id\":%d,\"ok\":true,\"name\":\"KNOB-W\",\"protocol\":1,\"uptime\":%u,"
            "\"battery\":%s,\"battery_mv\":%s,\"battery_cached\":%s,\"layer\":%u,\"profile\":%d,"
            "\"page\":%u,\"pairing\":%u,\"oled_asleep\":%u,\"rgb_idle\":%u,\"inputs\":[%u,%u,%u,%u],"
            "\"connected\":[%d,%d,%d],\"config\":{\"presets\":[%u,%u,%u,%u,%u],"
            "\"presses\":[%u,%u,%u,%u,%u],\"brightness\":%u,\"rgb\":%u,\"display\":%u,\"timeout\":%u,"
            "\"ccw\":[%u,%u,%u,%u,%u],\"cw\":[%u,%u,%u,%u,%u],"
            "\"hue\":%u,\"saturation\":%u,\"effect\":%u,\"speed\":%u,"
            "\"oled_timeout\":%u,\"doubles\":[%u,%u,%u,%u,%u]}}\n",
            req.id,(unsigned)(k_uptime_get()/1000),battery,voltage,knob_battery_cached()?"true":"false",
            zmk_keymap_highest_layer_active(),zmk_ble_active_profile_index(),
            knob_page,knob_pairing_allowed(),knob_is_asleep(),knob_rgb_is_idle(),
            knob_input_count(0),knob_input_count(1),knob_input_count(2),knob_input_count(3),
            zmk_ble_profile_is_connected(0),zmk_ble_profile_is_connected(1),zmk_ble_profile_is_connected(2),
            cfg.presets[0],cfg.presets[1],cfg.presets[2],cfg.presets[3],cfg.presets[4],
            cfg.presses[0],cfg.presses[1],cfg.presses[2],cfg.presses[3],cfg.presses[4],
            cfg.brightness,cfg.rgb_on,cfg.display_on,cfg.timeout_minutes,
            cfg.ccw[0],cfg.ccw[1],cfg.ccw[2],cfg.ccw[3],cfg.ccw[4],
            cfg.cw[0],cfg.cw[1],cfg.cw[2],cfg.cw[3],cfg.cw[4],
            cfg.hue,cfg.saturation,cfg.effect,cfg.speed,cfg.oled_minutes,
            cfg.doubles[0],cfg.doubles[1],cfg.doubles[2],cfg.doubles[3],cfg.doubles[4]);
    }
    send(reply);
    if (reset) {
        k_sleep(K_MSEC(250));
        if (boot) {
            /* 0x57 is the Adafruit/nice!nano UF2 magic. sys_reboot() does not carry
             * it through on nRF, so store it in GPREGRET and reset directly. */
            nrf_power_gpregret_set(NRF_POWER, 0, 0x57);
            NVIC_SystemReset();
        }
        sys_reboot(SYS_REBOOT_COLD);
    }
}
static void rpc(void *a,void *b,void *c) {
    bool usb_ready=device_is_ready(uart) && !uart_irq_callback_user_data_set(uart,ready,NULL);
    char line[RPC_LINE]; size_t len=0; bool overflow=false;
    for (;;) {
        struct ble_request request;
        if (!k_msgq_get(&ble_requests,&request,K_NO_WAIT)) {
            reply_conn=request.conn;
            if (allowed(reply_conn)) { handle(request.line); }
            reply_conn=NULL;
            bt_conn_unref(request.conn);
            continue;
        }
        unsigned char ch;
        if (!usb_ready || uart_poll_in(uart,&ch)) {
            if (usb_ready) { uart_irq_rx_enable(uart); }
            k_sem_take(&rx_ready,K_FOREVER);
            continue;
        }
        if (ch=='\r') { continue; }
        if (ch=='\n') {
            line[len]=0;
            if (!overflow && len) { handle(line); }
            len=0; overflow=false;
        } else if (len+1<sizeof(line) && !overflow) { line[len++]=ch; }
        else { overflow=true; }
    }
}
K_THREAD_DEFINE(knob_rpc_thread,4096,rpc,NULL,NULL,NULL,5,0,1000);
