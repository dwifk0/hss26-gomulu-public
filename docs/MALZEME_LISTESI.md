# HSS / Miğfer — Elektronik Malzeme Listesi (BOM)

> Taret ve kontrol elektroniğinin malzeme listesi. Fiyat içermez.
>
> ⚠ **Ateşleme kartının parça listesi ve yüksek gerilim kablajı bu depoda
> yayımlanmamıştır** — bkz. [`ATESLEME_SISTEMI.md`](ATESLEME_SISTEMI.md).
> Aşağıdaki emniyet ve ölçüm kalemleri, o tarafla arayüzü oldukları ölçüde
> yer alır.

---

## 0. Elde olan — SATIN ALINMAYACAK

| Parça | Adet | Durum |
|---|---|---|
| Raspberry Pi 5 8GB + AI HAT+ (Hailo-8L) | 1 | Çalışıyor |
| Raspberry Pi Camera Module 3 (IMX708) + CSI kablo | 1 | Çalışıyor, 180° ters monte |
| ESP32-S3-N16R8 | 1 | Çalışıyor (`/dev/ttyACM0`) |
| NEMA 23 (23HS5628) + TB6600 | 2 + 2 | Test edildi |
| NEMA 17 + A4988 (şarjör) | 1 + 1 | Kurulu |
| Benewake TF02-Pro LiDAR | 1 | UART, CP2102 üzerinden `/dev/ttyUSB0` |
| 24V 20A güç kaynağı | 1 | Ana hat (motor + namlu) |
| İzole 27W USB-C kaynak | 1 | Pi5 + sensörler (E-stop'ta ayakta kalır) |

---

## 2. EMNİYET

| # | Parça | Değer / Model | Adet | Not |
|---|---|---|---|---|
| 2.5 | **ARM rölesi** (şarj enable) | Opto-izole röle modülü, 1 kanal | 1 | **LV/giriş tarafında** kesecek, 450 V hattında değil. Bobin gerilimini sipariş ederken teyit et — elde bulunan 4'lü modül SRD-**12V**DC bobinli çıkmıştı, 5 V ile çekmiyor. 5 V bobinli (SRD-05VDC-SL-C) iste ya da 12 V besleme çek. |
| 2.6 | **E-Stop butonu** | Mantar başlı, kilitlemeli, NC, ≥25 A | 1 | 24 V ana hattı keser |
| 2.7 | E-Stop kontaktörü / röle | 24 V bobin, ≥30 A DC kontak | 1 | 20 A'i butonun kendi kontağından geçirmek yerine kontaktörle kes; buton sadece bobin hattını sürsün |
| 2.8 | Sigorta + yuva | 25 A otomotiv (ana 24 V) | 2 + 2 | Merkezi sigorta kutusu raporda geçiyor |
| 2.11 | HV uyarı etiketi | "450 V — DOKUNMA" | birkaç | Yarışma güvenlik puanı ve ekip güvenliği |

---

## 3. ÖLÇÜM ve ESP32 ARAYÜZÜ

| # | Parça | Değer / Model | Adet | Not |
|---|---|---|---|---|
| 3.1 | **ADS1115** modülü | 16-bit, 4 kanal, I2C | 2 | İzole fark yükselticinin çıkışı **differential** ve tepesi ESP32 ADC'sinin 3,3 V sınırının üstünde — doğrudan bağlanırsa pin yanar. Adres 0x48 / 0x49. |
| 3.5 | Seviye çevirici | TXS0108E (8 kanal, çift yön) | 2 | Ateşleme kartı 5 V mantık, ESP32-S3 **3,3 V**. 5 V sinyali doğrudan ESP32 pinine verme. |
| 3.6 | ESP32-S3 yedeği | ESP32-S3-N16R8 | 1 | Yarışma günü kartı yakarsan proje durur. Aynı modelden yedek al. |
| 3.7 | **Limit switch** ⭐ | Mikro switch, kollu, NC | 4 (+2) | **ZORUNLU.** Eksen başına 2. Enkodersiz mimaride homing'in tek dayanağı + donanımsal son durak. Açı bilgisinin tamamı buradan referans alıyor. |

### 3.A Açı ölçümü — verilen karar

**Enkodersiz gidiliyor** (28 Tem 2026). Gerekçe: step motor açık çevrim ama deterministik; limit switch homing + adım sayma ile 1/16 mikroadımda 0,1125°/adım çözünürlük zaten var. Enkoderin asıl maliyeti parça değil, mıknatısı mile eksenden kaçıksız oturtma işi + firmware süresi — yarışma takvimine göre bu şu an ödenmeyecek maliyet.

Uygulama kuralları:
- Açılışta her eksen homing yapar; yasak alan ancak homing bittikten sonra "geçerli" sayılır.
- İvme rampası muhafazakâr, TB6600 akımı bol → adım kaybı pratikte olmaz.
- Her şarjör dolumunda / angajman arasında re-home.
- Yasak alan sektörüne **+5° emniyet payı**.
- **Kayma ölçümü:** re-home'da beklenen adım sayısı ile switch'in tetiklendiği gerçek adım sayısı karşılaştırılır → adım kaybı olup olmadığı ölçülür. 50 tarama sonunda sapma yoksa enkoder gerekmiyor demektir.
- MPU6500 sökülmez; pitch'te kaba (±2-3°) çapraz kontrol ve kaide tesviyesi için kalır.
- Firmware açı kaynağını soyutlar → enkodere geçiş gerekirse tek noktadan değişir.

**Faz 2 — AS5600'e karar verildi (2026-07-31), çizime işlendi:**

| # | Parça | Model | Adet | Not |
|---|---|---|---|---|
| 3.8 | Mutlak açı enkoderi | AS5600 (12 bit) | 2 | Adresi 0x36'da **sabit**. İki enkoder **ayrı I²C hatlarına** bağlandı: pan → I²C0 `GPIO8/GPIO9` (INA226 0x40 ve GY9250 0x68 ile birlikte), tilt → I²C1 `GPIO10/GPIO11`. ESP32-S3'ün ikinci I²C çevre birimi kullanıldığı için mux gerekmiyor. `DIR` → GND (saat yönünde artan açı). |
| 3.9 | Diametral mıknatıs | 6 × 2,5 mm | 2 (+2) | **Radyal değil diametral.** Mil merkezine ≤0,25 mm kaçıklık, hava aralığı 0,5–2 mm. Enkoderi çıkış miline tak, motor miline değil. |
| 3.10 | Enkoder göbeği | 3D baskı, PETG/PLA | 2 (+2) | thingiverse.com/thing:7219474. **Demir dışı olmalı**; mıknatıs doğrudan çelik mil ucuna yapıştırılırsa merkezde durmaz, kenara kaçar. |

> ~~3.11 I²C mux TCA9548A~~ — **alınmayacak.** İki ayrı I²C hattı kullanıldığı için gereksiz.
>
> Modül üzerindeki pull-up dirençleri: aynı hatta birden çok modül varsa yalnız birinin pull-up'ı kalsın, diğerlerininki sökülsün (her modülde 4,7 k varsa paralel değer 3,3 V'ta sınırı zorlar).

