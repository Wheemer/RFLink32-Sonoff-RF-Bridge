"""Exercise the RF Bridge firmware download view without an HA server."""

from __future__ import annotations

import asyncio
import hashlib
import sys
import tempfile
from pathlib import Path
from typing import ClassVar

# Home Assistant installs its bundled validation compatibility layer during
# package import. Mirror that order before importing the integration.
import homeassistant  # noqa: F401

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from custom_components.rf_bridge.update import FirmwareDownloadView


class _Request:
    """Minimal request shape used by FirmwareDownloadView.get."""

    remote: ClassVar[str] = "192.168.1.221"
    headers: ClassVar[dict[str, str]] = {
        "Accept-Encoding": "identity;q=1,chunked;q=0.1,*;q=0"
    }


async def _check() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        artifact = Path(temporary_directory) / "firmware.bin.gz.signed"
        payload = b"\x1f\x8b" + bytes(range(251)) * 1024 + b"signed-trailer"
        artifact.write_bytes(payload)

        view = FirmwareDownloadView()
        token = view.add(artifact)
        response = await view.get(_Request(), token)

        assert response.body == payload
        assert response.headers["Content-Length"] == str(len(payload))
        assert response.headers["Content-Encoding"] == "identity"
        assert response.headers["Content-Type"] == "application/octet-stream"

        audit = view.audit(token)
        assert audit is not None
        assert audit["requests"] == 1
        assert audit["artifact_bytes"] == len(payload)
        assert audit["artifact_sha256"] == hashlib.sha256(payload).hexdigest()


if __name__ == "__main__":
    asyncio.run(_check())
    print("firmware_download_view=passed")
