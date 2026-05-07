#!/usr/bin/env python3

"""Send OTA firmware updates to a Homie device over MQTT.

This helper speaks the OTA contract implemented by the maintained
`homie-esp8266` fork used in the LSH stack:

- it waits for the device to be online and ready
- it reads the current `$fw/checksum`
- it checks `$implementation/ota/enabled`
- it publishes the firmware to `$implementation/ota/firmware/<md5>`
- it follows `$implementation/ota/status` until the device reboots
- it verifies the final `$fw/checksum` after the device is back online

The script keeps the historical OTA topic contract and firmware publish
semantics intact, but hardens the control flow around timeouts, terminal
errors and MQTT reconnects.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import sys
import threading
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol, TextIO, TypeAlias, cast

from paho.mqtt import client as mqtt


class _TomlModule(Protocol):
    """Small protocol for Python 3.11+'s tomllib module."""

    def loads(self, data: str, /) -> object:
        """Parse a TOML document."""


class _ReasonCode(Protocol):
    """Small protocol for Paho 2.x reason-code objects."""

    value: int


_TOML_MODULE: _TomlModule | None
try:
    import tomllib as _tomllib
except ImportError:  # pragma: no cover - Python < 3.11 can still use JSON configs.
    _TOML_MODULE = None
else:
    _TOML_MODULE = _tomllib

JsonValue: TypeAlias = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
Config: TypeAlias = dict[str, JsonValue]
ConfigAlias: TypeAlias = str | tuple[str, str]

DEFAULT_BROKER_HOST = "127.0.0.1"
DEFAULT_BROKER_PORT = 1883
DEFAULT_BASE_TOPIC = "homie/"
DEFAULT_KEEPALIVE_SECONDS = 60
DEFAULT_TIMEOUT_SECONDS = 300
DEFAULT_CLIENT_ID_PREFIX = "homie-ota-updater"
DEFAULT_HOMIE_VERSION = "3"
SUPPORTED_HOMIE_VERSIONS = ("3", "4", "5")
PROGRESS_BAR_WIDTH = 30
MD5_HEX_LENGTH = 32
STATUS_PAYLOAD_PARTS = 2
HTTP_OK = 200
HTTP_ACCEPTED = 202
HTTP_PROGRESS = 206
HTTP_NOT_MODIFIED = 304
HTTP_FORBIDDEN = 403
HTTP_ERROR_MIN = 400
HTTP_ERROR_MAX = 600
JSON_CONFIG_SUFFIX = ".json"
TOML_CONFIG_SUFFIX = ".toml"
TRUE_TEXTS = frozenset(("1", "true", "yes", "on"))
FALSE_TEXTS = frozenset(("0", "false", "no", "off"))
CONFIG_ALIASES: Mapping[str, tuple[ConfigAlias, ...]] = {
    "broker_host": ("broker_host", ("broker", "host")),
    "broker_port": ("broker_port", ("broker", "port")),
    "broker_username": ("broker_username", ("broker", "username")),
    "broker_username_env": ("broker_username_env", ("broker", "username_env")),
    "broker_password": ("broker_password", ("broker", "password")),
    "broker_password_env": ("broker_password_env", ("broker", "password_env")),
    "broker_tls_cacert": ("broker_tls_cacert", ("broker", "tls_cacert")),
    "broker_tls_certfile": ("broker_tls_certfile", ("broker", "tls_certfile")),
    "broker_tls_keyfile": ("broker_tls_keyfile", ("broker", "tls_keyfile")),
    "broker_tls_insecure": ("broker_tls_insecure", ("broker", "tls_insecure")),
    "base_topic": ("base_topic", ("homie", "base_topic")),
    "homie_version": ("homie_version", ("homie", "version")),
    "client_id": ("client_id", ("ota", "client_id")),
    "expected_md5": ("expected_md5", ("ota", "expected_md5")),
    "timeout": ("timeout", ("ota", "timeout")),
}


class OTAConfigError(ValueError):
    """Raised when a JSON/TOML OTA config file is not usable."""


