import asyncio
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

import diagnose_windows_ble as probe


@pytest.mark.parametrize('outcome', ['missing', 'connected', 'failed', 'scan_failed'])
def test_probe_is_bounded_and_reports_outcomes(monkeypatch, outcome):
    device = None if outcome == 'missing' else SimpleNamespace(address='test-device')
    scanner = AsyncMock(return_value=device)
    if outcome == 'scan_failed':
        scanner.side_effect = RuntimeError('radio off')
    monkeypatch.setattr(probe.BleakScanner, 'find_device_by_filter', scanner)
    monkeypatch.setattr(probe.asyncio, 'sleep', AsyncMock())
    events = []
    monkeypatch.setattr(probe, 'report', lambda event, **values: events.append((event, values)))

    class Client:
        def __init__(self, selected, *, pair, timeout):
            assert selected is device and pair and timeout == 25
            self.is_connected = True
            self.services = [SimpleNamespace(uuid='test-service')]

        async def __aenter__(self):
            if outcome == 'failed':
                raise RuntimeError('pair rejected')
            return self

        async def __aexit__(self, *args):
            return False

    monkeypatch.setattr(probe, 'BleakClient', Client)
    asyncio.run(probe.main())
    assert scanner.call_args.kwargs['timeout'] == 15
    assert [event for event, _ in events] == {
        'missing': ['scan_start', 'not_found'],
        'connected': ['scan_start', 'found', 'connected', 'connection_after_5_seconds'],
        'failed': ['scan_start', 'found', 'failed'],
        'scan_failed': ['scan_start', 'scan_failed'],
    }[outcome]
