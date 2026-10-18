"""Tek ekranlı kontrol paneli: gönderim klavyeyle, okuma canlı."""
import curses
import time

from . import discover
from .cli import shell_line_to_command
from .describe import throttle_to_value, value_meaning
from .link import DShotError, Link, parse_kv

SPEEDS = (150, 300, 600, 1200)
TICK_S = 0.1
STATUS_EVERY = 5
RX_STREAM_MS = 250
RX_STALE_S = 1.5

HELP = [
    "↑/↓ throttle ±10   ←/→ ±1   PgUp/PgDn ±100   Boşluk/0: DUR",
    "s: hız değiştir   t: telemetri biti   e: çıkış aç/kapa   b: bip",
    ":  komut yaz (ör. cmd spin_reversed, rate 2000, raw 1046 1, timeout 0)",
    "q: çık (çıkarken motor durdurulur)",
]


class Dashboard:
    def __init__(self, tx: Link, rx: Link, clock=time.monotonic):
        self.tx, self.rx, self.clock = tx, rx, clock
        self.throttle = 0
        self.status = {}
        self.rx_stats = {}
        self.rx_time = None
        self.message = "Hazır"
        self.input = None  # komut satırı modunda yazılan metin
        self._ticks = 0
        self.refresh_status()
        if self.rx:
            self.rx.command("RX POL AUTO")
            self.rx.command(f"RX STREAM {RX_STREAM_MS}")

    # --- kart ile konuşma ---
    def _tx(self, cmd, timeout=None):
        try:
            reply = self.tx.command(cmd, timeout=timeout)
            self.message = f"{cmd} → OK" + (f" {reply}" if reply else "")
            return True
        except DShotError as e:
            self.message = f"{cmd} → HATA: {e}"
            return False

    def refresh_status(self):
        if self.tx:
            self.status = parse_kv(self.tx.command("STATUS"))
            self.throttle = max(0, int(self.status.get("tx_val", 0)) - 47)

    def set_throttle(self, throttle):
        throttle = max(0, min(2000, throttle))
        if self._tx(f"TX THR {throttle}"):
            self.throttle = throttle
            self.status["tx_val"] = str(throttle_to_value(throttle))

    def stop(self):
        if self._tx("TX STOP"):
            self.throttle = 0
            self.status["tx_val"] = "0"

    def cycle_speed(self):
        cur = int(self.status.get("tx_speed", 600))
        nxt = SPEEDS[(SPEEDS.index(cur) + 1) % len(SPEEDS)] if cur in SPEEDS else 600
        stopped = self.throttle > 0
        if stopped:
            self.stop()
        if self._tx(f"TX SPEED {nxt}"):
            self.status["tx_speed"] = str(nxt)
            if stopped:
                self.message += "  (hız değişimi için motor durduruldu)"

    def run_command(self, text):
        try:
            cmd = shell_line_to_command(text)
        except DShotError as e:
            self.message = f"HATA: {e}"
            return
        if not cmd:
            return
        link = self.rx if cmd.upper().startswith("RX") and self.rx else self.tx
        try:
            reply = link.command(cmd, timeout=5.0)
            self.message = f"{cmd} → OK" + (f" {reply}" if reply else "")
        except DShotError as e:
            self.message = f"{cmd} → HATA: {e}"
        self.refresh_status()

    def tick(self):
        """Her TICK_S'de: failsafe'i besle, okumaları topla."""
        self._ticks += 1
        if self.tx:
            self.tx.command("PING")
            if self._ticks % STATUS_EVERY == 0:
                self.refresh_status()
        for link in {id(l): l for l in (self.tx, self.rx) if l}.values():
            for line in link.poll_async():
                self.on_async(line)

    def on_async(self, line):
        if line.startswith("#RX"):
            self.rx_stats = parse_kv(line[3:])
            self.rx_time = self.clock()
        elif line.startswith("#TX FAILSAFE"):
            self.throttle = 0
            self.message = "FAILSAFE: bağlantı kesildi sanıldı, motor durduruldu"

    def close(self):
        for fn in (lambda: self.tx and self.tx.command("TX STOP"),
                   lambda: self.rx and self.rx.command("RX STREAM 0")):
            try:
                fn()
            except DShotError:
                pass

    # --- klavye ---
    def handle_key(self, key) -> bool:
        """False dönerse çık."""
        if self.input is not None:
            if key in (curses.KEY_ENTER, 10, 13):
                text, self.input = self.input, None
                self.run_command(text)
            elif key == 27:
                self.input = None
            elif key in (curses.KEY_BACKSPACE, 127, 8):
                self.input = self.input[:-1]
            elif 32 <= key < 127:
                self.input += chr(key)
            return True
        steps = {curses.KEY_UP: 10, curses.KEY_DOWN: -10, curses.KEY_RIGHT: 1, curses.KEY_LEFT: -1,
                 curses.KEY_PPAGE: 100, curses.KEY_NPAGE: -100}
        if key in steps and self.tx:
            self.set_throttle(self.throttle + steps[key])
        elif key in (ord(" "), ord("0")) and self.tx:
            self.stop()
        elif key == ord("s") and self.tx:
            self.cycle_speed()
        elif key == ord("t") and self.tx:
            on = self.status.get("tx_tlm") == "1"
            if self._tx(f"TX TLM {0 if on else 1}"):
                self.status["tx_tlm"] = "0" if on else "1"
        elif key == ord("e") and self.tx:
            on = self.status.get("tx_en", "1") == "1"
            if self._tx(f"TX ENABLE {0 if on else 1}"):
                self.status["tx_en"] = "0" if on else "1"
        elif key == ord("b") and self.tx:
            self._tx("TX CMD 1 1", timeout=5.0)
        elif key in (ord(":"), 10, 13):
            self.input = ""
        elif key in (ord("q"), ord("Q")):
            return False
        return True

    # --- ekran ---
    def lines(self) -> list:
        out = []
        st = self.status
        if self.tx:
            val = int(st.get("tx_val", 0))
            timeout = st.get("tx_timeout", "?")
            out += [
                "── GÖNDERİM (PA8) " + "─" * 40,
                f"  Hız: DShot{st.get('tx_speed', '?')}    Frame/sn: {st.get('tx_rate', '?')}    "
                f"Çıkış: {'AÇIK' if st.get('tx_en', '1') == '1' else 'KAPALI'}    "
                f"Failsafe: {'kapalı' if timeout == '0' else timeout + ' ms'}",
                f"  Gönderilen: {val:4}  → {value_meaning(val)}",
                f"  Telemetri biti: {st.get('tx_tlm', '0')}",
                "",
            ]
        if self.rx:
            out.append("── OKUMA (PB6) " + "─" * 43)
            s = self.rx_stats
            fresh = self.rx_time is not None and self.clock() - self.rx_time < RX_STALE_S
            n = int(s.get("n", 0)) if fresh else 0
            if not fresh:
                out.append("  Durum: okuyucudan veri gelmiyor")
            elif n == 0:
                bad = int(s.get("other", 0)) + int(s.get("crc_err", 0))
                out.append("  Durum: " + ("sinyal var ama çözülemiyor (gürültü / yanlış bağlantı)" if bad
                                          else "SİNYAL YOK (PB6'ya bağlı mı? GND ortak mı?)"))
            else:
                vmin, vmax = int(s["vmin"]), int(s["vmax"])
                speed = s.get("speed", "?")
                inv = s.get("inv") == "1"
                out += [
                    f"  Durum: sinyal var ✓   Algılanan hız: {'DShot' + speed if speed != '?' else 'standart dışı'}"
                    f"   Polarite: {'ters (bidirectional)' if inv else 'normal'}",
                    f"  Okunan: {int(s['val']):4}  → {value_meaning(int(s['val']))}",
                    f"  Aralık: {vmin}–{vmax}" + ("  (sabit)" if vmin == vmax else "  (değişiyor)")
                    + f"    Telemetri biti: {s.get('tlm', '-')}",
                    f"  Frame/sn: {s.get('rate', '?')}    CRC hatası: {s.get('crc_err', '0')}    "
                    f"Bozuk burst: {s.get('other', '0')}",
                    f"  Bit süresi: {s.get('bit_ns', '?')} ns    0-biti high: {s.get('t0h_ns', '?')} ns    "
                    f"1-biti high: {s.get('t1h_ns', '?')} ns",
                ]
            out.append("")
        out.append("─" * 58)
        out += ["  " + h for h in HELP]
        out.append("")
        out.append(f"  > {self.input}_" if self.input is not None else f"  {self.message}")
        return out


