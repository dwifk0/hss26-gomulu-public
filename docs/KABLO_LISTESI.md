# HSS / Miğfer — Kablo Listesi (CAD ölçülü)

> **Bu belge `MALZEME_LISTESI.md` §6'nın yerini alır.** Oradaki uzunluklar gövde ölçüsü bilinmediği için bol paylı tahmindi; buradakiler montaj STEP modelinin geometrisinden ölçüldü.
> Tarih: 29 Temmuz 2026. Uzunluklar montaj STEP modelinin geometrisinden ölçüldü.
> **Boy hesabı:** ölçülen 3B merkez-merkez mesafe × **1,4** (köşe dönme + demetleme) + bağlantı/servis payı, 50 mm'e yukarı yuvarlak.

---

## 0. Varsayımlar — sipariş öncesi teyit et

CAD'de **olmayan** bileşenlerin yeri varsayıldı. Yer değişirse ilgili satır değişir:

| Varsayım | Etkilediği satırlar |
|---|---|
| ESP32-S3, Pi 5'in hemen yanına (kule yan duvarı) monte edilecek | C3, F1–F8 |
| Kamera `lidarplaka` üzerine, LiDAR'ın yanına monte edilecek | F9 |
| 450 V şarj kaynağı, **namlu grubuna** monte edilecek (bkz. §4 uyarısı) | E1, E2 |
| 6 kondansatör kartı namlu boyunca **dağıtık** (56 mm aralıkla) | E3, E4 |
| Şarjör (NEMA17+PG518) namlu arkasında, ~0,5 m mesafede | D3 |
| Pan geçişi **slip ring** ile (§2) | A4, A5, B |

---

## 1. Bölge haritası — kablo planının temeli

