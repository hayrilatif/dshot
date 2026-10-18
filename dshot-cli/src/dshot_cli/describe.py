"""DShot değerlerinin insan diliyle açıklaması."""
from .commands import SPECIAL_COMMANDS

_CMD_NAMES = {v: k for k, v in SPECIAL_COMMANDS.items()}


def value_meaning(value: int) -> str:
    if value == 0:
        return "DUR (motor kapalı / arm sinyali)"
    if value <= 47:
        name = _CMD_NAMES.get(value)
        return f"özel komut {value}" + (f" ({name})" if name else "")
    throttle = value - 47
    return f"throttle {throttle}/2000 (%{(value - 48) / 1999 * 100:.1f})"


def throttle_to_value(throttle: int) -> int:
    return throttle + 47 if throttle > 0 else 0
