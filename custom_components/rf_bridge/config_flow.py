from __future__ import annotations

import json
from typing import Any

import aiohttp
import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from . import async_fetch_bridge_status, parse_commands
from .const import (
    CONF_API_PORT,
    CONF_COMMANDS,
    CONF_HOST,
    CONF_HW_VERSION,
    CONF_IDENTIFIERS,
    CONF_KNOWN_COMMANDS,
    CONF_MODEL,
    CONF_NAME,
    CONF_SW_VERSION,
    DEFAULT_API_PORT,
    DOMAIN,
)


def _commands_to_text(commands: dict[str, Any]) -> str:
    return json.dumps(commands or {}, indent=2, sort_keys=True)


def _commands_from_text(text: str) -> dict[str, Any]:
    text = (text or "").strip()
    if not text:
        return {}
    data = json.loads(text)
    commands = parse_commands(data)
    if len(commands) != len(data):
        raise ValueError("One or more command definitions are invalid")
    return data


def _known_commands_to_text(commands: dict[str, Any]) -> str:
    return json.dumps(commands or {}, indent=2, sort_keys=True)


def _known_commands_from_text(text: str) -> dict[str, Any]:
    text = (text or "").strip()
    if not text:
        return {}
    data = json.loads(text)
    if not isinstance(data, dict):
        raise TypeError("Known commands must be a JSON object")
    for key, value in data.items():
        if not isinstance(key, str) or not key:
            raise ValueError("Known command keys must be non-empty strings")
        if isinstance(value, str):
            continue
        if isinstance(value, dict):
            name = value.get("name")
            icon = value.get("icon")
            if name is not None and not isinstance(name, str):
                raise ValueError("Known command name must be a string")
            if icon is not None and not isinstance(icon, str):
                raise ValueError("Known command icon must be a string")
            continue
        raise ValueError("Known command values must be strings or objects")
    return data


def _txt_value(value: Any) -> str | None:
    if isinstance(value, bytes):
        return value.decode("utf-8", "ignore")
    if isinstance(value, str):
        return value
    return None


def _discovery_identifier(value: Any, fallback: str) -> str:
    value = _txt_value(value)
    if value:
        compact = "".join(c for c in value.lower() if c.isalnum() or c == "_")
        if compact:
            if compact.startswith("rflink32_"):
                return compact
            return f"rflink32_{compact}"
    return fallback


def _api_port(data: dict[str, Any]) -> int:
    return data.get(CONF_API_PORT, DEFAULT_API_PORT)


class RFBridgeConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._discovery: dict[str, Any] = {}

    async def async_step_zeroconf(
        self, discovery_info: ZeroconfServiceInfo
    ) -> config_entries.ConfigFlowResult:
        host = discovery_info.host
        if not host:
            return self.async_abort(reason="invalid_discovery")

        properties = discovery_info.properties or {}
        unique_id = _discovery_identifier(properties.get("id"), discovery_info.name)
        await self.async_set_unique_id(unique_id)

        self._discovery = {
            CONF_HOST: host,
            CONF_IDENTIFIERS: [unique_id],
            CONF_HW_VERSION: _txt_value(properties.get(CONF_HW_VERSION)),
            CONF_MODEL: _txt_value(properties.get(CONF_MODEL)) or "Sonoff RF Bridge",
            CONF_NAME: _txt_value(properties.get("name")) or "RF Bridge",
            CONF_SW_VERSION: _txt_value(properties.get(CONF_SW_VERSION)),
            CONF_API_PORT: discovery_info.port or DEFAULT_API_PORT,
        }
        self.context["title_placeholders"] = {
            CONF_NAME: self._discovery[CONF_NAME],
        }
        self._abort_if_unique_id_configured(
            updates={
                CONF_HOST: host,
                CONF_HW_VERSION: self._discovery.get(CONF_HW_VERSION),
                CONF_MODEL: self._discovery.get(CONF_MODEL),
                CONF_NAME: self._discovery.get(CONF_NAME),
                CONF_SW_VERSION: self._discovery.get(CONF_SW_VERSION),
            }
        )
        return await self.async_step_zeroconf_confirm()

    def _async_create_entry_from_status(
        self, data: dict[str, Any], status: dict[str, Any]
    ) -> config_entries.ConfigFlowResult:
        return self.async_create_entry(
            title=self._discovery.get(CONF_NAME) or "RF Bridge",
            data={
                CONF_HOST: data[CONF_HOST],
                CONF_HW_VERSION: status.get("hardware")
                or self._discovery.get(CONF_HW_VERSION),
                CONF_IDENTIFIERS: self._discovery[CONF_IDENTIFIERS],
                CONF_MODEL: self._discovery.get(CONF_MODEL),
                CONF_NAME: self._discovery.get(CONF_NAME) or "RF Bridge",
                CONF_SW_VERSION: status.get(CONF_SW_VERSION)
                or self._discovery.get(CONF_SW_VERSION),
                CONF_API_PORT: _api_port(data),
            },
            options={CONF_COMMANDS: {}, CONF_KNOWN_COMMANDS: {}},
        )

    async def async_step_zeroconf_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if not self._discovery:
            return self.async_abort(reason="invalid_discovery")

        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                status = await self._async_get_status(self._discovery)
            except aiohttp.ClientError:
                errors["base"] = "cannot_connect"
            except (TimeoutError, OSError):
                errors["base"] = "cannot_connect"
            else:
                unique_id = self._discovery[CONF_IDENTIFIERS][0]
                await self.async_set_unique_id(unique_id)
                self._abort_if_unique_id_configured(
                    updates={
                        CONF_HOST: self._discovery[CONF_HOST],
                        CONF_SW_VERSION: status.get(CONF_SW_VERSION)
                        or self._discovery.get(CONF_SW_VERSION),
                        CONF_HW_VERSION: status.get("hardware")
                        or self._discovery.get(CONF_HW_VERSION),
                    }
                )
                return self._async_create_entry_from_status(self._discovery, status)

        return self.async_show_form(
            step_id="zeroconf_confirm",
            description_placeholders={
                CONF_NAME: self._discovery.get(CONF_NAME) or "RF Bridge",
            },
            errors=errors,
        )

    async def _async_get_status(self, data: dict[str, Any]) -> dict[str, Any]:
        session = async_get_clientsession(self.hass)
        return await async_fetch_bridge_status(
            session, data[CONF_HOST], _api_port(data)
        )

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                commands = _commands_from_text(user_input.get(CONF_COMMANDS, "{}"))
            except (json.JSONDecodeError, ServiceValidationError, ValueError):
                errors[CONF_COMMANDS] = "invalid_commands"
            try:
                known_commands = _known_commands_from_text(
                    user_input.get(CONF_KNOWN_COMMANDS, "{}")
                )
            except (json.JSONDecodeError, ValueError):
                errors[CONF_KNOWN_COMMANDS] = "invalid_known_commands"
            if not errors:
                try:
                    status = await self._async_get_status(user_input)
                except aiohttp.ClientError:
                    errors["base"] = "cannot_connect"
                except (TimeoutError, OSError):
                    errors["base"] = "cannot_connect"
            if not errors:
                assert commands is not None
                assert known_commands is not None
                unique_id = f"rflink32_{user_input[CONF_HOST].replace('.', '_')}"
                await self.async_set_unique_id(unique_id)
                self._abort_if_unique_id_configured()
                entry_title = status.get("name") or "RF Bridge"
                return self.async_create_entry(
                    title=entry_title,
                    data={
                        CONF_HOST: user_input[CONF_HOST],
                        CONF_HW_VERSION: status.get("hardware"),
                        CONF_IDENTIFIERS: [unique_id],
                        CONF_MODEL: "Sonoff RF Bridge",
                        CONF_NAME: entry_title,
                        CONF_SW_VERSION: status.get(CONF_SW_VERSION),
                        CONF_API_PORT: _api_port(user_input),
                    },
                    options={
                        CONF_COMMANDS: commands,
                        CONF_KNOWN_COMMANDS: known_commands,
                    },
                )

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_HOST): cv.string,
                    vol.Optional(CONF_API_PORT, default=DEFAULT_API_PORT): cv.port,
                    vol.Optional(CONF_COMMANDS, default="{}"): cv.string,
                    vol.Optional(CONF_KNOWN_COMMANDS, default="{}"): cv.string,
                }
            ),
            errors=errors,
        )

    async def async_step_import(
        self, import_config: dict[str, Any]
    ) -> config_entries.ConfigFlowResult:
        host = import_config[CONF_HOST]
        unique_id = f"rflink32_{host.replace('.', '_')}"
        await self.async_set_unique_id(unique_id)
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})
        commands = import_config.get(CONF_COMMANDS, {})
        known_commands = import_config.get(CONF_KNOWN_COMMANDS, {})
        parse_commands(commands)
        _known_commands_from_text(json.dumps(known_commands))
        return self.async_create_entry(
            title=import_config.get(CONF_NAME) or "RF Bridge",
            data={
                CONF_HOST: host,
                CONF_IDENTIFIERS: [unique_id],
                CONF_NAME: import_config.get(CONF_NAME) or "RF Bridge",
                CONF_API_PORT: _api_port(import_config),
            },
            options={
                CONF_COMMANDS: commands,
                CONF_KNOWN_COMMANDS: known_commands,
            },
        )

    @staticmethod
    @callback
    def async_get_options_flow(
        config_entry: config_entries.ConfigEntry,
    ) -> config_entries.OptionsFlow:
        return RFBridgeOptionsFlow(config_entry)


class RFBridgeOptionsFlow(config_entries.OptionsFlow):
    def __init__(self, entry: config_entries.ConfigEntry) -> None:
        self.entry = entry

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                commands = _commands_from_text(user_input.get(CONF_COMMANDS, "{}"))
            except (json.JSONDecodeError, ServiceValidationError, ValueError):
                errors[CONF_COMMANDS] = "invalid_commands"
            try:
                known_commands = _known_commands_from_text(
                    user_input.get(CONF_KNOWN_COMMANDS, "{}")
                )
            except (json.JSONDecodeError, ValueError):
                errors[CONF_KNOWN_COMMANDS] = "invalid_known_commands"
            if not errors:
                assert commands is not None
                assert known_commands is not None
                return self.async_create_entry(
                    title="",
                    data={
                        CONF_COMMANDS: commands,
                        CONF_KNOWN_COMMANDS: known_commands,
                    },
                )

        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema(
                {
                    vol.Optional(
                        CONF_COMMANDS,
                        default=_commands_to_text(
                            self.entry.options.get(CONF_COMMANDS, {})
                        ),
                    ): cv.string,
                    vol.Optional(
                        CONF_KNOWN_COMMANDS,
                        default=_known_commands_to_text(
                            self.entry.options.get(CONF_KNOWN_COMMANDS, {})
                        ),
                    ): cv.string,
                }
            ),
            errors=errors,
        )
