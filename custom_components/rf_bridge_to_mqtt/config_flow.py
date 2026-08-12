from __future__ import annotations

import json
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.service_info.mqtt import MqttServiceInfo

from . import parse_commands
from .const import (
    CONF_COMMANDS,
    CONF_IDENTIFIERS,
    CONF_NAME,
    CONF_SW_VERSION,
    CONF_TOPIC_IN,
    CONF_TOPIC_LWT,
    CONF_TOPIC_OUT,
    DEFAULT_TOPIC_IN,
    DEFAULT_TOPIC_LWT,
    DEFAULT_TOPIC_OUT,
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


class RFBridgeToMqttConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

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
