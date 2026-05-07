#!/usr/bin/env python3

"""Stable entry point for the maintained Homie MQTT OTA updater."""

from pathlib import Path
import sys


UPDATER_DIR = Path(__file__).resolve().parent / "ota_updater"
sys.path.insert(0, str(UPDATER_DIR))

from ota_updater import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
