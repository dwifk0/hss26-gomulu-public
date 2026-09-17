// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// =====================================================================
//  HSS / MIGFER - ANA FIRMWARE  (ESP32-S3-N16R8)
//  Teknofest Celikkubbe Hava Savunma Sistemleri
// =====================================================================
//
//  MIMARI NOTU (2026-08-06 karari):
//  Namlu ile ESP32 BAGIMSIZ. 6 SCR gate ve 6 IR gate hatti bu firmware'e
//  hic girmiyor; her kademe kendi IR fototransistoru ile kendi SCR'sini
//  tetikliyor. ESP32'nin atis uzerindeki tek yetkisi SARJOR'u surmek
//  (mermi vermek) ve sarj hattini yonetmek. Bu yuzden:
//    - "atisa yasak alan" SCR'yi degil SARJOR'u kilitler
//    - mermi namluya girdikten sonra yazilim atisi durduramaz
//    - kademe hizi (gate1->gate2) olculemez
//
//  ACIL STOP: donanim rolesi motor beslemesini keser. Bu firmware yalnizca
//  GPIO14'ten DURUMU okur ve adim darbelerini keser. Mandalli: buton
//  birakilinca kendiliginden devam ETMEZ, "estop reset" gerekir (sartname
//  hakem onayi maddesi).
//
//  DIP DURUMU (2026-08-06'da anahtarlar tek tek okundu):
//    S1=OFF S2=OFF S3=ON  -> 1/16 mikroadim, 3200 pulse/rev
//    S4=ON  S5=OFF S6=OFF -> 2,0 A faz akimi
//  Varsayilan ayarlar buna gore. DIP'i degistirirsen "mikroadim <n>" ver ve
//  "kaydet" de - IKISINI BIRDEN degistirmezsen aci muhasebesi sasar.
//  Motor etiketi 2,8 A (23HS30-2804S); yuk baglanmadan once S4=OFF S5=OFF
//  S6=ON yapilip tam moment acilmali.
//
//  HIZ TAVANI: 1/16'da pan tam turu 32.000 adim. Adim ureteci 25.000 adim/s
//  ile sinirli oldugundan pan en fazla ~281 deg/s, tilt ~562 deg/s doner.
//  Daha fazlasi gerekirse ISR_HZ 100 kHz'e cikarilabilir.
//
//  MEKANIK: pan 24T->240T = 10:1, tilt 20T->100T = 5:1 (STEP olcumu).
//  Yani tam tur = 200 * mikroadim * oran adim.
//
// ---------------------------------------------------------------------
//  PIN HARITASI
//    4/5    TB6600 #1 pan  PUL+/DIR+   (ortak katot)
//    6/7    TB6600 #2 tilt PUL+/DIR+   (ortak katot)
//    12     TB6600 #1 pan  ENBL+  (LOW=surucu ACIK)  + 10k pull-down
//    13     TB6600 #2 tilt ENA+   (LOW=surucu ACIK)  + 10k pull-down
//    15/16/17  A4988 sarjor STEP/DIR/ENABLE  (ENABLE aktif-DUSUK)
//    14     acil stop durum okuma (4k7 pull-up + 100nF; LOW=kapali/normal,
//           HIGH=basili veya kablo kopuk -> fail-safe)
//    8/9    I2C0 SDA/SCL - AS5600 pan 0x36, INA226 0x40, IMU 0x68
//    10/11  I2C1 SDA/SCL - AS5600 tilt 0x36
//    1      ADC1 - ACS724 akim sensoru cikisi (2/3 bolucu uzerinden!)
//    18     ESP guc kesme yolu -> ARM rolesi IN (LOW-trigger, LOW=kes).
//           Rolenin NC kontagi acil stop butonuyla SERI; ikisinden biri
//           tek basina motor beslemesini kesebilir.
//    Bos:   2 (ADC1), 21, 47
//    YASAK: 35/36/37 (oktal PSRAM), 48 (onboard RGB LED)
//    NOT:   12/13/18 ADC2'dedir, WiFi acikken ANALOG okunamaz. ENA'lar
//           dijital cikis oldugu icin bu kisit onlari etkilemez.
//
//  DERLEME: fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc   (CDCOnBoot SART)
// =====================================================================

#include <Wire.h>
#include <Preferences.h>
#include <soc/gpio_struct.h>   // ISR icinde hizli pin surme (out_w1ts/out_w1tc)

// ---------------------- pinler ----------------------
#define PAN_PUL      4
#define PAN_DIR      5
#define PAN_ENA      12      // TB6600 #1 ENBL+ . LOW = surucu ACIK, HIGH = kapali
#define TILT_PUL     6
#define TILT_DIR     7
// TILT ENA 2026-08-15'te CEKILDI (once hic bagli degildi). Hat var ama
// OTOMATIK bosta kapatma BILEREK YOK: tilt 5:1 duz disli, kendinden
// kilitlemeli degil, agirlik merkezi eksenin 145 mm onunde -> surucu
// kapaninca namlu kendi agirligiyla duser. Yalnizca elle "tiltkap" kapatir
// (bakim/tasima icin) ve ilk hareket komutunda kendiliginden geri acilir.
// Her iki ENA'da da 10k pull-down SART: boot/reset aninda firmware pini
// yapilandirana kadar ayak havada kalir; pull-down onu LOW'a ceker, yani
// suruculer ACIK baslar ve eksenler tutmaya devam eder.
#define TILT_ENA     13      // TB6600 #2 ENA+  . LOW = surucu ACIK, HIGH = kapali

#define SARJOR_STEP  15
#define SARJOR_DIR   16
#define SARJOR_EN    17      // A4988: LOW = enable

#define ESTOP_PIN    14

#define GUC_PIN      18      // ARM rolesi IN - NC kontagi buton halkasina SERI
#define GUC_ACIK     HIGH    // role birakik, NC kapali -> guc var
#define GUC_KESIK    LOW     // role ceker,  NC acilir  -> guc yok

#define I2C0_SDA     8
#define I2C0_SCL     9
#define I2C1_SDA     10
#define I2C1_SCL     11

#define AKIM_ADC     1       // ACS724 (bolucu uzerinden)

// ---------------------- sabitler ----------------------
#define ISR_HZ            50000UL   // adim uretecinin tick frekansi
#define MOTOR_TAM_ADIM    200
#define PAN_ORAN          10.0f
#define TILT_ORAN         5.0f

#define ADRES_AS5600      0x36
#define ADRES_INA226      0x40
#define ADRES_IMU         0x68

#define AS5600_RAW_ANGLE  0x0C
#define AS5600_STATUS     0x0B
#define AS5600_AGC        0x1A
#define INA226_SHUNT      0x01
#define INA226_BUS        0x02
#define INA226_CONFIG     0x00
#define INA226_ID         0xFF

#define ESTOP_DEBOUNCE_MS 30

// ---------------------- ayarlar (NVS'e kaydedilir) ----------------------
struct Ayarlar {
  uint16_t mikroadim;        // TB6600 DIP: 1/2/4/8/16/32
  uint16_t sarjorMikroadim;  // A4988 DIP
  uint32_t hizKruvaziyer;    // adim/s
  uint32_t hizBaslangic;     // adim/s (rampa alt ucu)
  uint32_t rampaAdim;        // kac adimda hizlanip yavaslar
  uint32_t sarjorHiz;        // adim/s
  uint32_t sarjorAdimAtis;   // bir mermi surmek icin adim
  float    tiltMin, tiltMax; // derece, yumusak limit
  float    panMin, panMax;   // derece, yumusak limit (esit ise sinirsiz)
  float    yasakPay;         // derece, sektor kenarina eklenen emniyet payi
  uint8_t  yasakSayi;
  float    yasakBas[4], yasakBit[4];   // pan sektorleri, derece
  float    tiltRefOfset;     // dayama noktasinin gercek acisi (derece)
  float    acsMVperA;        // ACS724 hassasiyeti (mV/A)
  float    acsSifirV;        // ACS724 sifir noktasi (V)
};

Ayarlar ay = {
  .mikroadim = 16,           // DIP 2026-08-06'da okundu: S1=OFF S2=OFF S3=ON = 1/16
  .sarjorMikroadim = 16,
  .hizKruvaziyer = 6000,
  .hizBaslangic  = 500,
  .rampaAdim     = 800,
  .sarjorHiz     = 1200,
  .sarjorAdimAtis = 3200,
  // Varsayilan tilt limiti bilerek GENIS birakildi: mekanik sinir montaja gore
  // degisiyor ve dar bir varsayilan sahada "RED: tilt limiti" ile onu kesiyordu.
  // Daraltmak operatorun isi: `tiltlim <min> <max>` + `kaydet`. NVS'te kayitli
  // deger varsa BU SATIR ETKISIZDIR - `d` dokumundeki degere bak.
  .tiltMin = -90.0f, .tiltMax = 90.0f,
  .panMin = 0.0f, .panMax = 0.0f,       // esit -> sinirsiz
  .yasakPay = 5.0f,
  .yasakSayi = 0,
  .yasakBas = {0,0,0,0}, .yasakBit = {0,0,0,0},
  .tiltRefOfset = 0.0f,
  // ACS724-50AB, 5 V beslemede (2026-08-06'da 3.3 V'tan geri alindi): katalog
  // degeri 40 mV/A, sifir = Vcc/2 = 2.5 V. Cip 4.5 V altinda spec disi oldugu
  // icin onceki 3.3 V beslemesi tahminiydi (26.4 mV/A, sifir 1.65 V).
  // Cikis Vcc'ye ORANLI: besleme 5.00 degilse hassasiyet 40 x Vgercek/5,
  // sifir da Vgercek/2 olur. Bu yuzden rewire sonrasi motor beslemesi KAPALI
  // iken "acs sifir" + "kaydet" sart - sifiri rayin gercek degerine oturtur.
  // NOT: bu varsayilanlar yalnizca NVS bosken gecerli; kayitli ayar varsa
  // "acs 40 2.5" ile calisirken degistirilir.
  .acsMVperA = 40.0f,
  .acsSifirV = 2.50f
};

Preferences nvs;