---

## 6. KABLAJ — kesit ve uzunluk tablosu

> ⚠️ **BU BÖLÜM GEÇERSİZ — yerine [`KABLO_LISTESI.md`](KABLO_LISTESI.md) kullan.** (29 Tem 2026)
> Aşağıdaki uzunluklar gövde ölçüsü bilinmediği için tahminliydi ve CAD ölçümüne göre 5–10 kat fazla çıktı.
> Ayrıca buradaki "Pi kaidede, kablo pan eksenini geçiyor" varsayımı **yanlış**: CAD'e göre tüm elektronik dönen kule üzerinde. Kesitler geçerli, uzunluklar değil.
>
> Uzunluklar taret ölçeğine göre **bol paylı** verildi (Pi kaidede, kablo pan eksenini geçiyor varsayımıyla). Gerçek gövde ölçüsü belli olunca kısalt — uzun alıp kesmek kolay, kısa alıp eklemek kötü.
> **Taret üstünde hareket eden her kablo silikon izolasyonlu / yüksek esneklikli (ince tel örgülü) olacak.** Sert PVC kablo dönen eksende yorulup kırılır.

### 6.1 Güç hatları

| Hat | Akım | Kesit | ~AWG | Al | Tip / Not |
|---|---|---|---|---|---|
| PSU → sigorta kutusu → dağıtım (ana 24 V) | 20 A | **4 mm²** | 12 | 3 m | Belgede "≥2,5 mm²" yazıyordu; 4 mm² gerilim düşümü ve ısınma için rahat pay bırakır |
| Sigorta kutusu → E-stop kontaktörü → yük | 20 A | **4 mm²** | 12 | 2 m | Güç yolu kontaktörden geçer |
| E-stop butonu → kontaktör bobini | <1 A | 0,75 mm² | 18 | 3 m | Buton 20 A taşımıyor, sadece bobini sürüyor |
| Dağıtım → TB6600 ×2 (besleme) | ~5 A/sürücü | 1,5 mm² | 16 | 2 m | |
| Dağıtım → A4988 / şarjör besleme | ~2 A | 1 mm² | 17 | 1,5 m | |
| Dağıtım → 450 V şarj kaynağı girişi (LV) | ~3-5 A | 1,5 mm² | 16 | 2 m | ARM rölesi bu hatta kesiyor |
| Pi 5 USB-C izole besleme | 5 A | hazır kablo | — | 1 | E-stop'ta bile ayakta kalacak, ana hatta bağlama |

