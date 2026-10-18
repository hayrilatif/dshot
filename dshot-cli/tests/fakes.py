"""Seri port ve firmware taklitleri."""


class FakeSerial:
    def __init__(self, handler):
        self.handler = handler
        self.rx = bytearray()
        self.written = []
        self.closed = False

    @property
    def in_waiting(self):
        return len(self.rx)

    def write(self, data):
        text = data.decode()
        self.written.append(text.strip())
        for line in text.splitlines():
            self.rx += self.handler(line).encode()

    def read(self, n):
        out = bytes(self.rx[:n])
        del self.rx[:n]
        return out

    def close(self):
        self.closed = True


class FakeFirmware:
    """TXRX kartın loopback'e yetecek kadar taklidi. corrupt(speed, value, tlm) → bozuk değer ya da None."""

    def __init__(self, corrupt=None):
        self.speed, self.value, self.tlm = 600, 0, 0
        self.timeout, self.stream, self.pol = 500, 0, "auto"
        self.corrupt = corrupt or (lambda speed, value, tlm: None)

    def __call__(self, line):
        t = line.split()
        u = [x.upper() for x in t]
        if u == ["STATUS"]:
            return (
                f"OK board=blackpill caps=tx,rx dev=0x423 fw=1.0.0 tx_speed={self.speed} tx_rate=1000 "
                f"tx_val={self.value} tx_tlm={self.tlm} tx_en=1 tx_timeout={self.timeout} "
                f"rx_pol={self.pol} rx_inv=0 rx_stream={self.stream}\n"
            )
        if u[:2] == ["TX", "SPEED"]:
            self.speed = int(t[2])
        elif u[:2] == ["TX", "RAW"]:
            self.value, self.tlm = int(t[2]), int(t[3]) if len(t) > 3 else 0
        elif u[:2] == ["TX", "TIMEOUT"]:
            self.timeout = int(t[2])
        elif u[:2] == ["RX", "STREAM"]:
            self.stream = int(t[2])
        elif u[:2] == ["RX", "POL"]:
            self.pol = t[2].lower()
        elif u[:2] == ["RX", "RESET"]:
            bad = self.corrupt(self.speed, self.value, self.tlm)
            val = self.value if bad is None else bad
            bit = round(1e6 / self.speed)
            return (
                "#RX n=3 rate=1000 val=1 vmin=1 vmax=1 tlm=0 crc_err=0 other=0 inv=0 speed=600 bit_ns=1 "
                "t0h_ns=0 t1h_ns=0\n"  # RESET'ten önceki eski özet: drain edilmeli
                "OK\n"
                f"#RX n=50 rate=1000 val={val} vmin={val} vmax={val} tlm={self.tlm} crc_err=0 other=0 inv=0 "
                f"speed={self.speed} bit_ns={bit} t0h_ns={round(bit * 0.375)} t1h_ns={round(bit * 0.75)}\n"
            )
        return "OK\n"