// ---------------------- adim ureteci (DDS) ----------------------
// Her eksen icin 32 bit faz akumulatoru. Her ISR tick'inde artis eklenir,
// tasma olunca bir adim darbesi cikar. Adim frekansi:
//     f_adim = ISR_HZ * artis / 2^32
// Darbe bir tick HIGH, en az bir tick LOW kalir -> tavan ISR_HZ/2 = 25 kHz.
struct Eksen {
  uint8_t  pulPin, dirPin;
  volatile int32_t  konum;        // adim, isaretli
  volatile int32_t  hedef;        // adim
  volatile uint32_t akum;
  volatile uint32_t artis;        // anlik hiz
  volatile uint32_t artisBas;     // rampa alt ucu
  volatile uint32_t artisKruv;    // kruvaziyer hiz
  volatile uint32_t rampa;        // bu hareketin rampa adimi
  volatile uint32_t hareketAdim;  // bu harekette atilan adim
  volatile bool     dirPinDurum;
  volatile bool     darbeAcik;
  volatile bool     serbest;      // true -> hedef yok, hareket etme
};

Eksen pan   = {PAN_PUL,  PAN_DIR,  0,0,0,0,0,0,0,0,false,false,true};
Eksen tilt  = {TILT_PUL, TILT_DIR, 0,0,0,0,0,0,0,0,false,false,true};
Eksen sarjor= {SARJOR_STEP, SARJOR_DIR, 0,0,0,0,0,0,0,0,false,false,true};

portMUX_TYPE muks = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t *zaman = nullptr;

volatile bool adimlarKesik = true;   // acil stop / disarm -> darbe uretme

// adim/s -> artis kelimesi
// Her adimdan sonra darbeyi indirmek icin BIR tick harcaniyor (LOW suresi
// garantisi). Yani gercek periyot = (tasma tick'i) + 1. Istenen hizin
// gerceklesmesi icin artis buna gore hesaplanir:
//     artis = 2^32 * hz / (ISR_HZ - hz)
// Tavan ISR_HZ/2 = 25000 adim/s (bir tick HIGH + bir tick LOW).
static inline uint32_t hzToArtis(uint32_t hz) {
  if (hz == 0) return 0;
  if (hz > ISR_HZ / 2) hz = ISR_HZ / 2;
  uint64_t a = ((uint64_t)hz << 32) / (ISR_HZ - hz);
  if (a > 0xFFFFFFFFULL) a = 0xFFFFFFFFULL;
  return (uint32_t)a;
}

// ISR icinde tek adimin hizini gunceller (her 8 adimda bir cagrilir)
static inline void IRAM_ATTR hizGuncelle(Eksen *e) {
  uint32_t kalan = (uint32_t)abs(e->hedef - e->konum);
  uint32_t r = e->rampa;
  uint32_t fark = e->artisKruv - e->artisBas;
  if (r == 0) { e->artis = e->artisKruv; return; }
  if (e->hareketAdim < r) {
    e->artis = e->artisBas + (uint32_t)(((uint64_t)fark * e->hareketAdim) / r);
  } else if (kalan < r) {
    e->artis = e->artisBas + (uint32_t)(((uint64_t)fark * kalan) / r);
  } else {
    e->artis = e->artisKruv;
  }
}

static inline void IRAM_ATTR eksenTick(Eksen *e) {
  // 1) onceki darbeyi indir, bu tick'te yeni darbe uretme (LOW suresi garanti)
  if (e->darbeAcik) {
    GPIO.out_w1tc = (1U << e->pulPin);   // hizli digitalWrite(LOW)
    e->darbeAcik = false;
    return;
  }
  if (e->serbest || adimlarKesik) { e->artis = 0; e->akum = 0; return; }

  int32_t delta = e->hedef - e->konum;
  if (delta == 0) { e->artis = 0; e->akum = 0; e->serbest = true; return; }

  // 2) yon pini degisecekse once onu ayarla, adimi bir sonraki tick'e birak
  bool istenenYon = (delta > 0);
  if (istenenYon != e->dirPinDurum) {
    e->dirPinDurum = istenenYon;
    if (istenenYon) GPIO.out_w1ts = (1U << e->dirPin);
    else            GPIO.out_w1tc = (1U << e->dirPin);
    return;                                   // >=20 us DIR setup suresi
  }

  // 3) faz akumulatoru
  uint32_t once = e->akum;
  e->akum = once + e->artis;
  if (e->akum < once) {                       // tasma -> adim
    GPIO.out_w1ts = (1U << e->pulPin);
    e->darbeAcik = true;
    e->konum += istenenYon ? 1 : -1;
    e->hareketAdim++;
    if ((e->hareketAdim & 0x07) == 0) hizGuncelle(e);
  }
}

void IRAM_ATTR zamanISR() {
  eksenTick(&pan);
  eksenTick(&tilt);
  eksenTick(&sarjor);
}

// ---------------------- eksen yardimcilari ----------------------
float derecePerAdim(float oran) {
  return 360.0f / ((float)MOTOR_TAM_ADIM * (float)ay.mikroadim * oran);
}
float panDerece()  { return pan.konum  * derecePerAdim(PAN_ORAN); }
float tiltDerece() { return tilt.konum * derecePerAdim(TILT_ORAN); }

int32_t dereceToAdim(float d, float oran) {
  return (int32_t)lroundf(d / derecePerAdim(oran));
}

void panSurucuAc();          // ileri bildirim; asagida tanimli
void tiltSurucuAc();

void eksenHedefle(Eksen *e, int32_t hedefAdim, uint32_t kruvHz) {
  // Pan surucusu bosta kapatilmis olabilir. Adim URETMEDEN once geri ac,
  // yoksa ilk darbeler bosa gider ve konum sayaci sessizce kayar. Tek cikis
  // noktasi burasi oldugu icin butun hareket komutlari otomatik kapsanir.
  // Tilt otomatik kapanmaz ama "tiltkap" ile elle kapatilmis olabilir.
  if      (e == &pan)  panSurucuAc();
  else if (e == &tilt) tiltSurucuAc();
  portENTER_CRITICAL(&muks);
  // Jog, hedefi ayni yonde surekli one itiyor. Her cagride rampayi sifirlamak
  // ekseni her seferinde baslangic hizina dusuruyor ve hareket duraklaya
  // duraklaya ilerliyordu. Zaten hareket halindeki bir eksen ayni yone
  // hedeflenirse hizlanma durumu KORUNUR; yalnizca yeni/ters hareket rampayi
  // bastan kurar.
  int32_t eskiKalan = e->hedef - e->konum;
  int32_t yeniKalan = hedefAdim - e->konum;
  bool ayniYonDevam = !e->serbest && eskiKalan != 0 && yeniKalan != 0 &&
                      ((eskiKalan > 0) == (yeniKalan > 0));

  e->hedef      = hedefAdim;
  e->artisBas   = hzToArtis(ay.hizBaslangic);
  e->artisKruv  = hzToArtis(kruvHz);
  uint32_t mesafe = (uint32_t)abs(yeniKalan);
  e->rampa      = min((uint32_t)ay.rampaAdim, mesafe / 2);
  if (!ayniYonDevam) {
    e->hareketAdim = 0;
    e->artis      = e->artisBas;
    e->akum       = 0;
  }
  e->serbest    = (mesafe == 0);
  portEXIT_CRITICAL(&muks);
}

void eksenDurdur(Eksen *e) {
  portENTER_CRITICAL(&muks);
  e->hedef   = e->konum;
  e->artis   = 0;
  e->akum    = 0;
  e->serbest = true;
  portEXIT_CRITICAL(&muks);
}

// YUMUSAK durdurma - `dur` komutunun ve jog tusu birakildiginda gozcunun
// kullandigi yol. Hedefi oldugu yere cakmak yerine mevcut hizin gerektirdigi
// FREN MESAFESI kadar one koyar; hizGuncelle()'nin yavaslama kolu (kalan <
// rampa) hizi oradan asagi indirir. Eskiden `dur` hedefi aninda konuma
// esitliyordu, tam hizda bu sert bir sarsinti ve adim kaybi demekti.
//
// Fren mesafesi hizlanmada ne kadar yol alindiysa o kadar: min(hareketAdim,
// rampa). Yavas giden eksen kisa, tam hizdaki uzun frenler.
//
// ACIL STOP BU YOLU KULLANMAZ - orada eksenDurdur() ile aninda kesiliyor.
void eksenYumusakDurdur(Eksen *e) {
  portENTER_CRITICAL(&muks);
  int32_t delta = e->hedef - e->konum;
  if (!e->serbest && delta != 0) {
    uint32_t fren = (e->hareketAdim < e->rampa) ? e->hareketAdim : e->rampa;
    if (fren == 0) fren = 1;
    // Fren egrisi MEVCUT hizdan bassin, yoksa hizGuncelle bir anlik
    // kruvaziyere ziplatir.
    e->artisKruv   = (e->artis > e->artisBas) ? e->artis : e->artisBas;
    e->rampa       = fren;
    e->hareketAdim = fren;              // hizlanma koluna geri dusmesin
    e->hedef       = e->konum + (int32_t)((delta > 0) ? fren : -fren);
  }
  portEXIT_CRITICAL(&muks);
}

bool eksenMesgul(Eksen *e) {
  portENTER_CRITICAL(&muks);
  bool m = !e->serbest;
  portEXIT_CRITICAL(&muks);
  return m;
}

// ---------------------- pan surucu ENA (bosta kapatma) ----------------------
// ENA baglanmadigi surece TB6600 24 V verili oldugu HER AN aciktir: motor dursa
// da, acil stop mandali kilitliyken de sargidan akim gecer. Olculdu 2026-08-06:
// duruşta 0.27 A / 24 V = ~6.5 W, motor basina ~3 W, hepsi isiya gidiyor. Pan
// motoru ve surucusu bu yuzden isiniyordu. GPIO12 ile bosta susturuyoruz.
//
// BOSTA KAPATMA SADECE PAN'DA. Tilt'in ENA hatti da 2026-08-15'te cekildi
// (GPIO13) ama orada cikis kapaninca namlu DUSER, o yuzden zaman asimli
// kapatma tilt'e uygulanmaz; GPIO13 yalnizca elle (`tiltkap`) kullanilir.
//
// NEDEN ZAMAN ASIMLI, "hep kapali" degil: enkoder mekanik olarak monteli
// olmadigi icin konum yalnizca adim sayimiyla biliniyor. Surucu kapaliyken
// taret disaridan itilirse sayac SESSIZCE kayar ve kimse fark etmez. Bu yuzden
// yalnizca uzun bosluklarda kapatiyoruz; ilk hareket komutunda geri aciliyor.
//
// NVS'e ALINMADI bilerek: Ayarlar yapisi NVS'e ham bayt yaziliyor ve boyut
// kontrolu var; yapiya alan eklemek kayitli TUM ayarlari siler (ACS724
// kalibrasyonu, mikroadim, yasak sektorler). Reset sonrasi varsayilana doner.
// TILT HIZ ORANI. `hiz` ayari GLOBAL (adim/s) ama iki eksenin adim basina
// dusen acisi ayni degil: pan 0.01125 deg/adim, tilt 0.02250 deg/adim. Yani
// ayni adim hizinda tilt ACISAL olarak TAM IKI KAT hizli doner (135 vs 67.5
// deg/s) ve jog'da asiri hassas hissettirir. Bu oran yalniz tilt'e uygulanir;
// varsayilan 0.5 iki ekseni acisal olarak esitler.
//
// NVS'E ALINMADI bilerek: Ayarlar yapisina alan eklemek kayitli TUM ayarlari
// siler (bkz. panEnaBeklet'in yanindaki not). Reset sonrasi varsayilana doner.
float tiltHizOran = 0.5f;

