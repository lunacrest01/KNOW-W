"""Checks against the compiled device tree, configuration and UF2 images."""
from pathlib import Path
import pickle
import struct
import sys

ROOT = Path(__file__).parent
sys.path.insert(0, str(ROOT / 'zephyr/scripts/dts/python-devicetree/src'))


def tree(target='knob_w'):
    with (ROOT / f'build/{target}/zephyr/edt.pickle').open('rb') as file:
        return pickle.load(file)


def config(target='knob_w'):
    return dict(line.split('=', 1) for line in
                (ROOT / f'build/{target}/zephyr/.config').read_text().splitlines()
                if line.startswith('CONFIG_') and '=' in line)


def test_layer_cycle():
    layers = list(tree().get_node('/keymap').children.values())
    assert len(layers) == 5
    assert [layer.props['display-name'].val for layer in layers] == [
        'VOLUME', 'BRIGHTNESS', 'SCROLL', 'H-SCROLL', 'MEDIA']
    for index, layer in enumerate(layers):
        bindings = layer.props['bindings'].val
        assert len(bindings) == 2
        assert bindings[0].controller.matching_compat == 'zmk,behavior-hold-tap'
        assert bindings[0].data == {'param1': 4, 'param2': 0}
        assert bindings[0].controller.props['tapping-term-ms'].val == 2000
        assert len(layer.props['sensor-bindings'].val) == 1


def test_encoder_push_actions():
    layers = list(tree().get_node('/keymap').children.values())
    for layer in layers:
        binding = layer.props['bindings'].val[1]
        assert binding.controller.matching_compat == 'braxon,knob-control'
        assert binding.data == {'param1': 1}


def test_discrete_scroll_steps():
    edt = tree()
    for path, expected in [('/behaviors/vertical', [0xFFFF, 1]),
                           ('/behaviors/horizontal', [0x10000, 0xFFFF0000])]:
        bindings = edt.get_node(path).props['bindings'].val
        assert [binding.data['param1'] for binding in bindings] == expected
        assert all(binding.controller.matching_compat == 'braxon,knob-scroll'
                   for binding in bindings)


def test_hardware():
    edt = tree()
    encoder = edt.get_node('/encoder')
    for prop, pin in [('a-gpios', 6), ('b-gpios', 8)]:
        gpio = encoder.props[prop].val[0]
        assert gpio.controller.path.endswith('gpio@50000000')
        assert gpio.data['pin'] == pin
    assert encoder.props['steps'].val == 80
    buttons = edt.get_node('/kscan').props['input-gpios'].val
    assert [gpio.data['pin'] for gpio in buttons] == [17, 20]
    assert all(gpio.controller.path.endswith('gpio@50000000') and
               gpio.data['flags'] == 17 for gpio in buttons)
    display = edt.chosen_node('zephyr,display')
    assert (display.props['width'].val, display.props['height'].val) == (128, 32)
    assert display.regs[0].addr == 0x3C
    assert edt.chosen_node('zmk,underglow').props['chain-length'].val == 2


def test_persistent_bonds_and_reset():
    for target in ['knob_w', 'reset']:
        cfg = config(target)
        assert cfg['CONFIG_FLASH'] == 'y'
        assert cfg['CONFIG_SETTINGS_NVS'] == 'y'
        assert cfg['CONFIG_FLASH_LOAD_OFFSET'] == '0x1000'
    assert config('reset')['CONFIG_ZMK_SETTINGS_RESET_ON_START'] == 'y'
    assert 'CONFIG_ZMK_SETTINGS_RESET_ON_START' not in config()
    assert config()['CONFIG_BT_DEVICE_NAME'] == '"KNOB-W"'
    assert config()['CONFIG_BT_SETTINGS'] == 'y'
    assert config()['CONFIG_ZMK_IDLE_TIMEOUT'] == '1800000'
    assert config()['CONFIG_BT_MAX_PAIRED'] == '3'
    assert config()['CONFIG_BT_MAX_CONN'] == '3'


