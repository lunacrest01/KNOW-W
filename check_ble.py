"""Read pairing diagnostics without changing settings or bonds: python check_ble.py"""
import json
import time
import serial
from serial.tools import list_ports


def read_diagnostics(command="get_ble_diag", **parameters):
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x1D50, 0x615E)]
    for port in ports:
        with serial.Serial(port, 115200, timeout=0.5) as device:
            for cmd in ("ping", command):
                request = {"id": 71, "cmd": cmd}
                if cmd == command:
                    request.update(parameters)
                device.write((json.dumps(request) + "\n").encode())
                end = time.monotonic() + 3
                while time.monotonic() < end:
                    try:
                        value = json.loads(device.readline())
                    except (ValueError, UnicodeError):
                        continue
                    if value.get("id") == 71:
                        break
                else:
                    break
                if cmd == "ping" and value.get("name") != "KNOB-W":
                    break
                if cmd == command:
                    return value
    raise SystemExit("No responding KNOB-W. Connect USB and disconnect Studio first.")


if __name__ == "__main__":
    import sys
    if "--watch" in sys.argv:
        from pathlib import Path
        end=time.monotonic()+600
        with (Path(__file__).parent/"build/ble-status-watch.jsonl").open("w",buffering=1) as log:
            while time.monotonic()<end:
                try:
                    row={"time":time.time(),"ble":read_diagnostics(),"fault":read_diagnostics("get_fault_diag")}
                    log.write(json.dumps(row)+"\n")
                except (SystemExit,serial.SerialException,ValueError) as error:
                    log.write(json.dumps({"time":time.time(),"error":str(error)})+"\n")
                time.sleep(1)
    else:
        print(json.dumps(read_diagnostics(), indent=2))
        print(json.dumps(read_diagnostics("get_fault_diag"), indent=2))
