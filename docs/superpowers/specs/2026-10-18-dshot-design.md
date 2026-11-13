# DShot Gönderici / Okuyucu / CLI — Tasarım

Tarih: 2026-10-18

## 1. Amaç ve başarı ölçütü

ESC'lere saf DShot sinyali göndermek, DShot hatlarını okuyup çözmek ve bunları
PC'den bir Python CLI ile kontrol etmek.

- **Gönderici:** Blackpill (STM32F401CC veya F411CE), 1 kanal.
- **Okuyucu:** Bluepill (STM32F103C8), 1 kanal. Hem Blackpill çıkışını
  doğrulamak hem de gerçek uçuş kontrolcüsü → ESC hattını dinlemek (sniffer) için.
- **Kombine:** Tek Blackpill'de gönderici + okuyucu birlikte (ayrı build).
- **CLI:** Python komut satırı aracı (`dshot`).

**Başarı ölçütü:** Loopback testi (gönderici çıkışı → okuyucu girişi + ortak GND)
DShot150/300/600/1200 hızlarının hepsinde, değer taraması + telemetri biti +
özel komut değerleri için **sıfır uyuşmazlık** verir. Bu test hem iki ayrı kartla
(Blackpill → Bluepill) hem de tek kombine Blackpill ile (PA8 → PB6) çalışır.

**Kapsam dışı:** Çok kanallı çıkış, ESC telemetri cevabının (GCR / eRPM) çözülmesi,
GUI, DShot dışı protokoller (Oneshot, Multishot, PWM).

## 2. DShot özeti

16 bitlik frame, MSB önce: `[11 bit değer][1 bit telemetri isteği][4 bit CRC]`.

- `v = (değer << 1) | telem`
- Normal CRC: `(v ^ (v >> 4) ^ (v >> 8)) & 0xF`
- Bidirectional (ters) CRC: `~(v ^ (v >> 4) ^ (v >> 8)) & 0xF`
- Değer: `0` = disarm/dur, `1..47` = özel komutlar, `48..2047` = throttle.

| Hız | Bit süresi | T0H (%37.5) | T1H (%75) |
|---|---|---|---|
| DShot150 | 6.667 µs | 2.50 µs | 5.00 µs |
| DShot300 | 3.333 µs | 1.25 µs | 2.50 µs |
| DShot600 | 1.667 µs | 0.625 µs | 1.25 µs |
| DShot1200 | 0.833 µs | 0.3125 µs | 0.625 µs |

Ortak test vektörleri (C ve Python aynı dosyayı kullanır):
- değer 1046, telem 0 → `0x82C6`
- değer 0, telem 0 → `0x0000` (tüm bitler sıfır; hız-bağımsız bit sınıflandırması için en zor durum)
- en az: değer 2047 telem 1, değer 48, komut değerleri (telem=1), ters CRC örnekleri.

## 3. Depo yapısı

```
dshot/
  common/                    donanımdan bağımsız C kodu + native testler
    dshot_frame.{h,c}        encode / decode / CRC (normal + ters)
    dshot_rx_decode.{h,c}    periyot/high dizilerinden frame çözme, hız tespiti
    proto.{h,c}              satır tabanlı komut ayrıştırıcı
    test/                    Unity testleri (pio test -e native)
    platformio.ini           sadece native test env'i
  test-vectors.json          ortak test vektörleri (tek kaynak)
  tools/gen_vectors.py       JSON → common/test/test_vectors.h
  blackpill/                 PlatformIO projesi
    env:tx                   sadece gönderici
    env:txrx                 gönderici + okuyucu
  bluepill-rx/               PlatformIO projesi, okuyucu
  dshot-cli/                 Python paketi (pyproject.toml, pyserial, pytest)
  docs/
```

Firmware projeleri `common/`'u `lib_extra_dirs` ile kullanır. Donanım katmanı
(timer, DMA, USB) her kart projesinde ayrı yazılır; çözme ve protokol mantığı ortaktır.

## 4. Donanım ve saat yapılandırması

