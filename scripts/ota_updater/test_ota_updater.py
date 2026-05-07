#!/usr/bin/env python3

"""Unit tests for the Homie MQTT OTA updater state machine."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import sys
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import TypedDict, cast

import pytest
from paho.mqtt import client as mqtt

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ota_updater


class _PublishRecord(TypedDict):
    topic: str
    payload: bytes | None
    qos: int
    retain: bool


@dataclass(frozen=True)
class _PublishInfo:
    rc: int = mqtt.MQTT_ERR_SUCCESS


class _FakeClient:
    def __init__(self) -> None:
        self.published: list[_PublishRecord] = []
        self.subscribed: list[str] = []
        self.username: str | None = None
        self.password: str | None = None
        self.tls_options: dict[str, str | None] | None = None
        self.tls_insecure = False

    def publish(
        self,
        topic: str,
        payload: bytes | None = None,
        qos: int = 0,
        *,
        retain: bool = False,
    ) -> _PublishInfo:
        self.published.append(
            {
                "topic": topic,
                "payload": payload,
                "qos": qos,
                "retain": retain,
            }
        )
        return _PublishInfo()

    def subscribe(self, topic: str) -> tuple[int, int]:
        self.subscribed.append(topic)
        return mqtt.MQTT_ERR_SUCCESS, len(self.subscribed)

    def username_pw_set(self, username: str, password: str | None = None) -> None:
        self.username = username
        self.password = password

    def tls_set(
        self,
        ca_certs: str | None = None,
        certfile: str | None = None,
        keyfile: str | None = None,
    ) -> None:
        self.tls_options = {
            "ca_certs": ca_certs,
            "certfile": certfile,
            "keyfile": keyfile,
        }

    def tls_insecure_set(self, *, value: bool) -> None:
        self.tls_insecure = value


@contextlib.contextmanager
def _quiet_stdout() -> Iterator[None]:
    with contextlib.redirect_stdout(io.StringIO()):
        yield


@contextlib.contextmanager
def _quiet_stderr() -> Iterator[None]:
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def _fake_client(client: _FakeClient) -> mqtt.Client:
    return cast("mqtt.Client", client)


def _make_updater(
    firmware: bytes = b"test firmware",
    homie_version: str = ota_updater.DEFAULT_HOMIE_VERSION,
) -> ota_updater.OTAUpdater:
    return ota_updater.OTAUpdater(
        settings=ota_updater.OTASettings(
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
            timeout_seconds=10,
            homie_version=homie_version,
        ),
        firmware=firmware,
    )


def test_base_topic_and_md5_helpers() -> None:
    assert ota_updater.normalize_base_topic("homie") == "homie/"
    assert ota_updater.normalize_base_topic("homie/") == "homie/"
    assert ota_updater.normalize_base_topic("homie", "5") == "homie/5/"
    assert ota_updater.normalize_base_topic("homie/5/", "5") == "homie/5/"
    assert ota_updater.normalize_base_topic("5", "5") == "5/5/"
    assert ota_updater.normalize_base_topic("lab", "4") == "lab/"
    assert ota_updater.base_topic_ends_with_segment("homie/5/", "5")
    assert not ota_updater.base_topic_ends_with_segment("5/", "5")
    assert not ota_updater.base_topic_ends_with_segment("homie/15/", "5")
    assert ota_updater.is_valid_md5("0123456789abcdef0123456789ABCDEF")
    assert not ota_updater.is_valid_md5("not-a-checksum")
    assert (
        ota_updater.md5_digest("0123456789abcdef0123456789ABCDEF")
        == "0123456789abcdef0123456789abcdef"
    )
    with pytest.raises(argparse.ArgumentTypeError):
        ota_updater.positive_int("0")
    with pytest.raises(argparse.ArgumentTypeError):
        ota_updater.md5_digest("not-a-checksum")


def test_configure_auth_accepts_username_without_password() -> None:
    updater = _make_updater()
    updater.broker_username = "user"
    fake_client = _FakeClient()

    updater._configure_auth_and_tls(_fake_client(fake_client))

    assert fake_client.username == "user"
    assert fake_client.password is None


def test_configure_tls_client_certificate_options() -> None:
    updater = _make_updater()
    updater.broker_ca_cert = "ca.pem"
    updater.broker_tls_certfile = "client.pem"
    updater.broker_tls_keyfile = "client.key"
    updater.broker_tls_insecure = True
    fake_client = _FakeClient()

    updater._configure_auth_and_tls(_fake_client(fake_client))

    assert fake_client.tls_options == {
        "ca_certs": "ca.pem",
        "certfile": "client.pem",
        "keyfile": "client.key",
    }
    assert fake_client.tls_insecure


def test_publish_waits_for_ota_flag_and_checksum() -> None:
    updater = _make_updater()
    fake_client = _FakeClient()
    updater._client = _fake_client(fake_client)

    with _quiet_stdout():
        updater._handle_ota_enabled("true")
        assert fake_client.published == []

        updater._handle_checksum("0" * 32)

    assert updater._published
    assert len(fake_client.published) == 1
    publish = fake_client.published[0]
    assert publish["topic"] == updater._firmware_topic
    assert publish["payload"] == updater.firmware
    assert publish["qos"] == 1
    assert not publish["retain"]


def test_progress_status_updates_upload_total() -> None:
    updater = _make_updater()
    updater._published = True

    with _quiet_stdout():
        updater._handle_status("206 12/24")

    assert updater._upload_total == 24
    assert not updater._done.is_set()


def test_invalid_progress_payload_finishes_session_as_failure() -> None:
    updater = _make_updater()
    updater._published = True

    with _quiet_stdout():
        updater._handle_status("206 25/24")

    assert updater._done.is_set()
    assert not updater._success


def test_terminal_failure_status_finishes_session() -> None:
    updater = _make_updater()
    updater._published = True

    with _quiet_stdout():
        updater._handle_status("500 FLASH_ERROR")

    assert updater._done.is_set()
    assert not updater._success


def test_reboot_readiness_then_matching_checksum_succeeds() -> None:
    updater = _make_updater()
    updater._client = _fake_client(_FakeClient())
    updater._published = True
    updater._waiting_for_reboot = True

    with _quiet_stdout():
        updater._handle_device_ready(updater._state_topic, "ready")
        updater._handle_checksum(updater.firmware_md5)

    assert updater._ready_for_post_upload_checksum
    assert updater._done.is_set()
    assert updater._success


def test_malformed_status_finishes_session_as_failure() -> None:
    updater = _make_updater()
    updater._published = True

    with _quiet_stdout():
        updater._handle_status("not-a-status")

    assert updater._done.is_set()
    assert not updater._success


def test_malformed_ota_enabled_finishes_session_as_failure() -> None:
    updater = _make_updater()

    with _quiet_stdout():
        updater._handle_ota_enabled("maybe")

    assert updater._done.is_set()
    assert not updater._success


def test_read_firmware_rejects_empty_and_md5_mismatch(tmp_path: Path) -> None:
    firmware_path = tmp_path / "firmware.bin"
    firmware_path.write_bytes(b"firmware")
    expected = hashlib.md5(b"firmware", usedforsecurity=False).hexdigest()

    assert ota_updater.read_firmware(firmware_path, expected) == b"firmware"
    with pytest.raises(RuntimeError):
        ota_updater.read_firmware(firmware_path, "0" * 32)

    empty_path = tmp_path / "empty.bin"
    empty_path.write_bytes(b"")
    with pytest.raises(RuntimeError):
        ota_updater.read_firmware(empty_path, None)


def test_parse_args_rejects_password_or_key_without_parent_option() -> None:
    with _quiet_stderr(), pytest.raises(SystemExit):
        ota_updater.parse_args(["--broker-password", "secret", "-i", "device", "fw.bin"])

    with _quiet_stderr(), pytest.raises(SystemExit):
        ota_updater.parse_args(["--broker-tls-keyfile", "key.pem", "-i", "device", "fw.bin"])


def test_parse_args_normalizes_homie_v5_base_topic() -> None:
    args = ota_updater.parse_args(["--homie-version", "5", "-t", "lab", "-i", "device", "fw.bin"])

    assert args.base_topic == "lab/5/"


def test_parse_args_loads_json_config_and_resolves_secret_env(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    config_path = tmp_path / "ota.json"
    config_path.write_text(
        json.dumps(
            {
                "broker": {
                    "host": "mqtt.lan",
                    "port": 8883,
                    "username": "homie",
                    "password_env": "HOMIE_OTA_TEST_PASSWORD",
                    "tls_cacert": "ca.pem",
                    "tls_insecure": True,
                },
                "homie": {"base_topic": "lab/homie", "version": "5"},
                "ota": {"timeout": 45, "client_id": "ota-client"},
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setenv("HOMIE_OTA_TEST_PASSWORD", "secret")

    args = ota_updater.parse_args(["--config", str(config_path), "-i", "device", "fw.bin"])

    assert args.broker_host == "mqtt.lan"
    assert args.broker_port == 8883
    assert args.broker_username == "homie"
    assert args.broker_password == "secret"
    assert args.broker_tls_cacert == "ca.pem"
    assert args.broker_tls_insecure
    assert args.base_topic == "lab/homie/5/"
    assert args.homie_version == "5"
    assert args.timeout == 45
    assert args.client_id == "ota-client"


def test_parse_args_allows_cli_to_override_config_secret_env(tmp_path: Path) -> None:
    config_path = tmp_path / "ota.json"
    config_path.write_text(
        json.dumps({"broker": {"username_env": "UNSET_USER_ENV"}}),
        encoding="utf-8",
    )
    args = ota_updater.parse_args(
        [
            "--config",
            str(config_path),
            "--broker-username",
            "cli-user",
            "-i",
            "device",
            "fw.bin",
        ]
    )

    assert args.broker_username == "cli-user"


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__]))
