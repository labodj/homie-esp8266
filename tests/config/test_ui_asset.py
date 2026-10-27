"""Verify that stale page content or security hashes cannot pass the build check."""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

GENERATOR = Path(__file__).resolve().parents[2] / "tools/generate_config_ui.py"


@pytest.mark.parametrize("change", ["none", "html", "csp"])
def test_asset_check_rejects_drift(tmp_path: Path, change: str) -> None:
    tool = tmp_path / "tools/generate_config_ui.py"
    tool.parent.mkdir()
    shutil.copyfile(GENERATOR, tool)
    page = tmp_path / "src/Homie/Boot/ConfigUi.html"
    page.parent.mkdir(parents=True)
    page.write_text("<style>body{color:black}</style><script>const ready=true;</script>")
    subprocess.run([sys.executable, str(tool)], check=True, capture_output=True)
    if change == "html":
        page.write_text(page.read_text() + "<!-- changed -->")
    elif change == "csp":
        header = page.with_suffix(".hpp")
        header.write_text(header.read_text().replace("connect-src 'self'", "connect-src *"))
    result = subprocess.run(
        [sys.executable, str(tool), "--check"], check=False, capture_output=True, text=True
    )
    assert (result.returncode == 0) == (change == "none")
    if change != "none":
        assert "Stale UI asset" in result.stderr
