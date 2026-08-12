from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components import mqtt
from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import RFBridgeToMqttHub
from .const import (
    CONF_HOST,
    CONF_HW_VERSION,
    CONF_IDENTIFIERS,
    CONF_MODEL,
    CONF_NAME,
    CONF_SW_VERSION,
    CONF_TOPIC_IN,
    CONF_TOPIC_LWT,
    CONF_TOPIC_OUT,
    DOMAIN,
)


@dataclass(frozen=True, slots=True)
class RFBridgeSensorDescription:
    key: str
    name: str
    value_fn: Callable[[RFBridgeToMqttHub], str | None]
    icon: str


SENSORS: tuple[RFBridgeSensorDescription, ...] = (
    RFBridgeSensorDescription(
        key="firmware",
        name="Firmware",
        value_fn=lambda hub: hub.entry.data.get(CONF_SW_VERSION),
        icon="mdi:chip",
    ),
    RFBridgeSensorDescription(
        key="hardware",
        name="Hardware",
        value_fn=lambda hub: hub.entry.data.get(CONF_HW_VERSION),
        icon="mdi:developer-board",
    ),
    RFBridgeSensorDescription(
        key="host",
        name="Host",
        value_fn=lambda hub: hub.entry.data.get(CONF_HOST),
        icon="mdi:ip-network",
    ),
    RFBridgeSensorDescription(
        key="command_topic",
        name="Command topic",
        value_fn=lambda hub: hub.topic_in,
        icon="mdi:publish",
    ),
    RFBridgeSensorDescription(
        key="message_topic",
        name="Message topic",
        value_fn=lambda hub: hub.topic_out,
        icon="mdi:subscribe",
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeToMqttHub = hass.data[DOMAIN][entry.entry_id]
    entities: list[SensorEntity] = [
        RFBridgeDiagnosticSensor(hub, description) for description in SENSORS
    ]
    entities.append(RFBridgeAvailabilitySensor(hub))
    async_add_entities(entities)


class RFBridgeBaseSensor(SensorEntity):
    _attr_has_entity_name = True
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, hub: RFBridgeToMqttHub, key: str, name: str, icon: str) -> None:
        self._hub = hub
        self._attr_name = name
        self._attr_icon = icon
        self._attr_unique_id = f"{hub.entry.entry_id}_{key}"

    @property
    def device_info(self) -> dict[str, Any]:
        identifiers = self._hub.entry.data.get(CONF_IDENTIFIERS) or [
            self._hub.entry.entry_id
        ]
        return {
            "identifiers": {(DOMAIN, identifier) for identifier in identifiers},
            "name": self._hub.entry.data.get(CONF_NAME, self._hub.entry.title),
            "manufacturer": "RFLink32",
            "model": self._hub.entry.data.get(CONF_MODEL, "Sonoff RF Bridge"),
            "sw_version": self._hub.entry.data.get(CONF_SW_VERSION),
            "hw_version": self._hub.entry.data.get(CONF_HW_VERSION),
        }


class RFBridgeDiagnosticSensor(RFBridgeBaseSensor):
    def __init__(
        self, hub: RFBridgeToMqttHub, description: RFBridgeSensorDescription
    ) -> None:
        super().__init__(hub, description.key, description.name, description.icon)
        self._description = description

    @property
    def native_value(self) -> str | None:
        return self._description.value_fn(self._hub)


class RFBridgeAvailabilitySensor(RFBridgeBaseSensor):
    def __init__(self, hub: RFBridgeToMqttHub) -> None:
        super().__init__(hub, "availability", "Availability", "mdi:lan-connect")
        self._attr_native_value: str | None = None

    async def async_added_to_hass(self) -> None:
        @callback
        def message_received(msg) -> None:
            self._attr_native_value = str(msg.payload)
            self.async_write_ha_state()

        self.async_on_remove(
            await mqtt.async_subscribe(
                self.hass,
                self._hub.topic_lwt,
                message_received,
                qos=0,
            )
        )