// Tilt icin kruvaziyer hiz. Taban 1 adim/s: oran cok kucuk verilirse eksen
// bilmeden kilitlenmesin.
static inline uint32_t tiltKruvHiz() {
  uint32_t h = (uint32_t)((float)ay.hizKruvaziyer * tiltHizOran);
  return h < 1 ? 1 : h;
}

uint32_t panEnaBeklet = 60000;    // ms. 0 = hic kapatma.
bool     panEnaKapali = false;
uint32_t panBostaBasT = 0;        // pan'in bosa dustugu an (0 = henuz olculmedi)

void panSurucuAc() {
  // Sayaci HER cagrida yenile, sadece kapaliyken degil: aksi halde "panac" ile
  // elle acilan surucu, eski panBostaBasT yuzunden bir sonraki loop'ta aninda
  // geri kapaniyordu (2026-08-07'de olculdu - akim hic degismedi).
  panBostaBasT = millis();
  if (!panEnaKapali) return;
  digitalWrite(PAN_ENA, LOW);
  panEnaKapali = false;
  // Opto sonmesi + sargilarin yeniden enerjilenmesi zaman alir. Hemen adim
  // atilirsa ilk darbeler kaybolur (TB6600 ENA->DIR->PUL sirasi bekler).
  delay(5);
}

void panSurucuKapat() {
  if (panEnaKapali) return;
  digitalWrite(PAN_ENA, HIGH);
  panEnaKapali = true;
}

void panEnaIsle() {
  if (eksenMesgul(&pan)) { panBostaBasT = millis(); return; }   // hareket ederken sayac sifirlanir
  if (panEnaBeklet == 0 || panEnaKapali) return;
  if (panBostaBasT == 0) { panBostaBasT = millis(); return; }
  if (millis() - panBostaBasT >= panEnaBeklet) {
    panSurucuKapat();
    Serial.printf("pan surucusu bosta kapatildi (%lu sn hareketsiz) - konum sayaci artik korunmuyor\n",
                  panEnaBeklet / 1000UL);
  }
}

// TILT ENA. Pan'daki gibi bir "panEnaIsle" karsiligi YOK ve olmayacak -
// otomatik kapatma namlunun dusmesi demek. Kapatma yalnizca elle, tasima ve
// bakim icin; ilk hareket komutunda eksenHedefle() geri aciyor.
bool tiltEnaKapali = false;

void tiltSurucuAc() {
  if (!tiltEnaKapali) return;
  digitalWrite(TILT_ENA, LOW);
  tiltEnaKapali = false;
  delay(5);                    // opto sonmesi + sarginin enerjilenmesi (pan ile ayni)
}

void tiltSurucuKapat() {
  if (tiltEnaKapali) return;
  digitalWrite(TILT_ENA, HIGH);
  tiltEnaKapali = true;
}

// ---------------------- acil stop ----------------------
bool     estopHam      = false;   // pin HIGH mi (basili/kopuk)
bool     estopMandal   = true;    // aciliste kilitli baslar, "estop reset" acar
uint32_t estopDegisimT = 0;
bool     estopSonOkuma = false;

// TEZGAH BAYPASI: buton/role henuz takili degilken GPIO14 bosta HIGH okunur
// ve sistem kalicı kilitli kalir. "estop bypass on" bunu gecici olarak yok
// sayar. BILEREK NVS'E KAYDEDILMIYOR - her resette kendiliginden kapanir,
// yarismada yanlislikla acik kalmasi mumkun degil.
bool     estopBaypas   = false;

// ---------------------- ESP guc kesme yolu (GPIO18) ----------------------
// ARM rolesi LOW-trigger; NC kontagi acil stop butonuyla SERI baglidir, yani
// ikisinden herhangi biri tek basina halkayi acabilir. Role yalnizca KESMEK
// icin enerjilenir, normal calismada birakiktir. Bunun sonucu: ESP resetlenip
// GPIO18 yuksek empedansa dondugunde modulun dahili pull-up'i pini HIGH tutar,
// role birakik kalir ve sistem calismaya devam eder. Guvenlik yetkisi butonda
// kalir; bu pin yalnizca EK bir durdurma yoludur, izin veren taraf degildir.
bool gucKesik = false;

void gucAyarla(bool kes) {
  gucKesik = kes;
  digitalWrite(GUC_PIN, kes ? GUC_KESIK : GUC_ACIK);
}

void estopIsle() {
  bool okuma = estopBaypas ? false : (digitalRead(ESTOP_PIN) == HIGH);
  if (okuma != estopSonOkuma) { estopSonOkuma = okuma; estopDegisimT = millis(); }
  if (millis() - estopDegisimT >= ESTOP_DEBOUNCE_MS && okuma != estopHam) {
    estopHam = okuma;
    if (estopHam) {
      estopMandal = true;                  // mandalla
      Serial.println("!! ACIL STOP - butona basildi veya kablo kopuk");
      // ARM rolesini de KES. Adim darbelerini kesmek yetmiyordu: motorlar
      // beslemede kaliyor, ates yolu yalnizca mandala baglı oluyordu.
      // Geri vermek AYRI ve BILINCLI bir karar olsun - 'estop reset' yalnizca
      // mandali acar, gucu vermez. Ikisi ayri olmazsa 'estop reset' tek basina
      // sistemi yetkili hale getiriyor ve mandalin bir anlami kalmiyor.
      if (!gucKesik) {
        gucAyarla(true);
        Serial.println("   ARM rolesi de kesildi - geri vermek icin 'guc ver'");
      }
    } else {
      Serial.println("-- acil stop butonu birakildi ('estop reset' bekliyor)");
    }
  }
  bool kes = estopHam || estopMandal || gucKesik;
  if (kes != adimlarKesik) {
    adimlarKesik = kes;
    if (kes) { eksenDurdur(&pan); eksenDurdur(&tilt); eksenDurdur(&sarjor);
               digitalWrite(SARJOR_EN, HIGH); }   // A4988 disable
  }
}

bool sistemHazir() { return !estopHam && !estopMandal && !gucKesik; }

// ---------------------- yasak alan ----------------------
float aciNormal(float a) {                       // 0..360
  while (a < 0)    a += 360.0f;
  while (a >= 360) a -= 360.0f;
  return a;
}

bool sektorIcinde(float aci, float bas, float bit, float pay) {
  float a = aciNormal(aci);
  float b = aciNormal(bas - pay);
  float s = aciNormal(bit + pay);
  if (b <= s) return (a >= b && a <= s);
  return (a >= b || a <= s);                     // 0'i saran sektor
}

// Atis serbest mi? Sebebi 'neden' icine yazilir.
bool atisSerbest(char *neden, size_t n) {
  if (estopHam || estopMandal) { snprintf(neden, n, "acil stop aktif"); return false; }
  if (gucKesik) { snprintf(neden, n, "guc ESP tarafindan kesik ('guc ver')"); return false; }
  float p = panDerece(), t = tiltDerece();
  for (uint8_t i = 0; i < ay.yasakSayi; i++) {
    if (sektorIcinde(p, ay.yasakBas[i], ay.yasakBit[i], ay.yasakPay)) {
      // aciNormal(p): p birikimli acidir (tam turlarla 1626.49 gibi buyur),
      // sektor sinirlari ise 0..360. Ham degeri basmak mesaji okunmaz yapiyordu.
      snprintf(neden, n, "pan %.2f yasak sektor %u icinde (%.1f..%.1f +-%.1f)",
               aciNormal(p), i, ay.yasakBas[i], ay.yasakBit[i], ay.yasakPay);
      return false;
    }
  }
  if (ay.tiltMin != ay.tiltMax &&
      (t < ay.tiltMin + ay.yasakPay || t > ay.tiltMax - ay.yasakPay)) {
    snprintf(neden, n, "tilt %.2f limit disinda (%.1f..%.1f, pay %.1f)",
             t, ay.tiltMin, ay.tiltMax, ay.yasakPay);
    return false;
  }
  if (eksenMesgul(&pan) || eksenMesgul(&tilt)) {
    snprintf(neden, n, "taret hareket halinde"); return false;
  }
  snprintf(neden, n, "serbest");
  return true;
}

// ---------------------- I2C cihazlar ----------------------
bool encPanVar = false, encTiltVar = false, inaVar = false, imuVar = false;

bool i2cVarMi(TwoWire &w, uint8_t adres) {
  w.beginTransmission(adres);
  return (w.endTransmission() == 0);
}

// ---------------------- IMU (MPU6500) ----------------------
// Kartta "GY-9250" yazsa da cip MPU6500: WHO_AM_I 0x70 (9250 olsa 0x71 olurdu),
// MANYETOMETRE YOK -> 6 eksen, mutlak yon veremez. Taret acisi enkoderlerden
// geldigi icin sorun degil; buradaki veri egim/titresim izlemek icindir.
#define MPU_PWR_MGMT_1    0x6B
#define MPU_ACCEL_XOUT_H  0x3B
#define MPU_WHO_AM_I      0x75
// Varsayilan tam olcekler: ivme +-2g -> 16384 LSB/g, jiro +-250 dps -> 131 LSB/dps
#define MPU_IVME_LSB      16384.0f
#define MPU_JIRO_LSB      131.0f

