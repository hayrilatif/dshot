import json
import pathlib

from dshot_cli import frame

VECTORS = json.loads((pathlib.Path(__file__).resolve().parents[2] / "test-vectors.json").read_text())["vectors"]


def test_vectors_encode_decode():
    for v in VECTORS:
        f = int(v["frame"], 16)
        assert frame.encode(v["value"], v["telemetry"], v["inverted"]) == f
        d = frame.decode(f, v["inverted"])
        assert (d.value, d.telemetry, d.crc_ok) == (v["value"], v["telemetry"], True)
        assert not frame.decode(f, not v["inverted"]).crc_ok


def test_encode_range():
    import pytest

    with pytest.raises(ValueError):
        frame.encode(2048)
