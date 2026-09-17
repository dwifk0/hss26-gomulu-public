// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// Cift I2C hatti tarama + AS5600 / IMU teshisi - ESP32-S3
// I2C0 (Wire ) = GPIO8 SDA / GPIO9 SCL  -> AS5600 pan (0x36), INA226 (0x40), IMU (0x68)
// I2C1 (Wire1) = GPIO10 SDA / GPIO11 SCL -> AS5600 tilt (0x36)

#include <Wire.h>

#define SDA0 8
#define SCL0 9
#define SDA1 10
#define SCL1 11

#define AS5600 0x36
#define IMU    0x68
#define INA226 0x40

const char *tanim(uint8_t a) {
  switch (a) {
    case 0x36: return "AS5600 enkoder";
    case 0x40: return "INA226 akim/gerilim";
    case 0x68: return "IMU (MPU6500/9250)";
    case 0x69: return "IMU (AD0=1)";
    case 0x0C: return "AK8963 manyetometre";
    case 0x48: return "ADS1115";
    default:   return "?";
  }
}

int tara(TwoWire &w, const char *ad) {
  Serial.printf("\n--- %s taramasi ---\n", ad);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) {
      Serial.printf("  0x%02X  %s\n", a, tanim(a));
      n++;
    }
  }
  if (n == 0) Serial.println("  HIC CIHAZ YOK");
  return n;
}

bool reg8(TwoWire &w, uint8_t adr, uint8_t reg, uint8_t &val) {
  w.beginTransmission(adr);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((int)adr, 1) != 1) return false;
  val = w.read();
  return true;
}

bool reg16(TwoWire &w, uint8_t adr, uint8_t reg, uint16_t &val) {
  w.beginTransmission(adr);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((int)adr, 2) != 2) return false;
  val = ((uint16_t)w.read() << 8) | w.read();
  return true;
}

void as5600Teshis(TwoWire &w, const char *ad) {
  uint8_t st, agc;
  uint16_t ham;
  if (!reg8(w, AS5600, 0x0B, st)) { Serial.printf("%s: STATUS okunamadi\n", ad); return; }
  reg8(w, AS5600, 0x1A, agc);
  reg16(w, AS5600, 0x0C, ham);
  ham &= 0x0FFF;

  bool md = st & 0x20;   // miknatis algilandi
  bool ml = st & 0x10;   // cok zayif / uzak
  bool mh = st & 0x08;   // cok guclu / yakin

  Serial.printf("%s: STATUS=0x%02X  MD=%d ML=%d MH=%d | AGC=%3u | RAW=%4u (%6.2f derece)\n",
                ad, st, md, ml, mh, agc, ham, ham * 360.0 / 4096.0);
  if (!md)      Serial.printf("   -> MIKNATIS YOK ya da cok uzak\n");
  else if (ml)  Serial.printf("   -> miknatis ZAYIF: hava araligini azalt (hedef 0.5-2 mm)\n");
  else if (mh)  Serial.printf("   -> miknatis COK YAKIN: hava araligini artir\n");
  else if (agc < 32 || agc > 224)
                Serial.printf("   -> AGC uc bantta, hava araligi sinirda\n");
  else          Serial.printf("   -> OK\n");
}

void imuTeshis(TwoWire &w, uint8_t adr) {
  uint8_t who;
  if (!reg8(w, adr, 0x75, who)) { Serial.println("IMU: WHO_AM_I okunamadi"); return; }
  Serial.printf("IMU adresi: 0x%02X\n", adr);
  const char *m = "bilinmeyen";
  if (who == 0x71) m = "MPU9250";
  else if (who == 0x73) m = "MPU9255";
  else if (who == 0x70) m = "MPU6500";
  else if (who == 0x68) m = "MPU6050";
  Serial.printf("IMU: WHO_AM_I=0x%02X -> %s\n", who, m);

  // uyandir ve bir olcum al
  w.beginTransmission(adr); w.write(0x6B); w.write(0x00); w.endTransmission();
  delay(100);
  w.beginTransmission(adr); w.write(0x3B); w.endTransmission(false);
  if (w.requestFrom(adr, 6) == 6) {
    int16_t x = (w.read() << 8) | w.read();
    int16_t y = (w.read() << 8) | w.read();
    int16_t z = (w.read() << 8) | w.read();
    float ax = x / 16384.0, ay = y / 16384.0, az = z / 16384.0;
    float bg = sqrt(ax * ax + ay * ay + az * az);
    Serial.printf("IMU: ax=%+.2f ay=%+.2f az=%+.2f g  |vektor|=%.2f g", ax, ay, az, bg);
    Serial.println((bg > 0.85 && bg < 1.15) ? "  -> OK (1g bekleniyor)" : "  -> SUPHELI");
  }
}

void setup() {
  Serial.begin(115200);
  delay(5000);

  Serial.println("\n=== HSS cift I2C hatti taramasi ===");
  Serial.printf("I2C0: SDA=GPIO%d SCL=GPIO%d | I2C1: SDA=GPIO%d SCL=GPIO%d\n", SDA0, SCL0, SDA1, SCL1);

  Wire.begin(SDA0, SCL0);
  Wire1.begin(SDA1, SCL1);
  Wire.setClock(100000);
  Wire1.setClock(100000);

  int n0 = tara(Wire,  "I2C0 (GPIO8/9)");
  int n1 = tara(Wire1, "I2C1 (GPIO10/11)");

  Serial.println("\n--- teshis ---");
  bool pan = false, tilt = false;
  Wire.beginTransmission(AS5600);  pan  = (Wire.endTransmission()  == 0);
  Wire1.beginTransmission(AS5600); tilt = (Wire1.endTransmission() == 0);

  if (pan)  as5600Teshis(Wire,  "AS5600 pan  (I2C0)");
  else      Serial.println("AS5600 pan  (I2C0): YOK");
  if (tilt) as5600Teshis(Wire1, "AS5600 tilt (I2C1)");
  else      Serial.println("AS5600 tilt (I2C1): YOK");

  bool imuBulundu = false;
  TwoWire *hatlar[2] = {&Wire, &Wire1};
  const char *hatAd[2] = {"I2C0", "I2C1"};
  for (int h = 0; h < 2; h++) {
    for (uint8_t adr : {0x68, 0x69}) {
      hatlar[h]->beginTransmission(adr);
      if (hatlar[h]->endTransmission() == 0) {
        Serial.printf("IMU %s hattinda:\n", hatAd[h]);
        imuTeshis(*hatlar[h], adr);
        imuBulundu = true;
      }
    }
  }
  if (!imuBulundu) Serial.println("IMU: iki hatta da 0x68/0x69 yok");

  Serial.printf("\nozet: I2C0=%d cihaz, I2C1=%d cihaz\n", n0, n1);
  Serial.println("=== simdi surekli aci okumasi (miknatisi cevir) ===");
}

void loop() {
  uint16_t a0 = 0xFFFF, a1 = 0xFFFF;
  bool p = reg16(Wire,  AS5600, 0x0C, a0);
  bool t = reg16(Wire1, AS5600, 0x0C, a1);
  a0 &= 0x0FFF; a1 &= 0x0FFF;

  Serial.printf("pan: %s   tilt: %s\n",
    p ? String(a0 * 360.0 / 4096.0, 2).c_str() : "---",
    t ? String(a1 * 360.0 / 4096.0, 2).c_str() : "---");
  delay(500);
}