### 6.2 Motor hatları

| Hat | Akım | Kesit | ~AWG | Al | Tip / Not |
|---|---|---|---|---|---|
| TB6600 → NEMA23 (pan) | 2,8 A/faz | 0,75 mm² ×4 | 18 | 3 m | **Ekranlı** 4 damarlı. Ekranı yalnız sürücü tarafında şaseye bağla (tek nokta) |
| TB6600 → NEMA23 (tilt) | 2,8 A/faz | 0,75 mm² ×4 | 18 | 3 m | Aynı |
| A4988 → NEMA17 (şarjör) | ~1,5 A/faz | 0,5 mm² ×4 | 20 | 2 m | Ekranlı tercih |

Motor kablosunu HV ölçüm ve IR gate kablolarıyla **aynı kanaldan paralel çekme** — step sürücünün anahtarlama gürültüsü analog okumaya biner. Kesişmesi gerekiyorsa dik açıyla kesişsin.

### 6.4 Sinyal hatları

| Hat | Kesit | ~AWG | Al | Tip / Not |
|---|---|---|---|---|
| **IR gate ×6 (namlu boyunca)** | 0,22 mm² | 24 | **10 m, ekranlı çok damarlı** | Zamanlama kritik + 1000 A darbenin tam yanında. **Ekranlı şart.** 8+ damarlı ekranlı tek kablo, namlu boyunca tek hat hâlinde |
| **SCR gate ×6** | 0,22 mm² | 24 | 5 m, ekranlı | Ekranlı; gate hattına gürültü binerse **istenmeyen ateşleme** olur |
| ESP32 → TB6600 (STEP/DIR/EN ×2 eksen) | 0,22 mm² | 24 | 5 m | 6 hat + ortak GND |
| I2C (MPU6500, INA226, ADS1115) | 0,22 mm² | 24 | 2 m | **≤30 cm tut.** Hat zaten 10 kHz'e düşürülmüş durumda, uzatma. Burgulu: SDA-GND, SCL-GND |
| Limit switch ×4 | 0,25 mm² ×2 | 24 | 8 m | Eksen başına 2 damar + ortak dönüş |
| Röle kontrol (ARM, dump) | 0,25 mm² | 24 | 3 m | |
| LiDAR TF02-Pro UART | 0,22 mm² ×4 | 24 | 2 m | TX/RX çapraz |
| Pi ↔ ESP32-S3 | USB-A ↔ USB-C | — | 1 m ×2 | Yedekli al, veri hattı olan tip (şarj kablosu değil) |
| Ethernet (YKİ telemetri) | Cat5e/6 | — | 10 m | Yarışmada kablolu bağlantı WiFi'dan güvenilir |

