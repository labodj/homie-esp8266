#!/usr/bin/env python3

"""Unit tests for the Homie MQTT OTA updater state machine."""

import contextlib
import io
import sys
import unittest
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


@contextlib.contextmanager
def _quiet_stdout():
    with contextlib.redirect_stdout(io.StringIO()):
        yield


def _make_updater(firmware=b"test firmware"):
    return ota_updater.OTAUpdater(
        broker_host="127.0.0.1",
        broker_port=1883,
        broker_username=None,
        broker_password=None,
        broker_ca_cert=None,
        base_topic="homie",
        device_id="device",
        firmware=firmware,
        timeout_seconds=10,
    )


class OTAUpdaterTests(unittest.TestCase):
    def test_base_topic_and_md5_helpers(self):
        self.assertEqual(ota_updater.normalize_base_topic("homie"), "homie/")
        self.assertEqual(ota_updater.normalize_base_topic("homie/"), "homie/")
        self.assertTrue(ota_updater.is_valid_md5("0123456789abcdef0123456789ABCDEF"))
        self.assertFalse(ota_updater.is_valid_md5("not-a-checksum"))
        with self.assertRaises(ota_updater.argparse.ArgumentTypeError):
            ota_updater.positive_int("0")

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


if __name__ == "__main__":
    unittest.main()
