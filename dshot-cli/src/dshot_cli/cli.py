"""dshot komut satırı aracı."""
import argparse
import sys
import threading
import time

from . import commands, discover, loopback
from .link import DShotError, Link, parse_kv

KEEPALIVE_S = 0.1

CHIPS = {
    "0x423": "STM32F401xB/C",
    "0x433": "STM32F401xD/E",
    "0x431": "STM32F411xC/E",
    "0x410": "STM32F103 medium-density",
    "0x000": "okunamadı (F103'te debugger yokken normal)",
}

TX_WORDS = {"SPEED", "RATE", "THR", "RAW", "TLM", "CMD", "STOP", "ENABLE", "TIMEOUT"}
RX_WORDS = {"STREAM", "POL", "RESET"}

SHELL_HELP = """Komutlar (TX/RX öneki isteğe bağlı):
  thr <0..2000>          throttle (0 = dur)
  raw <0..2047> [0|1]    ham değer [+ telemetri biti]
  speed 150|300|600|1200 hız (yalnız değer 0 iken)
  rate <hz>              frame/sn
  tlm 0|1                throttle frame'lerinde telemetri biti
  cmd <isim|1..47> [n]   özel komut (ör. cmd beep1, cmd spin_reversed)
  stop                   değer 0
  enable 0|1             pals üretimi
  timeout <ms>           failsafe (0 = kapalı)
  stream <ms> / pol auto|normal|inverted / reset   (okuyucu)
  status                 durum
  quit                   çık (çıkarken TX STOP gönderilir)"""


def shell_line_to_command(text: str):
    """Shell girdisini protokol satırına çevirir; boşsa None."""
    parts = text.split()
    if not parts:
        return None
    head = parts[0].upper()
    if head == "CMD" and len(parts) >= 2:
        num, rep = commands.resolve(parts[1])
        return f"TX CMD {num} {parts[2] if len(parts) > 2 else rep}"
    if head in ("TX", "RX", "STATUS", "PING"):
        return " ".join(parts)
    if head in TX_WORDS:
        return "TX " + " ".join(parts)
    if head in RX_WORDS:
        return "RX " + " ".join(parts)
    return " ".join(parts)


def _open(args, cap) -> Link:
    port = args.port or discover.pick(cap, discover.find_devices())
    return Link(port)


def _hold(link: Link) -> int:
    print("Değer tutuluyor — durdurmak için Ctrl+C", file=sys.stderr)
    try:
        while True:
            link.command("PING")
            for line in link.poll_async():
                print(line)
            time.sleep(KEEPALIVE_S)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            link.command("TX STOP")
            print("TX STOP gönderildi", file=sys.stderr)
        except DShotError as e:
            print(f"uyarı: TX STOP gönderilemedi: {e}", file=sys.stderr)
    return 0


def _warn_failsafe(link: Link):
    st = parse_kv(link.command("STATUS"))
    if st.get("tx_timeout", "0") != "0":
        print(
            f"uyarı: failsafe açık (timeout={st['tx_timeout']} ms) — program çıkınca değer 0'a düşer. "
            "Tutmak için --hold kullan ya da 'dshot tx timeout 0' ile failsafe'i kapat.",
            file=sys.stderr,
        )


def _shell(link: Link) -> int:
    stop = threading.Event()

    def keepalive():
        while not stop.wait(KEEPALIVE_S):
            try:
                link.command("PING")
                for line in link.poll_async():
                    print(f"\r{line}")
            except DShotError as e:
                print(f"\rbağlantı hatası: {e}")
                stop.set()

    th = threading.Thread(target=keepalive, daemon=True)
    th.start()
    print("DShot shell — 'help' yardım, 'quit' çıkış")
    try:
        while not stop.is_set():
            try:
                text = input("dshot> ")
            except EOFError:
                break
            word = text.strip().lower()
            if word in ("quit", "exit"):
                break
            if word in ("help", "?"):
                print(SHELL_HELP)
                continue
            try:
                cmd = shell_line_to_command(text)
                if cmd is None:
                    continue
                reply = link.command(cmd, timeout=5.0)
                print("OK" + (f" {reply}" if reply else ""))
            except DShotError as e:
                print(f"ERR {e}")
    except KeyboardInterrupt:
        print()
    finally:
        stop.set()
        th.join(timeout=1)
        try:
            link.command("TX STOP")
        except DShotError:
            pass
    return 0


