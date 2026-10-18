"""DShot frame kodlama/çözme (firmware ile aynı kurallar)."""
from dataclasses import dataclass


def crc(v12: int, inverted: bool = False) -> int:
    c = (v12 ^ (v12 >> 4) ^ (v12 >> 8)) & 0xF
    return (~c) & 0xF if inverted else c


def encode(value: int, telemetry: bool = False, inverted: bool = False) -> int:
    if not 0 <= value <= 2047:
        raise ValueError(f"değer 0..2047 olmalı: {value}")
    v12 = (value << 1) | int(telemetry)
    return (v12 << 4) | crc(v12, inverted)


@dataclass(frozen=True)
class Decoded:
    value: int
    telemetry: bool
    crc_ok: bool


def decode(frame: int, inverted: bool = False) -> Decoded:
    v12 = (frame >> 4) & 0xFFF
    return Decoded(v12 >> 1, bool(v12 & 1), (frame & 0xF) == crc(v12, inverted))
