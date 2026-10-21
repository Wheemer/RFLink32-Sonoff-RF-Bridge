from __future__ import annotations

import asyncio
import json
import logging
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime
from typing import Any

import aiohttp
import voluptuous as vol
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall, SupportsResponse, callback
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.event import async_call_later
from homeassistant.util import dt as dt_util

from .const import (
    CONF_API_PORT,
    CONF_COMMAND,
    CONF_COMMAND_KEY,
    CONF_COMMANDS,
    CONF_HOST,
    CONF_HW_VERSION,
    CONF_IDENTIFIERS,
    CONF_KNOWN_COMMANDS,
    CONF_MODEL,
    CONF_NAME,
    CONF_SW_VERSION,
    DEFAULT_API_PORT,
    DOMAIN,
    PLATFORMS,
    SERVICE_SEND_NAMED,
    SERVICE_SEND_RAW,
)

_LOGGER = logging.getLogger(__name__)

WS_CONNECT_TIMEOUT = aiohttp.ClientTimeout(total=5)
WS_COMMAND_TIMEOUT = 130
WS_ACTION_TIMEOUT = 30
WS_RECONNECT_SECONDS = 5
WS_HEARTBEAT_SECONDS = 15
WS_CONNECTION_WAIT_SECONDS = 8
FIRMWARE_UPDATE_CONNECTION_WAIT_SECONDS = 45
FIRMWARE_UPDATE_QUIET_SECONDS = 75
WS_MIN_COMMAND_TIMEOUT = 8
WS_COMMAND_TIMEOUT_MARGIN = 10
SENDRF_PREFIX = "10;signal;sendRF;"
RAW_BUFFER_SIZE = 512
INPUT_COMMAND_SIZE = 2000
# The bridge parses each complete WebSocket envelope into its 2 KB command
# buffer. Keep preflight validation at that exact transport boundary.
WS_INBOUND_SIZE = INPUT_COMMAND_SIZE
MAX_TX_MS = 120_000
DEFAULT_SEQUENCE_DELAY = 0.6
MAX_SEQUENCE_DELAY = 10.0


def diagnostics_signal(entry_id: str) -> str:
    return f"{DOMAIN}_{entry_id}_diagnostics_updated"


def rf_event_signal(entry_id: str) -> str:
    return f"{DOMAIN}_{entry_id}_rf_event"


CONFIG_SCHEMA = vol.Schema(
    {
        DOMAIN: vol.Schema(
            {
                vol.Required(CONF_HOST): cv.string,
                vol.Optional(CONF_API_PORT, default=DEFAULT_API_PORT): cv.port,
                vol.Optional(CONF_COMMANDS, default={}): dict,
                vol.Optional(CONF_KNOWN_COMMANDS, default={}): dict,
            }
        )
    },
    extra=vol.ALLOW_EXTRA,
)


@dataclass(slots=True)
class RFLink32Command:
    key: str
    name: str
    commands: tuple[str, ...]
    icon: str | None = None
    sequence_delay: float = DEFAULT_SEQUENCE_DELAY

    @property
    def command(self) -> str:
        return self.commands[0]


def parse_rflink_message(message: str) -> dict[str, Any]:
    """Parse a decoded RFLink event into normalized attributes."""
    parts = [part for part in message.strip().split(";") if part]
    data: dict[str, Any] = {"message": message.strip()}
    if parts:
        data["raw_parts"] = parts

    protocol_index = 1
    if (
        len(parts) >= 3
        and parts[0] == "20"
        and len(parts[1]) == 2
        and all(character in "0123456789abcdefABCDEF" for character in parts[1])
    ):
        protocol_index = 2

    if len(parts) > protocol_index:
        data["protocol"] = parts[protocol_index]

    for part in parts[protocol_index + 1 :]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        normalized_key = key.strip().lower()
        if normalized_key:
            data[normalized_key] = value.strip()

    if "cmd" in data:
        data["command"] = data["cmd"]

    protocol = data.get("protocol")
    device_id = data.get("id")
    switch = data.get("switch")
    command = data.get("command")
    if isinstance(protocol, str) and isinstance(device_id, str):
        key_parts = [protocol.lower(), device_id.lower()]
        if isinstance(switch, str):
            key_parts.append(switch.lower())
        base_key = "_".join(key_parts)
        data["command_key"] = base_key
        if isinstance(command, str):
            data["command_key_exact"] = f"{base_key}_{command.lower()}"

    return data


