# HSS gömülü donanım — 2026-08-03 çalışma kaydı

Bu oturumda pan/tilt sürüşü, AS5600 enkoder ve IMU uçtan uca ayağa kaldırıldı. Aşağıdaki her satır **ölçülmüş**, tahmin değil.

Ortam: Pi 5 `pi@<TARET_IP>`, ESP32-S3 `/dev/ttyACM0`, `arduino-cli` **`~/bin/` altında (PATH'te değil)**.

```bash
~/bin/arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" <sketch>
~/bin/arduino-cli upload -p /dev/ttyACM0 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" <sketch>
```

---

## 1. TB6600 pan/tilt — çalışıyor

Çizimdeki kablolama yanlıştı, sahadaki doğruydu. Doğrulanan bağlantı **ortak katot**:

| | PUL+ | DIR+ | ENA |
|---|---|---|---|
| A1 pan | **GPIO4** | **GPIO5** | bağlanmaz |
| A2 tilt | **GPIO6** | **GPIO7** | bağlanmaz |
| eksi uçlar | PUL−/DIR− köprülü → ESP32 GND | | |

`stepler/` klasöründeki ilk denemeler (klasik ESP32, GPIO25/26) zaten bu şemayı kullanıyordu; kod yorumlarında yazılı. `hss_3`/`hss_4` çizimlerinde `+` uçları GND'ye bağlıydı ve PUL hiç GPIO'ya gitmiyordu — o hâliyle optokuplör hiç iletmez.

**ENA neden boş:** enerjilenince sürücü çıkışları kapanır, motor serbest kalır. Tilt 5:1 düz dişli, kendinden kilitlemeli değil, AM eksenin 145 mm önünde → namlu düşer. Acil durdurma donanım E-stop'tan olmalı, GPIO'dan değil.

**Test:** `firmware/motor_test_s3` — pan ve tilt ayrı ayrı 1 tur ileri + 1 tur geri, miller boşta. Rampalı (1200→400 µs yarım periyot). **DIP = 1/16 mikroadım doğrulandı, 3200 adım = 1 tur.** Yön montajda belirlenecek.

**Açık:** ENA bağlı olmadığı için iki NEMA23 sürekli tam tutma akımı çekiyor. **Ölçülmedi** — INA226 takılınca ilk iş bu.

---

## 2. I²C — iki hat aynı anda çalışıyor

| Hat | Pinler | Cihaz |
|---|---|---|
| I²C0 | GPIO8 / GPIO9 | AS5600 pan `0x36` + IMU `0x68` |
| I²C1 | GPIO10 / GPIO11 | boş — tilt enkoderi için ayrıldı |

**TCA9548A mux alınmayacak** — iki I²C çevre biriminin aynı anda çalıştığı ölçüldü (test sırasında IMU geçici olarak I²C1'e alınıp iki hat birlikte okundu).

### AS5600 pan
Cevap veriyor, register'ları okunuyor. Mıknatıs henüz takılı değil: `STATUS=0x13` (MD=0, ML=1), `AGC=128` — 3,3 V modunda tavan değer, yani alan yok. **Bu beklenen hâl, arıza değil.** Modül pull-up'ı ölçüldü: **1,3 kΩ**, sökülmedi.

### IMU — MPU9250 değil, **MPU6500**
`WHO_AM_I = 0x70` (9250 olsaydı 0x71). Kartta "GY-9250" yazsa da **manyetometre yok**, 6 eksen. Mutlak yön veremez; taret açısı enkoderlerden geleceği için mimariyi bozmaz ama belgelerde "GY9250" geçen yerler yanıltıcı.

---

## 3. IMU kararlılık ölçümü

`firmware/imu_kararlilik` — 20 sn, 100 Hz, I²C 400 kHz, paylaşılan hat, sensör sabit:

```
2000 IMU + 2000 AS5600 okumasi -> HATA 0
ivme  az   ort 1.0408   std 0.0060 g
      |g|  ort 1.0410   std 0.0060 g
jiro  gx   ort -2.8335  std 0.1371 dps
      gy   ort -1.0703  std 0.1785 dps
      gz   ort +0.0679  std 0.1385 dps
sicaklik 31.2 C
```

- **Hat güvenilirliği:** 4000 işlemde tek hata yok, 400 kHz'de. Tasarım 100 kHz varsayıyordu, bol pay var.
- **Gürültü:** σ = 0,006 g — bu sınıf sensör için çok iyi.
- **Jiro bias −2,83 dps (gx).** Spec içinde (±5 dps) ama **düzeltilmezse açı dakikada ~170° kayar.** Firmware her açılışta sensör sabitken 1–2 sn ortalama alıp bias çıkarmalı. Sıcaklıkla kayar, sabit sayı yazılmamalı.
- **İvme ölçeği %4 yüksek.** Eğim açısını etkilemez (`atan2` oranı kullanır, ölçek sadeleşir); yalnız mutlak büyüklük kullanılırsa düzeltme gerekir.

---

## 4. Teşhis sketch'leri

I²C'de cihaz görünmezse sırayla çalıştır:

| Sketch | Ne yapar |
|---|---|
| `i2c_tarama_ikili` | İki hattı tarar, AS5600/IMU teşhisi, canlı açı okuması |
| `i2c_hat_teshis` | Hat seviyeleri, dış pull-up var mı, SDA/SCL ters mi |
| `i2c_pin_bul` | 25 güvenli GPIO'da pull-up arar, bulduklarının ikili kombinasyonlarını tarar |
| `as5600_teshis` | ADC ile hat gerilimi, pull-up toparlanma süresi, bit-bang tarama |
| `i2c_hat_ayrik` | SDA ve SCL ayrı net mi, aynı net mi |
| `i2c_kuplaj_olc` | Kuplajın cinsi: kısa devre mi, diyot mu, dirençli mi |
| `i2c_pullup_olc` / `_olc2` | Pull-up değeri (akım çekmez, güvenli). `_olc2` GPIO10/11 için |

### Bu oturumda iki kez tekrarlayan arıza

Veri kablosu SDA/SCL yerine **VCC/3V3 düğümüne** bastı — hem enkoderde hem IMU'da.

**İmzası:** hat 3,3 V'ta duruyor ve pull-up varmış gibi görünüyor, ama hiçbir adres ACK vermiyor ve 45 kΩ dahili pull-down pini kıpırdatamıyor. Yüksek akımda düğüm çöküyor (~1,27 V) ve iki hat birlikte iniyor.

**Tek ölçümlük ayırt etme:** multimetre ohm kademesi, **GPIO ↔ 3V3** arası → **kΩ olmalı**. Onlarca ohm (ölçülen: 80 Ω) görüyorsan besleme rayındasın. Düzeldiğinde aynı nokta 1,3 kΩ okudu.

Dikkat: **IMU kartında pin sırası `VCC–GND–SCL–SDA`**, AS5600'den farklı. Aynı refleksle takınca bir sıra kayıyor ve VCC'ye denk geliyor.

---

## 5. Sıradaki işler

1. **Mıknatıs** — göbek parçası (thingiverse 7219474) basılınca enkoder gerçek açı okur. Beklenen: `MD=1, ML=0`, AGC orta bantta. Bir tam turda hata tek sinüs veriyorsa kaçıklık var.
2. **Tilt enkoderi** → I²C1 (GPIO10/11), araç/taret üzerinde denenecek. Sketch hazır, kod değişmez.
3. **INA226** → I²C0'a `0x40`. İlk iş hareketsiz tutma akımını ölçmek (6S LiPo menzili ve ısı için).
4. **Jiro bias kalibrasyonu** asıl firmware'e girecek.
5. Motor yönü (DIR HIGH = hangi taraf) montajda belirlenecek.
