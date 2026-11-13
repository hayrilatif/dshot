# DShot araçları

ESC'lere saf DShot (150/300/600/1200) göndermek, DShot hatlarını okuyup çözmek ve bunları PC'den kontrol etmek için.

| Klasör | İçerik |
|---|---|
| `blackpill/` | STM32F401CC / F411CE firmware'i (PlatformIO + STM32Cube HAL). `env:txrx` gönderici + okuyucu, `env:tx` sadece gönderici |
| `bluepill/` | STM32F103C8 firmware'i — Blackpill'in aynısı. `env:txrx`, `env:tx`, `env:rx` (sadece okuyucu) |
| `common/` | Donanımdan bağımsız C kodu: frame, zamanlama, okuyucu çözücü, protokol, failsafe (+ native testler) |
| `dshot-cli/` | Python komut satırı aracı `dshot` |
| `test-vectors.json` | C ve Python testlerinin ortak frame vektörleri |

## Bağlantı

İki kartta da pinler aynı:

| Pin | İş |
|---|---|
| **PA8** | DShot çıkışı → ESC sinyal girişi (TIM1_CH1) |
| **PB6** | DShot girişi (TIM4, 5 V toleranslı, yüksek empedans) |
| **PB0** | Tetik çıkışı (osiloskop / lojik analizör için, aşağıya bak) |
| GND | ESC / ölçülen hat ile **ortak GND şart** |
| PC13 | LED (yanıp sönüyorsa firmware çalışıyor) |

### Tetik çıkışı (PB0)