def cmd_ports(args) -> int:
    devs = discover.find_devices()
    if not devs:
        print("DShot kartı bulunamadı.")
        return 1
    for d in devs:
        print(f"{d.port}\t{d.product}\t{','.join(sorted(d.caps))}\t{d.serial or ''}")
    return 0


def cmd_status(args) -> int:
    with _open(args, None) as link:
        st = parse_kv(link.command("STATUS"))
    for k, v in st.items():
        print(f"{k:12} {v}")
    print(f"{'chip':12} {CHIPS.get(st.get('dev', ''), 'bilinmiyor')}")
    return 0


def cmd_dfu(args) -> int:
    with _open(args, None) as link:
        link.command("DFU")
    print("Kart DFU moduna geçti. Yüklemek için: cd blackpill && pio run -e txrx -t upload")
    return 0


def cmd_tx(args) -> int:
    with _open(args, "tx") as link:
        a = args.action
        if a == "shell":
            return _shell(link)
        if a == "speed":
            link.command(f"TX SPEED {args.value}")
        elif a == "rate":
            link.command(f"TX RATE {args.value}")
        elif a in ("throttle", "raw"):
            if a == "throttle":
                link.command(f"TX THR {args.value}")
            else:
                link.command(f"TX RAW {args.value} {int(args.tlm)}")
            if args.hold:
                return _hold(link)
            _warn_failsafe(link)
        elif a == "tlm":
            link.command(f"TX TLM {int(args.state == 'on')}")
        elif a == "cmd":
            num, rep = commands.resolve(args.command)
            link.command(f"TX CMD {num} {args.repeat or rep}", timeout=5.0)
        elif a == "stop":
            link.command("TX STOP")
        elif a == "enable":
            link.command(f"TX ENABLE {int(args.state == 'on')}")
        elif a == "timeout":
            link.command(f"TX TIMEOUT {args.ms}")
        print("OK")
    return 0


def cmd_rx(args) -> int:
    with _open(args, "rx") as link:
        if args.action == "pol":
            link.command(f"RX POL {args.mode.upper()}")
            print("OK")
            return 0
        link.command(f"RX STREAM {args.interval}")
        link.command(f"RX RAW {args.raw}")
        try:
            while True:
                line = link.wait_async("#", timeout=1.0)
                if line:
                    print(line[1:], flush=True)
        except KeyboardInterrupt:
            pass
        finally:
            for c in ("RX STREAM 0", "RX RAW 0"):
                try:
                    link.command(c)
                except DShotError:
                    pass
    return 0


def cmd_loopback(args) -> int:
    speeds = [int(s) for s in args.speeds.split(",")]
    for s in speeds:
        if s not in loopback.SPEEDS:
            raise DShotError(f"geçersiz hız: {s}")
    if args.port:
        tx_port = rx_port = args.port
    elif args.tx and args.rx:
        tx_port, rx_port = args.tx, args.rx
    elif args.tx or args.rx:
        raise DShotError("--tx ve --rx birlikte verilmeli")
    else:
        tx_port, rx_port = discover.pick_loopback(discover.find_devices())
    cases = loopback.build_cases(speeds, quick=args.quick)
    tx = Link(tx_port)
    rx = tx if rx_port == tx_port else Link(rx_port)
    try:
        def progress(i, n):
            print(f"\r{i}/{n}", end="", file=sys.stderr, flush=True)

        results = loopback.run(tx, rx, cases, progress=progress)
        print(file=sys.stderr)
    finally:
        tx.close()
        if rx is not tx:
            rx.close()
    print(loopback.format_report(results))
    return 0 if all(r.ok for r in results) else 1


