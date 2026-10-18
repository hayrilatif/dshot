import pytest
import serial

from dshot_cli.link import DShotError, Link, parse_kv
from fakes import FakeSerial


def make(handler, timeout=0.2):
    return Link("FAKE", timeout=timeout, serial_obj=FakeSerial(handler))


def test_parse_kv():
    assert parse_kv("#RX n=5 val=- speed=?") == {"n": "5", "val": "-", "speed": "?"}


def test_ok_and_payload():
    link = make(lambda line: "OK board=x caps=tx\n")
    assert link.command("STATUS") == "board=x caps=tx"


def test_err_raises():
    link = make(lambda line: "ERR busy\n")
    with pytest.raises(DShotError, match="busy"):
        link.command("TX SPEED 300")


def test_timeout_raises():
    link = make(lambda line: "")
    with pytest.raises(DShotError, match="cevap gelmedi"):
        link.command("PING")


def test_async_flood_before_reply_is_kept_and_reply_found():
    flood = "".join(f"#F val={i}\n" for i in range(500))
    link = make(lambda line: "garbage\r\n" + flood + "OK\n")
    assert link.command("PING") == ""
    lines = link.poll_async()
    assert len(lines) == 500 and lines[0] == "#F val=0"


def test_wait_async_and_drain():
    link = make(lambda line: "#RX n=1\nOK\n#RX n=2\n")
    link.command("RX RESET")
    link.drain_async()
    assert link.wait_async("#RX", 0.2) == "#RX n=2"
    assert link.wait_async("#RX", 0.05) is None


def test_serial_exception_becomes_dshot_error():
    class Broken(FakeSerial):
        def read(self, n):
            raise serial.SerialException("device disconnected")

    link = Link("FAKE", serial_obj=Broken(lambda line: ""))
    with pytest.raises(DShotError, match="bağlantı koptu"):
        link.command("PING")
