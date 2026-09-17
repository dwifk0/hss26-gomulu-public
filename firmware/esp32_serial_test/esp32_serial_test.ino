// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// ESP32 <-> Pi seri haberlesme testi
unsigned long sonHeartbeat = 0;
unsigned long sayac = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32 HAZIR - seri test basladi");
}

void loop() {
  if (Serial.available()) {
    String gelen = Serial.readStringUntil('\n');
    gelen.trim();
    Serial.print("ECHO: ");
    Serial.println(gelen);
  }

  if (millis() - sonHeartbeat >= 1000) {
    sonHeartbeat = millis();
    sayac++;
    Serial.print("HEARTBEAT ");
    Serial.println(sayac);
  }
}
