from __future__ import annotations

from dataclasses import dataclass
import logging
from typing import Any

import voluptuous as vol

from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall, SupportsResponse
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv

from .const import (
    CONF_COMMAND,
    CONF_COMMAND_KEY,
    CONF_COMMANDS,
    CONF_TOPIC_IN,
    CONF_TOPIC_LWT,
    CONF_TOPIC_OUT,
    DEFAULT_TOPIC_LWT,
    DEFAULT_TOPIC_OUT,
    DOMAIN,
    PLATFORMS,
    SERVICE_SEND_NAMED,
    SERVICE_SEND_RAW,
)

_LOGGER = logging.getLogger(__name__)

CONFIG_SCHEMA = vol.Schema(
    {
        DOMAIN: vol.Schema(
            {
                vol.Required(CONF_TOPIC_IN): cv.string,
                vol.Optional(CONF_TOPIC_OUT, default=DEFAULT_TOPIC_OUT): cv.string,
                vol.Optional(CONF_TOPIC_LWT, default=DEFAULT_TOPIC_LWT): cv.string,
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


class RFBridgeToMqttHub:
    """MQTT command publisher for one RFLink32 bridge."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.hass = hass
        self.entry = entry
        self.topic_in: str = entry.options.get(
            CONF_TOPIC_IN, entry.data.get(CONF_TOPIC_IN)
        )
        self.topic_out: str = entry.options.get(
            CONF_TOPIC_OUT, entry.data.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT)
        )
        self.topic_lwt: str = entry.options.get(
            CONF_TOPIC_LWT, entry.data.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT)
        )
        self.commands: dict[str, RFLink32Command] = parse_commands(
            entry.options.get(CONF_COMMANDS, {})
        )
        self.last_command_key: str | None = None
        self.last_command: str | None = None
        self.last_error: str | None = None

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

        mqtt.async_publish(self.hass, self.topic_in, command, qos=0, retain=False)

        _LOGGER.debug("Published RFLink32 command key=%s bytes=%s", command_key, len(command))
        return {
            "entry_id": self.entry.entry_id,
            "topic": self.topic_in,
            "command_key": command_key,
            "bytes": len(command),
            "published": True,
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
    hub = RFBridgeToMqttHub(hass, entry)
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = hub

    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    _async_register_services(hass)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unload_ok:
        hass.data[DOMAIN].pop(entry.entry_id, None)
        if not hass.data[DOMAIN]:
            hass.services.async_remove(DOMAIN, SERVICE_SEND_NAMED)
            hass.services.async_remove(DOMAIN, SERVICE_SEND_RAW)
    return unload_ok


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)


def _get_hub(
    hass: HomeAssistant, entry_id: str | None = None
) -> RFBridgeToMqttHub:
    hubs: dict[str, RFBridgeToMqttHub] = hass.data.get(DOMAIN, {})
    if entry_id:
        hub = hubs.get(entry_id)
        if hub is None:
            raise ServiceValidationError(f"Unknown RFLink32 entry_id: {entry_id}")
        return hub
    if len(hubs) == 1:
        return next(iter(hubs.values()))
    raise ServiceValidationError("entry_id is required when multiple RFLink32 hubs exist")


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
