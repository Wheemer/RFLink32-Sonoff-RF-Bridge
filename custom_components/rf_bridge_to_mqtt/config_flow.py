from __future__ import annotations

import aiohttp
from aiohttp import BasicAuth
import json
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.components.mqtt.const import (
    CONF_BROKER as CONF_MQTT_BROKER,
    DEFAULT_PORT as DEFAULT_MQTT_PORT,
    DOMAIN as MQTT_DOMAIN,
)
from homeassistant.const import CONF_PASSWORD, CONF_PORT, CONF_USERNAME
from homeassistant.core import callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.service_info.mqtt import MqttServiceInfo
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from . import parse_commands
from .const import (
    CONF_COMMANDS,
    CONF_HOST,
    CONF_IDENTIFIERS,
    CONF_NAME,
    CONF_SW_VERSION,
    CONF_TOPIC_IN,
    CONF_TOPIC_LWT,
    CONF_TOPIC_OUT,
    CONF_WEB_PORT,
    DEFAULT_TOPIC_IN,
    DEFAULT_TOPIC_LWT,
    DEFAULT_TOPIC_OUT,
    DEFAULT_WEB_PASSWORD,
    DEFAULT_WEB_PORT,
    DEFAULT_WEB_USER,
    DOMAIN,
)


CONF_WEB_PASSWORD = "web_password"
CONF_WEB_USER = "web_user"


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


def _payload_to_dict(payload: str | bytes | bytearray | None) -> dict[str, Any]:
    if isinstance(payload, (bytes, bytearray)):
        payload = payload.decode("utf-8")
    if not isinstance(payload, str) or not payload.strip():
        raise ValueError("Empty discovery payload")
    data = json.loads(payload)
    if not isinstance(data, dict):
        raise ValueError("Discovery payload must be an object")
    return data


def _first_identifier(data: dict[str, Any]) -> str:
    identifiers = data.get(CONF_IDENTIFIERS)
    if isinstance(identifiers, list) and identifiers:
        identifier = identifiers[0]
    else:
        identifier = data.get(CONF_TOPIC_IN)
    if not isinstance(identifier, str) or not identifier:
        raise ValueError("Discovery payload is missing an identifier")
    return identifier


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


def _mqtt_config_from_hass(hass) -> dict[str, Any] | None:
    entries = hass.config_entries.async_entries(MQTT_DOMAIN)
    if not entries:
        return None

    entry = entries[0]
    data = {**entry.data, **entry.options}
    broker = data.get(CONF_MQTT_BROKER)
    if not isinstance(broker, str) or not broker:
        return None

    return {
        CONF_MQTT_BROKER: broker,
        CONF_PORT: int(data.get(CONF_PORT, DEFAULT_MQTT_PORT)),
        CONF_USERNAME: data.get(CONF_USERNAME) or "",
        CONF_PASSWORD: data.get(CONF_PASSWORD) or "",
    }


class RFBridgeToMqttConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
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
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})

        self._discovery = {
            CONF_HOST: host,
            CONF_IDENTIFIERS: [unique_id],
            CONF_NAME: _txt_value(properties.get("name")) or "RF Bridge to MQTT",
            CONF_WEB_PORT: discovery_info.port or DEFAULT_WEB_PORT,
            CONF_WEB_USER: DEFAULT_WEB_USER,
            CONF_WEB_PASSWORD: DEFAULT_WEB_PASSWORD,
        }
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        mqtt_config = _mqtt_config_from_hass(self.hass)
        if mqtt_config is None:
            return self.async_abort(reason="mqtt_not_configured")

        defaults = {
            CONF_HOST: self._discovery[CONF_HOST],
            CONF_WEB_PORT: self._discovery.get(CONF_WEB_PORT, DEFAULT_WEB_PORT),
            CONF_WEB_USER: self._discovery.get(CONF_WEB_USER, DEFAULT_WEB_USER),
            CONF_WEB_PASSWORD: self._discovery.get(
                CONF_WEB_PASSWORD, DEFAULT_WEB_PASSWORD
            ),
            CONF_MQTT_BROKER: mqtt_config[CONF_MQTT_BROKER],
            CONF_PORT: mqtt_config[CONF_PORT],
            CONF_USERNAME: mqtt_config[CONF_USERNAME],
            CONF_PASSWORD: mqtt_config[CONF_PASSWORD],
        }

        errors: dict[str, str] = {}
        if user_input is not None:
            merged = {**defaults, **user_input}
            try:
                await self._async_push_mqtt_config(merged)
            except aiohttp.ClientResponseError as err:
                errors["base"] = "auth_failed" if err.status == 401 else "cannot_connect"
            except (aiohttp.ClientError, TimeoutError, OSError):
                errors["base"] = "cannot_connect"
            else:
                unique_id = self._discovery[CONF_IDENTIFIERS][0]
                await self.async_set_unique_id(unique_id)
                self._abort_if_unique_id_configured(
                    updates={
                        CONF_HOST: merged[CONF_HOST],
                        CONF_TOPIC_IN: DEFAULT_TOPIC_IN,
                        CONF_TOPIC_OUT: DEFAULT_TOPIC_OUT,
                        CONF_TOPIC_LWT: DEFAULT_TOPIC_LWT,
                    }
                )
                return self.async_create_entry(
                    title=self._discovery.get(CONF_NAME) or "RF Bridge to MQTT",
                    data={
                        CONF_HOST: merged[CONF_HOST],
                        CONF_IDENTIFIERS: self._discovery[CONF_IDENTIFIERS],
                        CONF_NAME: self._discovery.get(CONF_NAME)
                        or "RF Bridge to MQTT",
                        CONF_TOPIC_IN: DEFAULT_TOPIC_IN,
                        CONF_TOPIC_OUT: DEFAULT_TOPIC_OUT,
                        CONF_TOPIC_LWT: DEFAULT_TOPIC_LWT,
                    },
                    options={
                        CONF_TOPIC_IN: DEFAULT_TOPIC_IN,
                        CONF_TOPIC_OUT: DEFAULT_TOPIC_OUT,
                        CONF_TOPIC_LWT: DEFAULT_TOPIC_LWT,
                        CONF_COMMANDS: {},
                    },
                )

        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_HOST, default=defaults[CONF_HOST]): cv.string,
                    vol.Required(
                        CONF_WEB_PORT, default=defaults[CONF_WEB_PORT]
                    ): cv.port,
                    vol.Required(
                        CONF_WEB_USER, default=defaults[CONF_WEB_USER]
                    ): cv.string,
                    vol.Required(
                        CONF_WEB_PASSWORD, default=defaults[CONF_WEB_PASSWORD]
                    ): cv.string,
                    vol.Required(
                        CONF_MQTT_BROKER, default=defaults[CONF_MQTT_BROKER]
                    ): cv.string,
                    vol.Required(CONF_PORT, default=defaults[CONF_PORT]): cv.port,
                    vol.Optional(
                        CONF_USERNAME, default=defaults[CONF_USERNAME]
                    ): cv.string,
                    vol.Optional(
                        CONF_PASSWORD, default=defaults[CONF_PASSWORD]
                    ): cv.string,
                }
            ),
            errors=errors,
        )

    async def _async_push_mqtt_config(self, data: dict[str, Any]) -> None:
        session = async_get_clientsession(self.hass)
        url = f"http://{data[CONF_HOST]}:{data[CONF_WEB_PORT]}/api/config"
        payload = {
            "mqtt": {
                "enabled": True,
                "server": data[CONF_MQTT_BROKER],
                "port": data[CONF_PORT],
                "id": self._discovery[CONF_IDENTIFIERS][0],
                "user": data.get(CONF_USERNAME) or "",
                "password": data.get(CONF_PASSWORD) or "",
                "topic_in": DEFAULT_TOPIC_IN,
                "topic_out": DEFAULT_TOPIC_OUT,
                "lwt_enabled": True,
                "topic_lwt": DEFAULT_TOPIC_LWT,
            }
        }

        async with session.post(
            url,
            json=payload,
            auth=BasicAuth(data[CONF_WEB_USER], data[CONF_WEB_PASSWORD]),
            timeout=aiohttp.ClientTimeout(total=15),
        ) as response:
            response.raise_for_status()

    async def async_step_mqtt(
        self, discovery_info: MqttServiceInfo
    ) -> config_entries.ConfigFlowResult:
        try:
            data = _payload_to_dict(discovery_info.payload)
            unique_id = _first_identifier(data)
        except (json.JSONDecodeError, UnicodeDecodeError, ValueError):
            return self.async_abort(reason="invalid_discovery")

        await self.async_set_unique_id(unique_id)
        self._abort_if_unique_id_configured(
            updates={
                CONF_TOPIC_IN: data.get(CONF_TOPIC_IN, DEFAULT_TOPIC_IN),
                CONF_TOPIC_OUT: data.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                CONF_TOPIC_LWT: data.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
                CONF_SW_VERSION: data.get(CONF_SW_VERSION),
            }
        )

        return self.async_create_entry(
            title=data.get(CONF_NAME) or "RF Bridge to MQTT",
            data={
                CONF_IDENTIFIERS: data.get(CONF_IDENTIFIERS, [unique_id]),
                CONF_NAME: data.get(CONF_NAME) or "RF Bridge to MQTT",
                CONF_TOPIC_IN: data.get(CONF_TOPIC_IN, DEFAULT_TOPIC_IN),
                CONF_TOPIC_OUT: data.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                CONF_TOPIC_LWT: data.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
                CONF_SW_VERSION: data.get(CONF_SW_VERSION),
            },
            options={
                CONF_TOPIC_IN: data.get(CONF_TOPIC_IN, DEFAULT_TOPIC_IN),
                CONF_TOPIC_OUT: data.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                CONF_TOPIC_LWT: data.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
                CONF_COMMANDS: {},
            },
        )

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                commands = _commands_from_text(user_input.get(CONF_COMMANDS, "{}"))
            except (json.JSONDecodeError, ValueError):
                errors[CONF_COMMANDS] = "invalid_commands"
            else:
                await self.async_set_unique_id(user_input[CONF_TOPIC_IN])
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=user_input.get("name") or "RF Bridge to MQTT",
                    data={
                        CONF_TOPIC_IN: user_input[CONF_TOPIC_IN],
                        CONF_TOPIC_OUT: user_input[CONF_TOPIC_OUT],
                        CONF_TOPIC_LWT: user_input[CONF_TOPIC_LWT],
                    },
                    options={
                        CONF_TOPIC_IN: user_input[CONF_TOPIC_IN],
                        CONF_TOPIC_OUT: user_input[CONF_TOPIC_OUT],
                        CONF_TOPIC_LWT: user_input[CONF_TOPIC_LWT],
                        CONF_COMMANDS: commands,
                    },
                )

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Optional("name", default="RF Bridge to MQTT"): cv.string,
                    vol.Optional(CONF_TOPIC_IN, default=DEFAULT_TOPIC_IN): cv.string,
                    vol.Optional(CONF_TOPIC_OUT, default=DEFAULT_TOPIC_OUT): cv.string,
                    vol.Optional(CONF_TOPIC_LWT, default=DEFAULT_TOPIC_LWT): cv.string,
                    vol.Optional(CONF_COMMANDS, default="{}"): cv.string,
                }
            ),
            errors=errors,
        )

    async def async_step_import(
        self, import_config: dict[str, Any]
    ) -> config_entries.ConfigFlowResult:
        topic_in = import_config.get(CONF_TOPIC_IN, DEFAULT_TOPIC_IN)
        await self.async_set_unique_id(topic_in)
        self._abort_if_unique_id_configured(
            updates={
                CONF_TOPIC_IN: topic_in,
                CONF_TOPIC_OUT: import_config.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                CONF_TOPIC_LWT: import_config.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
            }
        )
        commands = import_config.get(CONF_COMMANDS, {})
        parse_commands(commands)
        return self.async_create_entry(
            title="RF Bridge to MQTT",
            data={
                CONF_TOPIC_IN: topic_in,
                CONF_TOPIC_OUT: import_config.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                CONF_TOPIC_LWT: import_config.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
            },
            options={
                CONF_TOPIC_IN: topic_in,
                CONF_TOPIC_OUT: import_config.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                CONF_TOPIC_LWT: import_config.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
                CONF_COMMANDS: commands,
            },
        )

    @staticmethod
    @callback
    def async_get_options_flow(
        config_entry: config_entries.ConfigEntry,
    ) -> config_entries.OptionsFlow:
        return RFBridgeToMqttOptionsFlow(config_entry)


