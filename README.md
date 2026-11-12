# KNOB-W

Basic USB/Bluetooth ZMK knob for the Robu ProMicro nRF52840, one separate
layer button, encoder push switch, two WS2812B LEDs, and a 128x32 SSD1306 I2C OLED.
This is separate from TAC-W. Braxon Studio detects KNOB-W over USB and opens
isolated Knob, RGB and Display controls. TAC-W firmware is unchanged.

## Controls

Releasing the separate button advances to the next mode, wrapping after five.
Holding it for two seconds cycles Mode, Bluetooth and Bongo screens without
changing the layer. Boot shows the Braxon bitmap for two seconds, then Volume.
Encoder push has a separate configurable action on each layer, triggered on release.
Confirmed BLE profile changes are saved immediately, not after ZMK's normal
60-second debounce. Reboot retains the last selected host, independent of
which paired host reconnects first.

The Bongo screen uses the entire display without lock indicators. Existing HID
LED output reports remain supported to avoid changing paired hosts' descriptors.

On the Bluetooth screen, rotate to highlight profile 1, 2 or 3 without interrupting
the current host. Click to activate it: an empty slot pairs, a saved slot reconnects.
Hold the encoder for at least two seconds and release to forget just that slot and
pair a replacement. Remove the old KNOB-W entry on the host if it retains the old bond.
Pairing lasts 90 seconds, blinking RGB blue; connection shows green for five
seconds. Boot automatically reconnects stored profiles using a controller accept
list containing only saved hosts. Empty or cleared profiles are excluded.
Only an explicit pairing window on an empty selected slot uses unfiltered
advertising. Selecting a profile still determines which host receives HID.
Three links may be held; only the selected host gets HID.
Each connection captures its pairing slot before authentication. Pairing completion
cannot follow a later UI selection into another slot or assign the same host twice.
Resetting a slot disconnects only its links and invalidates its in-flight attempts.
Security is requested automatically from a deferred worker, including with only
a phone present. Green feedback waits for link encryption, not just link creation.

Outside the Bluetooth screen, hold the encoder for 4.5 seconds to show POWERING OFF and enter nRF System OFF.
Only the separate layer button (P0.17 to GND) is armed as a GPIO wake source.
Wake reboots the application. Power removal/reconnection can also reboot the board.

| Mode | Counterclockwise | Clockwise | Encoder press |
| --- | --- | --- | --- |
| Volume | Quieter | Louder | Mute |
| Brightness | Dimmer | Brighter | Unassigned |
| Vertical scroll | Up | Down | Cut (Ctrl+X) |
| Horizontal scroll | Left | Right | Cut (Ctrl+X) |
| Media | Previous track | Next track | Play/Pause |

Cut uses the Windows/Linux shortcut; macOS normally uses Command+X instead.

In Studio, Display lets you save a layer title (16 printable ASCII characters) and
left, push and right captions (6 each). Empty captions follow the assigned action.
These labels change the OLED only; use Knob to change the actual actions.

Brightness is a standard HID brightness command, not OLED brightness. Support
depends on the computer/OS/display; many external monitors ignore these keys.
Scroll is one discrete wheel step per detent. The default encoder is a typical
20-detent EC11 with 80 quadrature transitions per rotation. Other encoders may
need `steps` and `triggers-per-rotation` adjusted in the shield overlay.

## Wiring

Use the nRF GPIO labels below, not Arduino pin numbers. All grounds are common.

| Component | Pin | Board connection |
| --- | --- | --- |
| Encoder | A | P0.06 / 006 |
| Encoder | B | P0.08 / 008 |
| Encoder | C/common | GND |
| Encoder push switch | One terminal / SW | P0.20 / 020 |
| Encoder push switch | Other terminal | GND |
| Layer button | One terminal | P0.17 / 017 |
| Layer button | Other terminal | GND |
| OLED | SDA | P1.00 / 100 |
| OLED | SCL | P0.11 / 011 |
| OLED | VCC | 3.3 V |
| OLED | GND | GND |
| First RGB LED | DIN | P0.10 / 010 through a series data resistor |
| Second RGB LED | DIN | First LED DOUT |
| Both RGB LEDs | GND | GND |

The OLED address is 0x3C. Encoder/button inputs use internal pull-ups.
Swap encoder A/B if direction is reversed. The physical switch legs must be
identified correctly: opposite sides, not two already-connected legs.

