from __future__ import annotations

import logging
from typing import Any

from homeassistant.components.button import ButtonDeviceClass, ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import RFBridgeHub, RFLink32Command, diagnostics_signal
from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    entities: list[ButtonEntity] = [
        RFBridgeLearnButton(hub),
        RFBridgeRebootButton(hub),
    ]
    entities.extend(
        RFLink32CommandButton(hub, command) for command in hub.commands.values()
    )
    async_add_entities(entities)


class RFBridgeButtonBase(ButtonEntity):
    _attr_has_entity_name = True

    def __init__(self, hub: RFBridgeHub) -> None:
        self._hub = hub

    @property
    def device_info(self) -> dict[str, Any]:
        return self._hub.device_info

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


class RFBridgeRebootButton(RFBridgeButtonBase):
    _attr_device_class = ButtonDeviceClass.RESTART
    _attr_entity_category = EntityCategory.CONFIG
    _attr_icon = "mdi:restart"
    _attr_name = "Reboot"

    def __init__(self, hub: RFBridgeHub) -> None:
        super().__init__(hub)
        self._attr_unique_id = f"{hub.entry.entry_id}_reboot"

    async def async_press(self) -> None:
        await self._hub.async_reboot()


class RFBridgeLearnButton(RFBridgeButtonBase):
    _attr_entity_category = EntityCategory.CONFIG
    _attr_icon = "mdi:target"
    _attr_name = "Learn next RF event"

    def __init__(self, hub: RFBridgeHub) -> None:
        super().__init__(hub)
        self._attr_unique_id = f"{hub.entry.entry_id}_learn_next_rf_event"

    async def async_press(self) -> None:
        await self._hub.async_arm_learning()


class RFLink32CommandButton(RFBridgeButtonBase):
    def __init__(self, hub: RFBridgeHub, command: RFLink32Command) -> None:
        super().__init__(hub)
        self._command = command
        self._attr_name = command.name
        self._attr_icon = command.icon
        self._attr_unique_id = f"{hub.entry.entry_id}_{command.key}"

    async def async_press(self) -> None:
        _LOGGER.info("RF Bridge command button pressed: key=%s", self._command.key)
        await self._hub.async_send_named(self._command.key)