### Blackpill (tek build, F401CC ve F411CE)
- Build hedefi `blackpill_f401cc`. F411CE bunun üst kümesi olduğundan aynı imaj iki çipte de çalışır.
- HSE 25 MHz, PLL M=25 N=336 P=4 Q=7 → SYSCLK 84 MHz, USB 48 MHz.
- Flash 2 wait state, regülatör Scale 2 (iki çip için de 84 MHz'de geçerli).
- APB1 = 42 MHz (timer saati 84 MHz), APB2 = 84 MHz.
- `STATUS` komutu `DBGMCU->IDCODE` DEV_ID'yi raporlar (0x423 / 0x433 = F401, 0x431 = F411).
- LED: PC13 (aktif düşük).

### Bluepill
- HSE 8 MHz, PLL ×9 → 72 MHz; USB prescaler 1.5 → 48 MHz.
- APB1 = 36 MHz (timer saati 72 MHz).
- LED: PC13 (aktif düşük).
- Açılışta PA12 (D+) kısa süre GPIO olarak düşük çekilir (yeniden enumeration için).

### Pin ve kaynak tablosu

| Kart | İş | Pin | Timer | DMA |
|---|---|---|---|---|
| Blackpill | TX çıkış | PA8 | TIM1_CH1 (PWM) | DMA2 Stream5 Ch6 (TIM1_UP → CCR1) |
| Blackpill | TX frame zamanlayıcı | — | TIM2 (update IRQ, RATE Hz) | — |
| Blackpill (txrx) | RX giriş | PB6 (FT) | TIM4 CH1/CH2 PWM input | DMA1 Stream0 Ch2 (CH1), Stream3 Ch2 (CH2) |
| Bluepill | TX çıkış | PA8 | TIM1_CH1 (PWM) | DMA1 Ch5 (TIM1_UP → CCR1) |
| Bluepill | TX frame zamanlayıcı | — | TIM2 (update IRQ; 16 bit, 20 Hz altında 10 kHz taban) | — |
| Bluepill | bidir cevap yakalama | PA8 | TIM1 IC1 düşen + IC2 (TI1) yükselen | DMA1 Ch2 (CH1), Ch3 (CH2) |
| Bluepill | RX giriş | PB6 (FT) | TIM4 CH1/CH2 PWM input | DMA1 Ch1 (CH1), Ch4 (CH2) |
| İkisi de | Tetik çıkışı | PB0 | GPIO (frame kesmesinde) | — |

**Bluepill notları:** F103 timer'ları bir kanalda iki kenarı birden yakalayamaz; bidir cevabı iki kanalda
toplanıp `dshot_reply_merge` ile birleştirilir. F103'te USB DFU bootloader olmadığından yükleme ST-Link ile
yapılır, `DFU` komutu `ERR unsupported` döner.

**Tetik (PB0):** host'tan motor komutu (`TX THR|RAW|STOP|CMD|RPM`) gelince `trig_pending` kurulur; frame
kesmesi bu komutu taşıyan ilk frame'in DMA'sını başlatırken PB0'ı high yapar, bir sonraki frame kesmesinde
low'a çeker.

DMA eşlemeleri uygulama sırasında RM0368/RM0383 ve RM0008 DMA istek tablolarından
doğrulanacaktır. PB6 her iki çipte 5 V toleranslıdır.

**Bağlantı notu:** Okurken her zaman ortak GND bağlanmalı. Okuyucu girişi
yüksek empedanslıdır (dahili pull yok), hatta yük bindirmez.

## 5. Gönderici (TX)

### Sinyal üretimi
- TIM1: PSC=0, `ARR = round(f_tim / bitrate) - 1` (ör. DShot600 @84 MHz → 140 tick).
- `T0H = round((ARR+1) · 0.375)`, `T1H = round((ARR+1) · 0.75)`.
- CCR1 preload açık. DMA her update olayında bir sonraki CCR1 değerini yazar.
  Tampon: `[b15 … b0, 0, 0]` (18 half-word); sondaki sıfırlar hattı low bırakır ve
  preload gecikmesi yüzünden son bitin kesilmesini önler.
- Advanced timer olduğu için `BDTR.MOE` set edilir.
- TIM1 sürekli çalışır; frame dışında CCR1=0 olduğundan çıkış low kalır.
- TIM2 her `1/RATE` sn'de kesme üretir; kesme hazır tamponu (çift tampon) DMA'ya
  verir ve stream'i başlatır.
- Açılışta PA8, timer başlamadan önce GPIO low olarak sürülür (hat asla yüzmez).

### Davranış
- Açılışta çıkış etkin, değer 0 gönderilir (ESC arm olabilsin). `TX ENABLE 0` pals üretimini keser, hat low kalır.
- Varsayılan hız DShot600, varsayılan RATE 1000 Hz.
- RATE üst sınırı: `periyot ≥ 18 bit süresi + 20 µs`. Aşılırsa `ERR`.
- **Özel komutlar (1..47):** yalnız mevcut çıkış değeri 0 iken kabul edilir. Telemetri biti
  otomatik 1 yapılır, belirtilen sayıda (varsayılan 10) ardışık frame gönderilir, sonra değer 0'a döner.
  `OK` cevabı gönderim bitince verilir.
- **Failsafe:** Son TX komutundan veya `PING`'den beri `TIMEOUT` ms geçerse
  (varsayılan 500) değer 0'a düşer ve kart `#TX FAILSAFE` satırı basar.
  CDC DTR düşerse (port kapandı) failsafe hemen devreye girer. `TX TIMEOUT 0` failsafe'i (hem zaman
  aşımını hem DTR tetiğini) kapatır; yalnız tezgâh testi ve tek seferlik CLI komutları içindir.

