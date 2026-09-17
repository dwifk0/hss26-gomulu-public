// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
#include <Wire.h>
#include <math.h>
#define MPU 0x68

float pitch_f = 0, roll_f = 0;
bool ilk = true;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(8, 9);
  Wire.setClock(10000);
  delay(100);
  Wire.beginTransmission(MPU);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);
  delay(100);
}

void loop() {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  Wire.endTransmission(true);
  delay(2);
  byte n = Wire.requestFrom((int)MPU, (int)6, (int)true);
  if (n != 6) return;

  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();

  // Gecersiz veri kontrolu: az duz dururken ~16000 olmali
  // Toplam ivme buyuklugu ~1g (16384) civari degilse boz
  float mag = sqrt((float)ax*ax + (float)ay*ay + (float)az*az);
  if (mag < 12000 || mag > 20000) return;  // bozuk cerceve, atla

  float axg = ax / 16384.0;
  float ayg = ay / 16384.0;
  float azg = az / 16384.0;
  float pitch = atan2(axg, sqrt(ayg*ayg + azg*azg)) * 180.0 / PI;
  float roll  = atan2(ayg, sqrt(axg*axg + azg*azg)) * 180.0 / PI;

  // Yumusatma (dusuk geciren filtre)
  if (ilk) { pitch_f = pitch; roll_f = roll; ilk = false; }
  pitch_f = 0.8 * pitch_f + 0.2 * pitch;
  roll_f  = 0.8 * roll_f  + 0.2 * roll;

  Serial.print("Pitch: ");
  Serial.print(pitch_f, 1);
  Serial.print("   Roll: ");
  Serial.println(roll_f, 1);
  delay(100);
}
