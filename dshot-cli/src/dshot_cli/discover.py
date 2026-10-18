"""USB üzerinden DShot kartlarını bulma."""
from dataclasses import dataclass

from serial.tools import list_ports

from .link import DShotError

VID, PID = 0x0483, 0x5740
PRODUCT_CAPS = {
    "DShot TX": frozenset({"tx"}),
    "DShot RX": frozenset({"rx"}),
    "DShot TXRX": frozenset({"tx", "rx"}),
}


@dataclass(frozen=True)
class Device:
    port: str
    product: str
    caps: frozenset
    serial: str = None


def find_devices(ports=None) -> list:
    ports = list_ports.comports() if ports is None else ports
    return [
        Device(p.device, p.product, PRODUCT_CAPS[p.product], p.serial_number)
        for p in ports
        if p.vid == VID and p.pid == PID and p.product in PRODUCT_CAPS
    ]


def pick(cap, devices) -> str:
    """cap ('tx', 'rx' veya None=herhangi) yeteneğine sahip tek cihazın portu."""
    found = [d for d in devices if cap is None or cap in d.caps]
    if len(found) == 1:
        return found[0].port
    what = f"'{cap}' yetenekli " if cap else ""
    if not found:
        raise DShotError(f"{what}DShot kartı bulunamadı ('dshot ports' ile listele)")
    raise DShotError(f"birden fazla {what}kart var, --port ile seç: " + ", ".join(d.port for d in found))


def pick_loopback(devices) -> tuple:
    """(tx_port, rx_port): ayrı TX ve RX kartı ya da tek TXRX kartı."""
    tx_only = [d for d in devices if d.caps == {"tx"}]
    rx_only = [d for d in devices if d.caps == {"rx"}]
    combo = [d for d in devices if d.caps == {"tx", "rx"}]
    if len(tx_only) == 1 and len(rx_only) == 1:
        return tx_only[0].port, rx_only[0].port
    if len(combo) == 1 and not tx_only and not rx_only:
        return combo[0].port, combo[0].port
    raise DShotError("loopback için kartlar belirsiz; --port P (tek kart) veya --tx P --rx P kullan")
