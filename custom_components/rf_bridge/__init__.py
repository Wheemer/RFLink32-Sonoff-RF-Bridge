from __future__ import annotations

from dataclasses import dataclass
import logging
from typing import Any

import aiohttp
import voluptuous as vol

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall, SupportsResponse
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.dispatcher import async_dispatcher_send

from .const import (
    CONF_COMMAND,
    CONF_COMMAND_KEY,
    CONF_COMMANDS,
    CONF_API_PORT,
    CONF_HW_VERSION,
    CONF_IDENTIFIERS,
    CONF_HOST,
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


def diagnostics_signal(entry_id: str) -> str:
    return f"{DOMAIN}_{entry_id}_diagnostics_updated"

CONFIG_SCHEMA = vol.Schema(
    {
        DOMAIN: vol.Schema(
            {
                vol.Required(CONF_HOST): cv.string,
                vol.Optional(CONF_API_PORT, default=DEFAULT_API_PORT): cv.port,
                vol.Optional(CONF_COMMANDS, default={}): dict,
            }
        )
    },
    extra=vol.ALLOW_EXTRA,
)


@dataclass(slots=True)
class RFLink32Command:
    key: str
    name: str
    command: str
    icon: str | None = None


class RFBridgeHub:
    """Direct HTTP command client for one RFLink32 bridge."""

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
        self.rf_event_cursor: int = 0
        self.last_rf_event: dict[str, Any] | None = None

    def async_write_diagnostics(self) -> None:
        async_dispatcher_send(self.hass, diagnostics_signal(self.entry.entry_id))

    @property
    def base_url(self) -> str:
        return f"http://{self.host}:{self.api_port}"

    @property
    def device_info(self) -> dict[str, Any]:
        identifiers = self.entry.data.get(CONF_IDENTIFIERS) or [self.entry.entry_id]
        return {
            "identifiers": {(DOMAIN, identifier) for identifier in identifiers},
            "name": self.entry.data.get(CONF_NAME, self.entry.title),
            "manufacturer": "Sonoff",
            "model": self.entry.data.get(CONF_MODEL, "RF Bridge"),
            "sw_version": self.entry.data.get(CONF_SW_VERSION),
            "hw_version": self.entry.data.get(CONF_HW_VERSION),
        }

    async def async_get_status(self) -> dict[str, Any]:
        session = async_get_clientsession(self.hass)
        async with session.get(
            f"{self.base_url}/api/status",
            timeout=aiohttp.ClientTimeout(total=10),
        ) as response:
            response.raise_for_status()
            data = await response.json(content_type=None)
        if isinstance(data, dict):
            self.status = data
            return data
        self.status = {}
        return {}

    async def async_get_rf_events(self) -> list[dict[str, Any]]:
        session = async_get_clientsession(self.hass)
        async with session.get(
            f"{self.base_url}/api/events",
            params={"since": self.rf_event_cursor, "limit": 8},
            timeout=aiohttp.ClientTimeout(total=10),
        ) as response:
            response.raise_for_status()
            data = await response.json(content_type=None)
        if not isinstance(data, dict):
            return []

        cursor = data.get("cursor")
        if isinstance(cursor, int):
            self.rf_event_cursor = cursor

        events = data.get("events")
        if not isinstance(events, list):
            return []

        valid_events: list[dict[str, Any]] = []
        for event in events:
            if isinstance(event, dict) and isinstance(event.get("message"), str):
                valid_events.append(event)
        if valid_events:
            self.last_rf_event = valid_events[-1]
            self.async_write_diagnostics()
        return valid_events

    async def async_arm_learning(self) -> None:
        status = await self.async_get_status()
        cursor = status.get("rf_event_cursor")
        if isinstance(cursor, int):
            self.rf_event_cursor = cursor
        self.last_rf_event = None
        self.last_command_key = "learn_next_rf_event"
        self.last_command = None
        self.last_error = None
        self.last_success = True
        self.async_write_diagnostics()

    async def async_reboot(self) -> None:
        session = async_get_clientsession(self.hass)
        try:
            async with session.get(
                f"{self.base_url}/api/reboot",
                timeout=aiohttp.ClientTimeout(total=10),
            ) as response:
                text = await response.text()
                if response.status >= 400:
                    self.last_success = False
                    self.last_error = (
                        f"HTTP {response.status} {response.reason}: {text}"
                    )
                    self.async_write_diagnostics()
                    raise ServiceValidationError(
                        f"RF Bridge reboot failed for {self.host}: "
                        f"{self.last_error}"
                    )
        except (aiohttp.ClientError, TimeoutError, OSError) as err:
            self.last_success = False
            self.last_error = str(err)
            self.async_write_diagnostics()
            raise ServiceValidationError(
                f"RF Bridge reboot failed for {self.host}: {err}"
            ) from err
        self.last_command_key = "reboot"
        self.last_command = None
        self.last_error = None
        self.last_success = True
        self.async_write_diagnostics()

    async def async_send_named(self, command_key: str) -> dict[str, Any]:
        if command_key not in self.commands:
            raise ServiceValidationError(
                translation_domain=DOMAIN,
                translation_key="unknown_command",
                translation_placeholders={"command_key": command_key},
            )
        command = self.commands[command_key]
        return await self.async_send_raw(command.command, command_key=command.key)

    async def async_send_raw(
        self, command: str, *, command_key: str | None = None
    ) -> dict[str, Any]:
        command = normalize_command(command)
        self.last_command_key = command_key
        self.last_command = command
        self.last_error = None
        self.last_success = None
        self.async_write_diagnostics()

        session = async_get_clientsession(self.hass)
        try:
            async with session.post(
                f"{self.base_url}/api/command",
                json={CONF_COMMAND: command},
                timeout=aiohttp.ClientTimeout(total=15),
            ) as response:
                text = await response.text()
                if response.status >= 400:
                    self.last_success = False
                    self.last_error = (
                        f"HTTP {response.status} {response.reason}: {text}"
                    )
                    self.async_write_diagnostics()
                    raise ServiceValidationError(
                        f"RF Bridge command failed for {self.host}: "
                        f"{self.last_error}"
                    )
        except (aiohttp.ClientError, TimeoutError, OSError) as err:
            self.last_success = False
            self.last_error = str(err)
            self.async_write_diagnostics()
            raise ServiceValidationError(
                f"RF Bridge command failed for {self.host}: {err}"
            ) from err

        self.last_success = True
        self.async_write_diagnostics()
        _LOGGER.debug(
            "Sent RFLink32 command key=%s bytes=%s response=%s",
            command_key,
            len(command),
            text,
        )
        return {
            "entry_id": self.entry.entry_id,
            "host": self.host,
            "command_key": command_key,
            "bytes": len(command),
            "sent": True,
        }


def normalize_command(command: str) -> str:
    command = command.strip()
    if not command:
        raise ServiceValidationError("RFLink32 command cannot be empty")
    if "\n" in command or "\r" in command:
        raise ServiceValidationError("RFLink32 command must be a single line")
    return command


def parse_commands(raw: Any) -> dict[str, RFLink32Command]:
    if not isinstance(raw, dict):
        return {}
    commands: dict[str, RFLink32Command] = {}
    for key, value in raw.items():
        if not isinstance(key, str) or not key:
            continue
        if isinstance(value, str):
            command = value
            name = key.replace("_", " ").title()
            icon = None
        elif isinstance(value, dict):
            command = value.get(CONF_COMMAND)
            name = value.get("name") or key.replace("_", " ").title()
            icon = value.get("icon")
        else:
            continue
        if not isinstance(command, str) or not command.strip():
            continue
        commands[key] = RFLink32Command(
            key=key,
            name=str(name),
            command=normalize_command(command),
            icon=str(icon) if icon else None,
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
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unload_ok:
        hass.data[DOMAIN].pop(entry.entry_id, None)
    return unload_ok


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
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

    async def async_send_raw(call: ServiceCall) -> dict[str, Any]:
        hub = _get_hub(hass, call.data.get("entry_id"))
        return await hub.async_send_raw(call.data[CONF_COMMAND])

    async def async_send_named(call: ServiceCall) -> dict[str, Any]:
        hub = _get_hub(hass, call.data.get("entry_id"))
        return await hub.async_send_named(call.data[CONF_COMMAND_KEY])

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