@dataclass(frozen=True)
class OTASettings:
    """User-provided MQTT and Homie settings for one OTA session."""

    broker_host: str
    broker_port: int
    broker_username: str | None
    broker_password: str | None
    broker_ca_cert: str | None
    broker_tls_certfile: str | None
    broker_tls_keyfile: str | None
    broker_tls_insecure: bool
    base_topic: str
    device_id: str
    client_id: str | None
    timeout_seconds: int
    homie_version: str = DEFAULT_HOMIE_VERSION


def _write_line(message: str = "", *, stream: TextIO | None = None) -> None:
    output = stream if stream is not None else sys.stdout
    output.write(f"{message}\n")
    output.flush()


def _write_text(message: str) -> None:
    sys.stdout.write(message)
    sys.stdout.flush()


def _md5_hexdigest(content: bytes) -> str:
    return hashlib.md5(content, usedforsecurity=False).hexdigest()


def base_topic_ends_with_segment(value: str, segment: str) -> bool:
    """Return True when the normalized topic already ends with a path segment."""
    stripped = str(value).rstrip("/")
    return stripped.endswith(f"/{segment}")


def normalize_base_topic(value: str, homie_version: str = DEFAULT_HOMIE_VERSION) -> str:
    """Normalize the MQTT root for the selected Homie convention generation."""
    value = str(value)
    if not value.endswith("/"):
        value = f"{value}/"
    if str(homie_version) == "5" and not base_topic_ends_with_segment(value, "5"):
        value = f"{value}5/"
    return value


def mqtt_error_name(code: int) -> str:
    """Return a stable human-readable MQTT client error string."""
    return mqtt.error_string(code) if hasattr(mqtt, "error_string") else str(code)


def is_valid_md5(value: str) -> bool:
    """Return True when the payload looks like a hexadecimal MD5 digest."""
    return len(value) == MD5_HEX_LENGTH and all(
        character in "0123456789abcdefABCDEF" for character in value
    )


def md5_digest(value: str) -> str:
    """Argparse type that normalizes and validates an MD5 digest."""
    normalized = value.strip().lower()
    if not is_valid_md5(normalized):
        raise argparse.ArgumentTypeError("value must be a 32-character hexadecimal MD5 digest")
    return normalized


def positive_int(value: str) -> int:
    """Argparse type that rejects zero and negative integers."""
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be a positive integer")
    return parsed


def load_config(path: Path) -> Config:
    """Load optional JSON or TOML defaults for the OTA command."""
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise RuntimeError(f"failed to read config file {path}: {exc}") from exc

    try:
        suffix = path.suffix.lower()
        if suffix == JSON_CONFIG_SUFFIX:
            loaded: object = json.loads(raw.decode("utf-8"))
        elif suffix == TOML_CONFIG_SUFFIX:
            if _TOML_MODULE is None:
                raise OTAConfigError("TOML config files require Python 3.11+")
            loaded = _TOML_MODULE.loads(raw.decode("utf-8"))
        else:
            raise OTAConfigError("config file must use .json or .toml")
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise RuntimeError(f"failed to parse config file {path}: {exc}") from exc

    if not isinstance(loaded, dict):
        raise OTAConfigError(f"config file {path} must contain a JSON/TOML object")
    return cast("Config", loaded)


def config_defaults(config: Mapping[str, JsonValue]) -> Config:
    """Flatten supported config-file keys into argparse option names."""
    defaults: Config = {}
    for name, aliases in CONFIG_ALIASES.items():
        for alias in aliases:
            found, value = _config_value(config, alias)
            if found:
                defaults[name] = value
                break
    return defaults


def _config_value(config: Mapping[str, JsonValue], alias: ConfigAlias) -> tuple[bool, JsonValue]:
    if isinstance(alias, str):
        return alias in config, config.get(alias)

    current: JsonValue | Mapping[str, JsonValue] = config
    for key in alias:
        if not isinstance(current, dict) or key not in current:
            return False, None
        current = current[key]
    return True, cast("JsonValue", current)