def annotate_known_command(
    event_data: dict[str, Any], known_commands: dict[str, Any]
) -> None:
    """Attach configured known-command metadata to parsed RF event data."""
    command_keys = [
        key
        for key in (
            event_data.get("command_key_exact"),
            event_data.get("command_key"),
        )
        if isinstance(key, str)
    ]
    if not command_keys:
        return
    known_key = None
    known = None
    for command_key in command_keys:
        if command_key in known_commands:
            known_key = command_key
            known = known_commands[command_key]
            break
    if known is None:
        event_data["known"] = False
        return
    event_data["known"] = True
    event_data["matched_command_key"] = known_key
    if isinstance(known, str):
        event_data["name"] = known
        return
    if isinstance(known, dict):
        name = known.get("name")
        icon = known.get("icon")
        if isinstance(name, str):
            event_data["name"] = name
        if isinstance(icon, str):
            event_data["icon"] = icon


async def async_fetch_bridge_status(
    session: aiohttp.ClientSession, host: str, api_port: int
) -> dict[str, Any]:
    """Connect once over WebSocket and read the hello/status payload."""
    ws_url = f"ws://{host}:{api_port}/api/ws"
    async with session.ws_connect(ws_url, timeout=WS_CONNECT_TIMEOUT) as ws:
        msg = await ws.receive(timeout=WS_CONNECT_TIMEOUT.total)
        if msg.type != aiohttp.WSMsgType.TEXT:
            raise aiohttp.ClientError(f"Unexpected WebSocket message type: {msg.type}")
        payload = json.loads(msg.data)
        if payload.get("type") != "hello":
            raise aiohttp.ClientError("Bridge did not send hello")
        status = payload.get("status")
        if not isinstance(status, dict):
            status = {}
        sw_version = status.get(CONF_SW_VERSION) or status.get("build_id")
        if isinstance(sw_version, str):
            status[CONF_SW_VERSION] = sw_version
        return status


