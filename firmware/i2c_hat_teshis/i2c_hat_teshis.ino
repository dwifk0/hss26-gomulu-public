// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// I2C hat teshisi - kablo/pull-up sorununu adres taramasindan once ayirir
// Cihaz bulunamadiginda ilk calistirilacak sketch.

#include <Wire.h>

struct Hat { const char *ad; int sda; int scl; };
Hat hatlar[] = { {"I2C0", 8, 9}, {"I2C1", 10, 11} };

void seviyeOlc(Hat h) {
  // 1) tamamen serbest birak - disaridan pull-up var mi?
  pinMode(h.sda, INPUT);
  pinMode(h.scl, INPUT);
  delay(5);
  int sdaSerbest = digitalRead(h.sda);
  int sclSerbest = digitalRead(h.scl);

  // 2) dahili pull-up ile - hat GND'ye mi cekili?
  pinMode(h.sda, INPUT_PULLUP);
  pinMode(h.scl, INPUT_PULLUP);
  delay(5);
  int sdaPull = digitalRead(h.sda);
  int sclPull = digitalRead(h.scl);

  Serial.printf("\n%s (SDA=GPIO%d, SCL=GPIO%d)\n", h.ad, h.sda, h.scl);
  Serial.printf("  pull-upsuz : SDA=%d SCL=%d\n", sdaSerbest, sclSerbest);
  Serial.printf("  dahili p-up: SDA=%d SCL=%d\n", sdaPull, sclPull);

  if (sdaSerbest && sclSerbest) {
    Serial.println("  -> DIS PULL-UP VAR, hat saglikli. Sorun adres/besleme tarafinda.");
  } else if (sdaPull && sclPull) {
    Serial.println("  -> dis pull-up YOK ama hat serbest. Modul pull-uplari sokulmus ya da");
    Serial.println("     modulun VCC'si bagli degil. Dahili pull-up ile calisabilir.");
  } else {
    Serial.printf("  -> HAT GND'YE CEKILI (SDA=%d SCL=%d dahili pull-up ile bile LOW).\n", sdaPull, sclPull);
    Serial.println("     Kisa devre, ters takilmis modul ya da beslemesiz cihaz.");
  }
}

int tara(TwoWire &w, const char *ad) {
  int n = 0;
  Serial.printf("  %s tarama:", ad);
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  if (!n) Serial.print(" (bos)");
  Serial.println();
  return n;
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("\n=== I2C HAT TESHISI ===");

  for (Hat h : hatlar) seviyeOlc(h);

  Serial.println("\n--- dahili pull-up ile tarama ---");
  Wire.begin(8, 9);    Wire.setClock(50000);
  Wire1.begin(10, 11); Wire1.setClock(50000);
  delay(50);
  tara(Wire,  "I2C0");
  tara(Wire1, "I2C1");

  // hatlari capraz dene - SDA/SCL ters takilmis olabilir
  Serial.println("\n--- SDA/SCL TERS varsayimiyla tarama ---");
  Wire.end(); Wire1.end();
  delay(20);
  Wire.begin(9, 8);    Wire.setClock(50000);
  Wire1.begin(11, 10); Wire1.setClock(50000);
  delay(50);
  tara(Wire,  "I2C0-ters");
  tara(Wire1, "I2C1-ters");

  Serial.println("\n=== bitti ===");
}

void loop() {
  delay(5000);
  Serial.println("bosta");
}
