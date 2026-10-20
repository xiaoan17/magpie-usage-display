import asyncio
import unittest
from unittest.mock import AsyncMock, patch

from companion.codex_display.ble import BleCompanion


class FakeBleClient:
    def __init__(self, expected_writes):
        self.is_connected = True
        self.expected_writes = expected_writes
        self.writes = []

    async def write_gatt_char(self, uuid, payload, response):
        self.writes.append((uuid, payload, response))
        if len(self.writes) >= self.expected_writes:
            self.is_connected = False


class HangingBleClient:
    is_connected = True

    async def write_gatt_char(self, uuid, payload, response):
        await asyncio.Event().wait()

    async def disconnect(self):
        await asyncio.Event().wait()


class BleTests(unittest.IsolatedAsyncioTestCase):
    async def test_pending_pairing_keeps_connection_until_authenticated(self):
        companion = BleCompanion(AsyncMock(return_value=b"{}"), AsyncMock())
        client = FakeBleClient(expected_writes=1)
        client.start_notify = AsyncMock(side_effect=[
            RuntimeError("Authentication is insufficient."), None
        ])
        with patch("companion.codex_display.ble.asyncio.sleep", new=AsyncMock()):
            await companion._run_connection(client, asyncio.Event())
        self.assertEqual(client.start_notify.await_count, 2)
        self.assertEqual(len(client.writes), 1)

    async def test_unrelated_notification_failure_is_not_retried(self):
        companion = BleCompanion(AsyncMock(), AsyncMock())
        client = FakeBleClient(expected_writes=1)
        client.start_notify = AsyncMock(side_effect=RuntimeError("Disconnected"))
        with self.assertRaisesRegex(RuntimeError, "Disconnected"):
            await companion._run_connection(client, asyncio.Event())
        self.assertEqual(client.start_notify.await_count, 1)
        self.assertEqual(client.writes, [])

    async def test_pairing_wait_has_a_deadline(self):
        companion = BleCompanion(AsyncMock(), AsyncMock())
        client = FakeBleClient(expected_writes=1)
        client.start_notify = AsyncMock(
            side_effect=RuntimeError("Encryption is insufficient.")
        )
        with patch("companion.codex_display.ble.BLE_PAIRING_TIMEOUT_SECONDS", 0):
            with self.assertRaisesRegex(RuntimeError, "Encryption"):
                await companion._run_connection(client, asyncio.Event())
        self.assertEqual(client.start_notify.await_count, 1)
        self.assertEqual(client.writes, [])

    async def test_connection_refreshes_before_first_status(self):
        order = []

        async def status():
            order.append("status")
            return b"{}"

        async def refresh():
            order.append("refresh")

        companion = BleCompanion(status, refresh)
        client = FakeBleClient(expected_writes=1)
        client.start_notify = AsyncMock()
        disconnected = asyncio.Event()
        disconnected.set()

        await companion._run_connection(client, disconnected)

        self.assertEqual(order, ["refresh", "status"])
        self.assertFalse(companion.connected_event.is_set())

    async def test_status_change_sends_without_waiting_for_heartbeat(self):
        async def status():
            return b'{"a":1}'

        async def refresh():
            return None

        changed = asyncio.Event()
        companion = BleCompanion(
            status,
            refresh,
            heartbeat_seconds=999,
            status_changed=changed,
        )
        client = FakeBleClient(expected_writes=1)
        changed.set()

        await companion._heartbeat_loop(client)

        self.assertEqual(client.writes[0][1], b'{"a":1}')

    async def test_duplicate_request_is_not_executed_twice(self):
        async def status():
            return b"{}"

        async def refresh():
            return None

        companion = BleCompanion(status, refresh)
        command = b'{"v":4,"sid":55,"id":7,"a":"focus_magpie"}'
        companion._commands.put_nowait(command)
        companion._commands.put_nowait(command)
        client = FakeBleClient(expected_writes=3)

        action = AsyncMock(return_value=(True, "FOCUSED"))
        with patch("companion.codex_display.ble.perform_action", action):
            await companion._command_loop(client)

        action.assert_awaited_once()
        self.assertEqual(len(client.writes), 3)
        self.assertEqual(client.writes[0][1], client.writes[2][1])

    async def test_status_write_times_out(self):
        async def status():
            return b"{}"

        async def refresh():
            return None

        companion = BleCompanion(status, refresh)
        client = HangingBleClient()

        with patch(
            "companion.codex_display.ble.BLE_OPERATION_TIMEOUT_SECONDS",
            0.01,
        ):
            with self.assertRaises(asyncio.TimeoutError):
                await companion._send_status(client)

    async def test_disconnect_timeout_does_not_block_reconnect(self):
        async def status():
            return b"{}"

        async def refresh():
            return None

        companion = BleCompanion(status, refresh)
        client = HangingBleClient()

        with patch(
            "companion.codex_display.ble.BLE_DISCONNECT_TIMEOUT_SECONDS",
            0.01,
        ):
            await companion._disconnect(client)


if __name__ == "__main__":
    unittest.main()
