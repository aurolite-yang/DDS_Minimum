import os
import pathlib
import subprocess
import time

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_BIN = ROOT / "build" / "dds_demo"


def demo_binary():
    path = pathlib.Path(os.environ.get("DDS_DEMO_BIN", DEFAULT_BIN))
    if not path.exists():
        pytest.skip(
            "dds_demo binary not found. Build first with: "
            "cmake -S . -B build && cmake --build build"
        )
    return path


def run_pub_sub_pair(mode):
    binary = demo_binary()
    samples = "3"

    subscriber = subprocess.Popen(
        [str(binary), mode, "--samples", samples],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        time.sleep(1.0)
        publisher = subprocess.run(
            [str(binary), "pub", "--samples", samples, "--period-ms", "100"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=20,
            check=False,
        )
        sub_output, _ = subscriber.communicate(timeout=20)
    finally:
        if subscriber.poll() is None:
            subscriber.terminate()
            subscriber.wait(timeout=5)

    return publisher.returncode, publisher.stdout, subscriber.returncode, sub_output

# 通过装饰器实现参数化测试，分别测试两种订阅模式：sub-listener和sub-waitset
@pytest.mark.parametrize(
    ("mode", "marker"),
    [
        ("sub-listener", "LISTENER_SUB index=3"),
        ("sub-waitset", "WAITSET_SUB index=6"),
    ],
)
def test_pub_sub_receives_three_samples(mode, marker):
    pub_code, pub_output, sub_code, sub_output = run_pub_sub_pair(mode)

    assert pub_code == 0, pub_output
    assert sub_code == 0, sub_output
    assert "PUB index=3" in pub_output
    assert marker in sub_output