Power WS2812B LEDs from the supply specified for your exact LED variant.
Standard 5 V variants should use an appropriate 3.3-to-5 V logic buffer; do not
apply 5 V to any nRF GPIO. Battery operation with 5 V LEDs needs a suitable boost
supply. A 3.3 V LED supply is not guaranteed to work for every WS2812B variant.

## OLED, RGB and idle

OLED shows KNOB-W, selected USB/BLE transport, mode number and current mode.
The transport label is not a connection-confirmation indicator.
The SSD1306 uses Zephyr's character framebuffer with a full 512-byte frame
and a generated 6x8 font. This bypasses LVGL's monochrome conversion path.
The mode screen labels both rotation directions and the encoder press action.
The Bongo frames come from qmk-bongo; see THIRD_PARTY.md for source and license.
The Nordic TWIM transfer buffer is 513 bytes to join the SSD1306 control byte
and its 512-byte frame into one I2C transaction. The default 16 bytes is too small.
Both LEDs show the mode color at 20% brightness. Below 20% battery, RGB is disabled.
After ten minutes without activity, RGB stops and the OLED shows Bongo. After
fifteen minutes the OLED blanks; BLE remains available and either button or the
encoder resumes operation. Studio's idle setting defaults to ten minutes;
OLED sleep follows five minutes later. Automatic sleep is not System OFF.
RGB works without the OLED connected, and each boot restores solid mode lighting
even if idle shutdown previously saved an off state.
A button press or encoder activity resumes normal operation. Black LEDs still
consume standby current; this firmware does not switch their supply off.
Battery-only operation uses the nRF internal VDDH ADC channel, with the standard
ZMK voltage-based estimate. USB can raise VDDH, so USB readings are unavailable
until a valid battery-only reading exists, then explicitly marked cached in Studio.
The board-family assumption must be checked against a multimeter on this clone.
Runtime in Studio requires battery capacity and measured average load current;
it is an estimate, not a fuel-gauge measurement. Software System OFF does not
physically cut battery power to the OLED or WS2812B supply rails.

## Build and flash

BLE troubleshooting: connection reason 0x08 means supervision timeout, not expiry
of the 90-second user pairing window. A shorter pairing window cannot repair lost
radio traffic. Preferred connection intervals are 15-30 ms, latency zero, with a
four-second supervision timeout; the host negotiates actual parameters. Radio
transmit power is +8 dBm, which may increase battery use. Zephyr's built-in GATT
security re-establishment replaces the custom delayed security worker. Advertising
is not stopped/restarted when its open/filtered mode is unchanged. The explicit
pairing window uses 20 ms advertising for faster discovery. These changes require
physical pairing/reconnection tests; they are not a guarantee of a fix.
`./build.ps1 -Logging` produces a separate diagnostic UF2 without overwriting the
saved normal UF2. Do not erase bonds or remove unrelated Windows Bluetooth devices
as a routine update step.

The current diagnostic overlay uses LFCLK synthesized from HFCLK to test clock
timing independently of LFRC calibration. It consumes more power and is NOT a
release power configuration. The Studio build also uses the synthesized clock
to retain the timing configuration that passed the Windows pairing test.
With this image only, `ble_start_pairing` over USB accepts an explicit zero-based
slot in `layer` (0-2), opens the existing pairing window, and never erases a bond.
`diagnose_windows_ble.py` uses installed Bleak/Windows APIs for one bounded scan,
pair, service-discovery and five-second connection test; it does not unpair devices.
The Windows clock test reached SMP and then recorded fatal reason 2 in
`prio_recv_thread_data` (the controller RX-priority thread, not the host RX queue).
Its former 448-byte stack is now 2048 bytes; controller normal RX gets 3072 bytes.
The debug image enables stack watermarking; `get_stack_diag` reports unused bytes
for both controller RX threads after a pairing attempt.
The follow-up Windows test completed SMP at security level 2, stored profile 1,
and Windows logged BTHUSB event 8 (successful pairing). Its keyboard, mouse and
consumer-control PnP interfaces all reported OK. The priority RX watermark had
1472/2048 bytes free: 576 bytes used, exceeding the former 448-byte allocation.
This verifies the overflow fix on Windows with the synthesized-clock debug build;
iOS, Android, power-cycle reconnection and release battery use remain unverified.

### Studio controls

Studio configuration works over USB CDC or the encrypted BLE configuration
service. BLE commands are accepted only from the active bonded profile. On
Windows, pair the knob in Bluetooth settings, then use Authorize BLE in the
local Studio launched with the bridge's `--device KNOB-W` option.