def _open_links(port):
    if port:
        link = Link(port)
        caps = parse_kv(link.command("STATUS")).get("caps", "")
        return (link if "tx" in caps else None), (link if "rx" in caps else None)
    devs = discover.find_devices()
    if not devs:
        raise DShotError("DShot kartı bulunamadı ('dshot ports' ile kontrol et)")
    combo = [d for d in devs if d.caps == {"tx", "rx"}]
    if combo:
        link = Link(combo[0].port)
        return link, link
    tx = [d for d in devs if "tx" in d.caps]
    rx = [d for d in devs if "rx" in d.caps]
    return (Link(tx[0].port) if tx else None), (Link(rx[0].port) if rx else None)


def _loop(scr, dash: Dashboard, title: str):
    curses.curs_set(0)
    scr.timeout(int(TICK_S * 1000))
    scr.keypad(True)
    next_tick = 0.0
    while True:
        now = time.monotonic()
        if now >= next_tick:
            dash.tick()
            next_tick = now + TICK_S
        scr.erase()
        h, w = scr.getmaxyx()
        rows = [title, ""] + dash.lines()
        for y, text in enumerate(rows[: h - 1]):
            scr.addnstr(y, 0, text, w - 1, curses.A_BOLD if y == 0 else 0)
        scr.refresh()
        key = scr.getch()
        if key != -1 and not dash.handle_key(key):
            return


def run(port=None) -> int:
    tx, rx = _open_links(port)
    links = {id(l): l for l in (tx, rx) if l}
    try:
        first = tx or rx
        st = parse_kv(first.command("STATUS"))
        title = f" DShot Kontrol — {first.port}  ({st.get('board', '?')}, dev {st.get('dev', '?')}, fw {st.get('fw', '?')})"
        dash = Dashboard(tx, rx)
        try:
            curses.wrapper(_loop, dash, title)
        except KeyboardInterrupt:
            pass
        finally:
            dash.close()
    finally:
        for l in links.values():
            l.close()
    return 0
