// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// ESP32-S3 ARM role test (FE_SRly, LOW-trigger)
// Boot'ta role KAPALI (guvenli). Seri: ARM / DISARM
#define ROLE_PIN 18
#define ROLE_KAPALI HIGH   // LOW-trigger: HIGH = role birakildi
#define ROLE_ACIK   LOW    // LOW = role cekti

void setup() {
  digitalWrite(ROLE_PIN, ROLE_KAPALI);
  pinMode(ROLE_PIN, OUTPUT);
  digitalWrite(ROLE_PIN, ROLE_KAPALI);
  Serial.begin(115200);
  delay(500);
  Serial.println("ROLE TEST HAZIR - baslangic: KAPALI");
  Serial.println("Komut: ARM / DISARM");
}

void loop() {
  if (Serial.available()) {
    String k = Serial.readStringUntil('\n');
    k.trim();
    k.toUpperCase();
    if (k == "ARM") {
      digitalWrite(ROLE_PIN, ROLE_ACIK);
      Serial.println(">> ARM - role CEKTI (acik)");
    } else if (k == "DISARM") {
      digitalWrite(ROLE_PIN, ROLE_KAPALI);
      Serial.println(">> DISARM - role BIRAKTI (kapali)");
    } else {
      Serial.print("Bilinmeyen: ");
      Serial.println(k);
    }
  }
}
