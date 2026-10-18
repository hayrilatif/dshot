"""DShot değerlerinin insan diliyle açıklaması."""
from .commands import SPECIAL_COMMANDS

_CMD_NAMES = {v: k for k, v in SPECIAL_COMMANDS.items()}


def value_meaning(value: int, mode3d: bool = False) -> str:
    if value == 0:
        return "DUR (motor kapalı / arm sinyali)"
    if value <= 47:
        name = _CMD_NAMES.get(value)
        return f"özel komut {value}" + (f" ({name})" if name else "")
    if mode3d:
        if value <= 1047:
            return f"3D GERİ %{(value - 47) / 1000 * 100:.1f}"
        if value == 1048:
            return "3D nötr"
        return f"3D İLERİ %{(value - 1048) / 999 * 100:.1f}"
    throttle = value - 47
    return f"throttle {throttle}/2000 (%{(value - 48) / 1999 * 100:.1f})"


def throttle_to_value(throttle: int) -> int:
    return throttle + 47 if throttle > 0 else 0
