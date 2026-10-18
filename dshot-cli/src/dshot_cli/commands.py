"""DShot özel komutları (değer 1..47)."""
from .link import DShotError

SPECIAL_COMMANDS = {
    "beep1": 1, "beep2": 2, "beep3": 3, "beep4": 4, "beep5": 5,
    "esc_info": 6,
    "spin_dir_1": 7, "spin_dir_2": 8,
    "3d_off": 9, "3d_on": 10,
    "settings_request": 11,
    "save": 12,
    "spin_normal": 20, "spin_reversed": 21,
    "led0_on": 22, "led1_on": 23, "led2_on": 24, "led3_on": 25,
    "led0_off": 26, "led1_off": 27, "led2_off": 28, "led3_off": 29,
}
# ESC'lerin kabul etmesi için art arda birkaç kez gönderilmesi gerekenler
REPEAT_REQUIRED = {7, 8, 9, 10, 12, 20, 21}


def resolve(token: str) -> tuple:
    """İsim veya sayı → (komut, varsayılan tekrar)."""
    t = token.lower()
    if t in SPECIAL_COMMANDS:
        num = SPECIAL_COMMANDS[t]
    else:
        try:
            num = int(t)
        except ValueError:
            names = ", ".join(sorted(SPECIAL_COMMANDS))
            raise DShotError(f"bilinmeyen komut '{token}'. Bilinenler: {names}") from None
    if not 1 <= num <= 47:
        raise DShotError(f"özel komut 1..47 olmalı: {num}")
    return num, (10 if num in REPEAT_REQUIRED else 1)
