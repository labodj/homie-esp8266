#!/usr/bin/env python3

"""Stable entry point for the maintained Homie MQTT OTA updater."""

from __future__ import annotations

import importlib.util
import sys
from collections.abc import Sequence
from pathlib import Path
from types import ModuleType
from typing import Protocol, cast

UPDATER_PATH = Path(__file__).resolve().parent / "ota_updater" / "ota_updater.py"


class _UpdaterModule(Protocol):
    """Callable shape exported by the OTA updater module."""

    def main(self, argv: Sequence[str]) -> int:
        """Run the OTA updater and return a process exit code."""


def _load_updater_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location("_homie_ota_updater", UPDATER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load OTA updater from {UPDATER_PATH}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(spec.name, None)
        raise
    return module


def main(argv: Sequence[str] | None = None) -> int:
    """Run the maintained OTA updater."""
    updater_module = cast("_UpdaterModule", _load_updater_module())
    return updater_module.main(sys.argv[1:] if argv is None else list(argv))


if __name__ == "__main__":
    raise SystemExit(main())