bool imuUyandir(TwoWire &w) {
  w.beginTransmission(ADRES_IMU);
  w.write(MPU_PWR_MGMT_1);
  w.write(0x00);                      // uyku bitini temizle
  return (w.endTransmission() == 0);
}

// ivme g, jiro derece/s, sicaklik C. 14 bayt tek okumada gelir.
bool imuOku(TwoWire &w, float *ivme, float *jiro, float &sicaklik) {
  w.beginTransmission(ADRES_IMU);
  w.write(MPU_ACCEL_XOUT_H);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((uint8_t)ADRES_IMU, (uint8_t)14) != 14) return false;
  int16_t ham[7];
  for (uint8_t i = 0; i < 7; i++) ham[i] = ((int16_t)w.read() << 8) | w.read();
  for (uint8_t i = 0; i < 3; i++) ivme[i] = ham[i] / MPU_IVME_LSB;
  sicaklik = ham[3] / 333.87f + 21.0f;
  for (uint8_t i = 0; i < 3; i++) jiro[i] = ham[4 + i] / MPU_JIRO_LSB;
  return true;
}

bool as5600Oku(TwoWire &w, uint16_t &ham) {
  w.beginTransmission(ADRES_AS5600);
  w.write(AS5600_RAW_ANGLE);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((uint8_t)ADRES_AS5600, (uint8_t)2) != 2) return false;
  ham = ((uint16_t)w.read() << 8) | w.read();
  ham &= 0x0FFF;
  return true;
}

bool as5600Durum(TwoWire &w, uint8_t &durum, uint8_t &agc) {
  w.beginTransmission(ADRES_AS5600);
  w.write(AS5600_STATUS);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((uint8_t)ADRES_AS5600, (uint8_t)1) != 1) return false;
  durum = w.read();
  w.beginTransmission(ADRES_AS5600);
  w.write(AS5600_AGC);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((uint8_t)ADRES_AS5600, (uint8_t)1) != 1) return false;
  agc = w.read();
  return true;
}

float as5600Derece(uint16_t ham) { return ham * 360.0f / 4096.0f; }

bool ina226Oku16(uint8_t reg, uint16_t &deger) {
  Wire.beginTransmission(ADRES_INA226);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)ADRES_INA226, (uint8_t)2) != 2) return false;
  deger = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

// DIKKAT: sunt direnci modul uzerinde R100 (0.1 ohm) yaziyor. Uzerine
// paralel 0.01 ohm lehimlendiyse etkin deger 0.00909. Hangisi oldugu
// karttan gozle dogrulanmali - yanlissa akim 11 kat sapar.
float rSunt = 0.00909f;

bool ina226Olc(float &busV, float &suntMV, float &akimA, bool &doyum) {
  uint16_t hs, hb;
  if (!ina226Oku16(INA226_SHUNT, hs)) return false;
  if (!ina226Oku16(INA226_BUS, hb))   return false;
  suntMV = (int16_t)hs * 0.0025f;              // 2.5 uV/bit
  busV   = hb * 0.00125f;                      // 1.25 mV/bit
  akimA  = (suntMV / 1000.0f) / rSunt;
  doyum  = (fabsf(suntMV) > 81.0f);
  return true;
}

// ---------------------- ACS724 (analog akim) ----------------------
// ELDEKI PARCA: ACS724T LLC-50AB  -> +-50 A, 40 mV/A, Vcc=5 V, sifir=Vcc/2.
// 40 mV/A cok dusuk oldugu icin BOLUCU YOK - cikis 2.5 V etrafinda oynar ve
// +-20 A'e kadar 3.3 V'un altinda kalir. Araya 1k seri + 100nF (GPIO1-GND).
// UYARI: 40 mV/A demek, sifir noktasindaki 40 mV hata = 1 A hata demek.
// Motor beslemesi KAPALIYKEN "acs sifir" ile kalibre et.
// Diger varyantlar: 05AB=400 mV/A, 10AB=200, 20AB=100, 30AB=66.
// 2026-08-06: ESP32-S3 kartinin 5V pini olu cikti, modul 3.3 V'tan besleniyor.
// Hassasiyet ve sifir NVS'te (ay.acsMVperA / ay.acsSifirV).
float acsBolucu    = 1.0f;     // dogrudan baglanti (bolucu yok)
// ACS724 kalici olarak devrede oldugu icin ACIK basliyor. Onceden false idi ve
// her reset sonrasi "acs on" verilmesi gerekiyordu; verilmeyince telemetride
// akim=0.000 gorunup sensor bozuk saniliyordu. Bunu NVS'e ALMADIK bilerek:
// Ayarlar yapisi NVS'e ham bayt olarak yaziliyor ve boyut kontrolu var
// (getBytesLength != sizeof -> "NVS bos"), yani yapiya alan eklemek kayitli
// TUM ayarlari (mikroadim, ACS sifir kalibrasyonu, yasak sektorler) siler.
// Kapatmak icin "acs off" duruyor.
bool  acsVar       = true;

// Ortalama alinmis ham gerilim (mV). 64 ornek ~4 ms surer; 256 kullanilinca
// 5 Hz'lik telemetri bile adim ISR'sinden gozle gorulur zaman caliyordu.
// "acs sifir" bunu 4 kez cagirdigi icin kalibrasyonda yine 256 ornek oluyor.
float acsHamMV() {
  uint32_t toplam = 0;
  for (int i = 0; i < 64; i++) toplam += analogReadMilliVolts(AKIM_ADC);
  return (toplam / 64.0f) * acsBolucu;
}

float acsOlc() {
  return (acsHamMV() - ay.acsSifirV * 1000.0f) / ay.acsMVperA;
}

// Sifir noktasini olcer. Motor beslemesi kapali / akim sifir olmali.
void acsSifirla() {
  Serial.println("ACS724 sifir kalibrasyonu - MOTOR BESLEMESI KAPALI OLMALI!");
  Serial.println("(besleme acikken yapilirsa surucülerin bosta akimi sifira gomulur)");
  float eskiSifir = ay.acsSifirV;
  float mv = acsHamMV();
  // tekrarlanabilirlik: 4 olcum alip yayilimi da bildir
  float en_az = mv, en_cok = mv;
  for (int i = 0; i < 3; i++) {
    float m = acsHamMV();
    mv = (mv * (i + 1) + m) / (i + 2);
    if (m < en_az) en_az = m;
    if (m > en_cok) en_cok = m;
  }
  ay.acsSifirV = mv / 1000.0f;
  Serial.printf("sifir = %.1f mV (%.4f V), yayilim %.1f mV -> +-%.3f A gurultu\n",
                mv, ay.acsSifirV, en_cok - en_az, (en_cok - en_az) / ay.acsMVperA);

  float kayma = (ay.acsSifirV - eskiSifir) * 1000.0f;
  if (fabsf(kayma) > 2.0f) {
    Serial.printf("!! UYARI: sifir eskisinden %+.1f mV kaydi (= %+.2f A)\n",
                  kayma, kayma / ay.acsMVperA);
    Serial.println("!! Bu kadar kayma genelde MOTOR BESLEMESI ACIK unutuldugunda olur.");
    Serial.println("!! Emin degilsen beslemeyi kapat ve 'acs sifir' tekrar ver.");
  }
  if (en_cok - en_az > 2.0f) {
    Serial.println("!! Yayilim yuksek - surucü gurultusu var, besleme acik olabilir.");
  }
  Serial.println("kalici olmasi icin 'kaydet'");
}

// Bilinen bir akima gore hassasiyeti duzeltir (multimetre seri baglanip
// gercek akim okunur, sonra "acs kal <amper>" verilir).
void acsKalibre(float gercekAkim) {
  if (fabsf(gercekAkim) < 0.2f) {
    Serial.println("RED: kalibrasyon icin en az 0.2 A gerekli");
    return;
  }
  float mv = acsHamMV();
  float fark = mv - ay.acsSifirV * 1000.0f;
  float yeni = fark / gercekAkim;
  if (yeni <= 0.0f) {
    Serial.printf("RED: isaret ters (fark %.1f mV). IP+/IP- yonunu ya da\n", fark);
    Serial.println("     verdigin akimin isaretini kontrol et.");
    return;
  }
  Serial.printf("ham %.1f mV, sifirdan fark %.1f mV, gercek akim %.3f A\n",
                mv, fark, gercekAkim);
  Serial.printf("hassasiyet %.2f -> %.2f mV/A olarak guncellendi\n", ay.acsMVperA, yeni);
  ay.acsMVperA = yeni;
  Serial.println("kalici olmasi icin 'kaydet'");
}

// ---------------------- akim profili ----------------------
// Telemetri 5 Hz'de ve 256 ornek ortalamasiyla oktugu icin kisa hareketlerde
// tepe akimi kacirir. Bu test hareket boyunca siki dongude ornekler.
// DIKKAT: analogReadMilliVolts ~60 us suruyor ve siki dongude cagrilirsa
// 50 kHz'lik adim ISR'sini geciktirip motoru yavaslatiyor (ilk surumde
// 6000 adim/s hedefi 3800'e dusmustu). Bu yuzden ornek sayisi 4'e indirildi
// ve aralara bosluk kondu -> ~%20 CPU, saniyede ~800 ornek.
float acsHizliOlc() {
  uint32_t toplam = 0;
  for (int i = 0; i < 4; i++) toplam += analogReadMilliVolts(AKIM_ADC);
  float mv = (toplam / 4.0f) * acsBolucu;
  return (mv - ay.acsSifirV * 1000.0f) / ay.acsMVperA;
}

