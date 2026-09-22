// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// I2C modullerinin hangi pinlere bagli oldugunu bulur.
// 1) Tum guvenli GPIO'larda dis pull-up / pull-down arar
// 2) Pull-up bulunan pinlerin her ikili kombinasyonunda I2C taramasi dener

#include <Wire.h>

// 19/20 native USB, 26-37 flash+PSRAM, 43/44 UART, 0/45/46 strapping -> disarida
const int adaylar[] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                       15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48};
const int N = sizeof(adaylar) / sizeof(adaylar[0]);

int pullupPin[N];
int pullupSayi = 0;

void pinleriTara() {
  Serial.println("\n--- pin seviyeleri ---");
  Serial.println("pin | pull-down ile | pull-up ile | yorum");
  for (int i = 0; i < N; i++) {
    int p = adaylar[i];
    pinMode(p, INPUT_PULLDOWN); delay(3);
    int a = digitalRead(p);
    pinMode(p, INPUT_PULLUP);   delay(3);
    int b = digitalRead(p);
    pinMode(p, INPUT);

    const char *yorum = "serbest";
    if (a == 1 && b == 1) { yorum = "DIS PULL-UP <<<"; pullupPin[pullupSayi++] = p; }
    else if (a == 0 && b == 0) yorum = "GND'ye cekili";

    Serial.printf(" %2d |       %d       |      %d      | %s\n", p, a, b, yorum);
  }
}

int probe(TwoWire &w, uint8_t adr) {
  w.beginTransmission(adr);
  return w.endTransmission() == 0;
}

void ciftleriDene() {
  if (pullupSayi < 2) {
    Serial.println("\nDis pull-up bulunan pin sayisi 2'den az - I2C modulu bagli degil ya da");
    Serial.println("modulun VCC'si gelmiyor. Once 3V3 ve GND'yi olc.");
    return;
  }
  Serial.printf("\n--- %d pull-up pini bulundu, ikili kombinasyonlar deneniyor ---\n", pullupSayi);
  for (int i = 0; i < pullupSayi; i++) {
    for (int j = 0; j < pullupSayi; j++) {
      if (i == j) continue;
      int sda = pullupPin[i], scl = pullupPin[j];
      Wire.end();
      delay(5);
      if (!Wire.begin(sda, scl)) continue;
      Wire.setClock(50000);
      delay(20);

      String bulunan = "";
      for (uint8_t a = 1; a < 127; a++) {
        if (probe(Wire, a)) {
          char buf[8];
          snprintf(buf, sizeof(buf), " 0x%02X", a);
          bulunan += buf;
        }
      }
      if (bulunan.length()) {
        Serial.printf("  SDA=%2d SCL=%2d ->%s   <<< BULUNDU\n", sda, scl, bulunan.c_str());
      }
    }
  }
  Serial.println("--- kombinasyon taramasi bitti ---");
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("\n=== I2C PIN BULUCU ===");
  pinleriTara();
  ciftleriDene();
  Serial.println("\n=== bitti ===");
}

void loop() {
  delay(5000);
  Serial.println("bosta");
}