class RFBridgeHub:
    """WebSocket client for one RFLink32 bridge."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.hass = hass
        self.entry = entry
        self.host: str = entry.data[CONF_HOST]
        self.api_port: int = entry.data.get(CONF_API_PORT, DEFAULT_API_PORT)
        self.commands: dict[str, RFLink32Command] = parse_commands(
            entry.options.get(CONF_COMMANDS, {})
        )
        self.status: dict[str, Any] = {}
        self.last_command_key: str | None = None
        self.last_command: str | None = None
        self.last_error: str | None = None
        self.last_success: bool | None = None
        self.last_elapsed_seconds: float | None = None
        self.last_runtime: dict[str, Any] = {}
        self.last_command_started_at: str | None = None
        self.last_command_finished_at: str | None = None
        self.last_ws_connected_at: str | None = None
        self.last_ws_disconnected_at: str | None = None
        self.ws_connect_count = 0
        self.ws_disconnect_count = 0
        self.last_status_at: str | None = None
        self.rf_event_cursor: int = 0
        self.last_rf_event: dict[str, Any] | None = None
        self.available: bool = False
        self.ws_connected: bool = False
        self._ws: aiohttp.ClientWebSocketResponse | None = None
        self._ws_task: asyncio.Task | None = None
        self._ws_ready = asyncio.Event()
        self._ws_lock = asyncio.Lock()
        self._tx_lock = asyncio.Lock()
        self._pending: dict[int, asyncio.Future[dict[str, Any]]] = {}
        self._request_id = 0
        self._ota_quiet_until = 0.0
        self._ota_quiet_event = asyncio.Event()
        self._ota_quiet_event.set()
        self._learning_expiry_cancel: Callable[[], None] | None = None

    @property
    def ws_url(self) -> str:
        return f"ws://{self.host}:{self.api_port}/api/ws"

    def async_write_diagnostics(self) -> None:
        async_dispatcher_send(self.hass, diagnostics_signal(self.entry.entry_id))

    @property
    def device_info(self) -> dict[str, Any]:
        identifiers = self.entry.data.get(CONF_IDENTIFIERS) or [self.entry.entry_id]
        return {
            "identifiers": {(DOMAIN, identifier) for identifier in identifiers},
            "name": self.entry.data.get(CONF_NAME, self.entry.title),
            "manufacturer": "Sonoff",
            "model": self.entry.data.get(CONF_MODEL, "RF Bridge"),
            "sw_version": self.status.get(CONF_SW_VERSION)
            or self.entry.data.get(CONF_SW_VERSION),
            "hw_version": self.status.get("hardware")
            or self.entry.data.get(CONF_HW_VERSION),
        }

    def _mark_unreachable(self, err: str) -> None:
        self.available = False
        if self.ws_connected:
            self.ws_disconnect_count += 1
            self.last_ws_disconnected_at = dt_util.utcnow().isoformat()
        self.ws_connected = False
        self._ws_ready.clear()
        if self.last_command is not None and self.last_success is None:
            self.last_success = False
        self.last_error = err
        self.async_write_diagnostics()

    def _mark_reachable(self) -> None:
        changed = not self.available or not self.ws_connected
        self.available = True
        self.ws_connected = True
        self._ws_ready.set()
        self.last_error = None
        if changed:
            self.ws_connect_count += 1
            self.last_ws_connected_at = dt_util.utcnow().isoformat()
            self.async_write_diagnostics()

    def _reject_pending(self, exc: Exception) -> None:
        for future in self._pending.values():
            if not future.done():
                future.set_exception(exc)
        self._pending.clear()

    async def async_start(self) -> None:
        """Start the reconnect loop without blocking Home Assistant startup."""
        if self._ws_task is None:
            self._ws_task = self.hass.async_create_background_task(
                self._ws_loop(),
                f"rf_bridge_{self.entry.entry_id}_ws",
            )

    async def async_stop(self) -> None:
        self._cancel_learning_expiry()
        if self._ws_task is not None:
            self._ws_task.cancel()
            try:
                await self._ws_task
            except asyncio.CancelledError:
                pass
            self._ws_task = None
        if self._ws is not None and not self._ws.closed:
            await self._ws.close()
        self._ws = None
        self.ws_connected = False
        self._ws_ready.clear()
        self._reject_pending(aiohttp.ClientError("WebSocket stopped"))

    async def _ws_loop(self) -> None:
        session = async_get_clientsession(self.hass)
        while True:
            quiet_seconds = self._ota_quiet_until - asyncio.get_running_loop().time()
            if quiet_seconds > 0:
                try:
                    await asyncio.wait_for(
                        self._ota_quiet_event.wait(), timeout=quiet_seconds
                    )
                except TimeoutError:
                    pass
                continue
            try:
                async with session.ws_connect(
                    self.ws_url,
                    timeout=WS_CONNECT_TIMEOUT,
                    heartbeat=WS_HEARTBEAT_SECONDS,
                ) as ws:
                    self._ws = ws
                    async for msg in ws:
                        if msg.type == aiohttp.WSMsgType.TEXT:
                            try:
                                payload = json.loads(msg.data)
                            except json.JSONDecodeError:
                                _LOGGER.debug("Ignored non-JSON WebSocket frame")
                                continue
                            await self._handle_ws_message(payload, persistent=True)
                        elif msg.type in (
                            aiohttp.WSMsgType.CLOSED,
                            aiohttp.WSMsgType.ERROR,
                        ):
                            break
            except asyncio.CancelledError:
                raise
            except Exception as err:  # noqa: BLE001 - reconnect loop
                self._mark_unreachable(str(err))
                self._reject_pending(
                    aiohttp.ClientError(f"WebSocket disconnected: {err}")
                )
            finally:
                self._reject_pending(aiohttp.ClientError("WebSocket disconnected"))
                self._ws = None
                if self.ws_connected:
                    self.ws_disconnect_count += 1
                    self.last_ws_disconnected_at = dt_util.utcnow().isoformat()
                self.ws_connected = False
                self._ws_ready.clear()
                self.async_write_diagnostics()
            await asyncio.sleep(WS_RECONNECT_SECONDS)

    async def _handle_ws_message(
        self, payload: dict[str, Any], *, persistent: bool
    ) -> None:
        msg_type = payload.get("type")
        if msg_type == "hello":
            status = payload.get("status")
            if isinstance(status, dict):
                self.status = status
                self.last_status_at = dt_util.utcnow().isoformat()
                live_version = status.get(CONF_SW_VERSION)
                if isinstance(
                    live_version, str
                ) and live_version != self.entry.data.get(CONF_SW_VERSION):
                    device_registry = dr.async_get(self.hass)
                    identifiers = self.entry.data.get(CONF_IDENTIFIERS) or [
                        self.entry.entry_id
                    ]
                    for identifier in identifiers:
                        device = device_registry.async_get_device_by_identifier(
                            (DOMAIN, identifier)
                        )
                        if device is not None:
                            device_registry.async_update_device(
                                device.id, sw_version=live_version
                            )
            cursor = payload.get("cursor")
            if isinstance(cursor, int):
                self.rf_event_cursor = cursor
            if persistent:
                self._mark_reachable()
            else:
                self.available = True
                self.last_error = None
            self.async_write_diagnostics()
            return

        if msg_type == "rf_event":
            event = payload.get("event")
            if isinstance(event, dict):
                self._handle_rf_event(event)
            return

        request_id = payload.get("id")
        if isinstance(request_id, int):
            future = self._pending.pop(request_id, None)
            if future is not None and not future.done():
                future.set_result(payload)

    def _handle_rf_event(self, event: dict[str, Any]) -> None:
        sequence = event.get("sequence")
        if isinstance(sequence, int):
            self.rf_event_cursor = max(self.rf_event_cursor, sequence)
        self.last_rf_event = event
        if event.get("raw") is True and event.get("command"):
            # Raw events are only actionable when the user explicitly armed
            # learning. Preserve that state on the event before disarming so
            # the event platform cannot admit unsolicited raw traffic.
            event["learning"] = self.status.get("raw_learning_armed") is True
            self._cancel_learning_expiry()
            self.status["raw_learning_armed"] = False
        self.async_write_diagnostics()
        async_dispatcher_send(self.hass, rf_event_signal(self.entry.entry_id), event)

    def _cancel_learning_expiry(self) -> None:
        if self._learning_expiry_cancel is not None:
            self._learning_expiry_cancel()
            self._learning_expiry_cancel = None

    @callback
    def _learning_expired(self, _now: datetime) -> None:
        self._learning_expiry_cancel = None
        self.status["raw_learning_armed"] = False
        self.async_write_diagnostics()

    async def _ws_request(
        self,
        payload: dict[str, Any],
        *,
        timeout: float,
        connection_timeout: float = WS_CONNECTION_WAIT_SECONDS,
    ) -> dict[str, Any]:
        try:
            await asyncio.wait_for(self._ws_ready.wait(), timeout=connection_timeout)
        except TimeoutError as err:
            raise ServiceValidationError(
                f"RF Bridge WebSocket did not reconnect for {self.host}"
            ) from err

        request_id = (self._request_id + 1) & 0xFFFFFFFF
        if request_id == 0:
            request_id = 1
        self._request_id = request_id
        message = {**payload, "id": request_id}
        future: asyncio.Future[dict[str, Any]] = (
            asyncio.get_running_loop().create_future()
        )

        async with self._ws_lock:
            if self._ws is None or self._ws.closed or not self.ws_connected:
                _LOGGER.warning(
                    "RF Bridge WebSocket closed before request send: host=%s "
                    "request_id=%s type=%s",
                    self.host,
                    request_id,
                    payload.get("type"),
                )
                raise ServiceValidationError(
                    f"RF Bridge WebSocket is not connected for {self.host}"
                )
            self._pending[request_id] = future
            try:
                await self._ws.send_json(message)
            except Exception as err:
                self._pending.pop(request_id, None)
                self._mark_unreachable(str(err))
                self._reject_pending(
                    aiohttp.ClientError(f"WebSocket send failed: {err}")
                )
                _LOGGER.warning(
                    "RF Bridge WebSocket send failed: host=%s request_id=%s "
                    "type=%s error=%s",
                    self.host,
                    request_id,
                    payload.get("type"),
                    err,
                )
                raise ServiceValidationError(
                    f"RF Bridge WebSocket send failed for {self.host}: {err}"
                ) from err

        try:
            return await asyncio.wait_for(future, timeout=timeout)
        except TimeoutError as err:
            if self._ws is not None and not self._ws.closed:
                await self._ws.close()
            self._mark_unreachable(
                f"Timed out waiting for {payload.get('type')} response"
            )
            command = payload.get(CONF_COMMAND)
            _LOGGER.error(
                "RF Bridge timed out waiting for response: host=%s request_id=%s "
                "type=%s timeout=%ss command_key=%s command_bytes=%s "
                "ws_connected=%s uptime=%s reset_reason=%s runtime=%s",
                self.host,
                request_id,
                payload.get("type"),
                timeout,
                self.last_command_key,
                len(command) if isinstance(command, str) else None,
                self.ws_connected,
                self.status.get("uptime"),
                self.status.get("reset_reason"),
                self.last_runtime,
            )
            raise ServiceValidationError(
                f"RF Bridge timed out waiting for {payload.get('type')} on {self.host}"
            ) from err
        finally:
            self._pending.pop(request_id, None)

    async def async_get_status(self) -> dict[str, Any]:
        """Return cached status pushed by the WebSocket hello message."""
        return self.status

    async def async_refresh_status(self) -> dict[str, Any]:
        """Request the bridge's current runtime status over the live socket."""
        result = await self._ws_request({"type": "status"}, timeout=WS_ACTION_TIMEOUT)
        if result.get("type") != "status":
            message = result.get("message") or "Bridge returned an invalid status response"
            raise ServiceValidationError(f"RF Bridge status failed for {self.host}: {message}")
        status = result.get("status")
        if not isinstance(status, dict):
            raise ServiceValidationError(f"RF Bridge returned no status for {self.host}")
        self.status = status
        self.last_status_at = dt_util.utcnow().isoformat()
        self._mark_reachable()
        self.async_write_diagnostics()
        return status

    async def async_probe_status(self) -> dict[str, Any]:
        """Read status from a short-lived socket without using the live client."""
        status = await async_fetch_bridge_status(
            async_get_clientsession(self.hass), self.host, self.api_port
        )
        self.status = status
        self.last_status_at = dt_util.utcnow().isoformat()
        # This is deliberately not the persistent command socket. It proves
        # that the bridge is reachable, but commands must wait for _ws_loop
        # to establish its own connection.
        self.available = True
        self.last_error = None
        self.async_write_diagnostics()
        return status

    async def async_arm_learning(self) -> None:
        learn_seconds = 30
        # Clear the previous capture before asking the bridge to arm. A remote
        # can be pressed immediately after the firmware responds; clearing it
        # afterwards could erase that new event.
        self.last_rf_event = None
        result = await self._ws_request(
            {"type": "learn", "seconds": learn_seconds},
            timeout=WS_ACTION_TIMEOUT,
        )
        if result.get("type") == "error":
            message = result.get("message") or "Learn failed"
            self.last_success = False
            self.last_error = str(message)
            self.async_write_diagnostics()
            raise ServiceValidationError(
                f"RF Bridge learn failed for {self.host}: {message}"
            )
        cursor = result.get("cursor")
        if isinstance(cursor, int):
            self.rf_event_cursor = cursor
        self.last_command_key = "learn_next_rf_event"
        self.last_command = None
        self.last_error = None
        self.last_success = True
        runtime = result.get("runtime")
        if isinstance(runtime, dict):
            self.last_runtime = runtime
            self.status.update(runtime)
            self.last_status_at = dt_util.utcnow().isoformat()
        self.status["raw_learning_armed"] = True
        self._cancel_learning_expiry()
        self._learning_expiry_cancel = async_call_later(
            self.hass, learn_seconds, self._learning_expired
        )
        self.async_write_diagnostics()

    async def async_reboot(self) -> None:
        result = await self._ws_request(
            {"type": "reboot"},
            timeout=WS_ACTION_TIMEOUT,
        )
        if result.get("type") == "error":
            message = result.get("message") or "Reboot failed"
            self.last_success = False
            self.last_error = str(message)
            self.async_write_diagnostics()
            raise ServiceValidationError(
                f"RF Bridge reboot failed for {self.host}: {message}"
            )
        self.last_command_key = "reboot"
        self.last_command = None
        self.last_error = None
        self.last_success = True
        runtime = result.get("runtime")
        if isinstance(runtime, dict):
            self.last_runtime = runtime
            self.status.update(runtime)
            self.last_status_at = dt_util.utcnow().isoformat()
        self.async_write_diagnostics()

    async def async_schedule_firmware_update(self, url: str) -> None:
        """Ask the bridge to fetch one signed firmware artifact."""
        result = await self._ws_request(
            {"type": "update", "url": url},
            timeout=WS_ACTION_TIMEOUT,
            connection_timeout=FIRMWARE_UPDATE_CONNECTION_WAIT_SECONDS,
        )
        if result.get("type") != "update_scheduled" or result.get("ok") is not True:
            message = result.get("message") or "Update request was rejected"
            self.last_error = str(message)
            self.async_write_diagnostics()
            raise ServiceValidationError(
                f"RF Bridge update failed for {self.host}: {message}"
            )
        self.last_command_key = "firmware_update"
        self.last_command = None
        self.last_error = None
        self.last_success = True
        self.async_write_diagnostics()

        # The firmware acknowledges first, then closes WebSocket allocations
        # before it starts writing flash. Do not reconnect or probe during that
        # small transfer window.
        self._ota_quiet_until = (
            asyncio.get_running_loop().time() + FIRMWARE_UPDATE_QUIET_SECONDS
        )
        self._ota_quiet_event.clear()
        if self._ws is not None and not self._ws.closed:
            await self._ws.close()

    async def async_resume_after_firmware_update(self) -> None:
        """Let the persistent socket reconnect after OTA verification ends."""
        self._ota_quiet_until = 0.0
        self._ota_quiet_event.set()

    async def async_send_named(self, command_key: str) -> dict[str, Any]:
        if command_key not in self.commands:
            raise ServiceValidationError(
                translation_domain=DOMAIN,
                translation_key="unknown_command",
                translation_placeholders={"command_key": command_key},
            )
        command = self.commands[command_key]
        if len(command.commands) == 1:
            return await self.async_send_raw(command.command, command_key=command.key)
        return await self.async_send_sequence(
            command.commands,
            command_key=command.key,
            sequence_delay=command.sequence_delay,
        )

    async def async_send_sequence(
        self,
        commands: tuple[str, ...],
        *,
        command_key: str | None = None,
        sequence_delay: float = DEFAULT_SEQUENCE_DELAY,
    ) -> dict[str, Any]:
        normalized_commands = tuple(normalize_command(command) for command in commands)
        try:
            async with self._tx_lock:
                results: list[dict[str, Any]] = []
                for index, command in enumerate(normalized_commands, start=1):
                    results.append(
                        await self._async_send_raw(
                            command, command_key=f"{command_key}:{index}"
                        )
                    )
                    if index != len(normalized_commands):
                        await asyncio.sleep(sequence_delay)
                self.last_command_key = command_key
                self.last_command = f"{len(normalized_commands)} RFLink32 commands"
                self.last_success = True
                self.async_write_diagnostics()
                response = {
                    "entry_id": self.entry.entry_id,
                    "host": self.host,
                    "command_key": command_key,
                    "commands": len(normalized_commands),
                    "sequence_delay": sequence_delay,
                    "elapsed_seconds": round(
                        sum(
                            result.get("elapsed_seconds") or 0
                            for result in results
                            if isinstance(result, dict)
                        ),
                        2,
                    ),
                    "sent": True,
                }
        except Exception as err:
            self._fire_command_outcome(
                command_key=command_key, success=False, error=str(err)
            )
            raise

        self._fire_command_outcome(
            command_key=command_key,
            success=True,
            elapsed_seconds=response["elapsed_seconds"],
        )
        return response

    async def async_send_raw(
        self, command: str, *, command_key: str | None = None
    ) -> dict[str, Any]:
        command = normalize_command(command)
        try:
            async with self._tx_lock:
                response = await self._async_send_raw(command, command_key=command_key)
        except Exception as err:
            self._fire_command_outcome(
                command_key=command_key, success=False, error=str(err)
            )
            raise

        self._fire_command_outcome(
            command_key=command_key,
            success=True,
            elapsed_seconds=response.get("elapsed_seconds"),
        )
        return response

    def _fire_command_outcome(
        self,
        *,
        command_key: str | None,
        success: bool,
        error: str | None = None,
        elapsed_seconds: float | None = None,
    ) -> None:
        """Publish a command result without exposing its raw RF payload."""
        data: dict[str, Any] = {
            "entry_id": self.entry.entry_id,
            "host": self.host,
            "command_key": command_key,
        }
        if success:
            if elapsed_seconds is not None:
                data["elapsed_seconds"] = elapsed_seconds
            event_type = "rf_bridge_command_succeeded"
        else:
            data["error"] = error or "Command failed"
            event_type = "rf_bridge_command_failed"
        self.hass.bus.async_fire(event_type, data)

    async def _async_send_raw(
        self, command: str, *, command_key: str | None = None
    ) -> dict[str, Any]:
        self.last_command_key = command_key
        self.last_command = command
        self.last_error = None
        self.last_success = None
        self.last_elapsed_seconds = None
        self.last_command_started_at = dt_util.utcnow().isoformat()
        self.last_command_finished_at = None
        self.async_write_diagnostics()

        try:
            result = await self._ws_request(
                {"type": "command", CONF_COMMAND: command},
                timeout=command_timeout(command),
            )
        except Exception as err:
            self.last_success = False
            self.last_error = str(err)
            self.async_write_diagnostics()
            _LOGGER.exception(
                "RF Bridge command send failed: host=%s command_key=%s "
                "command_bytes=%s ws_connected=%s uptime=%s reset_reason=%s",
                self.host,
                command_key,
                len(command),
                self.ws_connected,
                self.status.get("uptime"),
                self.status.get("reset_reason"),
            )
            raise
        finally:
            self.last_command_finished_at = dt_util.utcnow().isoformat()
        if result.get("type") == "error":
            message = result.get("message") or "Command failed"
            self.last_success = False
            self.last_error = str(message)
            self.async_write_diagnostics()
            _LOGGER.error(
                "RF Bridge command returned firmware error: host=%s command_key=%s "
                "command_bytes=%s message=%s runtime=%s",
                self.host,
                command_key,
                len(command),
                message,
                result.get("runtime"),
            )
            raise ServiceValidationError(
                f"RF Bridge command failed for {self.host}: {message}"
            )
        if result.get("type") != "command_result" or result.get("ok") is not True:
            message = result.get("message") or "Command rejected"
            self.last_success = False
            self.last_error = str(message)
            self.async_write_diagnostics()
            _LOGGER.error(
                "RF Bridge command was rejected or returned unexpected response: "
                "host=%s command_key=%s command_bytes=%s response=%s",
                self.host,
                command_key,
                len(command),
                result,
            )
            raise ServiceValidationError(
                f"RF Bridge command failed for {self.host}: {message}"
            )

        elapsed_ms = result.get("elapsed_ms")
        if isinstance(elapsed_ms, (int, float)):
            self.last_elapsed_seconds = round(float(elapsed_ms) / 1000.0, 2)
        runtime = result.get("runtime")
        if isinstance(runtime, dict):
            self.last_runtime = runtime
            self.status.update(runtime)
            self.last_status_at = dt_util.utcnow().isoformat()

        self.last_success = True
        self.async_write_diagnostics()
        _LOGGER.debug(
            "Sent RFLink32 command key=%s bytes=%s elapsed=%ss",
            command_key,
            len(command),
            self.last_elapsed_seconds,
        )
        return {
            "entry_id": self.entry.entry_id,
            "host": self.host,
            "command_key": command_key,
            "bytes": len(command),
            "elapsed_seconds": self.last_elapsed_seconds,
            "sent": True,
        }


