// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// TB6600 pan/tilt deneme hareketi - ESP32-S3
// Ortak katot bagIanti: PUL+/DIR+ -> GPIO, PUL-/DIR- -> ESP32 GND, ENA bos.
// Eski a1/a2 testlerinin (klasik ESP32, GPIO25/26) S3 pinlerine uyarlanmis hali.

#define PAN_PUL   4
#define PAN_DIR   5
#define TILT_PUL  6
#define TILT_DIR  7

const int   ADIM_PER_TUR = 3200;   // TB6600 DIP = 1/16 mikroadim varsayimi
const int   HIZ_BASLANGIC = 1200;  // us, yarim periyot (yavas baslangic)
const int   HIZ_SON       = 400;   // us, yarim periyot (kruvaziyer hiz)
const int   RAMPA_ADIM    = 400;   // kac adimda hizlanip yavaslasin

void adimAt(int pulPin, int us) {
  digitalWrite(pulPin, HIGH);
  delayMicroseconds(us);
  digitalWrite(pulPin, LOW);
  delayMicroseconds(us);
}

// Rampali hareket: hizlan - sabit - yavasla
void hareket(int pulPin, int dirPin, bool yon, long adim) {
  digitalWrite(dirPin, yon ? HIGH : LOW);
  delayMicroseconds(10);           // TB6600 DIR setup suresi

  long rampa = min((long)RAMPA_ADIM, adim / 2);
  for (long i = 0; i < adim; i++) {
    int us;
    if (i < rampa) {
      us = HIZ_BASLANGIC + (HIZ_SON - HIZ_BASLANGIC) * i / rampa;
    } else if (i >= adim - rampa) {
      us = HIZ_SON + (HIZ_BASLANGIC - HIZ_SON) * (i - (adim - rampa)) / rampa;
    } else {
      us = HIZ_SON;
    }
    adimAt(pulPin, us);
  }
}

void eksenTesti(const char *ad, int pulPin, int dirPin) {
  Serial.printf("%s: 1 tur ileri (%d adim)...\n", ad, ADIM_PER_TUR);
  Serial.flush();
  hareket(pulPin, dirPin, true, ADIM_PER_TUR);
  delay(800);

  Serial.printf("%s: 1 tur geri...\n", ad);
  Serial.flush();
  hareket(pulPin, dirPin, false, ADIM_PER_TUR);
  delay(800);

  Serial.printf("%s: tamam\n", ad);
  Serial.flush();
}

void setup() {
  Serial.begin(115200);

  pinMode(PAN_PUL, OUTPUT);   digitalWrite(PAN_PUL, LOW);
  pinMode(PAN_DIR, OUTPUT);   digitalWrite(PAN_DIR, LOW);
  pinMode(TILT_PUL, OUTPUT);  digitalWrite(TILT_PUL, LOW);
  pinMode(TILT_DIR, OUTPUT);  digitalWrite(TILT_DIR, LOW);

  delay(5000);                // seri portun acilmasi icin pay

  Serial.println("\n=== HSS pan/tilt deneme hareketi ===");
  Serial.printf("pan  PUL=GPIO%d DIR=GPIO%d\n", PAN_PUL, PAN_DIR);
  Serial.printf("tilt PUL=GPIO%d DIR=GPIO%d\n", TILT_PUL, TILT_DIR);
  Serial.println("ENA bagli degil -> suruculer surekli enabled.");
  Serial.flush();
  delay(500);

  eksenTesti("PAN ", PAN_PUL, PAN_DIR);
  delay(1000);
  eksenTesti("TILT", TILT_PUL, TILT_DIR);

  Serial.println("=== dizi bitti, motorlar duruyor ===");
  Serial.flush();
}

void loop() {
  static uint32_t t = 0;
  if (millis() - t > 5000) {
    t = millis();
    Serial.println("bosta - yeniden calistirmak icin RST");
    Serial.flush();
  }
}
