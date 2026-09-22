// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// IMU kararlilik + paylasilan I2C hatti guvenilirlik testi
// 100 Hz'de hem MPU6500 hem AS5600 okunur; hata sayisi, ortalama ve
// standart sapma raporlanir. Sensor SABIT dursun.

#include <Wire.h>
#include <math.h>

#define SDA0 8
#define SCL0 9
#define IMU_ADR    0x68
#define AS5600_ADR 0x36

const int HZ = 100;
const int SANIYE = 20;
const int ORNEK = HZ * SANIYE;

struct Ist {
  double top = 0, topKare = 0;
  double enAz = 1e9, enCok = -1e9;
  long n = 0;
  void ekle(double v) {
    top += v; topKare += v * v; n++;
    if (v < enAz) enAz = v;
    if (v > enCok) enCok = v;
  }
  double ort() const { return n ? top / n : 0; }
  double std() const {
    if (n < 2) return 0;
    double m = ort();
    double v = topKare / n - m * m;
    return v > 0 ? sqrt(v) : 0;
  }
};

Ist sAx, sAy, sAz, sG, sGx, sGy, sGz, sSic, sAci;
long imuHata = 0, encHata = 0, encOkuma = 0;

bool imuOku(float &ax, float &ay, float &az, float &gx, float &gy, float &gz, float &sic) {
  Wire.beginTransmission(IMU_ADR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(IMU_ADR, 14) != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; i++) v[i] = (Wire.read() << 8) | Wire.read();
  ax = v[0] / 16384.0; ay = v[1] / 16384.0; az = v[2] / 16384.0;
  sic = v[3] / 333.87 + 21.0;
  gx = v[4] / 131.0;   gy = v[5] / 131.0;   gz = v[6] / 131.0;
  return true;
}

bool encOku(float &aci) {
  Wire.beginTransmission(AS5600_ADR);
  Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(AS5600_ADR, 2) != 2) return false;
  uint16_t h = ((uint16_t)Wire.read() << 8) | Wire.read();
  aci = (h & 0x0FFF) * 360.0 / 4096.0;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Wire.begin(SDA0, SCL0);
  Wire.setClock(400000);

  Serial.println("\n=== IMU KARARLILIK TESTI ===");
  Serial.printf("%d Hz x %d sn = %d ornek, I2C 400 kHz, paylasilan hat\n", HZ, SANIYE, ORNEK);

  // uyandir
  Wire.beginTransmission(IMU_ADR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission();
  delay(100);

  Serial.println("SENSORU SABIT TUT - olcum basliyor\n");
  uint32_t periyot = 1000000UL / HZ;
  uint32_t sonrakiRapor = 0;

  for (int i = 0; i < ORNEK; i++) {
    uint32_t t0 = micros();
    float ax, ay, az, gx, gy, gz, sic, aci;

    if (imuOku(ax, ay, az, gx, gy, gz, sic)) {
      sAx.ekle(ax); sAy.ekle(ay); sAz.ekle(az);
      sG.ekle(sqrt(ax * ax + ay * ay + az * az));
      sGx.ekle(gx); sGy.ekle(gy); sGz.ekle(gz);
      sSic.ekle(sic);
    } else imuHata++;

    encOkuma++;
    if (encOku(aci)) sAci.ekle(aci); else encHata++;

    if (i / HZ >= (int)sonrakiRapor && i % HZ == 0) {
      Serial.printf("  %2lu sn: |g|=%.3f  gz=%+.2f dps  aci=%.2f  hata: imu=%ld enc=%ld\n",
                    (unsigned long)sonrakiRapor, sG.n ? sG.ort() : 0,
                    sGz.n ? sGz.ort() : 0, sAci.n ? sAci.ort() : 0, imuHata, encHata);
      sonrakiRapor += 5;
    }
    while (micros() - t0 < periyot) { }
  }

  Serial.println("\n--- SONUC ---");
  Serial.printf("okuma: %d denendi | IMU hata %ld (%%%.2f) | AS5600 hata %ld (%%%.2f)\n",
                ORNEK, imuHata, 100.0 * imuHata / ORNEK, encHata, 100.0 * encHata / encOkuma);

  Serial.println("\nivme (g):        ortalama    std      min      max");
  Serial.printf("  ax           %9.4f %8.4f %8.4f %8.4f\n", sAx.ort(), sAx.std(), sAx.enAz, sAx.enCok);
  Serial.printf("  ay           %9.4f %8.4f %8.4f %8.4f\n", sAy.ort(), sAy.std(), sAy.enAz, sAy.enCok);
  Serial.printf("  az           %9.4f %8.4f %8.4f %8.4f\n", sAz.ort(), sAz.std(), sAz.enAz, sAz.enCok);
  Serial.printf("  |vektor|     %9.4f %8.4f %8.4f %8.4f\n", sG.ort(), sG.std(), sG.enAz, sG.enCok);

  Serial.println("\njiro (derece/sn): ortalama    std      min      max     <- sabitken 0 olmali");
  Serial.printf("  gx           %9.4f %8.4f %8.4f %8.4f\n", sGx.ort(), sGx.std(), sGx.enAz, sGx.enCok);
  Serial.printf("  gy           %9.4f %8.4f %8.4f %8.4f\n", sGy.ort(), sGy.std(), sGy.enAz, sGy.enCok);
  Serial.printf("  gz           %9.4f %8.4f %8.4f %8.4f\n", sGz.ort(), sGz.std(), sGz.enAz, sGz.enCok);

  Serial.printf("\nsicaklik: %.1f C   enkoder acisi std: %.3f derece\n", sSic.ort(), sAci.std());

  Serial.println("\n--- yorum ---");
  if (imuHata == 0 && encHata == 0)
    Serial.println("  I2C: tek hata yok, paylasilan hat guvenilir.");
  else
    Serial.println("  I2C: HATA VAR - pull-up, kablo boyu ya da hiz sorunu.");

  double gn = sG.std();
  if (gn < 0.01)       Serial.printf("  ivme gurultusu %.4f g - cok iyi\n", gn);
  else if (gn < 0.03)  Serial.printf("  ivme gurultusu %.4f g - normal\n", gn);
  else                 Serial.printf("  ivme gurultusu %.4f g - YUKSEK, titresim ya da besleme gurultusu\n", gn);

  double gsapma = fabs(sG.ort() - 1.0);
  if (gsapma > 0.05) Serial.printf("  |g| = %.3f, 1.000'den %.3f sapiyor -> olcek kalibrasyonu gerekir\n", sG.ort(), gsapma);

  double jb = fmax(fabs(sGx.ort()), fmax(fabs(sGy.ort()), fabs(sGz.ort())));
  Serial.printf("  jiro sapmasi (bias) en buyuk %.3f dps -> her aciliste kalibre et\n", jb);
  if (jb > 5) Serial.println("  UYARI: bias cok buyuk, sensor olcum sirasinda hareket etmis olabilir");

  Serial.println("\n=== bitti ===");
}

void loop() { delay(10000); }