def normalize_command(command: str) -> str:
    command = command.strip()
    if not command:
        raise ServiceValidationError("RFLink32 command cannot be empty")
    if "\n" in command or "\r" in command:
        raise ServiceValidationError("RFLink32 command must be a single line")
    try:
        validate_sendrf_command(command)
    except ValueError as err:
        raise ServiceValidationError(str(err)) from err
    return command


def command_timeout(command: str) -> float:
    """Pick a command timeout from the RF payload instead of always waiting forever."""
    estimated = estimate_sendrf_seconds(command)
    if estimated is None:
        return WS_ACTION_TIMEOUT
    return min(
        WS_COMMAND_TIMEOUT,
        max(WS_MIN_COMMAND_TIMEOUT, estimated + WS_COMMAND_TIMEOUT_MARGIN),
    )


def estimate_sendrf_seconds(command: str) -> float | None:
    if not command.startswith(SENDRF_PREFIX):
        return None
    try:
        payload = json.loads(command[len(SENDRF_PREFIX) :])
    except json.JSONDecodeError:
        return None
    pulses = payload.get("pulses")
    if not isinstance(pulses, list):
        return None
    estimated_us = 0
    for pulse in pulses:
        if isinstance(pulse, bool) or not isinstance(pulse, int) or pulse < 0:
            return None
        estimated_us += pulse
    repeat = payload.get("repeat", 0)
    delay = payload.get("delay", 0)
    if not isinstance(repeat, int) or not isinstance(delay, int):
        return None
    return ((estimated_us / 1000.0) * (repeat + 1) + (delay * repeat)) / 1000.0


