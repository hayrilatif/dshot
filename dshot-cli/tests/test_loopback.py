import pytest

from dshot_cli import cli, loopback
from dshot_cli.link import Link
from fakes import FakeFirmware, FakeSerial


def run_with(fw, cases):
    ser = FakeSerial(fw)
    link = Link("FAKE", timeout=0.2, serial_obj=ser)
    return loopback.run(link, link, cases, settle_s=0), ser


def test_cases_sorted_and_cover_commands():
    cases = loopback.build_cases([600], quick=False)
    values = [c.value for c in cases]
    assert values == sorted(values)
    assert set(range(0, 49)) <= set(values) and 2047 in values


def test_all_pass_with_perfect_firmware():
    fw = FakeFirmware()
    results, ser = run_with(fw, loopback.build_cases([150, 1200], quick=True))
    assert results and all(r.ok for r in results), [r.detail for r in results if not r.ok]
    assert fw.timeout == 500  # eski failsafe değeri geri yüklendi
    assert fw.value == 0 and fw.stream == 0 and fw.pol == "auto"
    assert "TX SPEED 150" in ser.written


def test_mismatch_detected():
    fw = FakeFirmware(corrupt=lambda speed, value, tlm: value + 1 if value == 1046 else None)
    results, _ = run_with(fw, loopback.build_cases([600], quick=True))
    bad = [r for r in results if not r.ok]
    assert len(bad) == 2 and all(r.case.value == 1046 for r in bad)
    assert "vmin=1047" in bad[0].detail
    assert "Uyuşmazlıklar (2)" in loopback.format_report(results)


def test_check_timing_and_errors():
    case = loopback.Case(600, 100, 0)
    base = {"n": "5", "vmin": "100", "vmax": "100", "tlm": "0", "speed": "600", "inv": "0",
            "crc_err": "0", "other": "0", "t0h_ns": "625", "t1h_ns": "1250"}
    assert loopback.check(base, case) == (True, "")
    assert not loopback.check({**base, "t0h_ns": "900"}, case)[0]
    assert not loopback.check({**base, "other": "2"}, case)[0]
    assert loopback.check({**base, "n": "0"}, case)[1].startswith("frame yok")


def test_ctrl_c_restores_settings():
    def corrupt(speed, value, tlm):
        if value == 48:
            raise KeyboardInterrupt
        return None

    fw = FakeFirmware(corrupt=corrupt)
    with pytest.raises(KeyboardInterrupt):
        run_with(fw, loopback.build_cases([600], quick=True))
    assert fw.timeout == 500 and fw.value == 0 and fw.stream == 0


def test_cli_loopback_end_to_end(monkeypatch, capsys):
    fw = FakeFirmware()
    monkeypatch.setattr(cli, "Link", lambda port: Link(port, timeout=0.2, serial_obj=FakeSerial(fw)))
    monkeypatch.setattr(loopback.time, "sleep", lambda s: None)
    assert cli.main(["loopback-test", "--port", "X", "--quick", "--speeds", "600"]) == 0
    assert "sıfır uyuşmazlık" in capsys.readouterr().out


def test_cli_reports_device_error(monkeypatch, capsys):
    monkeypatch.setattr(cli, "Link", lambda port: Link(port, timeout=0.2, serial_obj=FakeSerial(lambda l: "ERR busy\n")))
    assert cli.main(["tx", "speed", "300", "--port", "X"]) == 2
    assert "busy" in capsys.readouterr().err
