"""One bounded Windows pairing attempt; never resets bonds or unrelated devices."""
import asyncio
import json
import time

from bleak import BleakClient, BleakScanner


def report(event, **values):
    print(json.dumps({"time": time.time(), "event": event, **values}), flush=True)


async def main():
    report("scan_start")
    try:
        device = await BleakScanner.find_device_by_filter(
            lambda device, advert: advert.local_name == "KNOB-W", timeout=15
        )
    except Exception as error:
        report("scan_failed", error_type=type(error).__name__, error=str(error))
        return
    if device is None:
        report("not_found")
        return
    report("found", address=device.address)
    try:
        async with BleakClient(device, pair=True, timeout=25) as client:
            report("connected", connected=client.is_connected,
                   services=[service.uuid for service in client.services])
            await asyncio.sleep(5)
            report("connection_after_5_seconds", connected=client.is_connected)
    except Exception as error:
        report("failed", error_type=type(error).__name__, error=str(error))


if __name__ == "__main__":
    try:
        asyncio.run(asyncio.wait_for(main(), timeout=50))
    except TimeoutError:
        report("overall_timeout")