## 6. Okuyucu (RX)

### Yakalama
- TIM4 PWM input modu, PSC=0. TI1 → IC1 (başlangıç kenarı, periyot) ve IC2 (bitiş kenarı, high süresi).
  Slave reset modu ile sayaç her başlangıç kenarında sıfırlanır.
- CC1 ve CC2, DMA ile iki ayrı dairesel tampona yazılır (periyotlar ve high süreleri).
- `ARR` = 10 µs'ye karşılık gelen tick (frame sonu zaman aşımı), `URS=1` (yalnız taşma update üretir).
- Frame sınırları için kesme şeması:
  - Boştayken CC1 kesmesi açık, update kesmesi kapalıdır.
  - İlk kenar gelince CC1 kesmesi update'i açar, kendini kapatır.
  - Taşma (≥10 µs sessizlik) olunca update kesmesi bir "burst"ün bittiğini işaretler:
    DMA yazma konumlarını okur, burst'ü çözme kuyruğuna koyar, update'i kapatır, CC1'i açar.
  - Bu şemayla frame başına yalnızca 2 kesme oluşur.
- Çözme ana döngüde yapılır (`common/dshot_rx_decode`).

### Çözme (donanımsız)
- Burst'te tam 16 pals yoksa → `bursts_other` sayacı (ör. bidirectional ESC cevabı, gürültü).
- Bit periyodu = 2..16. CC1 değerlerinin medyanı (ilk CC1 boşluk süresidir, atılır).
- Bit kararı: `high > periyot / 2` → 1. Böylece hız ayarı gerekmez ve tüm-sıfır frame de doğru çözülür.
- Hız, periyoda en yakın standart değerden belirlenir. Sapma %10'dan büyükse `speed=?` olarak raporlanır.
- CRC normal modda normal, ters modda ters formülle kontrol edilir.

### Polarite
- `RX POL AUTO|NORMAL|INVERTED`, varsayılan AUTO.
- AUTO'da burst sonrası hattın boşta kalma seviyesi örneklenir. Art arda 8 kez high görülürse
  ters (bidirectional) moda geçilir: IC1 düşen, IC2 yükselen kenara alınır ve ters CRC beklenir.
  Ters moddayken 8 kez low görülürse normal moda dönülür.

### Raporlama
- Varsayılan olarak hiçbir şey basılmaz. `RX STREAM <ms>` (ör. 100) periyodik özet satırını açar:
  `#RX n=<frame> rate=<Hz> val=<son> vmin=<> vmax=<> tlm=<0|1|mix> crc_err=<> other=<> inv=<0|1> speed=<150|300|600|1200|?> bit_ns=<> t0h_ns=<> t1h_ns=<>`
  (`n`, `vmin`/`vmax`, `crc_err`, `other`, `tlm` son özetten bu yana olan aralığı kapsar.)
- `RX RAW <N>` her N'inci frame için `#F val=<> tlm=<> crc=<ok|bad> inv=<> bit_ns=<>` basar; 0 = kapalı.

## 7. Seri protokol (USB CDC, üç firmware için ortak)

