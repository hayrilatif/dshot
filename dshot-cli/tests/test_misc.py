from types import SimpleNamespace

import pytest

from dshot_cli import commands, discover
from dshot_cli.cli import shell_line_to_command
from dshot_cli.link import DShotError


def test_resolve_commands():
    assert commands.resolve("beep1") == (1, 1)
    assert commands.resolve("SPIN_REVERSED") == (21, 10)
    assert commands.resolve("12") == (12, 10)
    with pytest.raises(DShotError):
        commands.resolve("48")
    with pytest.raises(DShotError):
        commands.resolve("uç")


def test_shell_translation():
    assert shell_line_to_command("thr 200") == "TX thr 200"
    assert shell_line_to_command("stream 100") == "RX stream 100"
    assert shell_line_to_command("cmd beep2") == "TX CMD 2 1"
    assert shell_line_to_command("cmd save 3") == "TX CMD 12 3"
    assert shell_line_to_command("RX RAW 5") == "RX RAW 5"
    assert shell_line_to_command("status") == "status"
    assert shell_line_to_command("   ") is None


def port(dev, product, vid=0x0483, pid=0x5740):
    return SimpleNamespace(device=dev, product=product, vid=vid, pid=pid, serial_number="S")


def test_discover():
    ports = [port("/dev/ttyACM0", "DShot TXRX"), port("/dev/ttyACM1", "Other"), port("/dev/ttyUSB0", "DShot TX", vid=1)]
    devs = discover.find_devices(ports)
    assert [d.port for d in devs] == ["/dev/ttyACM0"]
    assert discover.pick("rx", devs) == "/dev/ttyACM0"
    assert discover.pick_loopback(devs) == ("/dev/ttyACM0", "/dev/ttyACM0")
    with pytest.raises(DShotError, match="bulunamadı"):
        discover.pick("tx", [])


def test_discover_separate_boards_and_ambiguity():
    devs = discover.find_devices([port("/a", "DShot TX"), port("/b", "DShot RX")])
    assert discover.pick_loopback(devs) == ("/a", "/b")
    devs = discover.find_devices([port("/a", "DShot TXRX"), port("/b", "DShot TXRX")])
    with pytest.raises(DShotError, match="birden fazla"):
        discover.pick("tx", devs)
