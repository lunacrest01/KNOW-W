"""KNOB-W serial diagnostics: one tool for status, bond table, log capture and pairing.

    python knob_diag.py                    # status report
    python knob_diag.py --watch 60         # sample into build/knob-status.jsonl
    python knob_diag.py --capture 120      # raw debug log into build/pairing-capture.log
    python knob_diag.py --pair 2           # open a pairing window on slot 2 (diagnostic image only)
    python knob_diag.py --factory-bootloader   # erase every setting, then reboot to the UF2 bootloader

Connect the knob over USB and close Studio's serial connection first.
Nothing here erases a bond or changes a saved setting, except
``--factory-bootloader``, which erases everything by design.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

ROOT = Path(__file__).parent
STATUS_LOG = ROOT / "build/knob-status.jsonl"
CAPTURE_LOG = ROOT / "build/pairing-capture.log"
VID_PID = (0x1D50, 0x615E)
PAGES = {0: "MODE", 1: "BLE", 2: "IDLE"}
MODES = ["VOLUME", "BRIGHTNESS", "SCROLL", "H-SCROLL", "MEDIA"]
ADVERTISING = {0: "not advertising", 1: "advertising", 2: "directed advertising"}
REDACT = re.compile(r"(?i)(?:[0-9a-f]{2}[ :\-]?){8,}")


def port():
    for candidate in list_ports.comports():
        if (candidate.vid, candidate.pid) == VID_PID:
            return candidate.device
    return None


def request(command, **fields):
    """Send one JSON line and return the matching reply."""
    device = port()
    if not device:
        raise SystemExit("No KNOB-W serial port. Connect USB and close Studio's serial connection.")
    payload = {"id": 1, "cmd": command, **fields}
    for _ in range(3):
        try:
            with serial.Serial(device, 115200, timeout=0.25, write_timeout=1) as link:
                time.sleep(0.15)
                link.reset_input_buffer()
                link.write((json.dumps(payload) + "\n").encode())
                end = time.monotonic() + 5
                while time.monotonic() < end:
                    line = link.readline()
                    if not line:
                        continue
                    text = line.decode("utf-8", "replace").strip()
                    if not text.startswith("{"):
                        continue
                    try:
                        value = json.loads(text)
                    except ValueError:
                        continue
                    if value.get("id") == 1:
                        if "error" in value:
                            raise SystemExit(f"{command} failed: {value['error']}")
                        return value
        except serial.SerialException:
            pass
        time.sleep(0.5)
    raise SystemExit(f"No reply to {command}. Replug USB and try again.")


def sample():
    return {
        "time": time.time(),
        "state": request("ping"),
        "ble": request("get_ble_diag"),
        "bonds": request("get_bonds"),
        "fault": request("get_fault_diag"),
    }


def report(data):
    state, ble, bonds, fault = data["state"], data["ble"], data["bonds"], data["fault"]
    seconds = state.get("uptime", 0)
    clock = "%02d:%02d:%02d" % (seconds // 3600, seconds // 60 % 60, seconds % 60)
    layer = state.get("layer", 0)
    print(f"KNOB-W  uptime {clock}  layer {layer + 1} {MODES[layer]}  "
          f"profile {state.get('profile', 0) + 1}  page {PAGES.get(state.get('page'), '?')}")

    battery = state.get("battery")
    supply = "USB" if state.get("battery_cached") else "battery"
    config = state.get("config", {})
    print(f"config  idle {config.get('timeout')} min  oled {config.get('oled_timeout')} min  "
          f"brightness {config.get('brightness')}  rgb {'on' if config.get('rgb') else 'off'}  "
          f"supply {supply}  battery {battery if battery is not None else '-'}%")

    slots = bonds.get("slots", [])
    active = sum(1 for slot in slots if slot.get("connected"))
    # The firmware's "links" counter is connection attempts, not live links.
    print(f"BLE     {ADVERTISING.get(bonds.get('adv', 0), '?')}"
          f"{', filtered' if bonds.get('filtered') else ''}"
          f"  attempts {ble.get('links', 0)}  active links {active}"
          f"  pairing {'yes' if ble.get('pairing') else 'no'}")

    for index, slot in enumerate(slots):
        if not slot.get("bonded"):
            label = "empty"
        else:
            label = "CONNECTED" if slot.get("connected") else "bonded"
        marker = " (active)" if index == state.get("profile") else ""
        print(f"        {index + 1}  {slot.get('addr', '')[:17]:<17}  {label}{marker}")

    print(f"errors  link {ble.get('link_error', 0)}  auth {ble.get('auth_requests', 0)}  "
          f"pair {ble.get('pair_error', 0)}  security {ble.get('security_error', 0)}  "
          f"last_disconnect 0x{ble.get('disconnect_reason', 0):02x}")
    if fault.get("fault"):
        print(f"fault   reason {fault.get('reason')} at {fault.get('at_uptime')}s  "
              f"thread {fault.get('thread')}  pc 0x{fault.get('pc', 0):08x}")
    else:
        print("fault   none")


def capture(seconds):
    device = port()
    if not device:
        raise SystemExit("No KNOB-W serial port.")
    deadline = time.monotonic() + seconds
    CAPTURE_LOG.parent.mkdir(exist_ok=True)
    with CAPTURE_LOG.open("w", encoding="utf-8", buffering=1) as out:
        with serial.Serial(device, 115200, timeout=0.3) as link:
            out.write("capture start %s\n" % time.strftime("%H:%M:%S"))
            while time.monotonic() < deadline:
                line = link.readline()
                if line:
                    out.write(REDACT.sub("[REDACTED]", line.decode("utf-8", "replace")))
            out.write("capture end\n")
    print(f"captured {seconds}s to {CAPTURE_LOG}")


def watch(seconds):
    STATUS_LOG.parent.mkdir(exist_ok=True)
    deadline = time.monotonic() + seconds
    with STATUS_LOG.open("w", encoding="utf-8", buffering=1) as out:
        while time.monotonic() < deadline:
            try:
                row = sample()
            except SystemExit as error:
                row = {"time": time.time(), "error": str(error)}
            out.write(json.dumps(row) + "\n")
            time.sleep(1)
    print(f"wrote {STATUS_LOG}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--watch", type=float, metavar="SECONDS")
    parser.add_argument("--capture", type=float, metavar="SECONDS")
    parser.add_argument("--pair", type=int, choices=[1, 2, 3], metavar="SLOT")
    parser.add_argument("--factory-bootloader", action="store_true",
                        help="erase all settings and bonds, then reboot to the UF2 bootloader")
    parser.add_argument("--json", action="store_true", help="print the raw replies")
    options = parser.parse_args()

    if options.watch:
        watch(options.watch)
    elif options.capture:
        capture(options.capture)
    elif options.factory_bootloader:
        print("Erasing all settings and bonds, then rebooting to the bootloader...")
        try:
            request("reset_bootloader")
        except SystemExit as error:
            # The device resets before the reply always arrives; that is expected.
            print(f"no reply ({error}) - this is normal for a reset")
        print("Look for the NICENANO drive, then copy a UF2 image to it.")
    elif options.pair:
        reply = request("ble_start_pairing", layer=options.pair - 1)
        print(json.dumps(reply, indent=1) if options.json else
              f"pairing window open on slot {options.pair} for 90 s; "
              "pair from the host now (diagnostic image only)")
    else:
        data = sample()
        print(json.dumps(data, indent=1) if options.json else "", end="")
        if not options.json:
            report(data)


if __name__ == "__main__":
    sys.exit(main())
