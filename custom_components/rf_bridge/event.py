from __future__ import annotations

from datetime import timedelta
from typing import Any

from homeassistant.components.event import EventDeviceClass, EventEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import RFBridgeHub
from .const import DOMAIN

SCAN_INTERVAL = timedelta(seconds=2)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([RFBridgeRfEventEntity(hub)])


def _parse_rflink_message(message: str) -> dict[str, Any]:
    parts = [part for part in message.strip().split(";") if part]
    data: dict[str, Any] = {"message": message.strip()}
    if parts:
        data["raw_parts"] = parts
    if len(parts) >= 2:
        data["protocol"] = parts[1]
    for part in parts[2:]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        normalized_key = key.strip().lower()
        if normalized_key:
            data[normalized_key] = value.strip()
    if "cmd" in data:
        data["command"] = data["cmd"]
    return data


class RFBridgeRfEventEntity(EventEntity):
    _attr_device_class = EventDeviceClass.BUTTON
    _attr_event_types = ["received"]
    _attr_has_entity_name = True
    _attr_icon = "mdi:radio-tower"
    _attr_name = "RF event"

    def __init__(self, hub: RFBridgeHub) -> None:
        self._hub = hub
        self._attr_unique_id = f"{hub.entry.entry_id}_rf_event"

    @property
    def device_info(self) -> dict[str, Any]:
        return self._hub.device_info

    async def async_added_to_hass(self) -> None:
        await self._hub.async_get_status()

    async def async_update(self) -> None:
        for event in await self._hub.async_get_rf_events():
            message = event["message"]
            event_data = _parse_rflink_message(message)
            event_data["sequence"] = event.get("sequence")
            event_data["device_millis"] = event.get("millis")
            for key in ("raw", "pulse_count", "rssi", "truncated", "command"):
                if key in event:
                    event_data[key] = event[key]
            self._trigger_event("received", event_data)
            self.async_write_ha_state()