class RFBridgeToMqttOptionsFlow(config_entries.OptionsFlow):
    def __init__(self, entry: config_entries.ConfigEntry) -> None:
        self.entry = entry

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                commands = _commands_from_text(user_input.get(CONF_COMMANDS, "{}"))
            except (json.JSONDecodeError, ValueError):
                errors[CONF_COMMANDS] = "invalid_commands"
            else:
                return self.async_create_entry(
                    title="",
                    data={
                        CONF_TOPIC_IN: user_input[CONF_TOPIC_IN],
                        CONF_TOPIC_OUT: user_input[CONF_TOPIC_OUT],
                        CONF_TOPIC_LWT: user_input[CONF_TOPIC_LWT],
                        CONF_COMMANDS: commands,
                    },
                )

        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema(
                {
                    vol.Optional(
                        CONF_TOPIC_IN,
                        default=self.entry.options.get(
                            CONF_TOPIC_IN,
                            self.entry.data.get(CONF_TOPIC_IN, DEFAULT_TOPIC_IN),
                        ),
                    ): cv.string,
                    vol.Optional(
                        CONF_TOPIC_OUT,
                        default=self.entry.options.get(
                            CONF_TOPIC_OUT,
                            self.entry.data.get(CONF_TOPIC_OUT, DEFAULT_TOPIC_OUT),
                        ),
                    ): cv.string,
                    vol.Optional(
                        CONF_TOPIC_LWT,
                        default=self.entry.options.get(
                            CONF_TOPIC_LWT,
                            self.entry.data.get(CONF_TOPIC_LWT, DEFAULT_TOPIC_LWT),
                        ),
                    ): cv.string,
                    vol.Optional(
                        CONF_COMMANDS,
                        default=_commands_to_text(
                            self.entry.options.get(CONF_COMMANDS, {})
                        ),
                    ): cv.string,
                }
            ),
            errors=errors,
        )
