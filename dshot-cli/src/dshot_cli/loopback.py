"""Loopback kabul testi: TX çıkışı RX girişine bağlıyken değerleri tarar."""
import random
import time
from dataclasses import dataclass

from .link import DShotError, Link, parse_kv

SPEEDS = (150, 300, 600, 1200)
NOMINAL_BIT_NS = {s: 1_000_000 / s for s in SPEEDS}
TIMING_TOLERANCE = 0.15


@dataclass(frozen=True)
class Case:
    speed: int
    value: int
    tlm: int


@dataclass
class Result:
    case: Case
    ok: bool
    detail: str = ""


def build_cases(speeds, quick: bool = False, seed: int = 1) -> list:
    rng = random.Random(seed)
    if quick:
        values = {0, 1, 21, 47, 48, 1046, 2047} | set(rng.sample(range(49, 2047), 3))
    else:
        values = set(range(0, 49)) | {2047} | set(rng.sample(range(49, 2047), 40))
    # Artan sıra: throttle'dan özel komuta geçiş (firmware 'busy' der) olmaz
    return [Case(s, v, t) for s in speeds for v in sorted(values) for t in (0, 1)]


def check(stats: dict, case: Case) -> tuple:
    def num(key):
        try:
            return int(stats[key])
        except (KeyError, ValueError):
            return None

    problems = []
    if not num("n"):
        problems.append("frame yok")
    else:
        for key in ("vmin", "vmax"):
            if num(key) != case.value:
                problems.append(f"{key}={stats.get(key)}")
        if stats.get("tlm") != str(case.tlm):
            problems.append(f"tlm={stats.get('tlm')}")
        if stats.get("speed") != str(case.speed):
            problems.append(f"speed={stats.get('speed')}")
        if stats.get("inv") != "0":
            problems.append(f"inv={stats.get('inv')}")
        nominal = NOMINAL_BIT_NS[case.speed]
        for key, frac in (("t0h_ns", 0.375), ("t1h_ns", 0.75)):
            v = num(key)
            if v and abs(v - nominal * frac) > nominal * frac * TIMING_TOLERANCE:
                problems.append(f"{key}={v}")
    for key in ("crc_err", "other"):
        if num(key):
            problems.append(f"{key}={stats.get(key)}")
    return not problems, ", ".join(problems)


def run(tx: Link, rx: Link, cases, window_ms: int = 50, settle_s: float = 0.03, progress=None) -> list:
    old_timeout = parse_kv(tx.command("STATUS")).get("tx_timeout", "500")
    results = []
    tx.command("TX TIMEOUT 0")
    try:
        tx.command("TX RAW 0")
        rx.command("RX POL NORMAL")
        rx.command(f"RX STREAM {window_ms}")
        speed = None
        for i, case in enumerate(cases):
            if case.speed != speed:
                tx.command("TX RAW 0")
                tx.command(f"TX SPEED {case.speed}")
                speed = case.speed
            tx.command(f"TX RAW {case.value} {case.tlm}")
            time.sleep(settle_s)
            rx.command("RX RESET")
            rx.drain_async()
            line = rx.wait_async("#RX", timeout=window_ms / 1000 * 4 + 0.5)
            if line is None:
                results.append(Result(case, False, "#RX özeti gelmedi"))
            else:
                ok, detail = check(parse_kv(line), case)
                results.append(Result(case, ok, detail))
            if progress:
                progress(i + 1, len(cases))
    finally:
        for link, cmd in ((tx, "TX RAW 0"), (rx, "RX STREAM 0"), (rx, "RX POL AUTO"), (tx, f"TX TIMEOUT {old_timeout}")):
            try:
                link.command(cmd)
            except DShotError:
                pass
    return results


def format_report(results) -> str:
    lines = ["Hız     Geçti  Kaldı"]
    for s in sorted({r.case.speed for r in results}):
        rs = [r for r in results if r.case.speed == s]
        ok = sum(r.ok for r in rs)
        lines.append(f"{s:<7} {ok:>5}  {len(rs) - ok:>5}")
    bad = [r for r in results if not r.ok]
    lines.append("")
    if bad:
        lines.append(f"Uyuşmazlıklar ({len(bad)}):")
        for r in bad[:30]:
            lines.append(f"  DShot{r.case.speed} değer={r.case.value} tlm={r.case.tlm}: {r.detail}")
        if len(bad) > 30:
            lines.append(f"  ... ve {len(bad) - 30} tane daha")
    else:
        lines.append(f"SONUÇ: {len(results)} durumda sıfır uyuşmazlık")
    return "\n".join(lines)
