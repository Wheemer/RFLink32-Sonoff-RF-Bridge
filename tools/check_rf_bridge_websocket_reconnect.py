"""Guard the persistent RF Bridge WebSocket against stale sessions."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "custom_components" / "rf_bridge" / "__init__.py"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    required = (
        "WS_HEARTBEAT_SECONDS = 15",
        "heartbeat=WS_HEARTBEAT_SECONDS",
        "await asyncio.sleep(WS_RECONNECT_SECONDS)",
    )
    missing = [fragment for fragment in required if fragment not in source]
    if missing:
        raise SystemExit(
            "Persistent RF Bridge WebSocket reconnect guard is incomplete: "
            + ", ".join(missing)
        )


if __name__ == "__main__":
    main()
