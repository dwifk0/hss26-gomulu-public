// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// =====================================================================
//  HSS - TEK EKSEN ENKODER JOG TESTI          (ana montaj ilk calistirma)
//
//  Amac: KY-040 dondurucu enkoder ile tek ekseni elle ileri/geri surmek,
//  butona basinca 0 konumuna geri donmek. Ana firmware'den (hss_ana.ino)
//  BAGIMSIZ, tek dosyalik deneme sketch'i. Ates devresine, sarjora, I2C'ye,
//  role/acil stop cikisina DOKUNMAZ - sadece bir TB6600 surer.
//
//                        KLASIK ESP32      ESP32-S3
//  KY-040  CLK      ->   GPIO32            GPIO12
//          DT       ->   GPIO33            GPIO13
//          SW       ->   GPIO14            GPIO47
//          +        ->   3V3   (5V DEGIL)
//          GND      ->   ESP32 GND
//  TB6600  PUL+     ->   GPIO25            GPIO4
//          DIR+     ->   GPIO26            GPIO5
//          ENBL+    ->   bagli degil       GPIO21
//          PUL-/DIR-/ENBL- -> ESP32 GND    (ortak katot)
//
//  Enkoderin uc ucu de dahili pull-up ile okunuyor; modulun kendi 10k'lari
//  paralel kalir, sorun cikarmaz. KY-040'ta SW pininde genelde pull-up YOKTUR,
//  dahili sart. Klasik ESP32'de GPIO34-39 SADECE GIRIS ve dahili pull-up'i yok,
//  enkoder oraya baglanmaz; GPIO6-11 flash'a bagli, kart hic acilmaz.
//
//  DERLEME:
//     klasik ESP32 : fqbn esp32:esp32:esp32
//     ESP32-S3     : fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc   (CDCOnBoot SART)
//  Kart secimi asagidaki KART_S3 ile yapilir.
//
//  UYARI: bu sketch acil stop pinini VARSAYILAN OLARAK DINLEMEZ (montaj
//  sirasinda halka henuz bagli olmayabilir, bagli degilse pin HIGH okur ve
//  hareketi bosuna kilitler). Durumu her 'd' dokumunde yazar. Halka bagliysa
//  ESTOP_KULLAN'i 1 yap - o zaman fail-safe davranir.
// =====================================================================

#include <soc/gpio_struct.h>     // ISR icinde hizli pin surme (out_w1ts/w1tc)

// ---------------------- kart ve eksen secimi ----------------------
#define KART_S3        0         // 1 = ESP32-S3 (sistemdeki kart), 0 = klasik ESP32
                                 // 2026-08-13 tezgah karti: ESP32-WROOM-32 -> 0
#define EKSEN_TILT     0         // 0 = PAN denenir, 1 = TILT denenir
#define YON_TERS       0         // enkoder ters yone suruyorsa 1 yap

#if KART_S3
  #if EKSEN_TILT
    #define MOT_PUL   6
    #define MOT_DIR   7
    #define MOT_ENA   -1         // tilt'te ENA BILEREK bagli degil (namlu duser)
    #define ORAN      5.0f
    #define EKSEN_AD  "TILT"
  #else
    #define MOT_PUL   4
    #define MOT_DIR   5
    #define MOT_ENA   21         // TB6600 #1 ENBL+ : LOW = surucu ACIK
    #define ORAN      10.0f
    #define EKSEN_AD  "PAN"
  #endif
  #define ENK_CLK     12
  #define ENK_DT      13
  #define ENK_SW      47
  #define ESTOP_PIN   14
#else
  // Klasik ESP32 (en bastaki kart, eski a1/a2 testlerinin pinleri)
  #define MOT_PUL     25
  #define MOT_DIR     26
  #define MOT_ENA     -1
  #define ORAN        10.0f
  #define EKSEN_AD    "PAN"
  #define ENK_CLK     32
  #define ENK_DT      33
  #define ENK_SW      14      // a3/a5 boyle kablolanmis, kabloya uyduruldu
  #define ESTOP_PIN   -1
