// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// INA226 akim/gerilim olcumu - ESP32-S3, I2C0 (GPIO8 SDA / GPIO9 SCL), adres 0x40
//
// Amac: ENA bagli olmadigi icin surekli tutma akimi ceken iki NEMA23'un
// hareketsiz akimini olcmek. Bkz. hss-tb6600-dogrulama.
//
// Akim, kalibrasyon registeri KULLANILMADAN dogrudan shunt geriliminden
// hesaplanir (I = Vshunt / Rshunt). Boylece modulun shunt degeri yanlis
// bilinse bile ham mV degeri ekranda gorunur ve sonradan duzeltilebilir.
//
// DIKKAT: shunt ADC araligi +-81.92 mV. Eldeki modul R100 = 0.1 ohm, yani
// yalnizca +-0.819 A olcebiliyor ve 5-6 A'da 3 W harcayip yanar. Uzerine
// paralel 0.01 ohm lehimlenecek -> etkin 0.00909 ohm, tepe +-9.0 A.
//
// Gercek etkin direnci bilmek icin: seri hatta bilinen bir akim gecirip
// multimetreyle olc, sonra R = Vshunt / I. Degeri yeniden derlemeden
// girebilmek icin seri porta "r <ohm>" yaz (ornek: r 0.00909).

#include <Wire.h>

#define SDA0 8
#define SCL0 9
#define INA_ADDR 0x40

// Modul uzerindeki etkin shunt direnci (ohm). Calisirken "r <ohm>" ile degisir.
#define R_SHUNT_VARSAYILAN 0.1

float rShunt = R_SHUNT_VARSAYILAN;

#define REG_CONFIG 0x00
#define REG_SHUNT  0x01
#define REG_BUS    0x02
#define REG_MFG_ID 0xFE
#define REG_DIE_ID 0xFF

// AVG=128, VBUSCT=1.1ms, VSHCT=1.1ms, surekli shunt+bus
#define CONFIG_VAL 0x4927

// shunt ADC tam olcek: +-32767 * 2.5 uV = +-81.9175 mV
#define SHUNT_FS_MV 81.9175

bool yaz16(uint8_t reg, uint16_t v) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  Wire.write(v >> 8);
  Wire.write(v & 0xFF);
  return Wire.endTransmission() == 0;
}

bool oku16(uint8_t reg, uint16_t &out) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)INA_ADDR, (uint8_t)2) != 2) return false;
  out = (Wire.read() << 8) | Wire.read();
  return true;
}

void yazShunt() {
  Serial.printf("Shunt: %.5f ohm  (olculebilir tepe: +-%.2f A, cozunurluk %.0f uA)\n",
                rShunt, (SHUNT_FS_MV / 1000.0) / rShunt, 2.5e-6 / rShunt * 1e6);
}

// Hareket sirasindaki tepe akimi yakalamak icin
float akimMin = 1e9, akimMax = -1e9;

void komutIsle() {
  if (!Serial.available()) return;
  String k = Serial.readStringUntil('\n');
  k.trim();
  if (k.startsWith("r ")) {
    float v = k.substring(2).toFloat();
    if (v > 1e-6) {
      rShunt = v;
      akimMin = 1e9; akimMax = -1e9;
      yazShunt();
    } else {
      Serial.println("gecersiz deger");
    }
  } else if (k == "s") {
    akimMin = 1e9; akimMax = -1e9;
    Serial.println("tepe degerleri sifirlandi");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== INA226 AKIM OLCUMU ===");
  Serial.printf("I2C0: SDA=GPIO%d SCL=GPIO%d | adres 0x%02X\n", SDA0, SCL0, INA_ADDR);
  yazShunt();
  Serial.println("Komutlar: 'r <ohm>' shunt degeri, 's' tepe degerleri sifirla");

  Wire.begin(SDA0, SCL0, 400000);

  uint16_t mfg = 0, die = 0;
  if (!oku16(REG_MFG_ID, mfg) || !oku16(REG_DIE_ID, die)) {
    Serial.println("HATA: 0x40 cevap vermiyor - kablolamayi kontrol et.");
    return;
  }
  Serial.printf("Uretici ID = 0x%04X (INA226 icin 0x5449 beklenir)\n", mfg);
  Serial.printf("Die ID     = 0x%04X (INA226 icin 0x2260 beklenir)\n", die);
  if (die == 0x2260) Serial.println("  -> cip dogrulandi: INA226");
  else if (mfg == 0x5449) Serial.println("  -> TI cipi ama INA226 degil (INA219/INA260 olabilir)");
  else Serial.println("  -> UYARI: bu bir INA226 gibi gorunmuyor");

  yaz16(REG_CONFIG, CONFIG_VAL);
  uint16_t cfg = 0;
  oku16(REG_CONFIG, cfg);
  Serial.printf("Config = 0x%04X (yazilan 0x%04X)\n\n", cfg, CONFIG_VAL);

  Serial.println("\nbus_V\tshunt_mV\takim_A\ttepe_A\tguc_W\tdurum");
}

void loop() {
  komutIsle();

  uint16_t hamShunt = 0, hamBus = 0;
  if (!oku16(REG_SHUNT, hamShunt) || !oku16(REG_BUS, hamBus)) {
    Serial.println("okuma hatasi");
    delay(500);
    return;
  }

  int16_t sh = (int16_t)hamShunt;              // isaretli
  float shunt_mV = sh * 0.0025;                // 2.5 uV/LSB
  float bus_V = hamBus * 0.00125;              // 1.25 mV/LSB
  float akim_A = (shunt_mV / 1000.0) / rShunt;
  float guc_W = bus_V * akim_A;

  if (akim_A < akimMin) akimMin = akim_A;
  if (akim_A > akimMax) akimMax = akim_A;

  const char *durum = "";
  if (fabs(shunt_mV) > SHUNT_FS_MV * 0.95) durum = "<<< DOYUM! shunt cok buyuk ya da akim olcek disi";
  else if (fabs(shunt_mV) < 0.05)          durum = "(shunt gerilimi ~0 - yuk yok mu?)";

  Serial.printf("%.3f\t%+.3f\t\t%+.3f\t%+.3f\t%.2f\t%s\n",
                bus_V, shunt_mV, akim_A, akimMax, guc_W, durum);
  delay(500);
}