Each layer has a rotation preset and separate counterclockwise, clockwise and
press choices. Rotation overrides can be disabled or set back to the preset.
RGB settings include solid, breathing, spectrum and swirl, plus brightness,
hue, saturation and speed. These save alongside the existing OLED settings.
Older saved knob configurations retain their original assignments on upgrade.
The device's BLE screen uses a segmented battery icon; Studio still reports
the measured percentage when available.

Run `./build.ps1` for normal firmware or `./build.ps1 -Target reset` for a settings
erase image. Reset erases the board's ZMK settings and Bluetooth bonds, not the
bootloader. Remove the old pairing on the computer too.

Normal updates only need `KNOB-W.uf2`; do not erase settings again unless needed.
For a deliberate factory reset followed by installation:

1. Double-tap RESET to enter the NICENANO UF2 bootloader.
2. Copy `KNOB-W-settings-reset.uf2` to it. Let the board boot for several seconds.
3. Double-tap RESET again, then copy `KNOB-W.uf2` to the bootloader drive.
4. Pair the device named **KNOB-W**, or use USB.

These images target the inspected bootloader: nice!nano UF2 0.6.0,
`SoftDevice: not found`. The application starts at 0x1000, settings at 0xEC000,
and bootloader at 0xF4000. Do not use these images on a different bootloader
layout without rebuilding. The firmware supports three Bluetooth profiles.

## Source and dependencies

Custom source is under `config/`: shield wiring, five-mode keymap, small OLED
screen, and a discrete scroll behavior using Zephyr input events. Standard ZMK
handles USB, BLE, encoder decoding, layers, RGB, and idle behavior.

ZMK revision: `ead91ca094c6cd778574f73b9ae011ddab611c66`.
Zephyr revision: `58a5874a446ace2893a196848282d271a551e512`.
The `zmk` folder is an isolated Git worktree with local BLE policy and RGB overlay
hooks for KNOB-W. Those edits are archived in `patches/zmk-knob-w.patch`; see
`patches/README.md` to apply or regenerate them, because `zmk/` is not tracked
here. `zephyr` and `modules` are junctions to
the existing `C:/Users/bhavy/wire-x` dependency workspace to avoid duplicating
the SDK/dependencies. Build paths in `build.ps1` default to local-machine
locations; override them with `KNOB_W_CMAKE`, `KNOB_W_PYTHON`,
`KNOB_W_ZEPHYR_SDK` and `KNOB_W_ZEPHYR` on another machine. Set `KNOB_W_GCC`
if host GCC is not on `PATH` for the native bond-policy test.
The existing Zephyr ST7789 change is unrelated to this SSD1306 target.
Keep these dependency paths available when rebuilding; they are not bundled.

## Verification

Encoder dispatch uses the TAC-W global-workqueue model, with 6144-byte system,
4096-byte Bluetooth HID and 2048-byte input stacks. The Bluetooth receive stack
is 6144 bytes and the crypto/long workqueue has 4096 bytes. HID queue retries
are bounded rather than recursive. Encoder clicks use one delayed release
instead of separately enqueueing a press and release. Scroll enqueue waits at most
10 ms instead of forever; queue overload returns an error rather than hanging.
Hardware stack guards/assertions detect faults. `check_ble.py` reads both BLE errors and
the most recent fatal reason/uptime, thread address and exception PC/LR retained
through a software reboot. Connect
USB and close Studio's serial connection before running it. A power cut or
bootloader RAM overwrite can erase that diagnostic; it is not a flash crash log.

`knob_diag.py` is the serial diagnostic entry point. `--status` prints uptime,
layer, active profile, bond table, advertising mode and the fault record;
`--watch 60` samples every second into `build/knob-status.jsonl`;
`--capture 120` writes the debug log to `build/pairing-capture.log` with long
hex runs redacted; and `--pair 2` opens a 90-second pairing window on an empty
slot without erasing a bond (diagnostic image only, because the release build
compiles that command out). `get_bonds` also answers over raw JSON, listing each
slot's peer address with its bonded and connected state.

Both images compiled successfully. Run
`C:/Users/bhavy/zephyrproject/.venv/Scripts/python.exe -m pytest test_firmware.py -q`.
Sixteen checks plus compile-time power-boundary assertions cover OLED rendering/transfer buffer sizing, navigation, encoder bindings, RGB/OLED separation, discrete scroll direction/step values,
hardware configuration, persistent pairing/reset settings, and UF2 address and
vector-table boundaries. They do not replace physical encoder, OLED, RGB or BLE
testing. The build output reports current flash and RAM usage.
