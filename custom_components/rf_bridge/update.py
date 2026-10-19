"""Signed firmware updates for the modified Sonoff RF Bridge target."""

from __future__ import annotations

import asyncio
import hashlib
import logging
import secrets
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import aiohttp
from aiohttp import web
from homeassistant.components.http import HomeAssistantView
from homeassistant.components.update import (
    UpdateDeviceClass,
    UpdateEntity,
    UpdateEntityFeature,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import RFBridgeHub, diagnostics_signal
from .const import CONF_SW_VERSION, DOMAIN

_LOGGER = logging.getLogger(__name__)

MANIFEST_URL = (
    "https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/"
    "releases/latest/download/manifest.json"
)
MANIFEST_TARGET = "modified_sonoff_rf_bridge"
MAX_FIRMWARE_BYTES = 450_000
DOWNLOAD_ROUTE = "/api/rf_bridge/firmware/{token}"
DOWNLOAD_VIEW_KEY = "firmware_download_view"
DOWNLOAD_VIEW_DATA_KEY = f"{DOMAIN}_{DOWNLOAD_VIEW_KEY}"
FIRMWARE_UPDATE_VERIFY_SECONDS = 45
BOOTSTRAP_UPDATE_VERIFY_SECONDS = 45
VERSION_CHARACTERS = "0123456789.-_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


@dataclass(frozen=True, slots=True)
class FirmwareRelease:
    """A verified release manifest advertised by this project."""

    version: str
    url: str
    sha256: str
    signing_key_id: str
    release_url: str | None
    release_notes: str | None
    bootstrap: BootstrapArtifact | None


@dataclass(frozen=True, slots=True)
class BootstrapArtifact:
    """A raw signed bridge image used only to migrate the legacy OTA core."""

    url: str
    sha256: str


def _parse_release(data: Any) -> FirmwareRelease:
    """Validate the narrowly scoped manifest consumed by this integration."""
    if not isinstance(data, dict) or data.get("target") != MANIFEST_TARGET:
        raise ValueError("manifest does not target the modified Sonoff RF Bridge")

    version = data.get("version")
    url = data.get("url")
    sha256 = data.get("sha256")
    signing_key_id = data.get("signing_key_id")
    if (
        not isinstance(version, str)
        or not version.strip()
        or any(character not in VERSION_CHARACTERS for character in version)
    ):
        raise ValueError("manifest version is invalid")
    parsed_url = urlparse(url) if isinstance(url, str) else None
    if not isinstance(url, str) or parsed_url.scheme != "https" or not parsed_url.netloc:
        raise ValueError("manifest firmware URL must use HTTPS")
    if (
        not isinstance(sha256, str)
        or len(sha256) != 64
        or any(character not in "0123456789abcdef" for character in sha256.lower())
    ):
        raise ValueError("manifest SHA-256 is invalid")
    if (
        not isinstance(signing_key_id, str)
        or len(signing_key_id) != 64
        or any(character not in "0123456789abcdef" for character in signing_key_id.lower())
    ):
        raise ValueError("manifest signing key ID is invalid")

    release_url = data.get("release_url")
    if not isinstance(release_url, str):
        release_url = None
    release_notes = data.get("release_notes")
    if not isinstance(release_notes, str):
        release_notes = None
    bootstrap = None
    bootstrap_data = data.get("bootstrap")
    if bootstrap_data is not None:
        bootstrap_url = bootstrap_data.get("url") if isinstance(bootstrap_data, dict) else None
        bootstrap_sha256 = bootstrap_data.get("sha256") if isinstance(bootstrap_data, dict) else None
        bootstrap_parsed = urlparse(bootstrap_url) if isinstance(bootstrap_url, str) else None
        if (
            not isinstance(bootstrap_url, str)
            or bootstrap_parsed.scheme != "https"
            or not bootstrap_parsed.netloc
            or not isinstance(bootstrap_sha256, str)
            or len(bootstrap_sha256) != 64
            or any(character not in "0123456789abcdef" for character in bootstrap_sha256.lower())
        ):
            raise ValueError("manifest bootstrap artifact is invalid")
        bootstrap = BootstrapArtifact(bootstrap_url, bootstrap_sha256.lower())

    return FirmwareRelease(
        version=version.strip(),
        url=url,
        sha256=sha256.lower(),
        signing_key_id=signing_key_id.lower(),
        release_url=release_url,
        release_notes=release_notes,
        bootstrap=bootstrap,
    )


class FirmwareDownloadView(HomeAssistantView):
    """Serve one staged, signed firmware artifact through a random local URL."""

    url = DOWNLOAD_ROUTE
    name = "api:rf_bridge:firmware"
    requires_auth = False

    def __init__(self) -> None:
        self._downloads: dict[str, Path] = {}
        self._download_count: dict[str, int] = {}
        self._download_audit: dict[str, dict[str, str | int]] = {}

    def add(self, artifact: Path) -> str:
        """Register an unguessable URL for a signed artifact."""
        token = secrets.token_urlsafe(32)
        self._downloads[token] = artifact
        self._download_count[token] = 0
        return token

    def discard(self, token: str) -> None:
        """Remove a staged download URL after a rejected update request."""
        self._downloads.pop(token, None)
        self._download_count.pop(token, None)
        self._download_audit.pop(token, None)

    def downloaded(self, token: str) -> bool:
        """Return whether the bridge requested this staged artifact."""
        return self._download_count.get(token, 0) > 0

    def audit(self, token: str) -> dict[str, str | int] | None:
        """Return the last request metadata for a staged artifact."""
        audit = self._download_audit.get(token)
        return dict(audit) if audit is not None else None

    async def get(self, request: web.Request, token: str) -> web.StreamResponse:
        """Return a registered signed artifact and nothing else."""
        artifact = self._downloads.get(token)
        if artifact is None or not artifact.is_file():
            raise web.HTTPNotFound()
        payload = await asyncio.to_thread(artifact.read_bytes)
        payload_sha256 = hashlib.sha256(payload).hexdigest()
        # OTA clients may retry a partial download. Keep the unguessable URL
        # live until the caller verifies the bridge version or reports failure.
        request_count = self._download_count.get(token, 0) + 1
        self._download_count[token] = request_count
        self._download_audit[token] = {
            "requests": request_count,
            "remote": request.remote or "",
            "accept_encoding": request.headers.get("Accept-Encoding", ""),
            "range": request.headers.get("Range", ""),
            "artifact_bytes": len(payload),
            "artifact_sha256": payload_sha256,
        }
        # A signed OTA payload must be sent byte-for-byte as staged. Avoid
        # FileResponse negotiation, range handling, and inferred encodings.
        return web.Response(
            body=payload,
            headers={
                "Cache-Control": "no-store",
                "Content-Encoding": "identity",
                "Content-Length": str(len(payload)),
                "Content-Type": "application/octet-stream",
            },
        )


def _get_download_view(hass: HomeAssistant) -> FirmwareDownloadView:
    # Keep process-wide HTTP view state out of the per-entry hub registry.
    # The registry is also used to resolve services and decide whether they
    # must be removed on unload.
    data = hass.data.setdefault(DOWNLOAD_VIEW_DATA_KEY, {})
    view = data.get(DOWNLOAD_VIEW_KEY)
    if isinstance(view, FirmwareDownloadView):
        return view
    view = FirmwareDownloadView()
    hass.http.register_view(view)
    data[DOWNLOAD_VIEW_KEY] = view
    return view


async def _async_fetch_release(hass: HomeAssistant) -> FirmwareRelease:
    session = async_get_clientsession(hass)
    timeout = aiohttp.ClientTimeout(total=20)
    async with session.get(MANIFEST_URL, timeout=timeout) as response:
        response.raise_for_status()
        return _parse_release(await response.json(content_type=None))


async def _async_stage_artifact(
    hass: HomeAssistant,
    entry: ConfigEntry,
    *,
    url: str,
    sha256: str,
    filename: str,
) -> Path:
    """Download and hash-check the exact signed artifact before exposing it."""
    session = async_get_clientsession(hass)
    timeout = aiohttp.ClientTimeout(total=90)
    async with session.get(url, timeout=timeout) as response:
        response.raise_for_status()
        content_length = response.content_length
        if content_length is None or content_length <= 0 or content_length > MAX_FIRMWARE_BYTES:
            raise HomeAssistantError("Firmware download has an invalid size")
        artifact = await response.read()

    if len(artifact) != content_length or len(artifact) > MAX_FIRMWARE_BYTES:
        raise HomeAssistantError("Firmware download was incomplete")
    if hashlib.sha256(artifact).hexdigest() != sha256:
        raise HomeAssistantError("Firmware download checksum did not match the release")

    directory = Path(hass.config.path(".storage", "rf_bridge_firmware"))
    path = directory / filename
    await hass.async_add_executor_job(directory.mkdir, 0o700, True, True)
    await hass.async_add_executor_job(path.write_bytes, artifact)
    return path


def _requires_bootstrap(installed_version: str | None, target_version: str) -> bool:
    """Return whether the installed 2.7.4-era updater needs migration first."""
    if not installed_version or installed_version == target_version:
        return False
    try:
        parts = tuple(int(part) for part in installed_version.split("."))
    except ValueError:
        return True
    return parts < (0, 7, 30)


def _local_download_url(hass: HomeAssistant, token: str) -> str:
    base_url = hass.config.internal_url
    parsed = urlparse(base_url)
    if parsed.scheme != "http" or not parsed.netloc:
        raise HomeAssistantError(
            "RF Bridge updates require an HTTP internal URL reachable by the bridge"
        )
    return f"{base_url.rstrip('/')}{DOWNLOAD_ROUTE.format(token=token)}"


class RFBridgeFirmwareUpdate(UpdateEntity):
    """Install RSA-signed firmware through the bridge's local API."""

    _attr_has_entity_name = True
    _attr_device_class = UpdateDeviceClass.FIRMWARE
    _attr_supported_features = (
        UpdateEntityFeature.INSTALL | UpdateEntityFeature.RELEASE_NOTES
    )
    _attr_title = "RF Bridge firmware"
    # Home Assistant's update platform performs this lightweight manifest check
    # on its normal update cadence; firmware bytes are downloaded only on install.
    _attr_should_poll = True

    def __init__(self, hub: RFBridgeHub) -> None:
        self._hub = hub
        self._release: FirmwareRelease | None = None
        self._refresh_lock = asyncio.Lock()
        self._install_lock = asyncio.Lock()
        self._attr_unique_id = f"{hub.entry.entry_id}_firmware_update"

    @property
    def available(self) -> bool:
        return self._hub.available

    @property
    def device_info(self) -> dict[str, Any]:
        return self._hub.device_info

    @property
    def installed_version(self) -> str | None:
        value = self._hub.status.get(CONF_SW_VERSION) or self._hub.entry.data.get(
            CONF_SW_VERSION
        )
        return value if isinstance(value, str) else None

    @property
    def latest_version(self) -> str | None:
        return self._release.version if self._release else None

    @property
    def release_url(self) -> str | None:
        return self._release.release_url if self._release else None

    async def async_release_notes(self) -> str | None:
        return self._release.release_notes if self._release else None

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(
            async_dispatcher_connect(
                self.hass,
                diagnostics_signal(self._hub.entry.entry_id),
                self.async_write_ha_state,
            )
        )
        self.hass.async_create_task(self._async_refresh_release())

    async def async_update(self) -> None:
        await self._async_refresh_release()

    async def _async_refresh_release(self) -> None:
        async with self._refresh_lock:
            try:
                self._release = await _async_fetch_release(self.hass)
            except (aiohttp.ClientError, TimeoutError, ValueError) as err:
                self._release = None
                _LOGGER.debug("Could not fetch RF Bridge firmware manifest: %s", err)
            self.async_write_ha_state()

    async def _async_wait_for_version(self, version: str) -> bool:
        """Wait for and directly verify the bridge after a signed update."""
        deadline = asyncio.get_running_loop().time() + FIRMWARE_UPDATE_VERIFY_SECONDS
        next_probe = 0.0
        while asyncio.get_running_loop().time() < deadline:
            if self._hub.status.get(CONF_SW_VERSION) == version:
                return True
            now = asyncio.get_running_loop().time()
            if now >= next_probe:
                next_probe = now + 2.0
                try:
                    status = await self._hub.async_probe_status()
                    if status.get(CONF_SW_VERSION) == version:
                        return True
                except (aiohttp.ClientError, ValueError) as err:
                    _LOGGER.debug("RF Bridge has not returned from OTA yet: %s", err)
            await asyncio.sleep(1)
        return self._hub.status.get(CONF_SW_VERSION) == version

    async def _async_wait_for_bootstrap(self) -> bool:
        """Wait until the short-lived Core 3 bootstrap has joined Wi-Fi."""
        deadline = asyncio.get_running_loop().time() + BOOTSTRAP_UPDATE_VERIFY_SECONDS
        session = async_get_clientsession(self.hass)
        url = f"http://{self._hub.host}:{self._hub.api_port}/api/bootstrap/status"
        while asyncio.get_running_loop().time() < deadline:
            try:
                async with session.get(url, timeout=aiohttp.ClientTimeout(total=5)) as response:
                    if response.status == 200:
                        status = await response.json(content_type=None)
                        if status.get("mode") == "signed_ota_bootstrap":
                            return True
            except aiohttp.ClientError:
                pass
            await asyncio.sleep(1)
        return False

    async def _async_schedule_bootstrap_final(self, url: str) -> None:
        """Ask the Core 3 bootstrap to fetch the final signed gzip image."""
        session = async_get_clientsession(self.hass)
        endpoint = f"http://{self._hub.host}:{self._hub.api_port}/api/bootstrap/update"
        async with session.post(
            endpoint, params={"url": url}, timeout=aiohttp.ClientTimeout(total=15)
        ) as response:
            if response.status != 202:
                detail = await response.text()
                raise HomeAssistantError(
                    f"RF Bridge bootstrap rejected final firmware: {detail}"
                )

    async def async_install(
        self, version: str | None, backup: bool, **kwargs: Any
    ) -> None:
        """Stage a release locally, then schedule its signed bridge fetch."""
        del backup, kwargs
        async with self._install_lock:
            release = await _async_fetch_release(self.hass)
            if version is not None and version != release.version:
                raise HomeAssistantError("Requested RF Bridge firmware version is unavailable")
            device_key_id = self._hub.status.get("update_signing_key_id")
            if not isinstance(device_key_id, str) or not device_key_id:
                raise HomeAssistantError(
                    "RF Bridge firmware does not report its OTA signing key; "
                    "a compatible bootstrap firmware is required before OTA updates"
                )
            if device_key_id.lower() != release.signing_key_id:
                raise HomeAssistantError(
                    "RF Bridge OTA signing key does not match this release; "
                    "the update was not started"
                )
            view = _get_download_view(self.hass)
            final_artifact = await _async_stage_artifact(
                self.hass,
                self._hub.entry,
                url=release.url,
                sha256=release.sha256,
                filename=f"{self._hub.entry.entry_id}-{release.version}.bin.gz.signed",
            )
            token = view.add(final_artifact)
            disconnected_after_schedule = False
            try:
                if _requires_bootstrap(self.installed_version, release.version):
                    if release.bootstrap is None:
                        raise HomeAssistantError(
                            "This RF Bridge requires a signed OTA bootstrap, but the release does not provide one"
                        )
                    bootstrap_artifact = await _async_stage_artifact(
                        self.hass,
                        self._hub.entry,
                        url=release.bootstrap.url,
                        sha256=release.bootstrap.sha256,
                        filename=f"{self._hub.entry.entry_id}-{release.version}.bootstrap.bin.signed",
                    )
                    bootstrap_token = view.add(bootstrap_artifact)
                    try:
                        await self._hub.async_schedule_firmware_update(
                            _local_download_url(self.hass, bootstrap_token)
                        )
                    except aiohttp.ClientError as err:
                        if "WebSocket disconnected" not in str(err):
                            raise
                        disconnected_after_schedule = True
                    if not await self._async_wait_for_bootstrap():
                        raise HomeAssistantError("RF Bridge bootstrap did not return on Wi-Fi")
                    await self._async_schedule_bootstrap_final(_local_download_url(self.hass, token))
                    view.discard(bootstrap_token)
                else:
                    await self._hub.async_schedule_firmware_update(
                        _local_download_url(self.hass, token)
                    )
            except aiohttp.ClientError as err:
                if "WebSocket disconnected" not in str(err):
                    view.discard(token)
                    raise
                # The bridge starts the fetch immediately after accepting the
                # request, which legitimately closes its WebSocket on reboot.
                # Keep the staged URL alive through that transition.
                disconnected_after_schedule = True
                _LOGGER.debug(
                    "RF Bridge disconnected while starting firmware update; "
                    "waiting for version %s",
                    release.version,
                )
            except Exception:
                view.discard(token)
                raise

            if not await self._async_wait_for_version(release.version):
                update_detail: list[str] = []
                try:
                    status = await self._hub.async_probe_status()
                    result = status.get("update_last_result")
                    if isinstance(result, str) and result:
                        update_detail.append(f"bridge result={result}")
                    error_code = status.get("update_last_error_code")
                    if isinstance(error_code, int) and error_code:
                        update_detail.append(f"error_code={error_code}")
                    reported_error = status.get("update_error")
                    if isinstance(reported_error, str) and reported_error:
                        update_detail.append(reported_error)
                except (aiohttp.ClientError, HomeAssistantError, ValueError) as err:
                    _LOGGER.debug(
                        "Could not obtain RF Bridge update failure detail: %s", err
                    )
                downloaded = view.downloaded(token)
                request_audit = view.audit(token)
                if request_audit is not None:
                    _LOGGER.debug(
                        "RF Bridge staged firmware request audit: %s", request_audit
                    )
                view.discard(token)
                reason = (
                    "after scheduling the update"
                    if disconnected_after_schedule
                    else "while waiting for the update"
                )
                if not downloaded:
                    reason = f"{reason}; the bridge never requested the staged artifact"
                if update_detail:
                    reason = f"{reason}: {'; '.join(update_detail)}"
                raise HomeAssistantError(
                    f"RF Bridge did not report firmware {release.version} {reason}"
                )
            self._release = release
            self.async_write_ha_state()
            view.discard(token)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the update entity for one modified RF Bridge."""
    hub: RFBridgeHub = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([RFBridgeFirmwareUpdate(hub)])
