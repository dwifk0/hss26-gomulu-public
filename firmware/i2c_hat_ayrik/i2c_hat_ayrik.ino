// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// SDA ve SCL gercekten ayri hatlar mi, yoksa ayni nete mi bagli?
// Ayni netteyse (ornegin ikisi de modulun VCC'sine gitmisse) bulgular
// "saglikli pull-up" gibi gorunur ama I2C asla calismaz.

#define A 8    // SDA olmasi gereken
#define B 9    // SCL olmasi gereken

int capraz(int suren, int dinleyen, const char *ad) {
  pinMode(dinleyen, INPUT);
  pinMode(suren, OUTPUT); digitalWrite(suren, LOW);
  delayMicroseconds(500);
  int v = digitalRead(dinleyen);
  pinMode(suren, INPUT);
  delayMicroseconds(500);
  int geri = digitalRead(dinleyen);
  Serial.printf("  GPIO%d LOW surulurken GPIO%d = %d  (birakinca %d)  %s\n",
                suren, dinleyen, v, geri, ad);
  return v;
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("\n=== SDA/SCL AYRIK MI TESTI ===");

  int a = capraz(A, B, "");
  int b = capraz(B, A, "");

  Serial.println();
  if (a == 0 || b == 0) {
    Serial.println("SONUC: GPIO8 ve GPIO9 AYNI NETE BAGLI.");
    Serial.println("  Iki kablo da modulun ayni pinine (buyuk ihtimalle VCC'ye) gitmis,");
    Serial.println("  ya da modulde SDA-SCL kisa devre. I2C bu sekilde asla calismaz.");
    Serial.println("  -> Modul uzerindeki SDA ve SCL bacaklarini tek tek dogrula.");
  } else {
    Serial.println("SONUC: hatlar AYRIK, pull-up'lar bagimsiz.");
    Serial.println("  Kablolama SDA/SCL olarak dogru gorunuyor ve VCC 3V3.");
    Serial.println("  Geriye iki ihtimal kaliyor:");
    Serial.println("   1) Modulun GND'si ESP32 GND'sine bagli degil (en olasi)");
    Serial.println("   2) Cip olmus - daha once 5V verildiyse AS5600 (max 3.6V) yanar");
  }
  Serial.println("\n=== bitti ===");
}

void loop() { delay(5000); Serial.println("bosta"); }
