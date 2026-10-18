from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.util import dt as dt_util

from . import RFBridgeHub, diagnostics_signal
from .const import DOMAIN


@dataclass(frozen=True, slots=True)
class RFBridgeSensorDescription:
    key: str
    name: str
    icon: str
    value_fn: Callable[[RFBridgeHub], str | datetime | None]
    device_class: SensorDeviceClass | None = None


def _bridge_status(hub: RFBridgeHub) -> str:
    if hub.status.get("raw_learning_armed") is True:
        return "learning"
    if hub.last_command is not None and hub.last_success is None:
        return "transmitting"
    return "idle"


def _bridge_uptime(hub: RFBridgeHub) -> datetime | None:
    value = hub.status.get("uptime")
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    status_at = dt_util.parse_datetime(hub.last_status_at) if hub.last_status_at else None
    if status_at is None:
        status_at = dt_util.utcnow()
    if status_at.tzinfo is None:
        status_at = status_at.replace(tzinfo=timezone.utc)
    return status_at - timedelta(seconds=value)


SENSORS: tuple[RFBridgeSensorDescription, ...] = (
    RFBridgeSensorDescription(
        key="status",
        name="Status",
        icon="mdi:list-status",
        value_fn=_bridge_status,
    ),
    RFBridgeSensorDescription(
        key="uptime",
        name="Uptime",
        icon="mdi:timer-outline",
        value_fn=_bridge_uptime,
        device_class=SensorDeviceClass.TIMESTAMP,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(RFBridgeSensor(hub, description) for description in SENSORS)


class RFBridgeSensor(SensorEntity):
    _attr_entity_category = EntityCategory.DIAGNOSTIC
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, hub: RFBridgeHub, description: RFBridgeSensorDescription) -> None:
        self._hub = hub
        self._description = description
        self._attr_icon = description.icon
        self._attr_name = description.name
        self._attr_unique_id = f"{hub.entry.entry_id}_{description.key}"
        self._attr_device_class = description.device_class

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(
            async_dispatcher_connect(
                self.hass,
                diagnostics_signal(self._hub.entry.entry_id),
                self.async_write_ha_state,
            )
        )

    @property
    def available(self) -> bool:
        return self._hub.available

    @property
    def device_info(self) -> dict[str, object]:
        return self._hub.device_info

    @property
    def native_value(self) -> str | datetime | None:
        return self._description.value_fn(self._hub)