void akimTesti(int32_t adim) {
  if (!acsVar)        { Serial.println("RED: once 'acs on' ver"); return; }
  if (!sistemHazir()) { Serial.println("RED: acil stop aktif");   return; }

  float durusOnce = acsOlc();
  Serial.printf("durus akimi (once) : %.3f A\n", durusOnce);

  int32_t bas = pan.konum;
  uint32_t t0 = millis();
  eksenHedefle(&pan, bas + adim, ay.hizKruvaziyer);

  // ADC okumasi adim ISR'sini blokladigi icin seyrek ornekle (~%1.5 CPU),
  // gurultuyu kaybetmemek icin 20 ms'lik kovalarda ortala. Tepe degeri
  // kova ortalamasindan alinir -> tek bir gurultu sicramasi tepe olmaz.
  const uint32_t KOVA_MS = 20;
  float enAz = 1e9f, enCok = -1e9f, toplam = 0.0f;
  float kovaToplam = 0.0f;
  uint32_t n = 0, kovaN = 0, kovaSayi = 0, kovaT = millis();

  while (eksenMesgul(&pan)) {
    kovaToplam += acsHizliOlc(); kovaN++; n++;
    if (millis() - kovaT >= KOVA_MS && kovaN > 0) {
      float ort = kovaToplam / kovaN;
      if (ort < enAz)  enAz  = ort;
      if (ort > enCok) enCok = ort;
      toplam += ort; kovaSayi++;
      kovaToplam = 0.0f; kovaN = 0; kovaT = millis();
    }
    if (!sistemHazir()) { Serial.println("KESILDI: acil stop"); break; }
    delayMicroseconds(4000);          // ISR'ye nefes aldir
  }
  uint32_t sure = millis() - t0;
  if (kovaSayi == 0) { Serial.println("hareket cok kisa, olcum yok"); return; }
  toplam /= kovaSayi;                 // artik ortalama akim
  delay(1200);                       // TB6600 yarim akima insin
  float durusSonra = acsOlc();

  // Beklenen sure RAMPA DAHIL hesaplanir. Hiz adim sayisiyla dogrusal
  // arttigi icin rampa suresi t = (r/(v1-v0)) * ln(v1/v0).
  float v0 = (float)ay.hizBaslangic, v1 = (float)ay.hizKruvaziyer;
  float mesafe = fabsf((float)adim);
  float r = fminf((float)ay.rampaAdim, mesafe / 2.0f);
  float beklenen = (mesafe - 2.0f * r) / v1;
  if (v1 > v0 && r > 0) beklenen += 2.0f * (r / (v1 - v0)) * logf(v1 / v0);
  float oran = (beklenen > 0) ? (beklenen / (sure / 1000.0f)) : 0.0f;

  Serial.printf("hareket            : %ld adim, %.2f s, %lu ornek / %lu kova\n",
                (long)adim, sure / 1000.0f, (unsigned long)n, (unsigned long)kovaSayi);
  Serial.printf("sure               : beklenen %.2f s (rampa dahil) -> %%%.0f  %s\n",
                beklenen, oran * 100.0f,
                (oran > 0.92f) ? "TAMAM" : "<<< ADIM URETECI YAVASLADI");
  Serial.printf("akim  en az %.3f A | ortalama %.3f A | TEPE %.3f A\n",
                enAz, toplam, enCok);
  Serial.printf("durus akimi (sonra): %.3f A  (yarim akim)\n", durusSonra);
  Serial.printf("guc (tepe, 22.4 V varsayimiyla): %.1f W\n", enCok * 22.4f);
}

// ---------------------- tilt dayama referansi ----------------------
// Limit switch yok. Tilt'i yavasca asagi surer, AS5600'u izler; adimlar
// gidiyor ama enkoder acisi degismiyorsa dayamaya oturulmustur.
bool tiltReferansAra(float &bulunanHamDerece) {
  if (!encTiltVar) {
    Serial.println("HATA: tilt AS5600 yok, dayama aramasi yapilamaz");
    return false;
  }
  Serial.println("tilt dayama araniyor (asagi, yavas)...");
  uint16_t ham;
  if (!as5600Oku(Wire1, ham)) { Serial.println("HATA: enkoder okunamadi"); return false; }
  float oncekiAci = as5600Derece(ham);
  int   takiliSayac = 0;
  const int  BLOK_ADIM   = 40;      // her kontrolde atilacak adim
  const float ESIK_DER   = 0.6f;    // bu kadar donmediyse "takildi"
  const int  TAKILI_YETER= 3;       // ust uste kac blok
  const int  AZAMI_BLOK  = 400;     // guvenlik: en fazla bu kadar dene

  for (int blok = 0; blok < AZAMI_BLOK; blok++) {
    if (!sistemHazir()) { Serial.println("iptal: acil stop"); return false; }
    eksenHedefle(&tilt, tilt.konum - BLOK_ADIM, ay.hizBaslangic);
    while (eksenMesgul(&tilt)) delay(2);
    if (!as5600Oku(Wire1, ham)) { Serial.println("HATA: enkoder okunamadi"); return false; }
    float aci  = as5600Derece(ham);
    float fark = fabsf(aci - oncekiAci);
    if (fark > 180.0f) fark = 360.0f - fark;        // sarma
    oncekiAci = aci;
    if (fark < ESIK_DER) {
      if (++takiliSayac >= TAKILI_YETER) {
        bulunanHamDerece = aci;
        Serial.printf("DAYAMA bulundu: enkoder ham %.2f deg, %d blok sonra\n", aci, blok + 1);
        return true;
      }
    } else {
      takiliSayac = 0;
    }
  }
  Serial.println("HATA: dayama bulunamadi (azami blok asildi)");
  return false;
}

// ---------------------- hiz tavani testi ----------------------
// Kademe kademe hizlanarak pan'i tam tur ileri + tam tur geri dondurur.
// Her kademede basa donuldugu icin, AS5600 ayni ham acisi okumali;
// okumuyorsa o hizda ADIM KAYBI var demektir (miknatis takili olmali).
// Test sirasinda seri porta bir sey yazmak testi iptal eder.
void hizTesti(uint32_t bas, uint32_t artis, uint32_t son) {
  const int32_t tamTur = (int32_t)MOTOR_TAM_ADIM * ay.mikroadim * (int32_t)PAN_ORAN;
  uint32_t kayitli = ay.hizKruvaziyer;
  bool encKullan = encPanVar;
  uint8_t durum, agc;
  if (encKullan && as5600Durum(Wire, durum, agc) && !((durum >> 5) & 1)) {
    Serial.println("UYARI: enkoder miknatisi yok (MD=0) - adim kaybi OLCULEMEZ,");
    Serial.println("       sadece gozle takip et.");
    encKullan = false;
  }
  Serial.printf("\n=== HIZ TAVANI TESTI: %lu -> %lu, adim %lu (tam tur = %ld adim) ===\n",
                (unsigned long)bas, (unsigned long)son, (unsigned long)artis, (long)tamTur);

  for (uint32_t h = bas; h <= son; h += artis) {
    if (!sistemHazir()) { Serial.println("IPTAL: acil stop"); break; }
    if (Serial.available()) { Serial.println("IPTAL: kullanici"); while (Serial.available()) Serial.read(); break; }

    float rpm = (float)h * 60.0f / ((float)MOTOR_TAM_ADIM * ay.mikroadim);
    Serial.printf("\n>>> %lu adim/s  (motor %.0f rpm, taret %.0f deg/s)\n",
                  (unsigned long)h, rpm, h * derecePerAdim(PAN_ORAN));

    uint16_t hamOnce = 0, hamSonra = 0;
    if (encKullan) as5600Oku(Wire, hamOnce);
    int32_t basKonum = pan.konum;

    eksenHedefle(&pan, basKonum + tamTur, h);
    while (eksenMesgul(&pan)) { delay(5); if (!sistemHazir()) break; }
    delay(500);
    eksenHedefle(&pan, basKonum, h);
    while (eksenMesgul(&pan)) { delay(5); if (!sistemHazir()) break; }
    delay(500);

    if (encKullan && as5600Oku(Wire, hamSonra)) {
      int32_t fark = (int32_t)hamSonra - (int32_t)hamOnce;
      if (fark >  2048) fark -= 4096;
      if (fark < -2048) fark += 4096;
      float farkDer = fark * 360.0f / 4096.0f;
      Serial.printf("    enkoder farki: %+ld sayim (%+.2f deg motor mili)  %s\n",
                    (long)fark, farkDer,
                    (abs(fark) <= 12) ? "TAMAM" : "<<< ADIM KAYBI");
    } else {
      Serial.println("    (enkoder yok - gozle degerlendir)");
    }
  }
  ay.hizKruvaziyer = kayitli;
  Serial.printf("\n=== test bitti, kruvaziyer hiz %lu adim/s'e geri alindi ===\n",
                (unsigned long)kayitli);
}

// ---------------------- NVS ----------------------
void ayarKaydet() {
  nvs.begin("hss", false);
  nvs.putBytes("ayar", &ay, sizeof(ay));
  nvs.end();
  Serial.println("ayarlar kaydedildi");
}

void ayarYukle() {
  nvs.begin("hss", true);
  size_t n = nvs.getBytesLength("ayar");
  if (n == sizeof(ay)) { nvs.getBytes("ayar", &ay, sizeof(ay)); Serial.println("ayarlar NVS'ten yuklendi"); }
  else                 { Serial.println("NVS bos - varsayilan ayarlar"); }
  nvs.end();
}