def test_rgb_independent_of_oled():
    shield = ROOT / 'config/boards/shields/knob_w'
    rgb = (shield / 'knob_w_rgb.c').read_text()
    display = (shield / 'knob_w_display.c').read_text()
    assert 'zmk_rgb_underglow' not in display
    assert 'lvgl' not in rgb and 'zmk/display' not in rgb
    assert 'SETTINGS_STATIC_HANDLER_DEFINE' in rgb
    assert 'zmk_rgb_underglow_on()' in rgb
    assert 'knob_config.effect' in rgb and 'knob_config.speed' in rgb
    cfg = config()
    assert int(cfg['CONFIG_ZMK_RGB_UNDERGLOW_BRT_START']) == 100
    assert cfg['CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE'] == 'y'


def test_oled_native_monochrome_renderer():
    cfg = config()
    display = tree().chosen_node('zephyr,display')
    width, height = display.props['width'].val, display.props['height'].val
    assert width * height // 8 == 512
    assert cfg['CONFIG_CHARACTER_FRAMEBUFFER'] == 'y'
    assert 'CONFIG_CHARACTER_FRAMEBUFFER_USE_DEFAULT_FONTS' not in cfg
    assert 'CONFIG_LVGL' not in cfg
    assert int(cfg['CONFIG_HEAP_MEM_POOL_SIZE']) >= 1024
    source = (ROOT / 'config/boards/shields/knob_w/knob_w_display.c').read_text()
    assert 'knob_is_idle()' in source
    assert 'cfb_framebuffer_invert' not in source
    assert 'display_blanking_on(oled)' in source
    assert 'cfb_framebuffer_finalize(oled)' in source
    # The custom 6x8 font keeps the heading and three action columns in bounds.
    for layer in tree().get_node('/keymap').children.values():
        text = '5 ' + layer.props['display-name'].val
        assert len(text) <= 12
        x = (128 - len(text) * 6) // 2
        assert x >= 0 and x + len(text) * 6 <= 128
    for text in ['PRESS','LIGHT','PASTE','RIGHT']:
        assert len(text)*6 <= 42


def test_battery_usb_guard_and_bitmap_sizes():
    import re
    shield = ROOT / 'config/boards/shields/knob_w'
    battery = (shield / 'knob_battery.c').read_text()
    assert '!zmk_usb_is_powered()' in battery
    assert config()['CONFIG_ZMK_BATTERY_NRF_VDDH'] == 'y'
    source = (shield / 'knob_assets.h').read_text()
    for name, count in [('knob_logo',1),('knob_cat',2)]:
        section = source.split(f'{name}[{count}][512] = {{')[1].split('};')[0]
        assert len(re.findall('0x[0-9a-f]{2}',section)) == count*512


def test_hid_dispatch_has_stack_headroom_and_bounded_scroll():
    cfg=config()
    assert cfg['CONFIG_EC11_TRIGGER_GLOBAL_THREAD']=='y'
    for name, size in [('SYSTEM_WORKQUEUE',4096),('ZMK_BLE_THREAD',2048),('INPUT_THREAD',2048)]:
        assert int(cfg[f'CONFIG_{name}_STACK_SIZE'])>=size
    source=(ROOT/'config/boards/shields/knob_w/knob_w_scroll.c').read_text()
    assert 'K_FOREVER' not in source
    assert cfg['CONFIG_HW_STACK_PROTECTION']=='y'
    assert int(cfg['CONFIG_BT_RX_STACK_SIZE'])>=4096
    assert int(cfg['CONFIG_BT_LONG_WQ_STACK_SIZE'])>=4096
    assert int(cfg['CONFIG_BT_CTLR_RX_PRIO_STACK_SIZE'])>=2048
    assert int(cfg['CONFIG_BT_CTLR_RX_STACK_SIZE'])>=3072
    hog=(ROOT/'zmk/app/src/hog.c').read_text()
    assert config()['CONFIG_BT_PERIPHERAL_PREF_LATENCY']=='0'
    assert 'report, K_MSEC(100)' not in hog
    assert hog.count('case -ENOMSG:')==3
    assert '.version = 0x0111,' in hog
    for kind in ('keyboard','consumer','mouse'):
        assert f'return zmk_hog_send_{kind}_report(report);' not in hog
        assert f'k_msgq_put(&zmk_hog_{kind}_msgq, report, K_NO_WAIT)' in hog
    control=(ROOT/'config/boards/shields/knob_w/knob_control.c').read_text()
    assert 'K_WORK_DELAYABLE_DEFINE(press_release_work,release_encoder_tap)' in control
    assert 'zmk_behavior_queue_add(&event,tap' not in control