def _optional_text(value: JsonValue, option_name: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise argparse.ArgumentTypeError(f"{option_name} must be a string")
    return value


def _optional_positive_int(value: JsonValue, option_name: str) -> int | None:
    if value is None:
        return None
    try:
        return positive_int(str(value))
    except (TypeError, ValueError, argparse.ArgumentTypeError) as exc:
        raise argparse.ArgumentTypeError(f"{option_name} must be a positive integer") from exc


def _optional_bool(value: JsonValue, option_name: str) -> bool | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in TRUE_TEXTS:
            return True
        if normalized in FALSE_TEXTS:
            return False
    raise argparse.ArgumentTypeError(f"{option_name} must be a boolean")


def _env_value(
    env_name: str | None,
    option_name: str,
    parser: argparse.ArgumentParser,
) -> str | None:
    if env_name is None:
        return None
    value = os.environ.get(env_name)
    if value is None:
        parser.error(f"{option_name} environment variable {env_name!r} is not set")
    return value


class OTAUpdater:
    """Own the OTA session state and MQTT callbacks."""

    def __init__(self, settings: OTASettings, firmware: bytes) -> None:
        """Create an OTA updater for one device and one firmware image."""
        self.broker_host = settings.broker_host
        self.broker_port = settings.broker_port
        self.broker_username = settings.broker_username
        self.broker_password = settings.broker_password
        self.broker_ca_cert = settings.broker_ca_cert
        self.broker_tls_certfile = settings.broker_tls_certfile
        self.broker_tls_keyfile = settings.broker_tls_keyfile
        self.broker_tls_insecure = settings.broker_tls_insecure
        self.homie_version = str(settings.homie_version)
        self.base_topic = normalize_base_topic(settings.base_topic, self.homie_version)
        self.device_id = settings.device_id
        self.client_id = settings.client_id or f"{DEFAULT_CLIENT_ID_PREFIX}-{settings.device_id}"
        self.firmware = firmware
        self.firmware_md5 = _md5_hexdigest(firmware)
        self.timeout_seconds = settings.timeout_seconds

        self._client: mqtt.Client | None = None
        self._done = threading.Event()
        self._success = False

        self._published = False
        self._ota_enabled: bool | None = None
        self._old_md5: str | None = None
        self._upload_total = 0
        self._waiting_for_reboot = False
        self._ready_for_post_upload_checksum = False
        self._info_topics_subscribed = False
        self._connection_lost_logged = False

    def run(self) -> int:
        """Run the OTA session and return a process exit code."""
        client = self._create_client()
        self._client = client

        try:
            self._configure_auth_and_tls(client)
            _write_line(f"Connecting to mqtt broker {self.broker_host} on port {self.broker_port}")
            client.connect(self.broker_host, self.broker_port, DEFAULT_KEEPALIVE_SECONDS)
            client.loop_start()
        except Exception as exc:  # noqa: BLE001 - CLI boundary for MQTT/TLS setup errors.
            self._finish(success=False, message=f"Failed to prepare or connect MQTT client: {exc}")
            return 1

        try:
            finished = self._done.wait(self.timeout_seconds)
            if not finished:
                self._finish_timeout()
        finally:
            with contextlib.suppress(Exception):
                client.disconnect()
            client.loop_stop()

        return 0 if self._success else 1

    @property
    def _state_topic(self) -> str:
        return f"{self.base_topic}{self.device_id}/$state"

    @property
    def _online_topic(self) -> str:
        return f"{self.base_topic}{self.device_id}/$online"

    @property
    def _status_topic(self) -> str:
        return f"{self.base_topic}{self.device_id}/$implementation/ota/status"

    @property
    def _ota_enabled_topic(self) -> str:
        return f"{self.base_topic}{self.device_id}/$implementation/ota/enabled"

    @property
    def _fw_checksum_topic(self) -> str:
        return f"{self.base_topic}{self.device_id}/$fw/checksum"

    @property
    def _firmware_topic(self) -> str:
        return f"{self.base_topic}{self.device_id}/$implementation/ota/firmware/{self.firmware_md5}"

    def _create_client(self) -> mqtt.Client:
        """Create a Paho client compatible with both 1.x and 2.x."""
        if hasattr(mqtt, "CallbackAPIVersion"):
            client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
                client_id=self.client_id,
            )
        else:
            client = mqtt.Client(client_id=self.client_id)

        if hasattr(client, "reconnect_delay_set"):
            client.reconnect_delay_set(min_delay=1, max_delay=5)

        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        return client

    def _configure_auth_and_tls(self, client: mqtt.Client) -> None:
        """Apply optional MQTT authentication and TLS settings to a client."""
        if self.broker_username is not None:
            client.username_pw_set(self.broker_username, self.broker_password)

        tls_requested = any(
            (
                self.broker_ca_cert,
                self.broker_tls_certfile,
                self.broker_tls_keyfile,
                self.broker_tls_insecure,
            )
        )
        if not tls_requested:
            return

        client.tls_set(
            ca_certs=self.broker_ca_cert,
            certfile=self.broker_tls_certfile,
            keyfile=self.broker_tls_keyfile,
        )
        if self.broker_tls_insecure:
            client.tls_insecure_set(value=True)

    def _finish(self, *, success: bool, message: str) -> None:
        """Finish the session exactly once."""
        if self._done.is_set():
            return

        _write_line(message)
        self._success = success
        self._done.set()

    def _finish_timeout(self) -> None:
        if self._waiting_for_reboot:
            self._finish(
                success=False,
                message=(
                    f"Timed out after {self.timeout_seconds}s while waiting for the device "
                    "to reboot and report the new checksum."
                ),
            )
            return

        if self._published:
            self._finish(
                success=False,
                message=(
                    f"Timed out after {self.timeout_seconds}s while waiting for OTA completion."
                ),
            )
            return

        self._finish(
            success=False,
            message=(
                f"Timed out after {self.timeout_seconds}s while waiting for the device "
                "to become ready."
            ),
        )

    def _require_client(self) -> mqtt.Client:
        if self._client is None:
            raise RuntimeError("MQTT client is not available")
        return self._client

    def _subscribe_or_finish(self, topic: str) -> bool:
        """Subscribe to a topic and fail fast if the client rejects it."""
        result, _mid = self._require_client().subscribe(topic)
        if result != mqtt.MQTT_ERR_SUCCESS:
            self._finish(
                success=False,
                message=f"Failed to subscribe to {topic!r}: {mqtt_error_name(result)}",
            )
            return False
        return True

    def _subscribe_device_info_topics(self) -> bool:
        """Subscribe to the retained topics needed to decide and verify the OTA."""
        if self._info_topics_subscribed:
            return True

        if not self._subscribe_or_finish(self._status_topic):
            return False
        if not self._subscribe_or_finish(self._ota_enabled_topic):
            return False
        if not self._subscribe_or_finish(self._fw_checksum_topic):
            return False

        self._info_topics_subscribed = True
        _write_line("Waiting for device info...")
        return True

    def _maybe_publish_firmware(self) -> None:
        """Publish the firmware once the device checksum and OTA flag are known."""
        if self._published:
            return
        if self._ota_enabled is not True:
            return
        if self._old_md5 is None:
            return

        self._published = True

        # Use QoS 1 to make the firmware publish at-least-once end-to-end.
        # The maintained homie-esp8266 fork now hardens OTA handling against
        # duplicate delivery and overlapping retransmits.
        _write_line(f"Publishing new firmware with checksum {self.firmware_md5}")
        info = self._require_client().publish(
            self._firmware_topic, self.firmware, qos=1, retain=False
        )
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            self._finish(
                success=False,
                message=f"Failed to publish firmware payload: {mqtt_error_name(info.rc)}",
            )

    def _print_progress(self, progress: int, total: int) -> None:
        """Render an in-place progress bar."""
        if total <= 0:
            return

        filled = int(PROGRESS_BAR_WIDTH * (progress / float(total)))
        bar = "+" * filled
        padding = " " * (PROGRESS_BAR_WIDTH - filled)
        _write_text(f"\r[{bar}{padding}] {progress}")
        if progress >= total:
            _write_line()

    def _parse_status_payload(self, payload: str) -> tuple[int, str]:
        """Split a status payload into its numeric code and optional detail."""
        parts = payload.strip().split(None, 1)
        if not parts:
            raise ValueError("empty status payload")
        return int(parts[0]), parts[1] if len(parts) == STATUS_PAYLOAD_PARTS else ""

    def _parse_progress_payload(self, payload: str) -> tuple[int, int]:
        """Parse `<written>/<total>` progress values from a 206 payload."""
        written_text, separator, total_text = payload.partition("/")
        if separator != "/":
            raise ValueError(f"invalid OTA progress payload: {payload!r}")
        written = int(written_text)
        total = int(total_text)
        if total <= 0:
            raise ValueError("OTA progress total must be positive")
        if written < 0:
            raise ValueError("OTA progress cannot be negative")
        if written > total:
            raise ValueError("OTA progress cannot exceed the total")
        return written, total

    def _handle_status(self, payload: str) -> None:
        """Handle `$implementation/ota/status` updates."""
        try:
            status, detail = self._parse_status_payload(payload)
        except ValueError as exc:
            self._finish(
                success=False,
                message=f"Received malformed OTA status payload {payload!r}: {exc}",
            )
            return

        if not self._published:
            return

        if status == HTTP_PROGRESS:
            self._handle_progress_status(payload, detail)
            return
        if status == HTTP_OK:
            self._handle_upload_complete_status()
            return
        if status == HTTP_ACCEPTED:
            _write_line("Checksum accepted")
            return

        self._handle_terminal_status(status, payload)

    def _handle_progress_status(self, payload: str, detail: str) -> None:
        try:
            progress, total = self._parse_progress_payload(detail)
        except ValueError as exc:
            self._finish(
                success=False,
                message=f"Received malformed OTA progress payload {payload!r}: {exc}",
            )
            return
        self._upload_total = total
        self._print_progress(progress, total)

    def _handle_upload_complete_status(self) -> None:
        if self._upload_total > 0:
            self._print_progress(self._upload_total, self._upload_total)
        self._waiting_for_reboot = True
        _write_line("Firmware uploaded successfully. Waiting for device to come back online.")

    def _handle_terminal_status(self, status: int, payload: str) -> None:
        if status == HTTP_NOT_MODIFIED:
            self._finish(
                success=True,
                message=(
                    f"Device firmware already up to date with md5 checksum: {self.firmware_md5}"
                ),
            )
            return

        if status == HTTP_FORBIDDEN:
            self._finish(success=False, message="Device OTA disabled, aborting...")
            return

        if HTTP_ERROR_MIN <= status < HTTP_ERROR_MAX:
            self._finish(success=False, message=f"Device reported OTA failure: {payload}")
            return

        self._finish(success=False, message=f"Device reported unexpected OTA status: {payload}")

    def _handle_checksum(self, payload: str) -> None:
        """Handle `$fw/checksum` and decide whether to start or validate the OTA."""
        checksum = payload.strip()
        if not is_valid_md5(checksum):
            self._finish(
                success=False, message=f"Received malformed $fw/checksum payload: {payload!r}"
            )
            return

        if not self._published:
            if checksum == self.firmware_md5:
                self._finish(
                    success=True,
                    message=f"Device firmware already up to date with md5 checksum: {checksum}",
                )
                return

            self._old_md5 = checksum
            self._maybe_publish_firmware()
            return

        if checksum == self.firmware_md5:
            self._finish(success=True, message="Device back online. Update successful!")
            return

        if not self._ready_for_post_upload_checksum:
            return

        self._finish(
            success=False,
            message=f"Expecting checksum {self.firmware_md5}, got {checksum}, update failed!",
        )

    def _handle_ota_enabled(self, payload: str) -> None:
        """Handle `$implementation/ota/enabled`."""
        normalized = payload.strip().lower()
        if normalized not in ("true", "false"):
            self._finish(
                success=False, message=f"Received malformed OTA enabled payload: {payload!r}"
            )
            return

        enabled = normalized == "true"
        self._ota_enabled = enabled

        if not enabled and not self._published:
            self._finish(success=False, message="Device OTA disabled, aborting...")
            return

        self._maybe_publish_firmware()

    def _handle_device_ready(self, topic: str, payload: str) -> None:
        """Handle `$state` and legacy `$online` readiness topics."""
        payload = payload.strip()

        if topic == self._state_topic and payload != "ready":
            return
        if topic == self._online_topic and payload.lower() != "true":
            return

        if not self._subscribe_device_info_topics():
            return

        if self._waiting_for_reboot:
            self._ready_for_post_upload_checksum = True

    def _on_connect(
        self,
        _client: mqtt.Client,
        _userdata: object,
        _flags: object,
        rc: object,
        _properties: object | None = None,
    ) -> None:
        """Subscribe to device liveness topics once the broker connection is up."""
        code = _mqtt_code(rc)
        if code != mqtt.MQTT_ERR_SUCCESS:
            self._finish(success=False, message=f"Connection failed with result code {code}")
            return

        self._connection_lost_logged = False
        self._info_topics_subscribed = False

        _write_line(f"Connected with result code {code}")

        if not self._subscribe_or_finish(self._state_topic):
            return
        if not self._subscribe_or_finish(self._online_topic):
            return

        _write_line("Waiting for device to come online...")

    def _on_disconnect(
        self,
        _client: mqtt.Client,
        _userdata: object,
        rc: object,
        _properties: object | None = None,
    ) -> None:
        """Keep the user informed when the MQTT session drops unexpectedly."""
        code = _mqtt_code(rc)
        if self._done.is_set():
            return

        self._info_topics_subscribed = False
        if code != mqtt.MQTT_ERR_SUCCESS and not self._connection_lost_logged:
            self._connection_lost_logged = True
            _write_line(f"MQTT connection lost ({code}). Waiting for reconnection...")

    def _on_message(self, _client: mqtt.Client, _userdata: object, msg: mqtt.MQTTMessage) -> None:
        """Route incoming MQTT messages to the relevant handler."""
        payload = msg.payload.decode("utf-8", errors="replace")

        if msg.topic == self._status_topic:
            self._handle_status(payload)
        elif msg.topic == self._fw_checksum_topic:
            self._handle_checksum(payload)
        elif msg.topic == self._ota_enabled_topic:
            self._handle_ota_enabled(payload)
        elif msg.topic in {self._state_topic, self._online_topic}:
            self._handle_device_ready(msg.topic, payload)