### 6.5 Pan eksenini geçen kablolar — ayrı düşün

Kamera CSI, tilt motoru, IR gate ve SCR gate hatları dönen eksenden geçiyor. Taret **360° sürekli dönüyorsa hiçbir kablo dayanmaz** → slip ring gerekir. Sınırlı yayda dönüyorsa:
- Eksen etrafında **servis kangalı** bırak (bir tur boşluk, gergin değil)
- **Enerji zinciri / drag chain** kullan (25×25 mm iç ölçü, 1 m)
- Bu geçişteki her kablo silikon + ince tel örgülü olacak

---

## 6.A SİPARİŞ ÖZETİ — kabloda alınacaklar

| Kalem | Miktar |
|---|---|
| Silikon kablo 1,5 mm² | 5 + 5 m |
| Silikon kablo 1 mm² | 5 m |
| Silikon kablo 0,75 mm² | 5 m |
| Ekranlı 4 damarlı 0,75 mm² (motor) | 8 m |
| Sinyal kablosu 0,22-0,25 mm², çok renkli | 20 m |
| Cat5e/6 hazır kablo | 10 m |
| USB-A ↔ USB-C veri kablosu | 2 adet |
| Enerji zinciri (drag chain) 25×25 | 1 m |

---

## 6.B SARF MALZEME

| Parça | Miktar | Not |
|---|---|---|
| **Kamera CSI kablosu (yedek + uzatma)** | 300 mm ×1, 500 mm ×1 | **22 pin ↔ 15 pin** (Pi 5 tarafı 22, Camera Module 3 tarafı 15). 500 mm takılır, 300 mm yedek kalır. **DSI adaptörü DEĞİL** — DSI ekran içindir, pinout farklı, kamera görünmez. 50 cm üstü gerekiyorsa düz FPC yerine aktif CSI uzatma kiti (CSI-to-HDMI) al, yoksa MIPI CSI-2 hattında kilitlenme/kare kaybı başlar. |
| Isı ile daralan makaron seti | 1 | Yüksek gerilim uçlarında **çift kat** |
| Kablo yüksüğü (ferrule) + pense | 4/1,5/0,75/0,5 mm² | Klemense çok telli kablo takarken şart — çıplak tel zamanla gevşer ve yüksek gerilimde ark yapar |
| Kablo pabucu (M4/M5, 4-6 mm²) | 20 | Bara ve PSU bağlantıları |
| Spiral kablo sargısı / makaron | 5 m | Demetleme |
| Kablo bağı + yapışkanlı taban | 1 paket | Taret üstünde kabloyu sabitle — serbest kablo dönerken takılır |
| Ferrit boncuk (klipsli) | 10 | Motor ve sinyal demetlerinin uçlarına, gürültü için |
| Lehim, flux, kablo pabucu | — | |
| Kart ayağı / standoff, vida | 1 takım | |

---

## 7. YÜKSEK GERİLİM — çalışmadan önce oku

- 6 kondansatör **toplam ~413 J** (kademe başına 68,8 J) depoluyor. İnsan için tehlikeli sınır ~10 J civarında anılır. Bu enerji **öldürebilir**; 450 V göğüsten geçerse kalp durdurur.
- Kart üzerinde çalışmadan önce her seferinde: ARM rölesi aç → dump'ı tetikle → **boşaltma çubuğuyla elle boşalt** → **voltmetreyle <10 V doğrula**. Üç adımı da yap, birine güvenme.
- Tek elle çalış (diğer eli cebinde tut) — akımın gövdeden geçmesini engeller.
- Cap'ler boşaltıldıktan sonra dielektrik geri kazanımı ile kendiliğinden birkaç on volta çıkabilir. Uzun süre kapalı kalacaksa cap uçlarını kısa devre teli ile bağlı bırak.
- Test ederken yanında ikinci bir kişi bulunsun.