def test_fullscreen_cat_and_immediate_profile_save():
    assert config()['CONFIG_ZMK_HID_INDICATORS']=='y'
    display=(ROOT/'config/boards/shields/knob_w/knob_w_display.c').read_text()
    assert 'hid_indicators' not in display
    assert 'lock_overlay' not in display
    assert 'bitmap(knob_cat[k_uptime_get()/400%2])' in display
    source=(ROOT/'zmk/app/src/ble.c').read_text()
    save=source.split('static int ble_save_profile(void)')[1].split('int zmk_ble_prof_select')[0]
    assert '#if IS_ENABLED(CONFIG_SHIELD_KNOB_W)' in save
    assert 'return settings_save_one("ble/active_profile", &active_profile, sizeof(active_profile));' in save
    assert 'if (err<0) { active_profile=previous; return err; }' in source


def native_compiler():
    """Find a host GCC without baking one machine's path into the tests."""
    import os
    import shutil
    override = os.environ.get('KNOB_W_GCC')
    if override:
        return Path(override)
    for candidate in ('C:/QMK_MSYS/mingw64/bin/gcc.exe', 'C:/msys64/mingw64/bin/gcc.exe'):
        if Path(candidate).exists():
            return Path(candidate)
    found = shutil.which('gcc')
    return Path(found) if found else None


def test_bond_policy_native(tmp_path):
    import subprocess
    import os
    compiler=native_compiler()
    if not compiler or not compiler.exists():
        import pytest
        pytest.skip('Native GCC unavailable; set KNOB_W_GCC or install gcc')
    output=tmp_path/'bond_policy_test.exe'
    env={**os.environ,'PATH':str(compiler.parent)+os.pathsep+os.environ.get('PATH','')}
    subprocess.run([str(compiler),'-std=c11','-I',str(ROOT/'zmk/app/include'),
                    str(ROOT/'tests/bond_policy_test.c'),'-o',str(output)],check=True,env=env)
    subprocess.run([str(output)],check=True,env=env)


def test_ble_profile_isolation_and_explicit_discovery():
    source = (ROOT / 'zmk/app/src/ble.c').read_text()
    connected = source.split('static void connected(')[1].split('static void disconnected(')[0]
    assert 'pending_slot' not in source
    assert 'bt_conn_set_security' not in connected
    policy=source.split('static bool pairing_allowed_for_current_profile')[1].split('static enum bt_security_err')[0]
    assert 'KNOB_BOND_VALID' in policy
    assert 'set_profile_address(pairing_context[bt_conn_index(conn)].slot, dst)' in source
    assert 'profile_generation[profile]++' in source
    assert 'secure_links_work' not in source
    assert config()['CONFIG_BT_GATT_AUTO_SEC_REQ']=='y'
    assert 'if (bt_conn_get_info(conn, &info)) { return; }' in connected
    assert connected.index('if (err)') < connected.index('bt_conn_get_info')
    assert '!knob_pairing_allowed() || known>=0' in connected
    control=(ROOT/'config/boards/shields/knob_w/knob_control.c').read_text()
    assert 'if (knob_ble_hid_ready())' in control
    assert 'knob_new_pairing() || knob_reconnect_needed()' in source
    assert 'if (knob_new_pairing())' in source
    assert 'BT_LE_ADV_OPT_FILTER_CONN | BT_LE_ADV_OPT_FILTER_SCAN_REQ' in source
    assert 'if (zmk_ble_profile_is_open(i)) { continue; }' in source
    assert 'bt_le_filter_accept_list_add(zmk_ble_profile_address(i))' in source
    assert config()['CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE'] == 'y'
    assert config().get('CONFIG_ZMK_BLE_PASSKEY_ENTRY', 'n') == 'n'
    assert config().get('CONFIG_BT_CTLR_XTAL_ADVANCED','n') == 'n'
    assert config().get('CONFIG_BT_CTLR_ASSERT_OVERHEAD_START','n') == 'n'
    assert config()['CONFIG_ASSERT']=='y'


