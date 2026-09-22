// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// ESP32-S3 I2C tarama - INA226 (0x40) + MPU9250 (0x68) ara
#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("\nI2C TARAMA BASLADI (SDA=8, SCL=9)");
}

void loop() {
  byte bulunan = 0;
  Serial.println("Taraniyor...");
  for (byte adres = 1; adres < 127; adres++) {
    Wire.beginTransmission(adres);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Cihaz bulundu: 0x");
      if (adres < 16) Serial.print("0");
      Serial.println(adres, HEX);
      bulunan++;
    }
  }
  if (bulunan == 0) Serial.println("  Hicbir cihaz yok!");
  else Serial.print("  Toplam: "), Serial.println(bulunan);
  Serial.println("---");
  delay(3000);
}
