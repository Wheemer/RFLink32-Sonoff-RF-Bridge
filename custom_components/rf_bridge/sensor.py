from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.dispatcher import async_dispatcher_connect

from . import RFBridgeHub, diagnostics_signal
from .const import (
    CONF_HOST,
    CONF_API_PORT,
    CONF_SW_VERSION,
    DOMAIN,
)


@dataclass(frozen=True, slots=True)
class RFBridgeSensorDescription:
    key: str
    name: str
    value_fn: Callable[[RFBridgeHub], str | int | None]
    icon: str
    attrs_fn: Callable[[RFBridgeHub], dict[str, Any]] | None = None
    poll_status: bool = False


SENSORS: tuple[RFBridgeSensorDescription, ...] = (
    RFBridgeSensorDescription(
        key="firmware",
        name="Firmware",
        value_fn=lambda hub: hub.entry.data.get(CONF_SW_VERSION)
        or hub.status.get(CONF_SW_VERSION),
        icon="mdi:chip",
    ),
    RFBridgeSensorDescription(
        key="host",
        name="Host",
        value_fn=lambda hub: hub.entry.data.get(CONF_HOST),
        icon="mdi:ip-network",
    ),
    RFBridgeSensorDescription(
        key="api_port",
        name="API port",
        value_fn=lambda hub: hub.entry.data.get(CONF_API_PORT),
        icon="mdi:web",
    ),
    RFBridgeSensorDescription(
        key="uptime",
        name="Uptime",
        value_fn=lambda hub: hub.status.get("uptime"),
        icon="mdi:timer-outline",
        poll_status=True,
    ),
    RFBridgeSensorDescription(
        key="heap_free",
        name="Free heap",
        value_fn=lambda hub: hub.status.get("heap_free"),
        icon="mdi:memory",
        poll_status=True,
    ),
    RFBridgeSensorDescription(
        key="last_command",
        name="Last command",
        value_fn=lambda hub: hub.last_command_key
        or (
            f"raw ({len(hub.last_command)} bytes)"
            if hub.last_command is not None
            else None
        ),
        icon="mdi:remote",
        attrs_fn=lambda hub: {"command": hub.last_command}
        if hub.last_command is not None
        else {},
    ),
    RFBridgeSensorDescription(
        key="last_result",
        name="Last result",
        value_fn=lambda hub: (
            "success"
            if hub.last_success is True
            else "failed"
            if hub.last_success is False
            else None
        ),
        icon="mdi:check-network",
    ),
    RFBridgeSensorDescription(
        key="last_error",
        name="Last error",
        value_fn=lambda hub: hub.last_error,
        icon="mdi:alert-circle-outline",
        attrs_fn=lambda hub: {"error": hub.last_error} if hub.last_error else {},
    ),
    RFBridgeSensorDescription(
        key="last_rf_event",
        name="Last RF event",
        value_fn=lambda hub: (
            hub.last_rf_event.get("message") if hub.last_rf_event else None
        ),
        icon="mdi:radio-tower",
        attrs_fn=lambda hub: {
            key: hub.last_rf_event[key]
            for key in ("command", "raw", "pulse_count", "rssi", "truncated")
            if hub.last_rf_event and key in hub.last_rf_event
        },
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        RFBridgeDiagnosticSensor(hub, description) for description in SENSORS
    )


class RFBridgeDiagnosticSensor(SensorEntity):
    _attr_has_entity_name = True
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(
        self, hub: RFBridgeHub, description: RFBridgeSensorDescription
    ) -> None:
        self._hub = hub
        self._description = description
        self._attr_name = description.name
        self._attr_icon = description.icon
        self._attr_unique_id = f"{hub.entry.entry_id}_{description.key}"

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
    def native_value(self) -> str | int | None:
        value = self._description.value_fn(self._hub)
        if isinstance(value, str) and len(value) > 255:
            return f"{value[:252]}..."
        return value

    @property
    def extra_state_attributes(self) -> dict[str, Any] | None:
        if self._description.attrs_fn is None:
            return None
        return self._description.attrs_fn(self._hub) or None

    async def async_update(self) -> None:
        if self._description.poll_status:
            await self._hub.async_get_status()
