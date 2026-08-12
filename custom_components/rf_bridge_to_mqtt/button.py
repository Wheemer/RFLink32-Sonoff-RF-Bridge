from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import RFBridgeToMqttHub, RFLink32Command
from .const import CONF_IDENTIFIERS, CONF_NAME, CONF_SW_VERSION, DOMAIN


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeToMqttHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        RFLink32CommandButton(hub, command) for command in hub.commands.values()
    )


class RFLink32CommandButton(ButtonEntity):
    _attr_has_entity_name = True

    def __init__(self, hub: RFBridgeToMqttHub, command: RFLink32Command) -> None:
        self._hub = hub
        self._command = command
        self._attr_name = command.name
        self._attr_icon = command.icon
        self._attr_unique_id = f"{hub.entry.entry_id}_{command.key}"
        identifiers = self._hub.entry.data.get(CONF_IDENTIFIERS) or [
            self._hub.entry.entry_id
        ]
        self._attr_device_info = {
            "identifiers": {(DOMAIN, identifier) for identifier in identifiers},
            "name": hub.entry.data.get(CONF_NAME, hub.entry.title),
            "manufacturer": "RFLink32",
            "model": "RF Bridge to MQTT",
            "sw_version": hub.entry.data.get(CONF_SW_VERSION),
        }

    async def async_press(self) -> None:
        await self._hub.async_send_named(self._command.key)
