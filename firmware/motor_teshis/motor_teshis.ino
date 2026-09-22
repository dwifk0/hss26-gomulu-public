// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// =====================================================================
//  TB6600 SINYAL TESHISI - klasik ESP32
//
//  Motor kilitli ama donmuyorsa: guc/sargi tarafi saglam, sorun adim
//  darbelerinde demektir. Bu sketch darbeyi ELLE kontrol ettirir, boylece
//  multimetreyle olculebilir - osiloskop gerekmez.
//
//  PUL+ -> GPIO25    DIR+ -> GPIO26    PUL-/DIR- -> ESP32 GND
//
//  KOMUTLAR (seri, 115200):
//     1     PUL surekli HIGH   -> PUL+ ile ESP GND arasi 3,3 V okumali
//     0     PUL surekli LOW    -> ayni yerde 0 V okumali
//     k     10 sn boyunca 1 Hz kare dalga -> multimetre ~1,6 V ortalama
//     y     200 adim, 5 ms/adim  (COK yavas, mile bakinca gorulur)
//     o     800 adim, 1 ms/adim  (orta)
//     h     3200 adim, 0,5 ms/adim (a1 ile ayni hiz = 1 tam tur)
//     t     DIR yonunu ters cevir
//     d     durum
//
//  OLCUM YERI: PUL+ ucu ile ESP32 GND arasi. Sonuc beklenenden farkliysa
//  ya kablo yanlis pinde, ya PUL- baglanmamis, ya da GPIO olmayan bir yere
//  gidiyor.
// =====================================================================

#define PUL 25
#define DIR 26

bool yon = true;

void adimlar(long adet, uint32_t usYari) {
  digitalWrite(DIR, yon ? HIGH : LOW);
  delayMicroseconds(20);                 // TB6600 DIR setup suresi
  for (long i = 0; i < adet; i++) {
    digitalWrite(PUL, HIGH);
    delayMicroseconds(usYari);
    digitalWrite(PUL, LOW);
    delayMicroseconds(usYari);
  }
  Serial.printf("bitti: %ld adim, yon=%s, yarim periyot=%lu us\n",
                adet, yon ? "HIGH" : "LOW", (unsigned long)usYari);
}

void durum() {
  Serial.println(F("--- motor teshisi ---"));
  Serial.printf("PUL=GPIO%d  DIR=GPIO%d  yon=%s\n", PUL, DIR, yon ? "HIGH" : "LOW");
  Serial.printf("PUL pin su an: %s\n", digitalRead(PUL) ? "HIGH" : "LOW");
  Serial.println(F("komutlar: 1 0 k y o h t d"));
  Serial.println(F("olcum: PUL+ ile ESP GND arasi DC volt"));
  Serial.println(F("---------------------"));
}

void setup() {
  Serial.begin(115200);
  pinMode(PUL, OUTPUT); digitalWrite(PUL, LOW);
  pinMode(DIR, OUTPUT); digitalWrite(DIR, LOW);
  delay(2000);
  Serial.println(F("\n=== TB6600 sinyal teshisi ==="));
  durum();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == '\n' || c == '\r' || c == ' ') return;

  switch (c) {
    case '1':
      digitalWrite(PUL, HIGH);
      Serial.println("PUL = HIGH sabit  -> PUL+ / GND arasi 3,3 V olmali");
      break;
    case '0':
      digitalWrite(PUL, LOW);
      Serial.println("PUL = LOW sabit   -> PUL+ / GND arasi 0 V olmali");
      break;
    case 'k': {
      Serial.println("1 Hz kare dalga, 10 sn  -> multimetre ~1,6 V ortalama");
      for (int i = 0; i < 10; i++) {
        digitalWrite(PUL, HIGH); delay(500);
        digitalWrite(PUL, LOW);  delay(500);
      }
      Serial.println("kare dalga bitti");
      break;
    }
    case 'y': adimlar(200,  5000); break;   // 100 adim/s
    case 'o': adimlar(800,  1000); break;   // 500 adim/s
    case 'h': adimlar(3200,  500); break;   // 1000 adim/s, a1 ile ayni
    case 't':
      yon = !yon;
      Serial.printf("yon artik %s\n", yon ? "HIGH" : "LOW");
      break;
    case 'd': durum(); break;
    default:
      Serial.printf("bilinmeyen: '%c'\n", c);
  }
}
