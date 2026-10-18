from __future__ import annotations

from typing import Any

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import RFBridgeHub, diagnostics_signal
from .const import DOMAIN


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([RFBridgeConnectedBinarySensor(hub)])


class RFBridgeConnectedBinarySensor(BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY
    _attr_entity_category = EntityCategory.DIAGNOSTIC
    _attr_has_entity_name = True
    _attr_name = "Connected"
    _attr_should_poll = False

    def __init__(self, hub: RFBridgeHub) -> None:
        self._hub = hub
        self._attr_unique_id = f"{hub.entry.entry_id}_connected"

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(
            async_dispatcher_connect(
                self.hass,
                diagnostics_signal(self._hub.entry.entry_id),
                self.async_write_ha_state,
            )
        )

    @property
    def device_info(self) -> dict[str, Any]:
        return self._hub.device_info

    @property
    def is_on(self) -> bool:
        return self._hub.available