Host'tan her **motor komutu** geldiğinde (`TX THR`, `TX RAW`, `TX STOP`, `TX CMD`, `TX RPM` — panelde ok tuşları,
Boşluk, bip, RPM hedefi) PB0, o komutu taşıyan **ilk DShot frame'i başlarken** high olur ve bir frame periyodu
(1000 Hz'de 1 ms) high kalır. PA8'deki frame'in ilk biti yükselen kenardan **1–2 bit süresi + kesme gecikmesi**
sonra başlar (DShot600'de ~1.7–3.5 µs, DShot150'de ~13 µs'ye kadar; CCR1 preload yüzünden).

- Osiloskopu PB0'ın yükselen kenarında tetikle, ESC'nin motor fazını diğer kanalda izle → komut ile ESC tepkisi
  arasındaki gecikme ekranda okunur.
- Mikrosaniye hassasiyeti gerekiyorsa PA8'i de bir kanala al: ESC komutu 16. bit bitince alır, asıl sıfır noktası
  frame'in sonudur.
- RPM kontrolcüsünün her frame'deki küçük ayarları ve failsafe tetiklemez; yalnız host'tan gelen komutlar.
  `TX RPM`'de tetik komutun geldiği anı gösterir; kontrolcü değeri bir sonraki telemetri cevabından sonra değiştirir.
- Komut aynı değeri tekrar gönderse de (ör. `TX THR 100` iki kez) tetikler.

Kendi kendine test için PA8 ↔ PB6 arasına jumper tak.

## Kurulum

```bash
python3 -m venv .venv && .venv/bin/pip install -e "dshot-cli[test]"
```

## Firmware yükleme

**Blackpill** — ilk sefer: BOOT0 basılıyken NRST'ye bas → DFU modu. Sonrakilerde tuşa gerek yok:

```bash
.venv/bin/dshot dfu                        # kartı DFU'ya al
cd blackpill && pio run -e txrx -t upload   # veya -e tx
```

**Bluepill** — F103'te USB DFU yok; ST-Link ile (SWDIO, SWCLK, GND, 3.3V):

```bash
cd bluepill && pio run -e txrx -t upload    # veya -e tx / -e rx
```

Bluepill'de `dev` STATUS'ta `0x000` görünebilir (F103, debugger bağlı değilken IDCODE'u okuyamaz; normal).

## Kullanım

En kolayı — **kontrol paneli** (gönderim + canlı okuma tek ekranda):

```bash
.venv/bin/dshot
```

| Tuş | İş |
|---|---|
| ↑ / ↓ | throttle ±10 (←/→ ±1, PgUp/PgDn ±100) |
| Boşluk veya 0 | DUR |
| s | hız değiştir 150→300→600→1200 (motor önce durdurulur) |
| d | bidirectional DShot (telemetri) aç/kapa (motor önce durdurulur) |
| r | RPM hedefi yaz (kapalı çevrim; RPM modunda oklar hedefi değiştirir) |
| t / e / b | telemetri biti / çıkış aç-kapa / bip |
| : | komut yaz (`cmd spin_reversed`, `rate 2000`, `raw 1046 1`, `timeout 0`...) |
| q | çık (motor durdurulur) |

Aynı anda **tek bir** `dshot` penceresi açık olmalı; ikincisi "port kullanılıyor" der.

Tek seferlik komutlar:

```bash
dshot ports                     # bağlı kartlar
dshot status                    # durum + çip tipi
dshot tx shell                  # interaktif: thr 200, speed 300, cmd beep1, stop, quit
dshot tx throttle 200 --hold    # Ctrl+C'ye kadar tut, çıkışta dur
dshot tx speed 300              # hız (yalnız değer 0 iken)
dshot tx rate 2000              # frame/sn (varsayılan 1000)
dshot tx cmd spin_reversed      # özel komut (değer 0 iken, gerekli tekrar sayısıyla)
dshot rx monitor                # okuyucu özeti (hız, değer aralığı, CRC hataları, zamanlama)
dshot rx monitor --raw 100      # + her 100. frame ve tüm bozuk burst'ler
dshot rx pol auto               # bidirectional (ters) DShot'ı otomatik algıla
dshot loopback-test             # PA8→PB6 jumper'ıyla kabul testi (tüm hızlar, ~1 dk)
                                # ⚠ 0..2047 TÜM değerleri gönderir: PA8'e ESC/motor BAĞLIYKEN ÇALIŞTIRMA
```

### Bidirectional DShot (aynı kablodan telemetri)

Panelde `d` veya `dshot tx bidir on`. Sinyal ters (boşta high) ve ters CRC'li gönderilir; her frame'den sonra PA8
girişe geçip ESC'nin cevabını yakalar. Panel devir (RPM; kutup sayısı `:poles 14`) ve ESC destekliyorsa
EDT ile sıcaklık / voltaj / akım gösterir (`:cmd edt_on` ile açılır).

- ESC firmware'i desteklemeli: Bluejay, AM32, BLHeli_32 (düz BLHeli_S desteklemez).
- ESC modu açılışta algılar: bidirectional açtıktan sonra ESC'nin gücünü kesip tekrar ver.
- Motor dururken de cevap gelir (devir 0) — test için motoru döndürmek gerekmez.

### RPM hedefi (kapalı çevrim)

Bidirectional açıkken `r` → `3000` (3D modda `rpm 3000 geri` / `ileri`). Firmware her frame'de ölçülen
eRPM'den mekanik RPM'i hesaplar (`:poles N`) ve PI kontrolcüyle throttle'ı ayarlar.

- `:3d 1` — ESC 3D moddaysa mutlaka söyle; aksi halde yön aralıkları yanlış olur.
- `:rpmmax N` — kontrolcünün verebileceği en yüksek throttle (varsayılan 300 = normal modda %15, 3D'de %30).
- `:kp N` / `:ki N` — kazançlar (milli birim, varsayılan 50 / 1000). Salınım varsa ikisini de düşür.
- 100 ms geçerli telemetri gelmezse motor durur (`#TX RPM LOST`); elle throttle / Boşluk / failsafe RPM modunu kapatır.

### Güvenlik (failsafe)

Varsayılan olarak **son komut/PING'den 500 ms sonra veya port kapanınca değer 0'a düşer** (`#TX FAILSAFE`).
Bu yüzden `dshot tx throttle 200` tek başına çıkınca durur — `--hold` ya da `shell` kullan.
Tezgâhta pervanesiz çalışırken `dshot tx timeout 0` ile kapatılabilir.
Motor dönerken hız değişimi ve özel komutlar reddedilir (`ERR busy`).

### Seri protokol

115200 baud CDC, satır sonu `\n`. Her komut tek satır `OK ...` / `ERR <sebep>` alır; `#` ile başlayan satırlar asenkrondur.
`STATUS`, `PING`, `DFU`, `TX SPEED|RATE|THR|RAW|TLM|CMD|STOP|ENABLE|TIMEOUT`, `RX STREAM|RAW|POL|RESET`.
Ayrıntı: `docs/superpowers/specs/2026-10-18-dshot-design.md` §7.

## Testler

```bash
cd common && pio test -e native        # C
cd bluepill && pio run                 # Bluepill derlemesi (txrx)
cd dshot-cli && ../.venv/bin/pytest -q # Python
dshot loopback-test                    # donanım (PA8↔PB6)
```

## Sorun giderme

- `dshot ports` boş: `ls /dev/ttyACM*`; kullanıcı `dialout` grubunda olmalı.
- DFU bulunamıyor: `dfu-util -l`; gerekirse BOOT0+NRST ile elle gir.
- Bluepill USB'de görünmüyor: R10 (D+ pull-up) 1.5 kΩ olmalı; bazı kartlarda 10 kΩ takılı gelir.
- Okuyucu `other` sayıyor: hat bağlı değil/gürültülü ya da bidirectional ESC cevapları (`rx pol auto`).