// ---------------------- durum / telemetri ----------------------
void durumYaz() {
  char neden[96];
  bool serbest = atisSerbest(neden, sizeof(neden));
  Serial.println("---------------- HSS DURUM ----------------");
  Serial.printf("acil stop : pin=%s mandal=%s -> %s\n",
                estopHam ? "HIGH(basili/kopuk)" : "LOW(normal)",
                estopMandal ? "KILITLI" : "acik",
                sistemHazir() ? "HAZIR" : "DURDURULDU");
  Serial.printf("guc       : %s\n",
                gucKesik ? "KESIK - ARM rolesi cekik ('guc ver')" : "VAR");
  Serial.printf("mikroadim : 1/%u  -> pan %.5f deg/adim (%ld adim/tur)\n",
                ay.mikroadim, derecePerAdim(PAN_ORAN),
                (long)(MOTOR_TAM_ADIM * ay.mikroadim * (int)PAN_ORAN));
  Serial.printf("            tilt %.5f deg/adim (%ld adim/tur)\n",
                derecePerAdim(TILT_ORAN),
                (long)(MOTOR_TAM_ADIM * ay.mikroadim * (int)TILT_ORAN));
  Serial.printf("pan       : %.3f deg  (adim %ld -> %ld) %s\n",
                panDerece(), (long)pan.konum, (long)pan.hedef,
                eksenMesgul(&pan) ? "HAREKET" : "durgun");
  Serial.printf("tilt      : %.3f deg  (adim %ld -> %ld) %s  limit %.1f..%.1f\n",
                tiltDerece(), (long)tilt.konum, (long)tilt.hedef,
                eksenMesgul(&tilt) ? "HAREKET" : "durgun", ay.tiltMin, ay.tiltMax);
  Serial.printf("hiz       : kruv %lu adim/s, baslangic %lu, rampa %lu adim\n",
                (unsigned long)ay.hizKruvaziyer, (unsigned long)ay.hizBaslangic,
                (unsigned long)ay.rampaAdim);
  Serial.printf("            pan %.1f deg/s | tilt %.1f deg/s (oran %.2f)\n",
                ay.hizKruvaziyer * derecePerAdim(PAN_ORAN),
                tiltKruvHiz() * derecePerAdim(TILT_ORAN), tiltHizOran);
  if (panEnaBeklet == 0)
    Serial.printf("pan ENA   : %s, bosta kapatma KAPALI ('panena <sn>')\n",
                  panEnaKapali ? "surucu KAPALI" : "surucu acik");
  else
    Serial.printf("pan ENA   : %s, %lu sn hareketsizlikte kapanir\n",
                  panEnaKapali ? "surucu KAPALI (motor serbest, konum sayaci korunmuyor)"
                               : "surucu acik (tutuyor)",
                  panEnaBeklet / 1000UL);
  Serial.printf("tilt ENA  : %s (otomatik kapatma yok)\n",
                tiltEnaKapali ? "surucu KAPALI - EKSEN SERBEST, NAMLU DUSEBILIR"
                              : "surucu acik (tutuyor)");
  Serial.printf("yasak alan: %u sektor, pay %.1f deg\n", ay.yasakSayi, ay.yasakPay);
  for (uint8_t i = 0; i < ay.yasakSayi; i++)
    Serial.printf("   [%u] %.1f .. %.1f deg\n", i, ay.yasakBas[i], ay.yasakBit[i]);
  Serial.printf("ATIS      : %s (%s)\n", serbest ? "SERBEST" : "KILITLI", neden);

  uint16_t ham; uint8_t d, agc;
  if (encPanVar && as5600Oku(Wire, ham))
    Serial.printf("enc pan   : ham %u -> %.2f deg (motor mili; 1 tur = %.0f deg taret)\n",
                  ham, as5600Derece(ham), 360.0f / PAN_ORAN);
  else Serial.println("enc pan   : YOK");
  if (encTiltVar && as5600Oku(Wire1, ham))
    Serial.printf("enc tilt  : ham %u -> %.2f deg (1 tur = %.0f deg taret)\n",
                  ham, as5600Derece(ham), 360.0f / TILT_ORAN);
  else Serial.println("enc tilt  : YOK");
  if (encPanVar && as5600Durum(Wire, d, agc))
    Serial.printf("enc pan hd: STATUS 0x%02X (MD=%d ML=%d MH=%d) AGC %u\n",
                  d, (d>>5)&1, (d>>4)&1, (d>>3)&1, agc);

  float bv, sm, ia; bool doy;
  if (inaVar && ina226Olc(bv, sm, ia, doy))
    Serial.printf("INA226    : bus %.3f V, sunt %.2f mV, akim %.3f A (R=%.5f) %s\n",
                  bv, sm, ia, rSunt, doy ? "<<< DOYUM!" : "");
  else Serial.println("INA226    : YOK");
  if (acsVar) Serial.printf("ACS724    : %.3f A  (ham %.1f mV, sifir %.4f V, %.2f mV/A)\n",
                            acsOlc(), acsHamMV(), ay.acsSifirV, ay.acsMVperA);
  Serial.println("------------------------------------------");
}

void telemetriYaz() {
  char neden[96];
  bool serbest = atisSerbest(neden, sizeof(neden));
  float bv = 0, sm = 0, ia = 0; bool doy = false;
  if (inaVar) ina226Olc(bv, sm, ia, doy);
  if (acsVar) ia = acsOlc();          // ACS724 varsa akim ondan gelsin
  uint16_t hamP = 0, hamT = 0;
  if (encPanVar)  as5600Oku(Wire,  hamP);
  if (encTiltVar) as5600Oku(Wire1, hamT);
  float ivme[3] = {0, 0, 0}, jiro[3] = {0, 0, 0}, imuSic = 0;
  bool imuOk = imuVar && imuOku(Wire, ivme, jiro, imuSic);
  // guc=1 -> besleme var. Yeni alanlar hep SONA eklenir: mevcut ayristiricilar
  // konum degil ad-deger okudugu icin bozulmaz.
  Serial.printf("T pan=%.3f tilt=%.3f encp=%u enct=%u estop=%d hazir=%d atis=%d bus=%.2f akim=%.3f mesgul=%d guc=%d",
                panDerece(), tiltDerece(), hamP, hamT,
                (estopHam || estopMandal) ? 1 : 0, sistemHazir() ? 1 : 0,
                serbest ? 1 : 0, bv, ia,
                (eksenMesgul(&pan) || eksenMesgul(&tilt)) ? 1 : 0,
                gucKesik ? 0 : 1);
  // panena: 1 = surucu bosta kapatilmis (motor serbest, konum sayaci korunmuyor)
  Serial.printf(" panena=%d tiltena=%d", panEnaKapali ? 1 : 0, tiltEnaKapali ? 1 : 0);
  if (imuOk) {
    Serial.printf(" ax=%.3f ay=%.3f az=%.3f gx=%.2f gy=%.2f gz=%.2f isi=%.1f",
                  ivme[0], ivme[1], ivme[2], jiro[0], jiro[1], jiro[2], imuSic);
  }
  Serial.println();
}

void yardim() {
  Serial.println(
    "KOMUTLAR\n"
    "  d                 durum\n"
    "  t [0|1]           telemetri akisini kapat/ac\n"
    "  pan <derece>      mutlak pan acisi\n"
    "  tilt <derece>     mutlak tilt acisi\n"
    "  padim <adim>      pan'i adim cinsinden bagil surme (test)\n"
    "  tadim <adim>      tilt'i adim cinsinden bagil surme (test)\n"
    "  dur               yumusak durdur (acil stop degil)\n"
    "  sifirla [pan|tilt] mevcut konumu 0 kabul et\n"
    "  ref tilt          dayama arayarak tilt sifirini bul (enkoder ile)\n"
    "  mikroadim <n>     TB6600 DIP degeri (1/2/4/8/16/32)\n"
    "  hiz <adim/s>      kruvaziyer hiz (GLOBAL)\n"
    "  tilthiz <oran>    tilt'e uygulanan hiz orani 0.05..1.00 (varsayilan 0.50;\n"
    "                    tilt adim basina 2 kat aci doner, bu onu esitler)\n"
    "  rampa <adim>      hizlanma mesafesi\n"
    "  tiltlim <min> <max>\n"
    "  yasak <bas> <bit> pan yasak sektoru ekle (derece)\n"
    "  yasak temizle     tum sektorleri sil\n"
    "  pay <derece>      yasak alan emniyet payi\n"
    "  akimtest [adim]   hareket boyunca akim profili (min/ort/tepe)\n"
    "                    adim verilmezse tam tur\n"
    "  hiztest [bas] [artis] [son]   hiz tavanini otomatik ara\n"
    "                    (varsayilan 3000 3000 24000; her kademede tam tur\n"
    "                     gidip geri gelir, enkoderle adim kaybi olcer)\n"
    "  ates              bir mermi sur (yasak alan + acil stop kontrollu)\n"
    "  estop             acil stop durumu\n"
    "  estop reset       mandali ac (buton birakilmis olmali)\n"
    "  estop bypass on   TEZGAH: buton yokken kilidi yok say (kaydedilmez)\n"
    "  estop bypass off  baypasi kapat\n"
    "  guc               ESP guc kesme yolunun durumu (GPIO18 / ARM rolesi)\n"
    "  guc kes           ARM rolesini cektir -> motor beslemesini kes\n"
    "  guc ver           ARM rolesini birak  -> beslemeyi geri ver\n"
    "                    (acil stop mandali AYRI; buton basildiysa 'estop reset' de gerekir)\n"
    "  sunt <ohm>        INA226 sunt direnci\n"
    "  acs on|off        ACS724 analog akim okumasi\n"
    "  acs sifir         sifir noktasini kalibre et (akim GECMEZKEN!)\n"
    "  acs ham           ham mV + hesaplanan akim\n"
    "  acs kal <amper>   bilinen akima gore hassasiyeti duzelt\n"
    "  acs <mVperA> [sifirV]   hassasiyeti elle gir\n"
    "  panena <sn>       pan surucusunu <sn> hareketsizlikten sonra kapat (0=kapatma)\n"
    "  panac | pankap    pan surucusunu elle ac / kapat\n"
    "  tiltac | tiltkap  tilt surucusunu elle ac / kapat (otomatik kapanma YOK)\n"
    "  tara              I2C taramasi\n"
    "  kaydet            ayarlari NVS'e yaz\n");
}

// ---------------------- atis ----------------------
void atesEt() {
  char neden[96];
  if (!atisSerbest(neden, sizeof(neden))) {
    Serial.printf("ATIS REDDEDILDI: %s\n", neden);
    return;
  }
  Serial.println("ATIS: sarjor suruluyor...");
  digitalWrite(SARJOR_EN, LOW);              // A4988 enable
  delayMicroseconds(50);
  eksenHedefle(&sarjor, sarjor.konum + (int32_t)ay.sarjorAdimAtis, ay.sarjorHiz);
  while (eksenMesgul(&sarjor)) {
    delay(2);
    if (!sistemHazir()) { Serial.println("ATIS KESILDI: acil stop"); break; }
  }
  digitalWrite(SARJOR_EN, HIGH);             // bosta tut, isinmasin
  Serial.println("ATIS: sarjor cevrimi bitti");
}

// ---------------------- seri komutlar ----------------------
bool telemetriAcik = false;

