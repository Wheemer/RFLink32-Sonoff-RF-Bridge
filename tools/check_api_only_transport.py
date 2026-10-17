"""Fail CI if the retired MQTT transport is reintroduced."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

RETIRED_PATHS = (
    "RFLink/6_Credentials.h",
    "RFLink/6_MQTT.cpp",
    "RFLink/6_MQTT.h",
    "custom_components/rf_bridge_to_mqtt",
)

RETIRED_TOKENS = (
    "PubSubClient",
    "6_MQTT",
    "Mqtt::",
    "setup_MQTT",
    "checkMQTT",
    "RFLINK_MQTT",
    "RFLink_default_MQTT",
    "MQTT_ENABLED",
)

SOURCE_PATHS = (
    ROOT / "RFLink",
    ROOT / "platformio.ini",
    ROOT / "custom_components" / "rf_bridge",
)

SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".ini", ".json", ".py"}


def iter_source_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    return [
        candidate
        for candidate in path.rglob("*")
        if candidate.is_file() and candidate.suffix in SOURCE_SUFFIXES
    ]


def main() -> None:
    failures: list[str] = []

    for relative_path in RETIRED_PATHS:
        if (ROOT / relative_path).exists():
            failures.append(f"retired MQTT path exists: {relative_path}")

    for source_path in SOURCE_PATHS:
        for candidate in iter_source_files(source_path):
            content = candidate.read_text(encoding="utf-8")
            for token in RETIRED_TOKENS:
                if token in content:
                    failures.append(
                        f"retired MQTT token {token!r} in "
                        f"{candidate.relative_to(ROOT)}"
                    )

    if failures:
        raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
