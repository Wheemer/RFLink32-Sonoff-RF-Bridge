"""Exercise RF Bridge OTA progress state without a running HA instance."""

from __future__ import annotations

import sys
from pathlib import Path

import homeassistant  # noqa: F401

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from custom_components.rf_bridge.update import RFBridgeFirmwareUpdate


def main() -> None:
    entity = object.__new__(RFBridgeFirmwareUpdate)
    writes = 0

    def write_state() -> None:
        nonlocal writes
        writes += 1

    entity.async_write_ha_state = write_state
    entity._set_install_progress(35)
    assert entity._attr_in_progress is True
    assert entity._attr_update_percentage == 35
    assert writes == 1

    entity._set_install_progress(35)
    assert writes == 1

    entity._set_install_progress(85)
    assert entity._attr_update_percentage == 85
    assert writes == 2

    entity._clear_install_progress()
    assert entity._attr_in_progress is False
    assert entity._attr_update_percentage is None
    assert writes == 3


if __name__ == "__main__":
    main()
    print("ota_update_progress=passed")