void komutIsle(String k) {
  k.trim();
  if (k.length() == 0) return;

  if (k == "d" || k == "durum") { durumYaz(); return; }
  if (k == "?" || k == "yardim") { yardim(); return; }

  if (k.startsWith("t")) {
    if (k == "t" || k == "t 1") { telemetriAcik = true;  Serial.println("telemetri ACIK"); return; }
    if (k == "t 0")             { telemetriAcik = false; Serial.println("telemetri KAPALI"); return; }
  }

  if (k.startsWith("pan ")) {
    float d = k.substring(4).toFloat();
    if (ay.panMin != ay.panMax && (d < ay.panMin || d > ay.panMax)) {
      Serial.printf("RED: pan limiti %.1f..%.1f\n", ay.panMin, ay.panMax); return;
    }
    if (!sistemHazir()) { Serial.println("RED: acil stop aktif"); return; }
    eksenHedefle(&pan, dereceToAdim(d, PAN_ORAN), ay.hizKruvaziyer);
    Serial.printf("pan hedef %.3f deg (%ld adim)\n", d, (long)pan.hedef);
    return;
  }
  if (k.startsWith("tilt ") && !k.startsWith("tiltlim")) {
    float d = k.substring(5).toFloat();
    // pan'daki kural tilt'e de getirildi: min ile max ESITSE limit yok.
    // `tiltlim 0 0` yumusak limiti tamamen kaldirir.
    if (ay.tiltMin != ay.tiltMax && (d < ay.tiltMin || d > ay.tiltMax)) {
      Serial.printf("RED: tilt limiti %.1f..%.1f\n", ay.tiltMin, ay.tiltMax); return;
    }
    if (!sistemHazir()) { Serial.println("RED: acil stop aktif"); return; }
    eksenHedefle(&tilt, dereceToAdim(d, TILT_ORAN), tiltKruvHiz());
    Serial.printf("tilt hedef %.3f deg (%ld adim)\n", d, (long)tilt.hedef);
    return;
  }
  if (k.startsWith("padim ")) {
    if (!sistemHazir()) { Serial.println("RED: acil stop aktif"); return; }
    long a = k.substring(6).toInt();
    eksenHedefle(&pan, pan.konum + a, ay.hizKruvaziyer);
    Serial.printf("pan %+ld adim\n", a); return;
  }
  if (k.startsWith("tadim ")) {
    if (!sistemHazir()) { Serial.println("RED: acil stop aktif"); return; }
    long a = k.substring(6).toInt();
    eksenHedefle(&tilt, tilt.konum + a, tiltKruvHiz());
    Serial.printf("tilt %+ld adim\n", a); return;
  }

  if (k == "dur") { eksenYumusakDurdur(&pan); eksenYumusakDurdur(&tilt);
                    eksenYumusakDurdur(&sarjor);
                    Serial.println("durduruldu (yumusak fren)"); return; }

  if (k == "sifirla")      { pan.konum = pan.hedef = 0; tilt.konum = tilt.hedef = 0;
                             Serial.println("pan ve tilt sifirlandi"); return; }
  if (k == "sifirla pan")  { pan.konum = pan.hedef = 0;  Serial.println("pan sifirlandi");  return; }
  if (k == "sifirla tilt") { tilt.konum = tilt.hedef = 0; Serial.println("tilt sifirlandi"); return; }

  if (k == "ref tilt") {
    float ham;
    if (tiltReferansAra(ham)) {
      tilt.konum = tilt.hedef = dereceToAdim(ay.tiltMin, TILT_ORAN);
      ay.tiltRefOfset = ham;
      Serial.printf("tilt referansi kuruldu: dayama = %.1f deg kabul edildi, enkoder ham %.2f\n",
                    ay.tiltMin, ham);
      Serial.println("(kalici olmasi icin 'kaydet')");
    }
    return;
  }

  if (k.startsWith("mikroadim ")) {
    int m = k.substring(10).toInt();
    if (m==1||m==2||m==4||m==8||m==16||m==32) {
      float eskiPan = panDerece(), eskiTilt = tiltDerece();
      ay.mikroadim = m;
      pan.konum  = pan.hedef  = dereceToAdim(eskiPan,  PAN_ORAN);   // aciyi koru
      tilt.konum = tilt.hedef = dereceToAdim(eskiTilt, TILT_ORAN);
      Serial.printf("mikroadim 1/%d -> pan %.5f deg/adim, tilt %.5f deg/adim\n",
                    m, derecePerAdim(PAN_ORAN), derecePerAdim(TILT_ORAN));
      Serial.println("UYARI: TB6600 DIP anahtarlari da ayni degerde olmali!");
    } else Serial.println("RED: 1/2/4/8/16/32 olmali");
    return;
  }

  if (k.startsWith("tilthiz ")) {
    float o = k.substring(8).toFloat();
    if (o < 0.05f || o > 1.0f) { Serial.println("RED: 0.05..1.00"); return; }
    tiltHizOran = o;
    Serial.printf("tilt hiz orani %.2f -> tilt %.1f deg/s (pan %.1f deg/s)\n",
                  tiltHizOran, tiltKruvHiz() * derecePerAdim(TILT_ORAN),
                  ay.hizKruvaziyer * derecePerAdim(PAN_ORAN));
    return;
  }

  if (k.startsWith("hiz ")) {
    uint32_t h = k.substring(4).toInt();
    if (h < 100 || h > 25000) { Serial.println("RED: 100..25000 adim/s"); return; }
    ay.hizKruvaziyer = h;
    Serial.printf("kruvaziyer hiz %lu adim/s = pan %.1f deg/s, tilt %.1f deg/s\n",
                  (unsigned long)h, h * derecePerAdim(PAN_ORAN),
                  tiltKruvHiz() * derecePerAdim(TILT_ORAN));
    return;
  }
  if (k.startsWith("rampa ")) { ay.rampaAdim = k.substring(6).toInt();
                                Serial.printf("rampa %lu adim\n", (unsigned long)ay.rampaAdim); return; }

  if (k.startsWith("tiltlim ")) {
    int bosluk = k.indexOf(' ', 8);
    if (bosluk < 0) { Serial.println("kullanim: tiltlim <min> <max>"); return; }
    ay.tiltMin = k.substring(8, bosluk).toFloat();
    ay.tiltMax = k.substring(bosluk + 1).toFloat();
    Serial.printf("tilt limiti %.1f .. %.1f\n", ay.tiltMin, ay.tiltMax);
    return;
  }

  if (k == "yasak temizle") { ay.yasakSayi = 0; Serial.println("yasak sektorler silindi"); return; }
  if (k.startsWith("yasak ")) {
    int bosluk = k.indexOf(' ', 6);
    if (bosluk < 0) { Serial.println("kullanim: yasak <bas> <bit>"); return; }
    if (ay.yasakSayi >= 4) { Serial.println("RED: en fazla 4 sektor"); return; }
    ay.yasakBas[ay.yasakSayi] = k.substring(6, bosluk).toFloat();
    ay.yasakBit[ay.yasakSayi] = k.substring(bosluk + 1).toFloat();
    ay.yasakSayi++;
    Serial.printf("yasak sektor eklendi: %.1f .. %.1f (toplam %u)\n",
                  ay.yasakBas[ay.yasakSayi-1], ay.yasakBit[ay.yasakSayi-1], ay.yasakSayi);
    return;
  }
  if (k.startsWith("pay ")) { ay.yasakPay = k.substring(4).toFloat();
                              Serial.printf("emniyet payi %.1f deg\n", ay.yasakPay); return; }

  if (k == "hiztest" || k.startsWith("hiztest ")) {
    if (!sistemHazir()) { Serial.println("RED: acil stop aktif"); return; }
    uint32_t bas = 3000, art = 3000, son = 24000;
    if (k.length() > 8) {
      String p = k.substring(8); p.trim();
      int b1 = p.indexOf(' ');
      if (b1 > 0) {
        int b2 = p.indexOf(' ', b1 + 1);
        bas = p.substring(0, b1).toInt();
        art = p.substring(b1 + 1, b2 > 0 ? b2 : p.length()).toInt();
        if (b2 > 0) son = p.substring(b2 + 1).toInt();
      }
    }
    if (bas == 0 || art == 0 || son < bas) { Serial.println("kullanim: hiztest [bas] [artis] [son]"); return; }
    hizTesti(bas, art, son);
    return;
  }

  if (k == "akimtest" || k.startsWith("akimtest ")) {
    int32_t a = (k.length() > 9) ? k.substring(9).toInt() : 0;
    if (a == 0) a = (int32_t)MOTOR_TAM_ADIM * ay.mikroadim * (int32_t)PAN_ORAN;  // tam tur
    akimTesti(a);
    return;
  }

  if (k == "ates") { atesEt(); return; }

  if (k == "estop") {
    Serial.printf("pin=%s mandal=%s adimlar=%s\n",
                  digitalRead(ESTOP_PIN) == HIGH ? "HIGH(basili/kopuk)" : "LOW(normal)",
                  estopMandal ? "KILITLI" : "acik",
                  adimlarKesik ? "KESIK" : "serbest");
    return;
  }
  if (k == "estop reset") {
    if (!estopBaypas && digitalRead(ESTOP_PIN) == HIGH) {
      Serial.println("RED: buton hala basili (veya kablo kopuk / hic bagli degil)");
      Serial.println("     tezgahta test icin: 'estop bypass on'");
      return;
    }
    estopMandal = false;
    // Mandali acmak GUCU VERMEZ. Acil stop sirasinda ARM kesildiyse operator
    // ayrica 'guc ver' demeli - iki ayri onay.
    if (gucKesik)
      Serial.println("acil stop mandali acildi - ARM HALA KESIK, 'guc ver' bekliyor");
    else
      Serial.println("acil stop mandali acildi - sistem HAZIR");
    return;
  }
  if (k == "estop bypass on") {
    estopBaypas = true;
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!! ACIL STOP BAYPAS EDILDI - SADECE TEZGAH TESTI ICIN !!");
    Serial.println("!! Buton takilinca 'estop bypass off' ver.            !!");
    Serial.println("!! Reset sonrasi kendiliginden kapanir.               !!");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    return;
  }
  if (k == "estop bypass off") {
    estopBaypas = false; estopMandal = true;
    Serial.println("baypas kapatildi, mandal yeniden kilitlendi");
    return;
  }

  if (k == "guc") {
    Serial.printf("guc=%s  GPIO%d=%s (ARM rolesi %s, NC %s)\n",
                  gucKesik ? "KESIK" : "VAR", GUC_PIN,
                  gucKesik ? "LOW" : "HIGH",
                  gucKesik ? "cekik" : "birakik",
                  gucKesik ? "acik" : "kapali");
    return;
  }
  if (k == "guc kes") {
    gucAyarla(true);
    Serial.println("GUC KESILDI - ARM rolesi cekti, NC acildi, motor beslemesi yok");
    Serial.println("   adim darbeleri de kesildi. DIKKAT: tilt serbest, namlu duser.");
    return;
  }
  if (k == "guc ver") {
    gucAyarla(false);
    Serial.println("guc verildi - ARM rolesi birakildi, NC kapandi");
    if (estopHam || estopMandal)
      Serial.println("   NOT: acil stop hala aktif, adimlar kesik ('estop reset' gerekir)");
    return;
  }

  if (k.startsWith("sunt ")) {
    float r = k.substring(5).toFloat();
    if (r > 0) { rSunt = r; Serial.printf("sunt %.5f ohm -> olculebilir tepe +-%.2f A\n",
                                          rSunt, 0.0819175f / rSunt); }
    return;
  }
  if (k.startsWith("panena")) {
    int b = k.indexOf(' ', 6);
    if (b > 0) {
      long sn = k.substring(b + 1).toInt();
      if (sn < 0) { Serial.println("RED: negatif sure"); return; }
      panEnaBeklet = (uint32_t)sn * 1000UL;
      panBostaBasT = 0;
      if (panEnaBeklet == 0) panSurucuAc();      // kapatma iptal edildi -> hemen ac
    }
    if (panEnaBeklet == 0)
      Serial.println("pan ENA: bosta kapatma KAPALI, surucu surekli acik (isinir)");
    else
      Serial.printf("pan ENA: %lu sn hareketsizlikte kapanir, su an %s\n",
                    panEnaBeklet / 1000UL, panEnaKapali ? "KAPALI" : "acik");
    Serial.println("(NVS'e yazilmaz - reset sonrasi 60 sn'ye doner)");
    return;
  }

  if (k == "panac")  { panSurucuAc();    Serial.println("pan surucusu acildi"); return; }
  if (k == "pankap") { panSurucuKapat(); Serial.println("pan surucusu kapatildi - motor serbest"); return; }
  if (k == "tiltac") { tiltSurucuAc();   Serial.println("tilt surucusu acildi"); return; }
  if (k == "tiltkap") {
    tiltSurucuKapat();
    Serial.println("tilt surucusu kapatildi - EKSEN SERBEST, NAMLU DUSEBILIR");
    Serial.println("(ilk hareket komutunda kendiliginden geri acilir)");
    return;
  }

  if (k.startsWith("acs")) {
    if (k == "acs off")   { acsVar = false; Serial.println("ACS724 kapali"); return; }
    if (k == "acs sifir") { analogSetPinAttenuation(AKIM_ADC, ADC_11db);
                            acsVar = true; acsSifirla(); return; }
    if (k == "acs ham") {
      analogSetPinAttenuation(AKIM_ADC, ADC_11db);
      float mv = acsHamMV();                       // TEK olcum, ikisi de bundan
      float fark = mv - ay.acsSifirV * 1000.0f;
      Serial.printf("ham %.1f mV, sifirdan fark %+.1f mV -> %+.3f A\n",
                    mv, fark, fark / ay.acsMVperA);
      return;
    }
    if (k == "acs kal") { Serial.println("kullanim: acs kal <amper>   (multimetrenin okudugu gercek akim)"); return; }
    if (k.startsWith("acs kal ")) { analogSetPinAttenuation(AKIM_ADC, ADC_11db);
                                    acsVar = true;
                                    acsKalibre(k.substring(8).toFloat()); return; }
    acsVar = true;
    // 2026-08-06 HATA DUZELTMESI: burasi indexOf(' ', 4) idi. "acs" 3 harf, ilk
    // bosluk 3. indekste; aramaya 4'ten baslayinca o bosluk atlanip IKINCI
    // bosluk bulunuyordu. Sonuc: "acs 40 2.5" hassasiyeti 40 yerine 2.5 yapiyor
    // (okumalar 16 kat sisiyor), tek argumanli "acs 40" ise hicbir sey yapmiyordu.
    int b1 = k.indexOf(' ', 3);
    if (b1 > 0) {
      int b2 = k.indexOf(' ', b1 + 1);
      float mvA = k.substring(b1 + 1, b2 > 0 ? b2 : k.length()).toFloat();
      if (mvA > 0.0f) ay.acsMVperA = mvA;          // 0 girilirse ayari bozma
      else Serial.println("UYARI: gecersiz mV/A, eski deger korundu");
      if (b2 > 0) ay.acsSifirV = k.substring(b2 + 1).toFloat();
    }
    analogSetPinAttenuation(AKIM_ADC, ADC_11db);
    Serial.printf("ACS724 acik: %.2f mV/A, sifir %.4f V, bolucu x%.2f, pin GPIO%d\n",
                  ay.acsMVperA, ay.acsSifirV, acsBolucu, AKIM_ADC);
    Serial.printf("olculebilir tepe ~+-%.0f A, 1 mV = %.3f A\n",
                  1650.0f / ay.acsMVperA, 1.0f / ay.acsMVperA);
    return;
  }

  if (k == "tara") {
    for (int hat = 0; hat < 2; hat++) {
      TwoWire &w = (hat == 0) ? Wire : Wire1;
      Serial.printf("I2C%d: ", hat);
      int n = 0;
      for (uint8_t a = 1; a < 127; a++)
        if (i2cVarMi(w, a)) { Serial.printf("0x%02X ", a); n++; }
      if (n == 0) Serial.print("(bos)");
      Serial.println();
    }
    return;
  }

  if (k == "kaydet") { ayarKaydet(); return; }

  Serial.printf("bilinmeyen komut: '%s'  ('?' ile liste)\n", k.c_str());
}

