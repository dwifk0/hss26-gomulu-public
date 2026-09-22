// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// =====================================================================
//  5 TAM TUR SAAT YONUNDE - klasik ESP32 + TB6600
//
//  Mevcut tezgah kablolamasi: PUL+ -> GPIO25, DIR+ -> GPIO26,
//  PUL- / DIR- -> ESP32 GND (ortak katot). ENA bagli degil.
//
//  a1/a2'den FARKI: rampa var. a1 dogrudan 1000 adim/s'de basliyordu;
//  yuklu bir eksende step motor bu hizda YERINDEN KALKAMAZ, sadece inler
//  veya hic kipirdamaz. Burada 300 adim/s'den baslayip 1500'e cikiyor.
//
//  ADIM/TUR = 200 x mikroadim. Mikroadim TB6600'un S1-S2-S3 DIP'inden
//  gelir; asagidaki MIKROADIM ile AYNI olmali, yoksa 5 tur donmez:
//     S1=OFF S2=OFF S3=ON   -> 1/16  -> 3200 adim/tur   (bizim ayar)
//     S1=OFF S2=ON  S3=OFF  -> 1/8   -> 1600 adim/tur
//     S1=ON  S2=OFF S3=OFF  -> 1/4   ->  800 adim/tur
//     S1=OFF S2=OFF S3=OFF  -> 1/32  -> 6400 adim/tur
//
//  YON: DIR=HIGH saat yonu KABUL EDILDI. Motor ters donerse ya YON_SAAT'i
//  LOW yap ya da bir sarginin (ornegin A+ ile A-) iki ucunu yer degistir.
//  Ikisi de ayni sonucu verir, kodla halletmek daha kolay.
// =====================================================================

#define PUL 25
#define DIR 26

#define MIKROADIM     16
#define ADIM_PER_TUR  (200L * MIKROADIM)      // 3200
#define TUR_SAYISI    5
#define YON_SAAT      HIGH                    // ters donerse LOW yap

#define HIZ_BAS       300     // adim/s, rampanin alt ucu
#define HIZ_SON      1500     // adim/s, kruvaziyer
#define RAMPA_ADIM    600     // kac adimda hizlanip yavaslar

// adim/s -> yarim periyot (us)
static inline uint32_t hizToUs(uint32_t adimSaniye) {
  return 500000UL / adimSaniye;
}

void adimAt(uint32_t usYari) {
  digitalWrite(PUL, HIGH);
  delayMicroseconds(usYari);
  digitalWrite(PUL, LOW);
  delayMicroseconds(usYari);
}

void hareket(bool yon, long toplam) {
  digitalWrite(DIR, yon);
  delayMicroseconds(20);                  // TB6600 DIR setup suresi

  long rampa = min((long)RAMPA_ADIM, toplam / 2);

  for (long i = 0; i < toplam; i++) {
    uint32_t hiz;
    if (i < rampa) {
      hiz = HIZ_BAS + (uint32_t)((HIZ_SON - HIZ_BAS) * i / rampa);
    } else if (i >= toplam - rampa) {
      hiz = HIZ_BAS + (uint32_t)((HIZ_SON - HIZ_BAS) * (toplam - 1 - i) / rampa);
    } else {
      hiz = HIZ_SON;
    }
    adimAt(hizToUs(hiz));

    if ((i % ADIM_PER_TUR) == 0 && i > 0)
      Serial.printf("  %ld. tur bitti\n", i / ADIM_PER_TUR);
  }
}

void besTur() {
  long toplam = ADIM_PER_TUR * TUR_SAYISI;
  Serial.printf("%d tur saat yonu, %ld adim, %ld adim/tur...\n",
                TUR_SAYISI, toplam, ADIM_PER_TUR);
  hareket(YON_SAAT, toplam);
  Serial.println("bitti. tekrar icin 'r', ters yon icin 't'");
}

void setup() {
  Serial.begin(115200);
  pinMode(PUL, OUTPUT); digitalWrite(PUL, LOW);
  pinMode(DIR, OUTPUT); digitalWrite(DIR, LOW);

  delay(2000);                    // seri portun acilmasi icin pay
  Serial.println(F("\n=== 5 tur saat yonunde ==="));
  Serial.printf("PUL=GPIO%d DIR=GPIO%d  mikroadim=1/%d\n", PUL, DIR, MIKROADIM);
  besTur();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'r') besTur();
  else if (c == 't') {
    Serial.println("ters yon...");
    hareket(!YON_SAAT, ADIM_PER_TUR * TUR_SAYISI);
    Serial.println("bitti");
  }
}
