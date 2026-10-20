from __future__ import annotations

from typing import Any, ClassVar

from homeassistant.components.event import EventDeviceClass, EventEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import (
    RFBridgeHub,
    annotate_known_command,
    diagnostics_signal,
    parse_rflink_message,
    rf_event_signal,
)
from .const import CONF_KNOWN_COMMANDS, DOMAIN


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([RFBridgeRfEventEntity(hub)])


class RFBridgeRfEventEntity(EventEntity):
    _attr_device_class = EventDeviceClass.BUTTON
    _attr_event_types: ClassVar[list[str]] = ["press"]
    _attr_has_entity_name = True
    _attr_should_poll = False
    _attr_icon = "mdi:radio-tower"
    _attr_name = "RF event"

    def __init__(self, hub: RFBridgeHub) -> None:
        self._hub = hub
        self._attr_unique_id = f"{hub.entry.entry_id}_rf_event"

    @property
    def device_info(self) -> dict[str, Any]:
        return self._hub.device_info

    @property
    def available(self) -> bool:
        return self._hub.available

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(
            async_dispatcher_connect(
                self.hass,
                rf_event_signal(self._hub.entry.entry_id),
                self._handle_rf_event,
            )
        )
        self.async_on_remove(
            async_dispatcher_connect(
                self.hass,
                diagnostics_signal(self._hub.entry.entry_id),
                self.async_write_ha_state,
            )
        )

    @callback
    def _handle_rf_event(self, event: dict[str, Any]) -> None:
        message = event.get("message")
        if not isinstance(message, str):
            return
        event_data = parse_rflink_message(message)
        known_commands = self._hub.entry.options.get(CONF_KNOWN_COMMANDS, {})
        if isinstance(known_commands, dict):
            annotate_known_command(event_data, known_commands)
        raw_capture = (
            event.get("raw") is True
            and event.get("command")
            and event.get("learning") is True
        )
        if event_data.get("known") is not True and not raw_capture:
            return
        event_data["sequence"] = event.get("sequence")
        event_data["device_millis"] = event.get("millis")
        for key in ("raw", "pulse_count", "rssi", "truncated", "command"):
            if key in event:
                event_data[key] = event[key]
        self._trigger_event("press", event_data)
        self.async_write_ha_state()
