from __future__ import annotations

import base64
import binascii
from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any

from homeassistant.components.sensor import (
    RestoreSensor,
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import (
    DEGREE,
    STATE_UNAVAILABLE,
    STATE_UNKNOWN,
    EntityCategory,
    UnitOfTemperature,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.util import dt as dt_util

from . import RFBridgeHub, diagnostics_signal, parse_rflink_message, rf_event_signal
from .const import DOMAIN


@dataclass(frozen=True, slots=True)
class RFBridgeSensorDescription:
    key: str
    name: str
    icon: str
    value_fn: Callable[[RFBridgeHub], str | datetime | None]
    device_class: SensorDeviceClass | None = None


@dataclass(frozen=True, slots=True)
class RFBridgeMeasurementDescription:
    key: str
    name: str
    icon: str | None = None
    device_class: SensorDeviceClass | None = None
    native_unit_of_measurement: str | None = None
    state_class: SensorStateClass | None = None


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
        status_at = status_at.replace(tzinfo=UTC)
    return status_at - timedelta(seconds=value)


SENSORS: tuple[RFBridgeSensorDescription, ...] = (
    RFBridgeSensorDescription("status", "Status", "mdi:list-status", _bridge_status),
    RFBridgeSensorDescription(
        "uptime", "Uptime", "mdi:timer-outline", _bridge_uptime, SensorDeviceClass.TIMESTAMP
    ),
)


# Matches RFLink's PACKET_FIELDS/VALUE_TRANSLATION table. Only measurement
# fields are included; decoded remotes and raw captures never create sensors.
MEASUREMENTS: dict[str, RFBridgeMeasurementDescription] = {
    "awinsp": RFBridgeMeasurementDescription("average_windspeed", "Average wind speed", "mdi:weather-windy", SensorDeviceClass.WIND_SPEED, "km/h", SensorStateClass.MEASUREMENT),
    "baro": RFBridgeMeasurementDescription("barometric_pressure", "Barometric pressure", "mdi:gauge", SensorDeviceClass.PRESSURE, "hPa", SensorStateClass.MEASUREMENT),
    "bat": RFBridgeMeasurementDescription("battery", "Battery", "mdi:battery"),
    "bforecast": RFBridgeMeasurementDescription("weather_forecast", "Weather forecast", "mdi:weather-cloudy-clock"),
    "co2": RFBridgeMeasurementDescription("co2_air_quality", "CO2 air quality", "mdi:molecule-co2", SensorDeviceClass.CO2, "ppm", SensorStateClass.MEASUREMENT),
    "current": RFBridgeMeasurementDescription("current_phase_1", "Current", "mdi:current-ac", SensorDeviceClass.CURRENT, "A", SensorStateClass.MEASUREMENT),
    "current2": RFBridgeMeasurementDescription("current_phase_2", "Current phase 2", "mdi:current-ac", SensorDeviceClass.CURRENT, "A", SensorStateClass.MEASUREMENT),
    "current3": RFBridgeMeasurementDescription("current_phase_3", "Current phase 3", "mdi:current-ac", SensorDeviceClass.CURRENT, "A", SensorStateClass.MEASUREMENT),
    "dist": RFBridgeMeasurementDescription("distance", "Distance", "mdi:map-marker-distance", SensorDeviceClass.DISTANCE, "mm", SensorStateClass.MEASUREMENT),
    "hstatus": RFBridgeMeasurementDescription("humidity_status", "Humidity status", "mdi:water-percent"),
    "hum": RFBridgeMeasurementDescription("humidity", "Humidity", "mdi:water-percent", SensorDeviceClass.HUMIDITY, "%", SensorStateClass.MEASUREMENT),
    "kwatt": RFBridgeMeasurementDescription("kilowatt", "Power", "mdi:flash", SensorDeviceClass.POWER, "kW", SensorStateClass.MEASUREMENT),
    "lux": RFBridgeMeasurementDescription("light_intensity", "Illuminance", "mdi:brightness-5", SensorDeviceClass.ILLUMINANCE, "lx", SensorStateClass.MEASUREMENT),
    "meter": RFBridgeMeasurementDescription("meter_value", "Meter value", "mdi:counter"),
    "rain": RFBridgeMeasurementDescription("total_rain", "Total rain", "mdi:weather-rainy", SensorDeviceClass.PRECIPITATION, "mm", SensorStateClass.TOTAL_INCREASING),
    "rainrate": RFBridgeMeasurementDescription("rain_rate", "Rain rate", "mdi:weather-pouring", SensorDeviceClass.PRECIPITATION_INTENSITY, "mm/h", SensorStateClass.MEASUREMENT),
    "raintot": RFBridgeMeasurementDescription("total_rain", "Total rain", "mdi:weather-rainy", SensorDeviceClass.PRECIPITATION, "mm", SensorStateClass.TOTAL_INCREASING),
    "temp": RFBridgeMeasurementDescription("temperature", "Temperature", "mdi:thermometer", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT),
    "sound": RFBridgeMeasurementDescription("noise_level", "Noise level", "mdi:bell-alert"),
    "uv": RFBridgeMeasurementDescription("uv_intensity", "UV index", "mdi:sunglasses", None, None, SensorStateClass.MEASUREMENT),
    "volt": RFBridgeMeasurementDescription("voltage", "Voltage", "mdi:flash-outline", SensorDeviceClass.VOLTAGE, "V", SensorStateClass.MEASUREMENT),
    "watt": RFBridgeMeasurementDescription("watt", "Power", "mdi:flash", SensorDeviceClass.POWER, "W", SensorStateClass.MEASUREMENT),
    "winchl": RFBridgeMeasurementDescription("windchill", "Wind chill", "mdi:thermometer", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT),
    "windir": RFBridgeMeasurementDescription("winddirection", "Wind direction", "mdi:compass", SensorDeviceClass.WIND_DIRECTION, DEGREE, SensorStateClass.MEASUREMENT_ANGLE),
    "wind": RFBridgeMeasurementDescription("windspeed", "Wind speed", "mdi:weather-windy", SensorDeviceClass.WIND_SPEED, "km/h", SensorStateClass.MEASUREMENT),
    "wings": RFBridgeMeasurementDescription("windgusts", "Wind gust", "mdi:weather-windy", SensorDeviceClass.WIND_SPEED, "km/h", SensorStateClass.MEASUREMENT),
    "winsp": RFBridgeMeasurementDescription("windspeed", "Wind speed", "mdi:weather-windy", SensorDeviceClass.WIND_SPEED, "km/h", SensorStateClass.MEASUREMENT),
    "wintmp": RFBridgeMeasurementDescription("windtemp", "Wind temperature", "mdi:thermometer", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT),
}
MEASUREMENTS_BY_KEY = {description.key: description for description in MEASUREMENTS.values()}


def _decode_hex(value: str) -> int | None:
    try:
        return int(value, 16)
    except ValueError:
        return None


def _decode_signed_tenths(value: str) -> float | None:
    integer = _decode_hex(value)
    if integer is None:
        return None
    return -(integer & 0x7FFF) / 10 if integer & 0x8000 else integer / 10


def _decode_measurement(field: str, value: str) -> str | int | float | None:
    if field == "bat":
        return value.upper()
    if field == "bforecast":
        return {"0": "No information", "1": "Sunny", "2": "Partly cloudy", "3": "Cloudy", "4": "Rain"}.get(value, "Unknown")
    if field in {"temp", "winchl", "wintmp"}:
        return _decode_signed_tenths(value)
    if field in {"awinsp", "rain", "rainrate", "raintot", "wings", "winsp"}:
        integer = _decode_hex(value)
        return integer / 10 if integer is not None else None
    if field == "windir":
        try:
            return int(value) * 22.5
        except ValueError:
            return None
    if field in {"baro", "kwatt", "lux", "uv", "watt"}:
        return _decode_hex(value)
    if field == "hstatus":
        return {"0": "Normal", "1": "Comfortable", "2": "Dry", "3": "Wet"}.get(value, "Unknown")
    try:
        return int(value)
    except ValueError:
        return None


def _sensor_token(protocol: str, identifier: str, measurement_key: str) -> str:
    value = f"{protocol}\x1f{identifier}\x1f{measurement_key}".encode()
    return base64.urlsafe_b64encode(value).decode("ascii").rstrip("=")


def _sensor_identity(entry_id: str, protocol: str, identifier: str, measurement_key: str) -> str:
    return f"{entry_id}_rf_sensor_{_sensor_token(protocol, identifier, measurement_key)}"


def _decode_sensor_identity(entry_id: str, unique_id: str) -> tuple[str, str, str] | None:
    prefix = f"{entry_id}_rf_sensor_"
    if not unique_id.startswith(prefix):
        return None
    try:
        token = unique_id.removeprefix(prefix)
        padding = "=" * (-len(token) % 4)
        protocol, identifier, measurement_key = base64.urlsafe_b64decode(token + padding).decode("utf-8").split("\x1f")
    except (binascii.Error, UnicodeDecodeError, ValueError):
        return None
    if not protocol or not identifier or measurement_key not in MEASUREMENTS_BY_KEY:
        return None
    return protocol, identifier, measurement_key


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
        self.async_on_remove(async_dispatcher_connect(self.hass, diagnostics_signal(self._hub.entry.entry_id), self.async_write_ha_state))

    @property
    def available(self) -> bool:
        return self._hub.available

    @property
    def device_info(self) -> dict[str, object]:
        return self._hub.device_info

    @property
    def native_value(self) -> str | datetime | None:
        return self._description.value_fn(self._hub)


class RFBridgeDiscoveredSensor(RestoreSensor):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, hub: RFBridgeHub, protocol: str, identifier: str, description: RFBridgeMeasurementDescription, value: str | float | None = None) -> None:
        self._hub = hub
        self._description = description
        self._value = value
        self._attr_unique_id = _sensor_identity(hub.entry.entry_id, protocol, identifier, description.key)
        self._attr_name = f"{protocol} {identifier} {description.name}"
        self._attr_icon = description.icon
        self._attr_device_class = description.device_class
        self._attr_native_unit_of_measurement = description.native_unit_of_measurement
        self._attr_state_class = description.state_class

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(async_dispatcher_connect(self.hass, diagnostics_signal(self._hub.entry.entry_id), self.async_write_ha_state))
        if self._value is not None:
            return
        state = await self.async_get_last_state()
        if state is not None and state.state not in {STATE_UNKNOWN, STATE_UNAVAILABLE}:
            self._value = state.state

    @property
    def available(self) -> bool:
        return self._hub.available

    @property
    def device_info(self) -> dict[str, object]:
        return self._hub.device_info

    @property
    def native_value(self) -> str | int | float | None:
        return self._value

    def async_set_value(self, value: str | float) -> None:
        if self._value == value:
            return
        self._value = value
        self.async_write_ha_state()


class RFBridgeDiscoveredSensorManager:
    """Create and update normal sensor entities from decoded RFLink frames."""

    def __init__(self, hass: HomeAssistant, hub: RFBridgeHub, async_add_entities: AddEntitiesCallback) -> None:
        self._hass = hass
        self._hub = hub
        self._async_add_entities = async_add_entities
        self._sensors: dict[str, RFBridgeDiscoveredSensor] = {}

    def restore(self) -> list[RFBridgeDiscoveredSensor]:
        registry = er.async_get(self._hass)
        restored: list[RFBridgeDiscoveredSensor] = []
        for entity_entry in er.async_entries_for_config_entry_id(registry, self._hub.entry.entry_id):
            if entity_entry.domain != "sensor":
                continue
            identity = _decode_sensor_identity(self._hub.entry.entry_id, entity_entry.unique_id)
            if identity is None:
                continue
            protocol, identifier, measurement_key = identity
            sensor = RFBridgeDiscoveredSensor(self._hub, protocol, identifier, MEASUREMENTS_BY_KEY[measurement_key])
            self._sensors[sensor.unique_id] = sensor
            restored.append(sensor)
        return restored

    @callback
    def handle_event(self, event: dict[str, Any]) -> None:
        if event.get("raw") is True:
            return
        message = event.get("message")
        if not isinstance(message, str):
            return
        data = parse_rflink_message(message)
        protocol = data.get("protocol")
        identifier = data.get("id")
        if not isinstance(protocol, str) or not isinstance(identifier, str):
            return
        for field, description in MEASUREMENTS.items():
            raw_value = data.get(field)
            if not isinstance(raw_value, str):
                continue
            value = _decode_measurement(field, raw_value)
            if value is None:
                continue
            unique_id = _sensor_identity(self._hub.entry.entry_id, protocol, identifier, description.key)
            sensor = self._sensors.get(unique_id)
            if sensor is None:
                sensor = RFBridgeDiscoveredSensor(self._hub, protocol, identifier, description, value)
                self._sensors[unique_id] = sensor
                self._async_add_entities([sensor])
                continue
            sensor.async_set_value(value)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    manager = RFBridgeDiscoveredSensorManager(hass, hub, async_add_entities)
    async_add_entities([*(RFBridgeSensor(hub, description) for description in SENSORS), *manager.restore()])
    entry.async_on_unload(async_dispatcher_connect(hass, rf_event_signal(entry.entry_id), manager.handle_event))