// ---------------------- setup / loop ----------------------
void setup() {
  Serial.begin(115200);

  pinMode(PAN_PUL, OUTPUT);    digitalWrite(PAN_PUL, LOW);
  pinMode(PAN_DIR, OUTPUT);    digitalWrite(PAN_DIR, LOW);
  // Pan ENA: seviyeyi ONCE yaz, SONRA cikisa al (GUC_PIN'deki ayni gerekce) -
  // pinMode cikisa alirken kayitli seviyeyi surer, ters sirada bir an HIGH
  // gorunup surucu kapanabilir. LOW = acik, yani bugunku davranisla baslar.
  digitalWrite(PAN_ENA, LOW);
  pinMode(PAN_ENA, OUTPUT);
  digitalWrite(PAN_ENA, LOW);
  pinMode(TILT_PUL, OUTPUT);   digitalWrite(TILT_PUL, LOW);
  pinMode(TILT_DIR, OUTPUT);   digitalWrite(TILT_DIR, LOW);
  // Tilt ENA da ayni sirayla: seviye once, cikis sonra. Burada kural daha da
  // katı - bir anlik HIGH tilt surucusunu kapatir ve namlu duser.
  digitalWrite(TILT_ENA, LOW);
  pinMode(TILT_ENA, OUTPUT);
  digitalWrite(TILT_ENA, LOW);
  pinMode(SARJOR_STEP, OUTPUT);digitalWrite(SARJOR_STEP, LOW);
  pinMode(SARJOR_DIR, OUTPUT); digitalWrite(SARJOR_DIR, LOW);
  pinMode(SARJOR_EN, OUTPUT);  digitalWrite(SARJOR_EN, HIGH);   // bosta = disable

  // Acil stop: disarida 4k7 pull-up var, dahili pull-up yedek olarak acik.
  // Kablo kopuk / buton basili -> HIGH (fail-safe).
  pinMode(ESTOP_PIN, INPUT_PULLUP);

  // ESP guc kesme yolu. Seviyeyi ONCE yaz, SONRA cikisa al: pinMode cikis
  // yazmacini 0 ile acarsa pin bir an LOW surulur ve ARM rolesi bosuna
  // cekip aciliste gucu kirpar.
  digitalWrite(GUC_PIN, GUC_ACIK);
  pinMode(GUC_PIN, OUTPUT);
  digitalWrite(GUC_PIN, GUC_ACIK);
  gucKesik = false;

  Wire.begin(I2C0_SDA, I2C0_SCL, 400000);
  Wire1.begin(I2C1_SDA, I2C1_SCL, 400000);

  delay(3000);                       // USB CDC'nin acilmasi icin pay

  Serial.println("\n===== HSS ANA FIRMWARE =====");
  Serial.println("Namlu ESP32'den BAGIMSIZ - SCR/IR gate hatti bu firmware'de YOK.");

  ayarYukle();

  encPanVar  = i2cVarMi(Wire,  ADRES_AS5600);
  encTiltVar = i2cVarMi(Wire1, ADRES_AS5600);
  inaVar     = i2cVarMi(Wire,  ADRES_INA226);
  imuVar     = i2cVarMi(Wire,  ADRES_IMU);
  // MPU6500 acilista UYKUDA gelir; uyandirilmazsa okumalar hep 0 doner.
  if (imuVar) imuUyandir(Wire);
  Serial.printf("I2C: enc-pan %s, enc-tilt %s, INA226 %s, IMU %s\n",
                encPanVar ? "VAR" : "yok", encTiltVar ? "VAR" : "yok",
                inaVar ? "VAR" : "yok", imuVar ? "VAR" : "yok");

  if (inaVar) {
    uint16_t id;
    if (ina226Oku16(INA226_ID, id)) Serial.printf("INA226 Die ID 0x%04X\n", id);
    // AVG=16, VBUSCT=1.1ms, VSHCT=1.1ms, surekli sunt+bus
    Wire.beginTransmission(ADRES_INA226);
    Wire.write(INA226_CONFIG); Wire.write(0x45); Wire.write(0x27);
    Wire.endTransmission();
  }

  // adim ureteci: 1 MHz sayac, ISR_HZ'de alarm
  zaman = timerBegin(1000000);
  timerAttachInterrupt(zaman, &zamanISR);
  timerAlarm(zaman, 1000000UL / ISR_HZ, true, 0);

  Serial.printf("adim ureteci %lu Hz (tavan %lu adim/s/eksen)\n",
                (unsigned long)ISR_HZ, (unsigned long)(ISR_HZ / 2));
  Serial.printf("guc kesme yolu GPIO%d (ARM rolesi, LOW=kes) - acilista GUC VAR\n", GUC_PIN);
  Serial.println("ACIL STOP MANDALI KILITLI - 'estop reset' ile ac.");
  Serial.println("'?' komut listesi\n");

  estopSonOkuma = (digitalRead(ESTOP_PIN) == HIGH);
  estopHam = estopSonOkuma;
  estopDegisimT = millis();
}

void loop() {
  estopIsle();
  panEnaIsle();

  static String tampon;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (tampon.length()) { komutIsle(tampon); tampon = ""; } }
    else tampon += c;
  }

  static uint32_t telT = 0;
  if (telemetriAcik && millis() - telT >= 200) { telT = millis(); telemetriYaz(); }
}