Taretin **tüm elektroniği dönen kule üzerinde** (Pi, PSU, TB6600'ler, motorlar, LiDAR, namlu). Bu yüzden kablolar dört bölgeye ayrılır:

```
 [A] SABİT TABAN          [B] PAN GEÇİŞİ        [C-F] DÖNEN KULE        [G] TİLT GEÇİŞİ
 şebeke girişi            slip ring             PSU, Pi, ESP32,          namlu grubu
 sigorta, kontaktör   →   (Ø22 kapsül,      →   TB6600 ×2, pan motoru →  tilt motoru, LiDAR,
 E-Stop butonu            6808'in Ø40           dağıtım klemensi         kamera, 6 kademe
                          deliğinin içine)                               ateşleme kartı
```

**Kural:** [B] ve [G] geçişlerinden geçen **her** kablo silikon izolasyonlu, ince tel örgülü (yüksek esnek) olacak. Sert PVC kablo dönen eksende yorulup kırılır.

---

## 2. [B] Pan geçişi — slip ring devre planı

İki adet 6808 yatağın iç çapı **Ø40 mm**; standart **Ø22 mm kapsül slip ring** içine girer.

| Devre | İşlev | Akım | Not |
|---|---|---|---|
| 1–2 | Şebeke L + N — **E-Stop'la kesilen** hat | ~3 A | 24 V PSU ve 450 V şarjı besler |
| 3 | PE (toprak) | — | Şase sürekliliği |
| 4–5 | Şebeke L + N — **kesilmeyen** hat | ~0,5 A | Yalnız Pi'nin izole USB-C kaynağı |
| 6–9 | Ethernet (2 çift: TX±, RX±) | — | 100BASE-TX. Alternatif: RS-485'e düşür, 2 devre yeter |
| 10–12 | Yedek | — | Sonradan devre eklemek slip ring değiştirmek demektir; boş bırak |

**Alınacak:** Ø22 mm, **12 devreli**, ≥2 A/devre kapsül slip ring — 1 adet (+1 yedek düşünülebilir, yarışma günü arızalanırsa taret döner ama kablo sarar).

**Slip ring almazsan:** ±180° sınırlı yay + eksende servis kangalı; firmware'e sarma sayacı ve yumuşak limit **şart**. Enerji zinciri (drag chain) bu ölçüye sığmaz — tabla ile taban arası yalnız ~40 mm.

---

## 3. Hat tabloları

### [A] Sabit taban (taret dışı)

| # | Hat | Ölçülen | **Boy** | Kesit / tip | Adet |
|---|---|---|---|---|---|
| A1 | Şebeke prizi → sigorta + kontaktör kutusu | saha | **10 m** | 3×1,5 mm² H05VV-F | 1 |
| A2 | E-Stop butonu → kesme rölesi `IN` + GPIO14 ⟨!⟩ | saha | **20 m** | **4×0,75 mm² TTR** (Öznur) | 1 |
| A3 | YKİ → taret (Ethernet) | saha | **10 m** | Cat5e/6 hazır | 1 |
| A4 | Kontaktör → slip ring (kesilen hat) | — | **1 m** | 3×1,5 mm² | 1 |
| A5 | Sigorta → slip ring (kesilmeyen Pi hattı) | — | **1 m** | 2×1,0 mm² | 1 |
| A6 | Slip ring → Ethernet ucu | — | **1 m** | Cat5e/6 | 2 (alt+üst) |

> **E-Stop mimarisi:** buton **röle/kontaktör bobinini** keser, 20 A'i buton kontağından geçirme. Kesilen hat motorları ve 450 V şarjı öldürür; kesilmeyen hat Pi'yi ayakta tutar → YKİ "acil durdurma aktif" görür ve loglar, SD kart bozulmaz.
>
> ⟨!⟩ **A2, 6 Ağustos'ta güncellendi.** Bu belge 29 Temmuz'da yazıldığında A2 "3 m, 2×0,75 mm²" idi; o sırada GPIO14 durum okuma hattı henüz kararlaştırılmamıştı. Kurulan ve doğrulanan hâli: **4×0,75 mm² TTR, 20 m.**
> **Damar 1–2:** 5 V + buton NC1 → kesme rölesi `IN`. **Damar 3–4:** GPIO14 + buton NC2 → GND.
> 20 m, hedeflerin 5–15 m'de durduğu atış zarfının dışına çıkmak için bilinçli seçim; **0,75 mm² elektriksel değil mekanik gerekçeyle** (kablo yerde sürükleniyor, akım <1 A).
> Halkanın çalışması için röle kartının dibinde **2,2 kΩ pull-down + 100 nF** ve jumper **`H`** şart — buton tarafında ya da kablonun ortasında işe yaramazlar.

### [C] Kule içi güç — 24 V DC

| # | Hat | Ölçülen | **Boy** | Kesit / tip | Adet |
|---|---|---|---|---|---|
| C1 | PSU çıkışı → dağıtım klemensi (ana bara) | — | **400 mm** | 4 mm² silikon | 2 (kırmızı+siyah) |
| C2 | Dağıtım → TB6600 #1 | 233 mm | **450 mm** | 1,5 mm² silikon | 2 |
| C3 | Dağıtım → TB6600 #2 | 254 mm | **500 mm** | 1,5 mm² silikon | 2 |
| C4 | Dağıtım → 450 V şarj kaynağı girişi | 150 mm | **350 mm** | 1,5 mm² silikon | 2 |
| C5 | Dağıtım → A4988 (şarjör sürücüsü) | ~500 mm | **1 m** | 1,0 mm² silikon | 2 |
| C6 | Kesilmeyen AC → izole USB-C kaynağı | — | **500 mm** | 1,0 mm² | 2 |
| C7 | İzole kaynak → Pi 5 | 103 mm | **250 mm** | hazır USB-C kablo | 1 |

> ARM rölesi C4 hattını keser (450 V şarjı yetkilendirme). Röle **LV tarafında**, 450 V hattında değil.
> Elde bulunan 4'lü röle modülü **SRD-12VDC** bobinli — 5 V ile çekmiyor. 5 V bobinli (SRD-05VDC-SL-C) sipariş et veya 12 V besleme çek.

### [D] Motor hatları

| # | Hat | Ölçülen | **Boy** | Kesit / tip | Adet |
|---|---|---|---|---|---|
| D1 | TB6600 #1 → NEMA23 **pan** | 96 mm | **250 mm** | ekranlı 4×0,75 mm² | 1 |
| D2 | TB6600 #2 → NEMA23 **tilt** ⟨G⟩ | 205 mm | **450 mm** | ekranlı 4×0,75 mm² silikon | 1 |
| D3 | A4988 → NEMA17-PG518 (şarjör) | ~500 mm | **750 mm** | ekranlı 4×0,5 mm² | 1 |

> Ekranı **yalnız sürücü tarafında** şaseye bağla (tek nokta). İki uçtan bağlarsan toprak halkası kurarsın.
> ⟨G⟩ = tilt eksenini geçer, bkz. §4.

### [E] Yüksek gerilim / ateşleme

| # | Hat | Ölçülen | **Boy** | Kesit / tip | Adet |
|---|---|---|---|---|---|
| E1 | 450 V şarj kaynağı → namlu arka (bara başı) | 175 mm | **400 mm** | 0,5 mm² **≥1 kV** silikon | 2 |
| E2 | 450 V bara — namlu boyunca 6 karta | 337 mm | **500 mm** | 0,5 mm² ≥1 kV silikon | 2 |
| E3 | Bara → kart inişi (kademe başına) | ~100 mm | **150 mm** | 0,5 mm² ≥1 kV silikon | 12 (6×2) |
| E4 | **Deşarj halkası** (cap → SCR → bobin) | — | **≤150 mm** | 4 mm² ≥600 V silikon | 12 (6×2) |
| E5 | Dump direnci hattı | — | **500 mm** | 1,5 mm² ≥1 kV | 2 |
| E6 | HV dönüş / şase barası | 337 mm | **1 m** | 6 mm² örgü bakır | 1 |

> **E4 en kritik kablaj.** Kademe başına gidiş+dönüş **≤15 cm** ve telleri **sıkı yan yana / burgulu** çek. Halka endüktansı doğrudan akımın yükselme hızını, o da namlu çıkış hızını belirler.
> **Kondansatörleri arkada tek kutuda toplama.** CAD'de kademe aralığı 56 mm; dağıtık kartla her halka ≤150 mm kalıyor. Merkezî toplarsan 6. kademenin halkası ~310 mm olur → endüktans 2–3 katına çıkar, güç ölçülebilir biçimde düşer.
> Tüm cap eksileri **E6 barasında** birleşsin, oradan **tek noktadan** şaseye.

### [F] Sinyal ve sensör

| # | Hat | Ölçülen | **Boy** | Tip | Adet |
|---|---|---|---|---|---|
| F1 | Pi 5 ↔ ESP32-S3 (USB) | 40 mm | **500 mm** | USB-A ↔ USB-C **veri** kablosu | 2 (1 yedek) |
| F2 | ESP32 → TB6600 #1 (PUL/DIR/GND) | 344 mm | **600 mm** | 4 damarlı 0,22 mm² kılıflı | 1 |
| F3 | ESP32 → TB6600 #2 (PUL/DIR/GND) | 357 mm | **600 mm** | 4 damarlı 0,22 mm² kılıflı | 1 |
| F4 | ESP32 → pan limit switch ×2 | 252 mm | **500 mm** | 3 damarlı 0,25 mm² | 1 |
| F5 | ESP32 → tilt limit switch ×2 ⟨G⟩ | 219 mm | **500 mm** | 3 damarlı 0,25 mm² silikon | 1 |
| F6 | **IR gate hattı** (6 fototransistör + 5V + GND) ⟨G⟩ | 155+337 mm | **1 m** | **ekranlı 8 damarlı** 0,22 mm² | 1 |
| F7 | **SCR gate hattı** (6 tetik + GND) ⟨G⟩ | 155+337 mm | **1 m** | **ekranlı 8 damarlı** 0,22 mm² | 1 |
| F8 | I2C → namlu arkasındaki ADS1115 ⟨G⟩ | 155 mm | **400 mm** | 4 damarlı **burgulu ekranlı** | 1 |
| F9 | Pi → kamera (CSI) ⟨G⟩ | 243 mm | **500 mm** | 22 pin ↔ 15 pin FPC, Pi 5 tipi | 1 (+300 mm yedek) |
| F10 | Pi/ESP32 → LiDAR TF02-Pro (UART) ⟨G⟩ | 276 mm | **550 mm** | 4 damarlı 0,22 mm² silikon | 1 |
| F11 | Kart üstü I2C (MPU6500, INA226) | — | **300 mm** | burgulu ikili | 2 |
| F12 | Röle kontrol (ARM + dump tetik) | — | **500 mm** | 2 damarlı 0,25 mm² | 2 |
| F13 | ESP32 → AS5600 pan enkoder | *ölçülmedi* | **500 mm** | 4 damarlı **burgulu ekranlı** | 1 |
| F14 | ESP32 → AS5600 tilt enkoder | *ölçülmedi* | **500 mm** | 4 damarlı **burgulu ekranlı** | 1 |

> **F13/F14 boyları varsayım.** Enkoderler pan/tilt motorlarının arka miline biniyor; motorlar limit switch'lerle aynı bölgede olduğu için F4 (252 mm) ve F5 (219 mm) ölçülerini temel aldım → ikisi de 500 mm. **Montajdan önce gerçek mesafeyi ölç.**
> Tilt enkoderi tilt eklemini geçiyorsa ⟨G⟩ kurallarına tabi olur (silikon, servis kangalı) — mekanik yerleşim netleşince işaretle.
> F8 ile aynı gerekçe: I2C 10 kHz'de çalıştığı için 500 mm burgulu-ekranlı hat sorunsuz.

> **F6/F7 ekranlı olmak zorunda.** 1000 A'lik darbenin tam yanından geçiyorlar; SCR gate hattına gürültü binerse **istenmeyen ateşleme** olur.
> **F6 sadeleştirmesi:** IR vericileri sürekli yak (5 V + 220 Ω seri). O zaman namlu boyunca 6 ayrı sürücü hattı gerekmez — 2 besleme damarı yeter, kalan 6 damar alıcı sinyalleri olur. Tek 8 damarlı kablo işi bitirir.
> **F8 notu:** I2C zaten 10 kHz'e düşürülmüş durumda; 400 mm burgulu-ekranlı hat bu hızda sorunsuz. ADS1115'i namlu arkasına koymak, 6 kanal izole analog çıkışı 1 m boyunca taşımaktan **çok** daha temiz.
> **F9:** `MALZEME_LISTESI.md` §6.B "300 mm veya 500 mm, ikisini de al" diyordu. Ölçüm netleştirdi: **500 mm doğru boy, 300 mm yetmez** (243 mm düz mesafe + tilt geçişi servis kangalı). 300 mm'yi yedek olarak al. **DSI adaptörü değil** — DSI ekran içindir, kamera görünmez.
>
> **⚠️ F9 konnektör notu.** Kamera **Raspberry Pi Camera Module 3** (IMX708),
> konnektörü **15 pin**; Pi 5 tarafı **22 pin**. Gereken kablo bu yüzden
> **22 ↔ 15 pin, 0,5 mm, 500 mm.** Bu proje bir ara Arducam B0262 (IMX477,
> 22 pin) ile kuruluydu ve o kamerada 22 ↔ 22 kablo gerekiyordu — eski
> sipariş notlarında bu ayrım karışabilir, konnektörü kameradan say.
>
> 22 ↔ 22 kablolar piyasada çoğunlukla 15 cm; jenerik (topraksız) FFC'yi
> 500 mm'ye uzatmak MIPI CSI-2 için risklidir. 50 cm üstü gerekiyorsa düz FPC
> yerine aktif CSI uzatma kiti kullanılmalı.

### [F-ek] TB6600 bağlantı tablosu — ORTAK KATOT (2026-08-03'te sahada doğrulandı)

TB6600'ün üç girişi de optokuplörlü. Çalışan bağlantı **ortak katot**: eksi uçlar ESP32 GND'de, artı uçlar GPIO'da. `hss_3`/`hss_4` çizimlerindeki hâli tersiydi ve `PUL` hiç bağlı değildi — o çizimler yanlıştı, kablolama doğruydu. Güncel çizim: **`hss_6.fzz`**.

| TB6600 ucu | **A1 — pan** | **A2 — tilt** |
|---|---|---|
| PUL+ | ESP32 **GPIO4** | ESP32 **GPIO6** |
| DIR+ | ESP32 **GPIO5** | ESP32 **GPIO7** |
| PUL− / DIR− | sürücü üzerinde köprü → ESP32 **GND** | aynı |
| ENA+ ⟨!⟩ | ESP32 **GPIO12** | ESP32 **GPIO13** |
| ENA− | ortak GND barası | ortak GND barası |
| ENA pull-down | **10 kΩ** GPIO12 → GND | **10 kΩ** GPIO13 → GND |
| VDC / GND | 24 V barası (6S LiPo da uygun, aşağıya bak) | aynı |

**F2 ve F3'ün ikisi de artık 4 damar KULLANIYOR** (PUL+, DIR+, ENA+, GND) —
yedek damar kalmıyor. Yedek isteniyorsa 5 damarlı çekilmeli.

> ⟨!⟩ **HER İKİ ENA DA BAĞLANDI (2026-08-15 kararı).** Bu tablo 3 Ağustos'ta
> yazıldığında ENA'lar boştaydı; 6 Ağustos'ta yalnız pan'a **GPIO21**'den hat
> çekilmişti. **15 Ağustos'ta ikisi de bağlandı ve pan GPIO21 → GPIO12'ye
> taşındı** (iki ENA komşu pinlerden çıksın diye).
>
> | | pan | tilt |
> |---|---|---|
> | ENA+ | GPIO**12** | GPIO**13** |
> | ENA− | ortak GND | ortak GND |
> | pull-down | 10 kΩ → GND | 10 kΩ → GND |
>
> Firmware (`hss_ana.ino:62,76`): **`LOW = sürücü AÇIK`, `HIGH = kapalı`.**
> Komutlar: `panac`/`pankap`/`panena <sn>` · `tiltac`/`tiltkap`.
> Telemetride `panena=` ve `tiltena=` alanları.
> Ölçülen kazanç (pan): boşta duruş akımı **0,279 A**.
>
> **10 kΩ pull-down her ikisinde de ŞART.** Boot ve reset anında firmware pini
> yapılandırana kadar ayak havada kalır; pull-down onu LOW'a çeker, yani
> sürücüler **açık** başlar ve eksenler tutar. Tilt'te bu kural daha kritik:
> pull-down yoksa açılışta bir anlık HIGH namlunun düşmesi demek.
> Seri direnç gerekmez (opto girişinin kendi direnci var, ENA statik sinyal).
> 12 ve 13 ADC2'de ama ENA dijital çıkış olduğu için o kısıt geçersiz.
>
> ⚠ `pan_ena_baglantisi_26.html` çizimi hâlâ **GPIO21** gösteriyor, güncellenmedi.

**Tilt'te OTOMATİK boşta kapatma neden yok:** ENA enerjilenince TB6600 çıkışları kapanır, motor serbest kalır. Tilt ekseni 5:1 düz dişli ve kendinden kilitlemeli değil, ağırlık merkezi eksenin 145 mm önünde → sürücü devre dışı kalırsa **namlu kendi ağırlığıyla düşer**. Acil durdurma GPIO'dan değil, motor beslemesini kesen donanım E-stop'tan olmalı. Her iki ucu da boş bırak; yalnız ENA−'yi GND'ye çekmek zararsızdır ama işe yaramaz (devre kapanmaz).

**Doğrulama (2026-08-03):** `firmware/motor_test_s3` ile pan ve tilt ayrı ayrı 1 tam tur ileri + 1 tam tur geri döndürüldü, miller boştayken. DIP = **1/16 mikroadım**, 3200 adım = 1 tur teyit edildi. Yön (DIR HIGH = hangi taraf) montajda belirlenecek.

**İki uyarı:**
- ESP32 3,3 V çıkışı optolar için sınırda (seri direnç 5 V'a göre boyutlanmış). Test hızında (400 µs yarım periyot) sorun çıkmadı; hedef hız yükselince adım kaçarsa **74HCT14** tampon ekle.
- **Güç:** VDC 9–42 V. Eldeki **6S LiPo** (18–25,2 V) doğrudan uygun. Şart: (+) uca **15 A sigorta**, XT60, doğru polarite. **LiPo eksisini ESP32 GND'ye bağlama** — optokuplör izolasyonu 1000 A'lik deşarj darbesine karşı en değerli korumamız.

### [F-ek] AS5600 enkoder bağlantı tablosu

Her iki enkoder de 4 damar: VCC, GND, SDA, SCL. `DIR` kablo çekilmez — modül üzerinde GND'ye köprülenir.

| AS5600 ucu | **ENC1 — pan** | **ENC2 — tilt** |
|---|---|---|
| VCC | ESP32 **3V3** | ESP32 **3V3** |
| GND | ESP32 **GND** | ESP32 **GND** |
| SDA | ESP32 **GPIO8** | ESP32 **GPIO10** |
| SCL | ESP32 **GPIO9** | ESP32 **GPIO11** |
| DIR | modül üzerinde GND'ye | modül üzerinde GND'ye |
| OUT | boşta | boşta |

**Neden iki ayrı I²C hattı:** AS5600'ün adresi `0x36`'da sabit, değiştirilemez. İkisi aynı hatta konamaz. ESP32-S3'ün iki I²C çevre birimi olduğu için mux gerekmiyor:

- **I²C0** = `GPIO8`/`GPIO9` → ENC1 pan (0x36) + INA226 (0x40) + GY9250 (0x68)
- **I²C1** = `GPIO10`/`GPIO11` → ENC2 tilt (0x36), tek başına

Firmware karşılığı: `Wire.begin(8, 9)` ve `Wire1.begin(10, 11)`, ikisinde de adres `0x36`.

Montaj kuralları:
- **5 V verme, 3V3 ver.** AS5600 3,3 V parçası.
- **Pull-up:** I²C0'da zaten INA226 ve GY9250'nin pull-up'ları var; ENC1 modülünün pull-up dirençlerini sök. ENC2 kendi hattında tek olduğu için onunkiler kalsın.
- **DIR = GND** → saat yönünde artan açı. Yön ters çıkarsa DIR'i VCC'ye al, firmware'de işaret çevirme.
- Mıknatıs **6 × 2,5 mm diametral** (radyal değil), hava aralığı 0,5–2 mm, kaçıklık ≤0,25 mm.
- Mıknatıs doğrudan çelik mil ucuna yapıştırılmaz — merkezde durmaz, kenara kayar. Demir dışı göbek kullan (3D baskı, thingiverse.com/thing:7219474).
- Montaj sonrası doğrulama: `STATUS` (0x0B) → `MD=1, ML=0, MH=0`; `AGC` (0x1A) orta bantta; bir tam turda `RAW_ANGLE` hatası tur başına tek sinüs veriyorsa kaçıklık var.

### [G] Tilt eksenini geçen hatlar — ayrı düşün

⟨G⟩ işaretli **8 hat** tilt ekleminden geçiyor: D2, F5, F6, F7, F8, F9, F10 (+ şarj kaynağı namlu grubunda değilse E1 de).

Kurallar:
1. Hepsi **silikon, ince tel örgülü**. Boylar servis kangalı payını zaten içeriyor.
2. Eklemde **tek demet** hâlinde toplanıp spiral sargıyla bağlansın, iki uçtan **gerginlik alıcı** (strain relief) ile sabitlensin. Kabloyu gergin bırakma, dönme yarıçapının içinden geçir.
3. Demeti tilt ekseninin **mümkün olduğunca yakınından** geçir — eksene uzak geçen kablo her derecede daha çok uzar.
4. Tilt hareket aralığını ölçüp kangalı ona göre bırak; kabloyu takıp **tam aralıkta elle 20 tur** çevirerek sürtme/gerilme var mı bak.

> ⚠️ **Tasarım önerisi: 450 V şarj kaynağını namlu grubuna taşı.** Kondansatörler namlu üzerinde olduğu için, kaynak kulede kalırsa **450 V tilt eklemini geçer** — hareketli eklemde ölümcül gerilim, kablo yorulması ve izolasyon aşınması demek. Kaynağı da namluya alırsan eklemi yalnız **24 V + sinyal** geçer. Bu, listedeki en önemli mimari karar.

---

## 4. SİPARİŞ ÖZETİ — kablo

Aşağıdaki metrajlar yukarıdaki hatların toplamı + kesme firesi payıdır.

| Kalem | Miktar | Not |
|---|---|---|
| Silikon kablo **4 mm²** | **2 m kırmızı + 2 m siyah** | Ana 24 V barası + 6 deşarj halkası |
| Silikon kablo **1,5 mm²** | **2 m kırmızı + 2 m siyah** | TB6600 ve şarj kaynağı beslemeleri |
| Silikon kablo **1,0 mm²** | **2 m + 2 m** | A4988, izole kaynak |
| **4×0,75 mm² TTR** | **20 m** | E-Stop butonu → kesme rölesi `IN` + GPIO14 (A2) |
| Silikon kablo **≥1 kV, 0,5 mm²** | **2 m + 2 m** | 450 V bara + kart inişleri + dump |
| Ekranlı **4×0,75 mm²** (motor) | **2 m** | D1 + D2, yedek payıyla |
| Ekranlı **4×0,5 mm²** | **1,5 m** | D3 şarjör motoru |
| Ekranlı **8 damarlı 0,22 mm²** | **5 m** | F2, F3, F6, F7 + yedek |
| Burgulu ekranlı **4 damarlı** | **2 m** | F8 I2C, F10 LiDAR |
| Sinyal kablosu 0,22–0,25 mm², çok renkli | **10 m** | F4, F5, F11, F12 ve montaj sırasında çıkacaklar |
| Örgü bakır şase barası **6 mm²** | **1 m** | E6 |
| Şebeke kablosu **3×1,5 mm² H05VV-F** | **12 m** | A1 + A4 |
| Cat5e/6 hazır kablo | **10 m** + 2 × 1 m | A3 + slip ring uçları |
| USB-A ↔ USB-C **veri** kablosu, 0,5 m | **2 adet** | Şarj kablosu değil, veri hattı olan tip |
| CSI FPC 22↔15 pin, **500 mm** | **1 adet** | Asıl kullanılacak |
| CSI FPC 22↔15 pin, 300 mm | **1 adet** | Yedek |
| **Kapsül slip ring Ø22, 12 devre, ≥2 A** | **1 adet** | §2 |

**Karşılaştırma:** eski listede ~55 m kablo vardı, bu listede ~**20 m** (saha kabloları hariç ~8 m). Fark, "Pi kaidede, kablolar 3–10 m" varsayımının gerçek gövde ölçüsüyle (437 mm yükseklik, tüm elektronik kule üzerinde) değişmesinden geliyor.

**Kesitleri düşürme.** Kısa hatta gerilim düşümü sorun değil ama 4 mm² kararı 20 A akıma ve mekanik dayanıma göre verildi; 1000 A darbe taşıyan E4 için de öyle.

---

## 5. Konnektör ve sarf malzeme

| Parça | Miktar | Not |
|---|---|---|
| Kablo yüksüğü (ferrule) + pense | 4 / 1,5 / 0,75 / 0,5 mm² setleri | Klemense çok telli kablo takarken **şart** — çıplak tel zamanla gevşer, HV'de ark yapar |
| Kablo pabucu M4/M5 (4–6 mm²) | 20 | Bara, PSU, şase bağlantıları |
| Isı ile daralan makaron seti | 1 set | **HV uçlarında çift kat** |
| Klemens KF128-5.08 (2/3'lü) | 16 | Alçak gerilim |
| Klemens KF7.62 (2'li) | 16 | HV / bobin çıkışı |
| Spiral kablo sargısı | 5 m | Demetleme, özellikle tilt geçişi |
| Kablo bağı + yapışkanlı taban | 1 paket | Dönen grupta serbest kablo bırakma |
| Ferrit boncuk (klipsli) | 10 | Motor ve sinyal demetlerinin uçlarına |
| Kablo etiketi / numaralandırma seti | 1 | Bu tablodaki **hat numaralarını** (A1, C2, F6…) kabloya yaz |
| HV uyarı etiketi "450 V — DOKUNMA" | birkaç | Yarışma güvenlik puanı |

**Renk kuralı (uy, sonra aramakla uğraşma):**
24 V+ **kırmızı** · GND **siyah** · 450 V+ **turuncu** (çift kat makaron) · HV dönüş **mavi** · 5 V **sarı** · sinyal **çok renkli** · PE **sarı-yeşil**.

---

## 6. Montaj kuralları (kısa)

1. **Motor kablolarını HV ölçüm ve gate kablolarıyla aynı kanaldan paralel çekme.** Step sürücünün anahtarlama gürültüsü analog okumaya biner. Kesişmeleri gerekiyorsa **dik açıyla** kesişsin.
2. **Şase tek noktadan.** Tüm cap eksileri E6 barasında birleşir, bara tek noktadan şaseye iner. Ekranlar yalnız bir uçtan bağlanır.
3. **Hareketli her demet gergin değil, kangallı.** Pan (slip ring yoksa) ve tilt geçişleri.
4. **Kart üzerinde çalışmadan önce her seferinde:** ARM rölesi aç → dump tetikle → boşaltma çubuğuyla elle boşalt → **voltmetreyle <10 V doğrula**. Üçünü de yap.
5. Kablo demetini **taretin dönme yönünde** takılmayacak şekilde sabitle; serbest kablo dönerken bir yere takılırsa ya kablo ya mekanik gider.

---

## 7. Bu listenin açık uçları

| Konu | Ne zaman kapanır |
|---|---|
| ESP32, şarjör ve 450 V kaynağının **kesin yeri** | v5 montajı STEP olarak çıkınca |
| Kondansatör kartlarının namludaki gerçek konumu | aynı |
| Tilt hareket aralığı (servis kangalı boyu buna bağlı) | mekanik ekipten limit açıları alınınca |
| Slip ring mi, sınırlı yay mı | karar verilince — B bölümü ve firmware sarma sayacı buna bağlı |
| Ethernet mi RS-485 mi (slip ring devre sayısı) | telemetri köprüsü yazılırken |