- ASCII, satır sonu `\n` (`\r` yok sayılır), büyük/küçük harf duyarsız.
- Her komut tam olarak bir cevap satırı alır: `OK [...]` veya `ERR <sebep>`.
- Asenkron satırlar `#` ile başlar ve cevaplarla karışabilir; CLI bunları ayırır.
- Desteklenmeyen önek (ör. Bluepill'de `TX ...`) → `ERR unsupported`.
- USB: VID 0x0483 / PID 0x5740. Ürün dizesi: `DShot TX`, `DShot RX` veya `DShot TXRX`.

| Komut | Açıklama |
|---|---|
| `STATUS` | `OK board=<blackpill|bluepill> caps=<tx,rx> dev=0x<id> fw=<sürüm> [tx: speed rate val tlm en timeout] [rx: pol stream]` |
| `PING` | `OK`; TX failsafe zamanlayıcısını sıfırlar |
| `TX SPEED 150|300|600|1200` | hız |
| `TX RATE <hz>` | frame/sn |
| `TX THR <0..2000>` | 0 → değer 0, n → değer n+47 |
| `TX RAW <0..2047> [tlm]` | ham değer (test için); 1..47, mevcut değer throttle (≥48) iken reddedilir (`ERR busy`) |
| `TX TLM 0|1` | throttle frame'lerinde telemetri biti |
| `TX CMD <1..47> [tekrar]` | özel komut (bkz. §5) |
| `TX STOP` | değer 0 |
| `TX ENABLE 0|1` | pals üretimi aç/kapat |
| `TX TIMEOUT <ms>` | failsafe süresi, 0 = kapalı |
| `RX STREAM <ms>` | özet periyodu, 0 = kapalı |
| `RX RAW <N>` | ham frame örnekleme |
| `RX POL AUTO|NORMAL|INVERTED` | polarite |
| `RX RESET` | sayaçları sıfırla |

## 8. Python CLI (`dshot-cli`)

Modüller:
- `frame.py`: encode/decode/CRC (test vektörleriyle doğrulanır)
- `link.py`: seri bağlantı, cevap/asenkron satır ayrımı, zaman aşımı, keepalive thread'i
- `discover.py`: VID/PID + ürün dizesiyle port bulma
- `cli.py`: argparse

Komutlar:
```
dshot ports
dshot status        [--port P]
dshot tx speed 600 | rate 2000 | throttle 500 [--hold] | raw 1046 [--tlm] | cmd beep1 [--repeat N]
         | stop | enable on|off | timeout 500 | shell
dshot rx monitor    [--interval 100] [--raw N]   | pol auto|normal|inverted
dshot loopback-test [--port P | --tx P --rx P] [--speeds 150,300,600,1200] [--quick]
```

- Port belirtilmezse yeteneğe göre otomatik seçilir. Belirsizse hata verip listeler.
- `throttle --hold`, Ctrl+C'ye kadar portu açık tutar ve 100 ms'de bir `PING` gönderir. Çıkışta `TX STOP` gönderilir.
- `tx shell` interaktif REPL'dir. Arka planda keepalive gönderir, çıkışta `TX STOP` yapar.
- Özel komutlar için isimler tanımlıdır (`beep1..5`, `esc_info`, `spin_dir_1/2`, `3d_off/on`,
  `save`, `spin_normal`, `spin_reversed` …) ve bunlar için uygun varsayılan tekrar sayıları kullanılır.
- `loopback-test` her (hız, değer, telem) için şu adımları uygular:
  1. `TX RAW` ile değeri ayarlar ve ~30 ms bekler.
  2. `RX RESET` gönderir, ardından bir `#RX` özeti bekler.
  3. Özette `vmin == vmax == değer`, `tlm` beklenen değerde, `crc_err == other == 0`,
     `speed` doğru ve `n > 0` olduğunu kontrol eder.

  Değer seti 0, 1..47, 48, 2047 ve aradaki rastgele değerlerden oluşur (`--quick` daha az değer dener).
  Sonunda hız başına özet tablo ve uyuşmazlık listesi basılır; uyuşmazlık varsa çıkış kodu 1 olur.
  Test sırasında failsafe `TX TIMEOUT 0` ile kapatılır, bitince eski değerine geri yüklenir.

## 9. Test stratejisi

- `test-vectors.json` tek kaynaktır. `tools/gen_vectors.py` bundan C başlığını üretir.
- `common/`: Unity ile native testler yapılır (frame encode/decode, CRC, rx decode, proto parser).
  Rx decode testleri sentetik periyot/high dizileriyle dört hızı, tüm-sıfır frame'i, ±%5 jitter'ı,
  15/17 palslı burst'leri ve ters polariteyi kapsar.
- `dshot-cli`: frame, protokol satır ayrıştırma ve sahte seri port üzerinden link/loopback mantığı pytest ile test edilir.
- Donanım: kabul testi olarak `dshot loopback-test` çalıştırılır (§1).

## 10. Uygulama sırası ve riskler

1. **Risk spike'ı:** iki kartta da minimal USB CDC echo kurulur (PlatformIO stm32cube USB
   middleware'i otomatik derlemez; `usbd_conf`/`usbd_desc`/CDC katmanları elle yazılacak).
2. `common/` + test vektörleri + native testler.
3. Blackpill TX.
4. Bluepill RX.
5. Blackpill txrx.
6. Python CLI + loopback testi.

Bilinen riskler / sorun giderme notları:
- Bazı Bluepill'lerde R10 (D+ pull-up) yanlış değerde (10 kΩ yerine 1.5 kΩ olmalı). Enumeration
  sorununda kontrol edilmeli.
- Klon çipler (CKS32, GD32, APM32) farklı davranabilir; ST-Link ile `IDCODE` kontrolü önerilir.
- Blackpill DFU: BOOT0 basılıyken NRST ile girilir. Bazı kartlarda USB DFU tanınması için
  PA10'un boşta olması gerekir.
