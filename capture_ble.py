"""Capture one pairing test to a local, redacted log for ten minutes."""
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports

root=Path(__file__).parent
log=root/"build/ble-pairing-capture.log"
deadline=time.monotonic()+600
with log.open("w",encoding="utf-8",buffering=1) as out:
    out.write("Waiting for KNOB-W USB. Debug values >= 8 hex bytes are redacted.\n")
    while time.monotonic()<deadline:
        ports=[p.device for p in list_ports.comports()
               if (p.vid,p.pid)==(0x1D50,0x615E) and p.serial_number=="8732BA44360E3E26"]
        if not ports:
            time.sleep(0.5)
            continue
        try:
            with serial.Serial(ports[0],115200,timeout=0.5) as device:
                out.write("CAPTURE CONNECTED\n")
                while time.monotonic()<deadline:
                    line=device.readline().decode("utf-8",errors="replace")
                    if line:
                        line=re.sub(r"(?i)(?:[0-9a-f]{2}[ :\-]?){8,}", "[REDACTED]",line)
                        out.write(line)
        except serial.SerialException as error:
            out.write(f"USB unavailable: {error}\n")
            time.sleep(1)
    out.write("CAPTURE FINISHED\n")
