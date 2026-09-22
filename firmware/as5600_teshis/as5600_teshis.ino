// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// Tek AS5600 modulu icin derin teshis - GPIO8/9
// Hat gerilimi (ADC), pull-up toparlanma suresi, yavas bit-bang I2C taramasi.

#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9

// ---- bit-bang I2C, ~10 kHz ----
#define YARIM 50   // us

static inline void sdaBirak() { pinMode(SDA_PIN, INPUT); }
static inline void sdaCek()   { pinMode(SDA_PIN, OUTPUT); digitalWrite(SDA_PIN, LOW); }
static inline void sclBirak() { pinMode(SCL_PIN, INPUT); }
static inline void sclCek()   { pinMode(SCL_PIN, OUTPUT); digitalWrite(SCL_PIN, LOW); }

void bbStart() { sdaBirak(); sclBirak(); delayMicroseconds(YARIM);
                 sdaCek(); delayMicroseconds(YARIM); sclCek(); delayMicroseconds(YARIM); }
void bbStop()  { sdaCek(); delayMicroseconds(YARIM); sclBirak(); delayMicroseconds(YARIM);
                 sdaBirak(); delayMicroseconds(YARIM); }

// bir bayt yaz, ACK dondur (0 = ACK geldi)
int bbYaz(uint8_t v) {
  for (int i = 0; i < 8; i++) {
    if (v & 0x80) sdaBirak(); else sdaCek();
    v <<= 1;
    delayMicroseconds(YARIM);
    sclBirak(); delayMicroseconds(YARIM);
    sclCek();   delayMicroseconds(YARIM);
  }
  sdaBirak();                       // ACK bitinde hatti birak
  delayMicroseconds(YARIM);
  sclBirak(); delayMicroseconds(YARIM);
  int ack = digitalRead(SDA_PIN);   // 0 = cihaz cekti = ACK
  sclCek(); delayMicroseconds(YARIM);
  return ack;
}

void hatGerilimi() {
  Serial.println("\n--- hat gerilimi (bosta, pull-up ile) ---");
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  delay(10);
  analogSetAttenuation(ADC_11db);          // ~0-3.1 V okuma araligi
  int mvSda = analogReadMilliVolts(SDA_PIN);
  int mvScl = analogReadMilliVolts(SCL_PIN);
  Serial.printf("  SDA(GPIO8) = %d mV\n", mvSda);
  Serial.printf("  SCL(GPIO9) = %d mV\n", mvScl);
  if (mvSda > 2900 && mvScl > 2900)
    Serial.println("  -> 3V3 seviyesinde, besleme normal");
  else if (mvSda < 500 || mvScl < 500)
    Serial.println("  -> COK DUSUK: hat GND'ye cekili veya pull-up yok");
  else
    Serial.println("  -> ARA SEVIYE: besleme dusuk ya da hatta yuk var (SUPHELI)");
}

void toparlanma() {
  Serial.println("\n--- pull-up toparlanma suresi ---");
  const int p[2] = {SDA_PIN, SCL_PIN};
  const char *ad[2] = {"SDA", "SCL"};
  for (int k = 0; k < 2; k++) {
    pinMode(p[k], OUTPUT); digitalWrite(p[k], LOW);
    delayMicroseconds(200);
    int alcak = digitalRead(p[k]);
    pinMode(p[k], INPUT);
    uint32_t t0 = micros();
    while (digitalRead(p[k]) == 0 && micros() - t0 < 5000) { }
    uint32_t dt = micros() - t0;
    Serial.printf("  %s: LOW surulunce %d okundu, birakinca %lu us'de HIGH\n",
                  ad[k], alcak, (unsigned long)dt);
    if (alcak != 0)  Serial.printf("     -> %s LOW'a CEKILEMIYOR: hat bir yere kisa devre\n", ad[k]);
    else if (dt > 100) Serial.printf("     -> yavas yukselis: pull-up cok zayif ya da kablo cok uzun\n");
  }
}

void bbTarama() {
  Serial.println("\n--- bit-bang I2C taramasi (10 kHz) ---");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    bbStart();
    int ack = bbYaz(a << 1);   // yazma adresi
    bbStop();
    if (ack == 0) { Serial.printf("  ACK: 0x%02X\n", a); n++; }
    delayMicroseconds(200);
  }
  if (!n) Serial.println("  hicbir adresten ACK yok");
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("\n=== AS5600 TEK MODUL TESHISI (GPIO8=SDA, GPIO9=SCL) ===");

  hatGerilimi();
  toparlanma();
  bbTarama();

  Serial.println("\n--- donanim I2C, 10 kHz ---");
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(10000);
  delay(20);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  bulundu 0x%02X\n", a); n++; }
  }
  if (!n) Serial.println("  bos");

  Serial.println("\n=== bitti ===");
}

void loop() { delay(5000); Serial.println("bosta"); }