#endif

#define ESTOP_KULLAN  0          // 1 = GPIO14 HIGH iken hareket kilitli
#if KART_S3 && ESTOP_KULLAN
  #define ESTOP_AKTIF 1          // klasik ESP32'de estop pini tanimli degil
#else
  #define ESTOP_AKTIF 0
#endif

// ---------------------- mekanik / hiz sabitleri ----------------------
// mikroadim TB6600 DIP anahtarindan gelir. 2026-08-06'da okundu: 1/16.
// YANLIS girilirse derece hesabi orantili sasar, motor yine doner.
#define MOTOR_TAM_ADIM   200
#define MIKROADIM        16

#define ISR_HZ           50000UL // adim uretecinin tick frekansi
#define HIZ_KRUVAZIYER   6000    // adim/s (~67.5 derece/s pan'da)
#define HIZ_BASLANGIC    500     // adim/s, rampa alt ucu
#define RAMPA_ADIM       800     // kac adimda hizlanip yavaslar

#define DERECE_PER_TIK   0.5f    // enkoderin bir tikinin karsiligi (derece)
#define LIMIT_DERECE     180.0f  // yumusak limit +/- ; 0 = sinirsiz
                                 // MONTAJDA KABLO SARMAMASI ICIN VAR

#define BUTON_DEBOUNCE   30      // ms
#define BUTON_UZUN_MS    1200    // bu kadar basili tutmak = burasi sifir

// Enkoder titreme (dither) suzgeci. KY-040'in detenti iki durum arasinda
// dururken sinyal gidip geliyor ve cozucu +1 / -1 / +1 ... uretiyor; net
// hareket sifir ama sistem bos yere calisiyor. Insan eliyle 5 ms'den kisa
// surede yon degistirilemez, o yuzden bu kadar hizli TERS tiki eliyoruz.
// Ayni yondeki tiklar hicbir zaman elenmez - hizli cevirmek serbest.
#define ENK_TERS_MS      5

// ---------------------- adim ureteci (DDS) ----------------------
// hss_ana.ino ile ayni yapi: 32 bit faz akumulatoru, tasinca bir darbe.
//     f_adim = ISR_HZ * artis / 2^32
struct Eksen {
  uint8_t  pulPin, dirPin;
  volatile int32_t  konum;
  volatile int32_t  hedef;
  volatile uint32_t akum;
  volatile uint32_t artis;
  volatile uint32_t artisBas;
  volatile uint32_t artisKruv;
  volatile uint32_t rampa;
  volatile uint32_t hareketAdim;
  volatile bool     dirPinDurum;
  volatile bool     darbeAcik;
  volatile bool     serbest;
};

Eksen eks = {MOT_PUL, MOT_DIR, 0,0,0,0,0,0,0,0,false,false,true};

portMUX_TYPE muks = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t *zaman = nullptr;

volatile bool adimlarKesik = false;

uint32_t hizKruv = HIZ_KRUVAZIYER;
float    tikDerece = DERECE_PER_TIK;
float    limitDerece = LIMIT_DERECE;

static inline uint32_t hzToArtis(uint32_t hz) {
  if (hz == 0) return 0;
  if (hz > ISR_HZ / 2) hz = ISR_HZ / 2;
  uint64_t a = ((uint64_t)hz << 32) / (ISR_HZ - hz);
  if (a > 0xFFFFFFFFULL) a = 0xFFFFFFFFULL;
  return (uint32_t)a;
}

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