def _mqtt_code(value: object) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value)
    return int(cast("_ReasonCode", value).value)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    """Parse CLI arguments and optional JSON/TOML defaults."""
    parser = argparse.ArgumentParser(
        description=(
            "Send an OTA firmware update to a Homie device that implements the "
            "homie-esp8266 OTA MQTT contract."
        )
    )

    parser.add_argument(
        "--config",
        type=Path,
        default=None,
        help=("optional JSON or TOML defaults file. CLI arguments override values from the file."),
    )
    parser.add_argument("-l", "--broker-host", help="host name or IP address of the MQTT broker")
    parser.add_argument("-p", "--broker-port", type=positive_int, help="port of the MQTT broker")
    parser.add_argument(
        "--broker-username-env",
        help="environment variable containing the broker username",
    )
    parser.add_argument(
        "-u",
        "--broker-username",
        help="username used to authenticate with the MQTT broker",
    )
    parser.add_argument(
        "--broker-password-env",
        help="environment variable containing the broker password",
    )
    parser.add_argument(
        "-d",
        "--broker-password",
        help="password used to authenticate with the MQTT broker",
    )
    parser.add_argument(
        "-t",
        "--base-topic",
        help=(
            "base topic/domain of the Homie devices on the broker. "
            "With --homie-version 5, the updater appends the required /5/ segment "
            "unless it is already present."
        ),
    )
    parser.add_argument(
        "--homie-version",
        "--convention-version",
        choices=SUPPORTED_HOMIE_VERSIONS,
        help=(
            "Homie convention generation used by the target firmware. "
            "Use 5 for devices built with HOMIE_CONVENTION_VERSION=5."
        ),
    )
    parser.add_argument("-i", "--device-id", required=True, help="Homie device id")
    parser.add_argument(
        "--broker-tls-cacert",
        help=(
            "CA certificate bundle used to validate TLS connections. "
            "If set, TLS is enabled on the broker connection."
        ),
    )
    parser.add_argument(
        "--broker-tls-certfile",
        help="client certificate file used for mutual TLS authentication",
    )
    parser.add_argument(
        "--broker-tls-keyfile",
        help="private key file used with --broker-tls-certfile",
    )
    parser.add_argument(
        "--broker-tls-insecure",
        action="store_true",
        default=None,
        help=(
            "enable TLS but skip broker certificate verification. "
            "Use only for temporary tests with private brokers."
        ),
    )
    parser.add_argument(
        "--client-id",
        help=(
            "MQTT client id used by the updater. "
            f"Defaults to {DEFAULT_CLIENT_ID_PREFIX}-<device-id>."
        ),
    )
    parser.add_argument(
        "--expected-md5",
        type=md5_digest,
        help="expected firmware MD5; aborts before publishing if the file does not match",
    )
    parser.add_argument(
        "--timeout",
        type=positive_int,
        help="maximum time in seconds to wait for the OTA workflow to complete",
    )
    parser.add_argument(
        "firmware",
        type=Path,
        help="path to the firmware binary to send to the device",
    )

    parser._optionals.title = "arguments"  # noqa: SLF001 - argparse has no public hook.
    args = parser.parse_args(argv)

    defaults: Config = {}
    if args.config is not None:
        try:
            defaults = config_defaults(load_config(args.config))
        except (OTAConfigError, RuntimeError) as exc:
            parser.error(str(exc))

    try:
        _merge_config_defaults(args, defaults, parser)
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))
    return args


