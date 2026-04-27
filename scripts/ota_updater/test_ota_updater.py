#!/usr/bin/env python3

"""Unit tests for the Homie MQTT OTA updater state machine."""

import contextlib
import io
import sys
import tempfile
import unittest
from hashlib import md5
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ota_updater


class _PublishInfo:
    def __init__(self, rc=ota_updater.mqtt.MQTT_ERR_SUCCESS):
        self.rc = rc


class _FakeClient:
    def __init__(self):
        self.published = []
        self.subscribed = []
        self.username = None
        self.password = None
        self.tls_options = None
        self.tls_insecure = False

    def publish(self, topic, payload=None, qos=0, retain=False):
        self.published.append(
            {
                "topic": topic,
                "payload": payload,
                "qos": qos,
                "retain": retain,
            }
        )
        return _PublishInfo()

    def subscribe(self, topic):
        self.subscribed.append(topic)
        return ota_updater.mqtt.MQTT_ERR_SUCCESS, len(self.subscribed)

    def username_pw_set(self, username, password=None):
        self.username = username
        self.password = password

    def tls_set(self, ca_certs=None, certfile=None, keyfile=None):
        self.tls_options = {
            "ca_certs": ca_certs,
            "certfile": certfile,
            "keyfile": keyfile,
        }

    def tls_insecure_set(self, value):
        self.tls_insecure = value


@contextlib.contextmanager
def _quiet_stdout():
    with contextlib.redirect_stdout(io.StringIO()):
        yield


@contextlib.contextmanager
def _quiet_stderr():
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def _make_updater(firmware=b"test firmware"):
    return ota_updater.OTAUpdater(
        broker_host="127.0.0.1",
        broker_port=1883,
        broker_username=None,
        broker_password=None,
        broker_ca_cert=None,
        broker_tls_certfile=None,
        broker_tls_keyfile=None,
        broker_tls_insecure=False,
        base_topic="homie",
        device_id="device",
        client_id=None,
        firmware=firmware,
        timeout_seconds=10,
    )


class OTAUpdaterTests(unittest.TestCase):
    def test_base_topic_and_md5_helpers(self):
        self.assertEqual(ota_updater.normalize_base_topic("homie"), "homie/")
        self.assertEqual(ota_updater.normalize_base_topic("homie/"), "homie/")
        self.assertTrue(ota_updater.is_valid_md5("0123456789abcdef0123456789ABCDEF"))
        self.assertFalse(ota_updater.is_valid_md5("not-a-checksum"))
        self.assertEqual(
            ota_updater.md5_digest("0123456789abcdef0123456789ABCDEF"),
            "0123456789abcdef0123456789abcdef",
        )
        with self.assertRaises(ota_updater.argparse.ArgumentTypeError):
            ota_updater.positive_int("0")
        with self.assertRaises(ota_updater.argparse.ArgumentTypeError):
            ota_updater.md5_digest("not-a-checksum")

    def test_configure_auth_accepts_username_without_password(self):
        updater = _make_updater()
        updater.broker_username = "user"
        fake_client = _FakeClient()

        updater._configure_auth_and_tls(fake_client)

        self.assertEqual(fake_client.username, "user")
        self.assertIsNone(fake_client.password)

    def test_configure_tls_client_certificate_options(self):
        updater = _make_updater()
        updater.broker_ca_cert = "ca.pem"
        updater.broker_tls_certfile = "client.pem"
        updater.broker_tls_keyfile = "client.key"
        updater.broker_tls_insecure = True
        fake_client = _FakeClient()

        updater._configure_auth_and_tls(fake_client)

        self.assertEqual(
            fake_client.tls_options,
            {
                "ca_certs": "ca.pem",
                "certfile": "client.pem",
                "keyfile": "client.key",
            },
        )
        self.assertTrue(fake_client.tls_insecure)

    def test_publish_waits_for_ota_flag_and_checksum(self):
        updater = _make_updater()
        fake_client = _FakeClient()
        updater._client = fake_client

        with _quiet_stdout():
            updater._handle_ota_enabled("true")
            self.assertEqual(fake_client.published, [])

            updater._handle_checksum("0" * 32)

        self.assertTrue(updater._published)
        self.assertEqual(len(fake_client.published), 1)
        publish = fake_client.published[0]
        self.assertEqual(publish["topic"], updater._firmware_topic)
        self.assertEqual(publish["payload"], updater.firmware)
        self.assertEqual(publish["qos"], 1)
        self.assertFalse(publish["retain"])

    def test_progress_status_updates_upload_total(self):
        updater = _make_updater()
        updater._published = True

        with _quiet_stdout():
            updater._handle_status("206 12/24")

        self.assertEqual(updater._upload_total, 24)
        self.assertFalse(updater._done.is_set())

    def test_invalid_progress_payload_finishes_session_as_failure(self):
        updater = _make_updater()
        updater._published = True

        with _quiet_stdout():
            updater._handle_status("206 25/24")

        self.assertTrue(updater._done.is_set())
        self.assertFalse(updater._success)

    def test_terminal_failure_status_finishes_session(self):
        updater = _make_updater()
        updater._published = True

        with _quiet_stdout():
            updater._handle_status("500 FLASH_ERROR")

        self.assertTrue(updater._done.is_set())
        self.assertFalse(updater._success)

    def test_reboot_readiness_then_matching_checksum_succeeds(self):
        updater = _make_updater()
        updater._client = _FakeClient()
        updater._published = True
        updater._waiting_for_reboot = True

        with _quiet_stdout():
            updater._handle_device_ready(updater._state_topic, "ready")
            updater._handle_checksum(updater.firmware_md5)

        self.assertTrue(updater._ready_for_post_upload_checksum)
        self.assertTrue(updater._done.is_set())
        self.assertTrue(updater._success)

    def test_malformed_status_finishes_session_as_failure(self):
        updater = _make_updater()
        updater._published = True

        with _quiet_stdout():
            updater._handle_status("not-a-status")

        self.assertTrue(updater._done.is_set())
        self.assertFalse(updater._success)

    def test_malformed_ota_enabled_finishes_session_as_failure(self):
        updater = _make_updater()

        with _quiet_stdout():
            updater._handle_ota_enabled("maybe")

        self.assertTrue(updater._done.is_set())
        self.assertFalse(updater._success)

    def test_read_firmware_rejects_empty_and_md5_mismatch(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            firmware_path = Path(tmpdir) / "firmware.bin"
            firmware_path.write_bytes(b"firmware")
            expected = md5(b"firmware").hexdigest()

            self.assertEqual(ota_updater.read_firmware(firmware_path, expected), b"firmware")
            with self.assertRaises(RuntimeError):
                ota_updater.read_firmware(firmware_path, "0" * 32)

            empty_path = Path(tmpdir) / "empty.bin"
            empty_path.write_bytes(b"")
            with self.assertRaises(RuntimeError):
                ota_updater.read_firmware(empty_path, None)

    def test_parse_args_rejects_password_or_key_without_parent_option(self):
        with _quiet_stderr():
            with self.assertRaises(SystemExit):
                ota_updater.parse_args(["--broker-password", "secret", "-i", "device", "fw.bin"])
            with self.assertRaises(SystemExit):
                ota_updater.parse_args(["--broker-tls-keyfile", "key.pem", "-i", "device", "fw.bin"])


if __name__ == "__main__":
    unittest.main()
