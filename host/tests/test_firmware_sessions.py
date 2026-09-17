"""Run the actual ESP-IDF C parser/arbiter with memory sanitizers on the host."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_native_firmware_sessions(tmp_path):
    idf = Path(os.environ.get("SAKI_IDF_PATH", ROOT.parent.parent / "esp-idf-v5.5.3"))
    cjson = idf / "components/json/cJSON"
    compiler = shutil.which("clang")
    if not compiler or not (cjson / "cJSON.c").is_file():
        pytest.skip("native firmware test needs clang and the pinned ESP-IDF checkout")
    (tmp_path / "esp_err.h").write_text(
        "typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_INVALID_SIZE 0x104\n"
    )
    (tmp_path / "esp_timer.h").write_text(
        "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n"
    )
    components = ROOT / "firmware/components"
    binary = tmp_path / "sessions-test"
    command = [
        compiler,
        "-std=c11",
        "-g",
        "-DSAKI_NATIVE_TEST",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(tmp_path),
        "-I",
        str(cjson),
    ]
    for component in ("saki_model", "saki_transport", "saki_protocol"):
        command += [
            "-I",
            str(components / component / "include"),
            str(components / component / f"{component}.c"),
        ]
    command += [
        str(cjson / "cJSON.c"),
        str(components / "saki_protocol/test/test_sessions.c"),
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True, capture_output=True, timeout=60)
    fixture = tmp_path / "sessions.ndjson"
    fixture.write_text(
        json.dumps(
            json.loads((ROOT / "protocol/fixtures/v1/valid/sessions-mixed.json").read_text()),
            ensure_ascii=False,
            separators=(",", ":"),
        )
    )
    result = subprocess.run(
        [str(binary), str(fixture)],
        capture_output=True,
        check=False,
        text=True,
        timeout=20,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    generic_known = subprocess.run(
        [str(binary), str(fixture), "generic-known"],
        capture_output=True,
        check=False,
        text=True,
        timeout=20,
    )
    assert generic_known.returncode == 0, generic_known.stdout + generic_known.stderr

    generic_fixture = tmp_path / "sessions-generic.ndjson"
    generic_fixture.write_text(
        json.dumps(
            json.loads(
                (ROOT / "protocol/fixtures/v1/valid/sessions-generic.json").read_text()
            ),
            ensure_ascii=False,
            separators=(",", ":"),
        )
    )
    generic = subprocess.run(
        [str(binary), str(generic_fixture), "generic"],
        capture_output=True,
        check=False,
        text=True,
        timeout=20,
    )
    assert generic.returncode == 0, generic.stdout + generic.stderr

    # All candidates have a newer global sequence: rejection must be validation,
    # never an accidental stale-sequence result. The committed list stays intact.
    original = json.loads(fixture.read_text())
    mutations = [
        lambda value: value["sessions"].append(value["sessions"][0]),
        lambda value: value["sessions"].__setitem__(1, value["sessions"][0]),
        lambda value: value.__setitem__("total", 33),
        lambda value: value.__setitem__("hidden_attention", 1),
        lambda value: value["sessions"][0].__setitem__("revision", 2**32),
        lambda value: value["sessions"][0].__setitem__("fresh", 1),
        lambda value: value["sessions"][0].__setitem__("run_id", "x" * 33),
        lambda value: value["sessions"][0].__setitem__("source", "unknown"),
        lambda value: value["sessions"][0]["task"].__setitem__("id", "raw-id"),
        lambda value: value["sessions"][0].pop("state"),
    ]
    for mutate in mutations:
        value = json.loads(json.dumps(original))
        value["seq"] = 2
        mutate(value)
        fixture.write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")))
        checked = subprocess.run(
            [str(binary), str(fixture), "reject"],
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
        assert checked.returncode == 0, checked.stdout + checked.stderr


def test_native_session_view(tmp_path):
    compiler = shutil.which("clang")
    if not compiler:
        pytest.skip("native session view test needs clang")
    components = ROOT / "firmware/components"
    binary = tmp_path / "session-view-test"
    command = [
        compiler,
        "-std=c11",
        "-DSAKI_NATIVE_TEST",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
    ]
    for name in ("saki_model", "saki_ui_policy"):
        command += ["-I", str(components / name / "include"), str(components / name / f"{name}.c")]
    command += [str(components / "saki_ui_policy/test/test_session_view.c"), "-o", str(binary)]
    subprocess.run(command, capture_output=True, check=True, timeout=30)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
