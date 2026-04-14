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

import argparse
import sys
import threading
from hashlib import md5
from pathlib import Path
from typing import List, Optional, Tuple

import paho.mqtt.client as mqtt

DEFAULT_BROKER_HOST = "127.0.0.1"
DEFAULT_BROKER_PORT = 1883
DEFAULT_BASE_TOPIC = "homie/"
DEFAULT_KEEPALIVE_SECONDS = 60
DEFAULT_TIMEOUT_SECONDS = 300
PROGRESS_BAR_WIDTH = 30


def normalize_base_topic(value: str) -> str:
    """Ensure the MQTT base topic always ends with a slash."""

    value = str(value)
    return value if value.endswith("/") else f"{value}/"


def mqtt_error_name(code: int) -> str:
    """Return a stable human-readable MQTT client error string."""

    return mqtt.error_string(code) if hasattr(mqtt, "error_string") else str(code)


def is_valid_md5(value: str) -> bool:
    """Return True when the payload looks like a hexadecimal MD5 digest."""

    return len(value) == 32 and all(character in "0123456789abcdefABCDEF" for character in value)


def positive_int(value: str) -> int:
    """Argparse type that rejects zero and negative integers."""

    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be a positive integer")
    return parsed


class OTAUpdater:
    """Own the OTA session state and MQTT callbacks."""

    def __init__(
        self,
        broker_host: str,
        broker_port: int,
        broker_username: Optional[str],
        broker_password: Optional[str],
        broker_ca_cert: Optional[str],
        base_topic: str,
        device_id: str,
        firmware: bytes,
        timeout_seconds: int,
    ) -> None:
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.broker_username = broker_username
        self.broker_password = broker_password
        self.broker_ca_cert = broker_ca_cert
        self.base_topic = normalize_base_topic(base_topic)
        self.device_id = device_id
        self.firmware = firmware
        self.firmware_md5 = md5(firmware).hexdigest()
        self.timeout_seconds = timeout_seconds

        self._client: Optional[mqtt.Client] = None
        self._done = threading.Event()
        self._success = False

        self._published = False
        self._ota_enabled: Optional[bool] = None
        self._old_md5: Optional[str] = None
        self._upload_total = 0
        self._waiting_for_reboot = False
        self._ready_for_post_upload_checksum = False
        self._info_topics_subscribed = False
        self._connection_lost_logged = False

    def run(self) -> int:
        """Run the OTA session and return a process exit code."""

        client = self._create_client()
        self._client = client

        if self.broker_username and self.broker_password:
            client.username_pw_set(self.broker_username, self.broker_password)

        if self.broker_ca_cert is not None:
            client.tls_set(ca_certs=self.broker_ca_cert)

        print(f"Connecting to mqtt broker {self.broker_host} on port {self.broker_port}")
        client.connect(self.broker_host, self.broker_port, DEFAULT_KEEPALIVE_SECONDS)
        client.loop_start()

        try:
            finished = self._done.wait(self.timeout_seconds)
            if not finished:
                if self._waiting_for_reboot:
                    self._finish(
                        False,
                        (
                            f"Timed out after {self.timeout_seconds}s while waiting for the "
                            "device to reboot and report the new checksum."
                        ),
                    )
                elif self._published:
                    self._finish(
                        False,
                        f"Timed out after {self.timeout_seconds}s while waiting for OTA completion.",
                    )
                else:
                    self._finish(
                        False,
                        f"Timed out after {self.timeout_seconds}s while waiting for the device to become ready.",
                    )
        finally:
            try:
                client.disconnect()
            except Exception:
                pass
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
            client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1)
        else:
            client = mqtt.Client()

        if hasattr(client, "reconnect_delay_set"):
            client.reconnect_delay_set(min_delay=1, max_delay=5)

        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        return client

    def _finish(self, success: bool, message: str) -> None:
        """Finish the session exactly once."""

        if self._done.is_set():
            return

        print(message)
        sys.stdout.flush()
        self._success = success
        self._done.set()

    def _subscribe_or_finish(self, topic: str) -> bool:
        """Subscribe to a topic and fail fast if the client rejects it."""

        assert self._client is not None

        result, _mid = self._client.subscribe(topic)
        if result != mqtt.MQTT_ERR_SUCCESS:
            self._finish(
                False,
                f"Failed to subscribe to {topic!r}: {mqtt_error_name(result)}",
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
        print("Waiting for device info...")
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
        assert self._client is not None
        print(f"Publishing new firmware with checksum {self.firmware_md5}")
        info = self._client.publish(self._firmware_topic, self.firmware, qos=1, retain=False)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            self._finish(
                False,
                f"Failed to publish firmware payload: {mqtt_error_name(info.rc)}",
            )

    def _print_progress(self, progress: int, total: int) -> None:
        """Render an in-place progress bar."""

        if total <= 0:
            return

        filled = int(PROGRESS_BAR_WIDTH * (progress / float(total)))
        bar = "+" * filled
        padding = " " * (PROGRESS_BAR_WIDTH - filled)
        print(f"\r[{bar}{padding}] {progress}", end="", flush=True)
        if progress >= total:
            print()

    def _parse_status_payload(self, payload: str) -> Tuple[int, str]:
        """Split a status payload into its numeric code and optional detail."""

        parts = payload.strip().split(None, 1)
        if not parts:
            raise ValueError("empty status payload")
        return int(parts[0]), parts[1] if len(parts) == 2 else ""

    def _parse_progress_payload(self, payload: str) -> Tuple[int, int]:
        """Parse `<written>/<total>` progress values from a 206 payload."""

        written_text, separator, total_text = payload.partition("/")
        if separator != "/":
            raise ValueError(f"invalid OTA progress payload: {payload!r}")
        return int(written_text), int(total_text)

    def _handle_status(self, payload: str) -> None:
        """Handle `$implementation/ota/status` updates."""

        try:
            status, detail = self._parse_status_payload(payload)
        except ValueError as exc:
            self._finish(False, f"Received malformed OTA status payload {payload!r}: {exc}")
            return

        if not self._published:
            return

        if status == 200:
            if self._upload_total > 0:
                self._print_progress(self._upload_total, self._upload_total)
            self._waiting_for_reboot = True
            print("Firmware uploaded successfully. Waiting for device to come back online.")
            return

        if status == 202:
            print("Checksum accepted")
            return

        if status == 206:
            try:
                progress, total = self._parse_progress_payload(detail)
            except ValueError as exc:
                self._finish(False, f"Received malformed OTA progress payload {payload!r}: {exc}")
                return
            self._upload_total = total
            self._print_progress(progress, total)
            return

        if status == 304:
            self._finish(
                True,
                f"Device firmware already up to date with md5 checksum: {self.firmware_md5}",
            )
            return

        if status == 403:
            self._finish(False, "Device OTA disabled, aborting...")
            return

        if 400 <= status < 600:
            self._finish(False, f"Device reported OTA failure: {payload}")
            return

        self._finish(False, f"Device reported unexpected OTA status: {payload}")

    def _handle_checksum(self, payload: str) -> None:
        """Handle `$fw/checksum` and decide whether to start or validate the OTA."""

        checksum = payload.strip()
        if not is_valid_md5(checksum):
            self._finish(False, f"Received malformed $fw/checksum payload: {payload!r}")
            return

        if not self._published:
            if checksum == self.firmware_md5:
                self._finish(
                    True,
                    f"Device firmware already up to date with md5 checksum: {checksum}",
                )
                return

            self._old_md5 = checksum
            self._maybe_publish_firmware()
            return

        if checksum == self.firmware_md5:
            self._finish(True, "Device back online. Update successful!")
            return

        if not self._ready_for_post_upload_checksum:
            return

        self._finish(
            False,
            f"Expecting checksum {self.firmware_md5}, got {checksum}, update failed!",
        )

    def _handle_ota_enabled(self, payload: str) -> None:
        """Handle `$implementation/ota/enabled`."""

        enabled = payload.strip().lower() == "true"
        self._ota_enabled = enabled

        if not enabled and not self._published:
            self._finish(False, "Device OTA disabled, aborting...")
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

    def _on_connect(self, client, userdata, flags, rc, properties=None) -> None:
        """Subscribe to device liveness topics once the broker connection is up."""

        code = int(getattr(rc, "value", rc))
        if code != 0:
            self._finish(False, f"Connection failed with result code {code}")
            return

        self._connection_lost_logged = False
        self._info_topics_subscribed = False

        print(f"Connected with result code {code}")

        if not self._subscribe_or_finish(self._state_topic):
            return
        if not self._subscribe_or_finish(self._online_topic):
            return

        print("Waiting for device to come online...")

    def _on_disconnect(self, client, userdata, rc, properties=None) -> None:
        """Keep the user informed when the MQTT session drops unexpectedly."""

        code = int(getattr(rc, "value", rc))
        if self._done.is_set():
            return

        self._info_topics_subscribed = False
        if code != 0 and not self._connection_lost_logged:
            self._connection_lost_logged = True
            print(f"MQTT connection lost ({code}). Waiting for reconnection...")

    def _on_message(self, client, userdata, msg) -> None:
        """Route incoming MQTT messages to the relevant handler."""

        payload = msg.payload.decode("utf-8", errors="replace")

        if msg.topic == self._status_topic:
            self._handle_status(payload)
        elif msg.topic == self._fw_checksum_topic:
            self._handle_checksum(payload)
        elif msg.topic == self._ota_enabled_topic:
            self._handle_ota_enabled(payload)
        elif msg.topic == self._state_topic or msg.topic == self._online_topic:
            self._handle_device_ready(msg.topic, payload)


def parse_args(argv: List[str]) -> argparse.Namespace:
    """Parse CLI arguments."""

    parser = argparse.ArgumentParser(
        description=(
            "Send an OTA firmware update to a Homie device that implements the "
            "homie-esp8266 OTA MQTT contract."
        )
    )

    parser.add_argument(
        "-l",
        "--broker-host",
        default=DEFAULT_BROKER_HOST,
        help="host name or IP address of the MQTT broker",
    )
    parser.add_argument(
        "-p",
        "--broker-port",
        type=positive_int,
        default=DEFAULT_BROKER_PORT,
        help="port of the MQTT broker",
    )
    parser.add_argument(
        "-u",
        "--broker-username",
        help="username used to authenticate with the MQTT broker",
    )
    parser.add_argument(
        "-d",
        "--broker-password",
        help="password used to authenticate with the MQTT broker",
    )
    parser.add_argument(
        "-t",
        "--base-topic",
        type=normalize_base_topic,
        default=DEFAULT_BASE_TOPIC,
        help="base topic of the Homie devices on the broker",
    )
    parser.add_argument(
        "-i",
        "--device-id",
        required=True,
        help="Homie device id",
    )
    parser.add_argument(
        "--broker-tls-cacert",
        default=None,
        help=(
            "CA certificate bundle used to validate TLS connections. "
            "If set, TLS is enabled on the broker connection."
        ),
    )
    parser.add_argument(
        "--timeout",
        type=positive_int,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="maximum time in seconds to wait for the OTA workflow to complete",
    )
    parser.add_argument(
        "firmware",
        type=Path,
        help="path to the firmware binary to send to the device",
    )

    parser._optionals.title = "arguments"
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    """CLI entry point."""

    args = parse_args(argv)
    firmware = args.firmware.read_bytes()

    updater = OTAUpdater(
        broker_host=args.broker_host,
        broker_port=args.broker_port,
        broker_username=args.broker_username,
        broker_password=args.broker_password,
        broker_ca_cert=args.broker_tls_cacert,
        base_topic=args.base_topic,
        device_id=args.device_id,
        firmware=firmware,
        timeout_seconds=args.timeout,
    )
    return updater.run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