def cmd_ui(args) -> int:
    from . import tui

    return tui.run(args.port)


def build_parser() -> argparse.ArgumentParser:
    port = argparse.ArgumentParser(add_help=False)
    port.add_argument("--port", help="seri port (varsayılan: otomatik bul)")

    p = argparse.ArgumentParser(prog="dshot", description="DShot gönderici/okuyucu kontrol aracı. "
                                "Argümansız çalıştırınca kontrol paneli açılır.")
    sub = p.add_subparsers(dest="group")
    sub.add_parser("ui", parents=[port], help="tek ekranlı kontrol paneli (varsayılan)").set_defaults(func=cmd_ui)

    sub.add_parser("ports", help="bağlı kartları listele").set_defaults(func=cmd_ports)
    sub.add_parser("status", parents=[port], help="kart durumu").set_defaults(func=cmd_status)
    sub.add_parser("dfu", parents=[port], help="kartı DFU (yükleme) moduna al").set_defaults(func=cmd_dfu)

    tx = sub.add_parser("tx", help="gönderici")
    tx.set_defaults(func=cmd_tx)
    txs = tx.add_subparsers(dest="action", required=True)
    s = txs.add_parser("speed", parents=[port])
    s.add_argument("value", type=int, choices=[150, 300, 600, 1200])
    s = txs.add_parser("rate", parents=[port], help="frame/sn")
    s.add_argument("value", type=int)
    s = txs.add_parser("throttle", parents=[port], help="0..2000 (0 = dur)")
    s.add_argument("value", type=int)
    s.add_argument("--hold", action="store_true", help="Ctrl+C'ye kadar tut, çıkışta dur")
    s = txs.add_parser("raw", parents=[port], help="ham DShot değeri 0..2047")
    s.add_argument("value", type=int)
    s.add_argument("--tlm", action="store_true", help="telemetri biti")
    s.add_argument("--hold", action="store_true")
    s = txs.add_parser("tlm", parents=[port])
    s.add_argument("state", choices=["on", "off"])
    s = txs.add_parser("cmd", parents=[port], help="özel komut: isim veya 1..47")
    s.add_argument("command")
    s.add_argument("--repeat", type=int)
    txs.add_parser("stop", parents=[port])
    s = txs.add_parser("enable", parents=[port])
    s.add_argument("state", choices=["on", "off"])
    s = txs.add_parser("timeout", parents=[port], help="failsafe ms (0 = kapalı)")
    s.add_argument("ms", type=int)
    txs.add_parser("shell", parents=[port], help="interaktif kontrol")

    rx = sub.add_parser("rx", help="okuyucu")
    rx.set_defaults(func=cmd_rx)
    rxs = rx.add_subparsers(dest="action", required=True)
    s = rxs.add_parser("monitor", parents=[port], help="canlı özet")
    s.add_argument("--interval", type=int, default=200, help="özet periyodu ms")
    s.add_argument("--raw", type=int, default=0, help="her N'inci frame'i ham bas")
    s = rxs.add_parser("pol", parents=[port])
    s.add_argument("mode", choices=["auto", "normal", "inverted"])

    lb = sub.add_parser("loopback-test", help="TX→RX kablosuyla kabul testi")
    lb.set_defaults(func=cmd_loopback)
    lb.add_argument("--port", help="tek TXRX kart (PA8→PB6)")
    lb.add_argument("--tx")
    lb.add_argument("--rx")
    lb.add_argument("--speeds", default="150,300,600,1200")
    lb.add_argument("--quick", action="store_true")
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if args.group is None:
        args.func, args.port = cmd_ui, None
    try:
        return args.func(args)
    except DShotError as e:
        print(f"hata: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
