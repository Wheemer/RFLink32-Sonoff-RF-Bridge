from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUTTON = ROOT / "custom_components" / "rf_bridge" / "button.py"
EVENT = ROOT / "custom_components" / "rf_bridge" / "event.py"
INIT = ROOT / "custom_components" / "rf_bridge" / "__init__.py"


def require(source: str, fragment: str, label: str) -> None:
    if fragment not in source:
        raise SystemExit(f"Missing {label}: {fragment}")


button = BUTTON.read_text(encoding="utf-8")
event = EVENT.read_text(encoding="utf-8")
init = INIT.read_text(encoding="utf-8")

for source, label in ((button, "button"), (event, "event")):
    require(source, "from homeassistant.helpers.dispatcher import async_dispatcher_connect", label)
    require(source, "diagnostics_signal(self._hub.entry.entry_id)", label)
    require(source, "self.async_write_ha_state", label)

require(init, "async_call_later(hass, 0, lambda _: hub.async_write_diagnostics())", "startup state refresh")