def validate_sendrf_command(command: str) -> None:
    """Validate raw sendRF command shape before it reaches firmware."""
    if not command.startswith(SENDRF_PREFIX):
        return
    if len(command) >= INPUT_COMMAND_SIZE - 1:
        raise ValueError("sendRF command exceeds firmware command buffer")

    ws_envelope_len = len(
        json.dumps(
            {"type": "command", "id": 1, CONF_COMMAND: command}, separators=(",", ":")
        )
    )
    if ws_envelope_len >= WS_INBOUND_SIZE:
        raise ValueError("sendRF command exceeds firmware WebSocket input buffer")

    try:
        payload = json.loads(command[len(SENDRF_PREFIX) :])
    except json.JSONDecodeError as err:
        raise ValueError("sendRF command JSON is invalid") from err

    pulses = payload.get("pulses")
    if not isinstance(pulses, list):
        raise TypeError("sendRF command must include a pulses list")
    if len(pulses) < 2:
        raise ValueError("sendRF command must include at least two pulses")
    if len(pulses) > RAW_BUFFER_SIZE:
        raise ValueError(f"sendRF command has more than {RAW_BUFFER_SIZE} pulses")
    if len(pulses) % 2:
        raise ValueError(
            "sendRF command has an odd pulse count; RFLink32 would drop the final pulse"
        )

    longest_pulse = 0
    estimated_us = 0
    for pulse in pulses:
        if isinstance(pulse, bool) or not isinstance(pulse, int) or pulse < 0:
            raise ValueError("sendRF pulses must be non-negative integers")
        longest_pulse = max(longest_pulse, pulse)
        estimated_us += pulse

    multiply = 1
    while (longest_pulse + (multiply // 2)) // multiply > 65535:
        multiply += 1
    if multiply > 255:
        raise ValueError("sendRF pulse is too long for firmware scaling")

    repeat = payload.get("repeat", 0)
    delay = payload.get("delay", 0)
    if not isinstance(repeat, int) or repeat < 0 or repeat > 100:
        raise ValueError("sendRF repeat must be an integer from 0 to 100")
    if not isinstance(delay, int) or delay < 0 or delay > 10000:
        raise ValueError("sendRF delay must be an integer from 0 to 10000 ms")
    if "multiply" in payload:
        raise ValueError(
            "sendRF multiply is not accepted; firmware calculates scaling itself"
        )
    estimated_ms = ((estimated_us // 1000) * (repeat + 1)) + (delay * repeat)
    if estimated_ms > MAX_TX_MS:
        raise ValueError("sendRF estimated transmit time is too long")


def parse_commands(raw: Any) -> dict[str, RFLink32Command]:
    if not isinstance(raw, dict):
        return {}
    commands: dict[str, RFLink32Command] = {}
    for key, value in raw.items():
        if not isinstance(key, str) or not key:
            continue
        command_values: list[str] = []
        if isinstance(value, str):
            command_values = [value]
            name = key.replace("_", " ").title()
            icon = None
            sequence_delay = DEFAULT_SEQUENCE_DELAY
        elif isinstance(value, dict):
            command = value.get(CONF_COMMAND)
            if isinstance(command, str):
                command_values = [command]
            elif isinstance(command, list):
                command_values = [
                    item for item in command if isinstance(item, str) and item.strip()
                ]
            name = value.get("name") or key.replace("_", " ").title()
            icon = value.get("icon")
            sequence_delay = value.get("sequence_delay", DEFAULT_SEQUENCE_DELAY)
        else:
            continue
        if not command_values:
            continue
        if (
            isinstance(sequence_delay, bool)
            or not isinstance(sequence_delay, (int, float))
            or sequence_delay < 0
            or sequence_delay > MAX_SEQUENCE_DELAY
        ):
            raise ServiceValidationError(
                f"RFLink32 command {key} sequence_delay must be 0..{MAX_SEQUENCE_DELAY} seconds"
            )
        commands[key] = RFLink32Command(
            key=key,
            name=str(name),
            commands=tuple(normalize_command(command) for command in command_values),
            icon=str(icon) if icon else None,
            sequence_delay=float(sequence_delay),
        )
    return commands


async def async_setup(hass: HomeAssistant, config: dict[str, Any]) -> bool:
    _async_register_services(hass)
    if DOMAIN in config:
        hass.async_create_task(
            hass.config_entries.flow.async_init(
                DOMAIN,
                context={"source": "import"},
                data=config[DOMAIN],
            )
        )
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    _async_register_services(hass)
    hub = RFBridgeHub(hass, entry)
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = hub
    identifiers = entry.data.get(CONF_IDENTIFIERS) or [entry.entry_id]
    device_registry = dr.async_get(hass)
    device_registry.async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, identifier) for identifier in identifiers},
        name=entry.data.get(CONF_NAME, entry.title),
        manufacturer="Sonoff",
        model=entry.data.get(CONF_MODEL, "RF Bridge"),
        sw_version=entry.data.get(CONF_SW_VERSION),
        hw_version=entry.data.get(CONF_HW_VERSION),
    )

    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    await hub.async_start()
    # The socket may receive its hello frame before every forwarded platform
    # has attached its dispatcher listener. Publish once on the next loop turn
    # so availability-derived entities cannot remain stuck at their startup state.
    @callback
    def _async_publish_initial_diagnostics(_: datetime) -> None:
        hub.async_write_diagnostics()

    async_call_later(hass, 0, _async_publish_initial_diagnostics)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hub: RFBridgeHub | None = hass.data.get(DOMAIN, {}).get(entry.entry_id)
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unload_ok:
        if hub is not None:
            await hub.async_stop()
        hass.data.get(DOMAIN, {}).pop(entry.entry_id, None)
        _async_unregister_services_if_unused(hass)
    return unload_ok


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    # Command options create/remove button entities, so reload the entry until
    # the button platform grows dynamic entity management.
    await hass.config_entries.async_reload(entry.entry_id)


def _get_hub(hass: HomeAssistant, entry_id: str | None = None) -> RFBridgeHub:
    hubs: dict[str, RFBridgeHub] = hass.data.get(DOMAIN, {})
    if entry_id:
        hub = hubs.get(entry_id)
        if hub is None:
            raise ServiceValidationError(f"Unknown RF Bridge entry_id: {entry_id}")
        return hub
    if len(hubs) == 1:
        return next(iter(hubs.values()))
    if not hubs:
        raise ServiceValidationError("No RF Bridge config entry is loaded")
    raise ServiceValidationError(
        "entry_id is required when multiple RF Bridge hubs exist"
    )


def _async_register_services(hass: HomeAssistant) -> None:
    if hass.services.has_service(DOMAIN, SERVICE_SEND_RAW):
        return

    async def async_send_raw(call: ServiceCall) -> dict[str, Any] | None:
        hub = _get_hub(hass, call.data.get("entry_id"))
        response = await hub.async_send_raw(call.data[CONF_COMMAND])
        return response if call.return_response else None

    async def async_send_named(call: ServiceCall) -> dict[str, Any] | None:
        hub = _get_hub(hass, call.data.get("entry_id"))
        response = await hub.async_send_named(call.data[CONF_COMMAND_KEY])
        return response if call.return_response else None

    hass.services.async_register(
        DOMAIN,
        SERVICE_SEND_RAW,
        async_send_raw,
        schema=vol.Schema(
            {
                vol.Required(CONF_COMMAND): cv.string,
                vol.Optional("entry_id"): cv.string,
            }
        ),
        supports_response=SupportsResponse.OPTIONAL,
    )
    hass.services.async_register(
        DOMAIN,
        SERVICE_SEND_NAMED,
        async_send_named,
        schema=vol.Schema(
            {
                vol.Required(CONF_COMMAND_KEY): cv.string,
                vol.Optional("entry_id"): cv.string,
            }
        ),
        supports_response=SupportsResponse.OPTIONAL,
    )


def _async_unregister_services_if_unused(hass: HomeAssistant) -> None:
    if hass.data.get(DOMAIN):
        return
    if hass.services.has_service(DOMAIN, SERVICE_SEND_RAW):
        hass.services.async_remove(DOMAIN, SERVICE_SEND_RAW)
    if hass.services.has_service(DOMAIN, SERVICE_SEND_NAMED):
        hass.services.async_remove(DOMAIN, SERVICE_SEND_NAMED)
