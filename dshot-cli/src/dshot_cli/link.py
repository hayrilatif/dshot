"""Kartla satır tabanlı seri haberleşme."""
import collections
import threading
import time

import serial


class DShotError(Exception):
    """Cihazdan ERR cevabı, zaman aşımı veya bağlantı hatası."""


def parse_kv(text: str) -> dict:
    """'a=1 b=x' → {'a': '1', 'b': 'x'}; '=' içermeyen kelimeler atlanır."""
    out = {}
    for tok in text.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


class Link:
    def __init__(self, port: str, timeout: float = 1.0, serial_obj=None):
        self.port = port
        self.timeout = timeout
        try:
            self._ser = serial_obj if serial_obj is not None else serial.Serial(port, 115200, timeout=0.02)
        except serial.SerialException as e:
            raise DShotError(f"{port} açılamadı: {e}") from e
        self._buf = bytearray()
        self._async = collections.deque(maxlen=10000)
        self._lock = threading.RLock()

    def close(self):
        try:
            self._ser.close()
        except serial.SerialException:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read_line(self, deadline: float):
        while True:
            i = self._buf.find(b"\n")
            if i >= 0:
                raw = bytes(self._buf[:i])
                del self._buf[: i + 1]
                return raw.decode("ascii", "replace").rstrip("\r")
            try:
                chunk = self._ser.read(self._ser.in_waiting or 1)
            except serial.SerialException as e:
                raise DShotError(f"{self.port}: bağlantı koptu: {e}") from e
            if chunk:
                self._buf += chunk
                continue
            if time.monotonic() >= deadline:
                return None

    def command(self, line: str, timeout: float = None) -> str:
        """Komutu gönderir, 'OK' sonrası metni döner; ERR'de DShotError."""
        with self._lock:
            try:
                self._ser.write(line.encode("ascii") + b"\n")
            except serial.SerialException as e:
                raise DShotError(f"{self.port}: yazılamadı: {e}") from e
            deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
            while True:
                reply = self._read_line(deadline)
                if reply is None:
                    raise DShotError(f"{self.port}: '{line}' için cevap gelmedi")
                if reply.startswith("#"):
                    self._async.append(reply)
                elif reply == "OK" or reply.startswith("OK "):
                    return reply[2:].strip()
                elif reply.startswith("ERR"):
                    raise DShotError(f"{line}: {reply[3:].strip()}")
                # diğer satırlar (açılıştaki artıklar) yok sayılır

    def drain_async(self):
        """Şimdiye kadar biriken asenkron satırları atar."""
        with self._lock:
            self._async.clear()

    def wait_async(self, prefix: str, timeout: float):
        """prefix ile başlayan ilk asenkron satırı bekler; yoksa None."""
        deadline = time.monotonic() + timeout
        with self._lock:
            while True:
                while self._async:
                    line = self._async.popleft()
                    if line.startswith(prefix):
                        return line
                line = self._read_line(deadline)
                if line is None:
                    return None
                if line.startswith("#"):
                    self._async.append(line)

    def poll_async(self) -> list:
        """Beklemeden, gelmiş asenkron satırları döner."""
        with self._lock:
            while True:
                line = self._read_line(time.monotonic())
                if line is None:
                    break
                if line.startswith("#"):
                    self._async.append(line)
            out = list(self._async)
            self._async.clear()
            return out