def _merge_config_defaults(
    args: argparse.Namespace,
    defaults: Mapping[str, JsonValue],
    parser: argparse.ArgumentParser,
) -> None:
    """Apply defaults from config files while keeping CLI arguments authoritative."""
    args.broker_host = (
        _optional_text(args.broker_host, "--broker-host")
        or _optional_text(defaults.get("broker_host"), "broker.host")
        or DEFAULT_BROKER_HOST
    )
    args.broker_port = (
        args.broker_port
        or _optional_positive_int(defaults.get("broker_port"), "broker.port")
        or DEFAULT_BROKER_PORT
    )
    args.broker_username = _optional_text(args.broker_username, "--broker-username")
    args.broker_username_env = _optional_text(args.broker_username_env, "--broker-username-env")
    if args.broker_username is None and args.broker_username_env is None:
        args.broker_username = _optional_text(defaults.get("broker_username"), "broker.username")
        args.broker_username_env = _optional_text(
            defaults.get("broker_username_env"),
            "broker.username_env",
        )

    args.broker_password = _optional_text(args.broker_password, "--broker-password")
    args.broker_password_env = _optional_text(args.broker_password_env, "--broker-password-env")
    if args.broker_password is None and args.broker_password_env is None:
        args.broker_password = _optional_text(defaults.get("broker_password"), "broker.password")
        args.broker_password_env = _optional_text(
            defaults.get("broker_password_env"),
            "broker.password_env",
        )

    if args.broker_username is not None and args.broker_username_env is not None:
        parser.error("set only one of --broker-username or --broker-username-env")
    if args.broker_password is not None and args.broker_password_env is not None:
        parser.error("set only one of --broker-password or --broker-password-env")

    args.broker_username = args.broker_username or _env_value(
        args.broker_username_env,
        "--broker-username-env",
        parser,
    )
    args.broker_password = args.broker_password or _env_value(
        args.broker_password_env,
        "--broker-password-env",
        parser,
    )

    if args.broker_password is not None and args.broker_username is None:
        parser.error("--broker-password requires --broker-username or --broker-username-env")

    args.base_topic = (
        _optional_text(args.base_topic, "--base-topic")
        or _optional_text(defaults.get("base_topic"), "homie.base_topic")
        or DEFAULT_BASE_TOPIC
    )
    args.homie_version = (
        _optional_text(args.homie_version, "--homie-version")
        or _optional_text(defaults.get("homie_version"), "homie.version")
        or DEFAULT_HOMIE_VERSION
    )
    if args.homie_version not in SUPPORTED_HOMIE_VERSIONS:
        parser.error(f"homie.version must be one of: {', '.join(SUPPORTED_HOMIE_VERSIONS)}")

    args.broker_tls_cacert = _optional_text(
        args.broker_tls_cacert,
        "--broker-tls-cacert",
    ) or _optional_text(defaults.get("broker_tls_cacert"), "broker.tls_cacert")
    args.broker_tls_certfile = _optional_text(
        args.broker_tls_certfile,
        "--broker-tls-certfile",
    ) or _optional_text(defaults.get("broker_tls_certfile"), "broker.tls_certfile")
    args.broker_tls_keyfile = _optional_text(
        args.broker_tls_keyfile,
        "--broker-tls-keyfile",
    ) or _optional_text(defaults.get("broker_tls_keyfile"), "broker.tls_keyfile")
    args.broker_tls_insecure = (
        args.broker_tls_insecure
        if args.broker_tls_insecure is not None
        else _optional_bool(defaults.get("broker_tls_insecure"), "broker.tls_insecure")
    ) or False
    if args.broker_tls_keyfile is not None and args.broker_tls_certfile is None:
        parser.error("--broker-tls-keyfile requires --broker-tls-certfile")

    args.client_id = _optional_text(args.client_id, "--client-id") or _optional_text(
        defaults.get("client_id"),
        "ota.client_id",
    )
    expected_md5_default = defaults.get("expected_md5")
    args.expected_md5 = args.expected_md5 or (
        md5_digest(str(expected_md5_default)) if expected_md5_default else None
    )
    args.timeout = (
        args.timeout
        or _optional_positive_int(defaults.get("timeout"), "ota.timeout")
        or DEFAULT_TIMEOUT_SECONDS
    )
    args.base_topic = normalize_base_topic(args.base_topic, args.homie_version)


