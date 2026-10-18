import curses

from dshot_cli import describe
from dshot_cli.link import Link
from dshot_cli.tui import Dashboard
from fakes import FakeSerial


class PanelFirmware:
    def __init__(self):
        self.val, self.speed, self.tlm, self.log = 0, 600, 0, []

    def __call__(self, line):
        self.log.append(line)
        u = line.upper().split()
        if u == ["STATUS"]:
            return (f"OK board=blackpill caps=tx,rx dev=0x423 fw=1.0.0 tx_speed={self.speed} tx_rate=1000 "
                    f"tx_val={self.val} tx_tlm={self.tlm} tx_en=1 tx_timeout=500 rx_pol=auto rx_inv=0 rx_stream=0\n")
        if u[:2] == ["TX", "THR"]:
            self.val = int(u[2]) + 47 if int(u[2]) else 0
        elif u[:2] == ["TX", "STOP"]:
            self.val = 0
        elif u[:2] == ["TX", "SPEED"]:
            if self.val:
                return "ERR busy\n"
            self.speed = int(u[2])
        elif u == ["PING"]:
            return ("OK\n#RX n=250 rate=1000 val=297 vmin=297 vmax=297 tlm=0 crc_err=0 other=0 inv=0 "
                    "speed=600 bit_ns=1667 t0h_ns=631 t1h_ns=1250\n")
        return "OK\n"


def make():
    fw = PanelFirmware()
    link = Link("FAKE", timeout=0.2, serial_obj=FakeSerial(fw))
    return Dashboard(link, link, clock=lambda: 100.0), fw


def test_value_meaning():
    assert describe.value_meaning(0).startswith("DUR")
    assert describe.value_meaning(21) == "özel komut 21 (spin_reversed)"
    assert describe.value_meaning(48) == "throttle 1/2000 (%0.0)"
    assert describe.value_meaning(2047) == "throttle 2000/2000 (%100.0)"


def test_keys_change_throttle_and_stop():
    d, fw = make()
    d.handle_key(curses.KEY_UP)
    d.handle_key(curses.KEY_UP)
    d.handle_key(curses.KEY_RIGHT)
    assert d.throttle == 21 and fw.val == 68
    d.handle_key(curses.KEY_NPAGE)
    assert d.throttle == 0 and fw.val == 0  # 0'ın altına inmez
    d.handle_key(curses.KEY_PPAGE)
    d.handle_key(ord(" "))
    assert fw.val == 0 and d.throttle == 0


def test_speed_change_stops_motor_first():
    d, fw = make()
    d.set_throttle(100)
    d.handle_key(ord("s"))
    assert fw.speed == 1200 and fw.val == 0 and "durduruldu" in d.message


def test_tick_feeds_failsafe_and_shows_reading():
    d, fw = make()
    d.tick()
    assert "PING" in fw.log
    text = "\n".join(d.lines())
    assert "sinyal var" in text and "Okunan:  297" in text and "throttle 250/2000" in text
    assert "DShot600" in text


def test_command_line_mode():
    d, fw = make()
    d.handle_key(ord(":"))
    for ch in "thr 5":
        d.handle_key(ord(ch))
    d.handle_key(10)
    assert fw.val == 52 and d.input is None and "OK" in d.message


def test_quit_and_failsafe_message():
    d, fw = make()
    d.on_async("#TX FAILSAFE")
    assert "FAILSAFE" in d.message
    assert d.handle_key(ord("q")) is False
    d.close()
    assert fw.log[-3:] == ["TX STOP", "TX TELEM 0", "RX STREAM 0"]


class BidirFirmware(PanelFirmware):
    def __init__(self, telem):
        super().__init__()
        self.bidir, self.telem = 0, telem

    def __call__(self, line):
        u = line.upper().split()
        if u[:2] == ["TX", "BIDIR"]:
            self.log.append(line)
            if self.val:
                return "ERR busy\n"
            self.bidir = int(u[2])
            return "OK\n"
        if u == ["STATUS"]:
            self.log.append(line)
            return super().__call__(line).replace("tx_timeout=500", f"tx_timeout=500 tx_bidir={self.bidir}")
        if u == ["PING"]:
            self.log.append(line)
            return f"OK\n#T {self.telem}\n"
        return super().__call__(line)


def make_bidir(telem):
    fw = BidirFirmware(telem)
    link = Link("FAKE", timeout=0.2, serial_obj=FakeSerial(fw))
    return Dashboard(link, link, clock=lambda: 100.0), fw


def test_bidir_toggle_stops_motor_and_shows_telemetry():
    d, fw = make_bidir("ok=240 bad=5 miss=5 erpm=70000 temp_c=35 volt_mv=16000 curr_a=3 stress=- status=-")
    assert "TX TELEM 250" in fw.log
    d.set_throttle(100)
    d.handle_key(ord("d"))
    assert fw.bidir == 1 and fw.val == 0
    d.tick()
    text = "\n".join(d.lines())
    assert "Bidirectional: AÇIK" in text
    assert "%96 başarılı" in text
    assert "Devir: 10000 RPM" in text  # 70000 / (14/2)
    assert "Sıcaklık: 35 °C" in text and "Voltaj: 16.00 V" in text and "Akım: 3 A" in text


def test_bidir_no_reply_hint_and_poles():
    d, fw = make_bidir("ok=0 bad=0 miss=250 erpm=- temp_c=- volt_mv=- curr_a=- stress=- status=-")
    d.handle_key(ord("d"))
    d.tick()
    text = "\n".join(d.lines())
    assert "ESC cevap vermiyor" in text and "gücünü kesip" in text
    d.run_command("poles 12")
    assert d.poles == 12
    d.run_command("poles 7")
    assert d.poles == 12 and "HATA" in d.message
