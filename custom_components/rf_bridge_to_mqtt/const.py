DOMAIN = "rf_bridge_to_mqtt"

CONF_COMMANDS = "commands"
CONF_COMMAND = "command"
CONF_COMMAND_KEY = "command_key"
CONF_HOST = "host"
CONF_HW_VERSION = "hw_version"
CONF_IDENTIFIERS = "identifiers"
CONF_ICON = "icon"
CONF_MODEL = "model"
CONF_WEB_PORT = "web_port"
CONF_NAME = "name"
CONF_SW_VERSION = "sw_version"
CONF_TOPIC_IN = "topic_in"
CONF_TOPIC_OUT = "topic_out"
CONF_TOPIC_LWT = "topic_lwt"

DEFAULT_TOPIC_IN = "/RFBridge/cmd"
DEFAULT_TOPIC_OUT = "/RFBridge/msg"
DEFAULT_TOPIC_LWT = "/RFBridge/lwt"
DEFAULT_WEB_PORT = 80
DEFAULT_WEB_USER = "rflink32"
DEFAULT_WEB_PASSWORD = "433mhz"

PLATFORMS = ["button", "sensor"]

SERVICE_SEND_NAMED = "send_named"
SERVICE_SEND_RAW = "send_raw"