def test_ble_compatibility_and_advertising_stability():
    cfg = config()
    # 1.25 ms units: 15-30 ms, no skipped events; supervision remains four seconds.
    assert cfg['CONFIG_BT_PERIPHERAL_PREF_MIN_INT'] == '12'
    assert cfg['CONFIG_BT_PERIPHERAL_PREF_MAX_INT'] == '24'
    assert cfg['CONFIG_BT_PERIPHERAL_PREF_TIMEOUT'] == '400'
    assert cfg['CONFIG_BT_CTLR_TX_PWR_DBM'] == '8'
    assert cfg.get('CONFIG_BT_CTLR_PHY_2M', 'n') == 'n'
    source = (ROOT / 'zmk/app/src/ble.c').read_text()
    update = source.split('int update_advertising(void)')[1].split('static void update_advertising_callback')[0]
    assert 'advertising_filtered ==' in update
    assert '!advertising_dirty && desired_adv' in update
    address = source.split('void set_profile_address(')[1].split('bool zmk_ble_active_profile_is_connected')[0]
    assert 'advertising_dirty = true;' in address
    assert 'params.interval_min = BT_GAP_MS_TO_ADV_INTERVAL(20);' in source
    assert 'params.interval_max = BT_GAP_MS_TO_ADV_INTERVAL(20);' in source
    assert update.index('advertising_filtered ==') < update.index('CHECKED_ADV_STOP()')


def test_oled_i2c_transfer_fits_full_frame():
    display = tree().chosen_node('zephyr,display')
    transaction_bytes = display.props['width'].val * display.props['height'].val // 8 + 1
    assert display.parent.props['zephyr,concat-buf-size'].val >= transaction_bytes
    assert display.parent.props['zephyr,flash-buf-max-size'].val >= transaction_bytes


def test_diagnostic_pairing_requires_an_explicit_valid_slot():
    rpc = (ROOT / 'config/boards/shields/knob_w/knob_rpc.c').read_text()
    command = rpc.split('!strcmp(req.cmd,"ble_start_pairing")')[1].split('!strcmp(req.cmd,"hard_reset")')[0]
    assert 'if (!IS_ENABLED(CONFIG_LOG) || reply_conn)' in command
    assert '!(found & BIT(2)) || req.layer<0 || req.layer>=3' in command
    assert 'knob_pair(false)' in command
    assert 'clear_bonds' not in command and 'factory_reset' not in command
    stack = rpc.split('!strcmp(req.cmd,"get_stack_diag")')[1].split('!strcmp(req.cmd,"get_fault_diag")')[0]
    assert '#if IS_ENABLED(CONFIG_INIT_STACKS)' in stack
    assert 'k_thread_stack_space_get(&prio_recv_thread_data' in stack
    assert 'k_thread_stack_space_get(&recv_thread_data' in stack


def test_uf2_boundaries_and_vector_table():
    for target in ['knob_w', 'reset']:
        raw = (ROOT / f'build/{target}/zephyr/zmk.uf2').read_bytes()
        assert len(raw) % 512 == 0
        addresses = []
        for number, offset in enumerate(range(0, len(raw), 512)):
            magic0, magic1, flags, address, size, index, total, family = struct.unpack_from('<8I', raw, offset)
            assert (magic0, magic1) == (0x0A324655, 0x9E5D5157)
            assert struct.unpack_from('<I', raw, offset + 508)[0] == 0x0AB16F30
            assert flags & 0x2000 and family == 0xADA52840
            assert index == number and total == len(raw) // 512
            assert 0 < size <= 476 and 0x1000 <= address < address + size <= 0xEC000
            addresses.append(address)
            if address == 0x1000:
                stack, reset = struct.unpack_from('<2I', raw, offset + 32)
                assert 0x20000000 < stack <= 0x20040000
                assert reset & 1 and 0x1000 <= (reset & ~1) < 0xEC000
        assert min(addresses) == 0x1000 and len(set(addresses)) == len(addresses)