void IRAM_ATTR zamanISR() {
  Eksen *e = &eks;

  // 1) onceki darbeyi indir, bu tick'te yeni darbe uretme (LOW suresi garanti)
  if (e->darbeAcik) {
    GPIO.out_w1tc = (1U << e->pulPin);
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
  if (e->akum < once) {
    GPIO.out_w1ts = (1U << e->pulPin);
    e->darbeAcik = true;
    e->konum += istenenYon ? 1 : -1;
    e->hareketAdim++;
    if ((e->hareketAdim & 0x07) == 0) hizGuncelle(e);
  }
}

// ---------------------- eksen yardimcilari ----------------------
float derecePerAdim() {
  return 360.0f / ((float)MOTOR_TAM_ADIM * (float)MIKROADIM * ORAN);
}
int32_t dereceToAdim(float d) { return (int32_t)lroundf(d / derecePerAdim()); }

int32_t konumOku() {
  portENTER_CRITICAL(&muks); int32_t k = eks.konum; portEXIT_CRITICAL(&muks);
  return k;
}
int32_t hedefOku() {
  portENTER_CRITICAL(&muks); int32_t h = eks.hedef; portEXIT_CRITICAL(&muks);
  return h;
}
bool mesgul() {
  portENTER_CRITICAL(&muks); bool m = !eks.serbest; portEXIT_CRITICAL(&muks);
  return m;
}

// Jog: hedefi UZAT. Hareket suruyorsa rampaya/akumulatore DOKUNMA - yoksa her
// tikta rampa bastan baslar ve hareket kesik kesik olur.
void hedefEkle(int32_t delta) {
  portENTER_CRITICAL(&muks);
  int32_t yeni = eks.hedef + delta;
  if (limitDerece > 0.0f) {
    int32_t lim = (int32_t)lroundf(limitDerece / derecePerAdim());
    if (yeni >  lim) yeni =  lim;
    if (yeni < -lim) yeni = -lim;
  }
  eks.hedef = yeni;
  if (eks.serbest && eks.hedef != eks.konum) {
    // Rampa SABIT tutuluyor (mesafe/2 degil): tek tiklik kisa hareketlerde
    // hiz rampanin ancak basina kadar cikar, motor sarsilmaz.
    eks.artisBas    = hzToArtis(HIZ_BASLANGIC);
    eks.artisKruv   = hzToArtis(hizKruv);
    eks.rampa       = RAMPA_ADIM;
    eks.hareketAdim = 0;
    eks.artis       = eks.artisBas;
    eks.akum        = 0;
    eks.serbest     = false;
  }
  portEXIT_CRITICAL(&muks);
}

// Uzun hareket (sifira donus): tam rampa, mesafenin yarisi ile sinirli.
void hedefle(int32_t hedefAdim) {
  portENTER_CRITICAL(&muks);
  uint32_t mesafe = (uint32_t)abs(hedefAdim - eks.konum);
  eks.hedef       = hedefAdim;
  eks.artisBas    = hzToArtis(HIZ_BASLANGIC);
  eks.artisKruv   = hzToArtis(hizKruv);
  eks.rampa       = min((uint32_t)RAMPA_ADIM, mesafe / 2);
  eks.hareketAdim = 0;
  eks.artis       = eks.artisBas;
  eks.akum        = 0;
  eks.serbest     = (mesafe == 0);
  portEXIT_CRITICAL(&muks);
}

void durdur() {
  portENTER_CRITICAL(&muks);
  eks.hedef   = eks.konum;
  eks.artis   = 0;
  eks.akum    = 0;
  eks.serbest = true;
  portEXIT_CRITICAL(&muks);
}

void burasiSifir() {
  portENTER_CRITICAL(&muks);
  eks.konum = 0; eks.hedef = 0; eks.serbest = true;
  portEXIT_CRITICAL(&muks);
}

// ---------------------- enkoder cozucu (Buxton tablosu) ----------------------
// Tam adim tablosu: bir detent = bir tik. Zipirdama tablonun kendisinde
// eleniyor, ayrica RC/debounce gerekmiyor.
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6
#define DIR_CW      0x10
#define DIR_CCW     0x20

const uint8_t ttable[7][4] = {
  {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},               // R_START
  {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},      // R_CW_FINAL
  {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},               // R_CW_BEGIN
  {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},               // R_CW_NEXT
  {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},               // R_CCW_BEGIN
  {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},     // R_CCW_FINAL
  {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},               // R_CCW_NEXT
};

uint8_t enkDurum = R_START;

// teshis sayaclari
uint32_t tikArti = 0, tikEksi = 0, tikElenen = 0;

// -1, 0 veya +1 dondurur
int enkOku() {
  uint8_t pin = (digitalRead(ENK_DT) << 1) | digitalRead(ENK_CLK);
  enkDurum = ttable[enkDurum & 0x0F][pin];
  uint8_t yon = enkDurum & 0x30;
  if (yon == DIR_CW)  return +1;
  if (yon == DIR_CCW) return -1;
  return 0;
}

// ---------------------- durum yazdirma ----------------------
// Son basilan degerler. Ayni satiri tekrar tekrar basmamak icin: enkoder
// titrerse konum degismeden "hareket basladi/bitti" gecisi olusuyor ve
// eskiden bu, saniyede yuzlerce ayni satir demekti.
int32_t sonYazKonum = INT32_MIN, sonYazHedef = INT32_MIN;

void durumYaz() {
  int32_t k = konumOku(), h = hedefOku();
  sonYazKonum = k; sonYazHedef = h;
  Serial.printf("%s  konum=%+8.2f  hedef=%+8.2f  adim=%ld  %s\n",
                EKSEN_AD, k * derecePerAdim(), h * derecePerAdim(),
                (long)k, mesgul() ? "hareket" : "duruyor");
}

void dokum() {
  Serial.println(F("---------------------------------------------"));
  Serial.printf("eksen      : %s   PUL=GPIO%d DIR=GPIO%d ", EKSEN_AD, MOT_PUL, MOT_DIR);
  if (MOT_ENA >= 0) Serial.printf("ENA=GPIO%d (acik)\n", MOT_ENA);
  else              Serial.println("ENA=yok");
  Serial.printf("enkoder    : CLK=GPIO%d DT=GPIO%d SW=GPIO%d\n",
                ENK_CLK, ENK_DT, ENK_SW);
  Serial.printf("cozunurluk : %.4f derece/adim  (tam tur = %ld adim)\n",
                derecePerAdim(),
                (long)(MOTOR_TAM_ADIM * MIKROADIM * (int)ORAN));
  Serial.printf("tik        : %.2f derece  (%ld adim)\n",
                tikDerece, (long)dereceToAdim(tikDerece));
  Serial.printf("hiz        : %lu adim/s (%.1f derece/s), rampa %d adim\n",
                (unsigned long)hizKruv, hizKruv * derecePerAdim(), RAMPA_ADIM);
  if (limitDerece > 0.0f)
    Serial.printf("limit      : +/- %.1f derece\n", limitDerece);
  else
    Serial.println("limit      : YOK (sinirsiz)");
#if ESTOP_AKTIF
  Serial.printf("acil stop  : GPIO%d = %s  (DINLENIYOR)\n", ESTOP_PIN,
                digitalRead(ESTOP_PIN) ? "HIGH -> KILITLI" : "LOW (normal)");
#else
  #if KART_S3
  Serial.printf("acil stop  : GPIO%d = %s  (dinlenmiyor, ESTOP_KULLAN=0)\n",
                ESTOP_PIN, digitalRead(ESTOP_PIN) ? "HIGH" : "LOW");
  #endif
#endif
  Serial.printf("tik sayaci : +%lu  -%lu  elenen=%lu (titreme suzgeci %d ms)\n",
                (unsigned long)tikArti, (unsigned long)tikEksi,
                (unsigned long)tikElenen, ENK_TERS_MS);
  durumYaz();
  Serial.println(F("komutlar: d  pin  0  sifir  dur  tik <derece>  hiz <adim/s>  limit <derece>"));
  Serial.println(F("---------------------------------------------"));
}

// ---------------------- pin teshisi ----------------------
// 1 saniye boyunca ham pinleri ornekler. EL DEGMIYORKEN "durum degisimi"
// sifirdan buyukse hat titriyor demektir: ya GND ortak degil, ya kablo uzun/
// gurultulu, ya da dugme tam detent sinirinda duruyor.
void pinTeshis() {
  uint16_t dagilim[4] = {0,0,0,0};
  uint16_t degisim = 0, swDusuk = 0;
  uint8_t  once = 0xFF;

  for (int i = 0; i < 1000; i++) {
    uint8_t p = (digitalRead(ENK_DT) << 1) | digitalRead(ENK_CLK);
    dagilim[p]++;
    if (p != once) { if (once != 0xFF) degisim++; once = p; }
    if (digitalRead(ENK_SW) == LOW) swDusuk++;
    delay(1);
  }

  Serial.println(F("--- pin teshisi (1 sn, 1000 orneklem) ---"));
  Serial.printf("DT:CLK dagilimi  00=%u  01=%u  10=%u  11=%u\n",
                dagilim[0], dagilim[1], dagilim[2], dagilim[3]);
  Serial.printf("durum degisimi   %u   (el degmiyorsa 0 olmali)\n", degisim);
  Serial.printf("SW dusuk         %u ms  (basili degilse 0 olmali)\n", swDusuk);
  Serial.printf("tik sayaci       +%lu  -%lu  elenen=%lu\n",
                (unsigned long)tikArti, (unsigned long)tikEksi,
                (unsigned long)tikElenen);
  Serial.println(F("-----------------------------------------"));
}

// ---------------------- seri komutlar ----------------------
void komutIsle(String s) {
  s.trim();
  if (s.length() == 0) return;

  if (s == "d" || s == "s")        { dokum(); return; }
  if (s == "pin")                  { pinTeshis(); return; }
  if (s == "0")                    { Serial.println("-> sifira donuluyor"); hedefle(0); return; }
  if (s == "sifir")                { burasiSifir(); Serial.println("-> burasi sifir kabul edildi"); return; }
  if (s == "dur")                  { durdur(); Serial.println("-> durduruldu"); return; }

  if (s.startsWith("tik ")) {
    float v = s.substring(4).toFloat();
    if (v > 0.0f) { tikDerece = v; Serial.printf("-> tik = %.2f derece\n", v); }
    else Serial.println("HATA: tik > 0 olmali");
    return;
  }
  if (s.startsWith("hiz ")) {
    long v = s.substring(4).toInt();
    if (v >= 100 && v <= (long)(ISR_HZ / 2)) {
      hizKruv = (uint32_t)v;
      Serial.printf("-> hiz = %ld adim/s (%.1f derece/s)\n", v, v * derecePerAdim());
    } else Serial.println("HATA: hiz 100..25000 arasi olmali");
    return;
  }
  if (s.startsWith("limit ")) {
    float v = s.substring(6).toFloat();
    limitDerece = (v < 0.0f) ? 0.0f : v;
    if (limitDerece > 0.0f) Serial.printf("-> limit +/- %.1f derece\n", limitDerece);
    else Serial.println("-> limit KALDIRILDI (sinirsiz)");
    return;
  }
  Serial.printf("bilinmeyen komut: '%s'  ('d' yaz)\n", s.c_str());
}

// ---------------------- setup ----------------------
void setup() {
  Serial.begin(115200);

  pinMode(MOT_PUL, OUTPUT); digitalWrite(MOT_PUL, LOW);
  pinMode(MOT_DIR, OUTPUT); digitalWrite(MOT_DIR, LOW);
#if KART_S3 && !EKSEN_TILT
  // TB6600 ENBL+ : LOW = surucu ACIK. Test boyunca surekli acik tutuluyor,
  // yoksa knob'u cevirirken eksen tutmaz. Uzun bosta motor isinir.
  pinMode(MOT_ENA, OUTPUT); digitalWrite(MOT_ENA, LOW);
#endif

  pinMode(ENK_CLK, INPUT_PULLUP);
  pinMode(ENK_DT,  INPUT_PULLUP);
  pinMode(ENK_SW,  INPUT_PULLUP);
#if KART_S3
  pinMode(ESTOP_PIN, INPUT);
#endif

  zaman = timerBegin(1000000);
  timerAttachInterrupt(zaman, &zamanISR);
  timerAlarm(zaman, 1000000UL / ISR_HZ, true, 0);

  delay(2500);                       // USB CDC'nin acilmasi icin pay
  Serial.println(F("\n=== HSS tek eksen enkoder jog testi ==="));
  Serial.println(F("Enkoderi cevir -> eksen doner.  Butona kisa bas -> 0'a doner."));
  Serial.printf ("Butonu %d ms basili tut -> BULUNULAN YER sifir kabul edilir.\n",
                 BUTON_UZUN_MS);
  Serial.println(F("Sifir = ESP32'nin acildigi andaki konum. Reset = sifir kaybi."));
  dokum();
}

// ---------------------- loop ----------------------
void loop() {
  // --- enkoder ---
  int tik = enkOku();
  if (tik != 0) {
#if YON_TERS
    tik = -tik;
#endif
    static int      sonYon  = 0;
    static uint32_t sonTikT = 0;
    uint32_t simdi = millis();
    if (tik == -sonYon && (simdi - sonTikT) < ENK_TERS_MS) {
      tikElenen++;                    // detent sinirindaki titreme, yok say
    } else {
      sonYon = tik; sonTikT = simdi;
      if (tik > 0) tikArti++; else tikEksi++;
      hedefEkle(tik * dereceToAdim(tikDerece));
    }
  }

  // --- buton ---
  static bool     sonSeviye  = HIGH;
  static bool     kararli    = HIGH;
  static uint32_t degisimT   = 0;
  static uint32_t basmaT     = 0;
  static bool     uzunYapildi = false;

  bool ham = digitalRead(ENK_SW);
  if (ham != sonSeviye) { sonSeviye = ham; degisimT = millis(); }

  if (millis() - degisimT > BUTON_DEBOUNCE && ham != kararli) {
    kararli = ham;
    if (kararli == LOW) {                      // basildi
      basmaT = millis();
      uzunYapildi = false;
    } else {                                   // birakildi
      if (!uzunYapildi) {
        Serial.println(">> BUTON: sifira donuluyor");
        hedefle(0);
      }
    }
  }
  // basili tutma - birakmayi beklemeden tetikler
  if (kararli == LOW && !uzunYapildi && millis() - basmaT > BUTON_UZUN_MS) {
    uzunYapildi = true;
    durdur();
    burasiSifir();
    Serial.println(">> BUTON (uzun): BURASI SIFIR kabul edildi");
  }

  // --- seri ---
  static String tampon;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { komutIsle(tampon); tampon = ""; }
    else if (tampon.length() < 40) tampon += c;
  }

  // --- durum yazdirma: hareket varken 250 ms'de bir, durunca son bir kez ---
  static uint32_t yazT = 0;
  static bool     oncekiMesgul = false;
  bool m = mesgul();
  // DEGISMEYEN satir basilmaz: enkoder titrerse konum sabit kalirken
  // "hareket basladi/bitti" gecisi olusur ve ekran bos yere dolar.
  bool degisti = (konumOku() != sonYazKonum) || (hedefOku() != sonYazHedef);
  if (degisti && (m ? (millis() - yazT > 250) : oncekiMesgul)) {
    yazT = millis();
    durumYaz();
  }
  oncekiMesgul = m;

#if ESTOP_AKTIF
  static bool oncekiEstop = false;
  bool estop = digitalRead(ESTOP_PIN);          // HIGH = basili veya kablo kopuk
  if (estop != oncekiEstop) {
    oncekiEstop = estop;
    adimlarKesik = estop;
    if (estop) { durdur(); Serial.println("!! ACIL STOP - adimlar kesildi"); }
    else       Serial.println("acil stop birakildi");
  }
#endif
}