def read_firmware(path: Path, expected_md5: str | None) -> bytes:
    """Read firmware bytes and fail early on empty or unexpected files."""
    try:
        firmware = path.read_bytes()
    except OSError as exc:
        raise RuntimeError(f"failed to read firmware file {path}: {exc}") from exc

    if not firmware:
        raise RuntimeError(f"firmware file {path} is empty")

    firmware_md5 = _md5_hexdigest(firmware)
    if expected_md5 is not None and firmware_md5 != expected_md5:
        raise RuntimeError(f"firmware MD5 mismatch: expected {expected_md5}, got {firmware_md5}")

    return firmware


def main(argv: Sequence[str]) -> int:
    """CLI entry point."""
    args = parse_args(argv)
    try:
        firmware = read_firmware(args.firmware, args.expected_md5)
    except RuntimeError as exc:
        _write_line(str(exc), stream=sys.stderr)
        return 1

    updater = OTAUpdater(
        settings=OTASettings(
            broker_host=args.broker_host,
            broker_port=args.broker_port,
            broker_username=args.broker_username,
            broker_password=args.broker_password,
            broker_ca_cert=args.broker_tls_cacert,
            broker_tls_certfile=args.broker_tls_certfile,
            broker_tls_keyfile=args.broker_tls_keyfile,
            broker_tls_insecure=args.broker_tls_insecure,
            base_topic=args.base_topic,
            device_id=args.device_id,
            client_id=args.client_id,
            timeout_seconds=args.timeout,
            homie_version=args.homie_version,
        ),
        firmware=firmware,
    )
    return updater.run()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
