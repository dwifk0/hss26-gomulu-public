// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// TB6600 Step Motor - ESP32 - ileri/geri test (a1 bazli)
#define PUL 25
#define DIR 26

void setup() {
  pinMode(PUL, OUTPUT);
  pinMode(DIR, OUTPUT);
}

void loop() {
  digitalWrite(DIR, HIGH);
  for (int i = 0; i < 3200; i++) {
    digitalWrite(PUL, HIGH);
    delayMicroseconds(500);
    digitalWrite(PUL, LOW);
    delayMicroseconds(500);
  }
  delay(1000);

  digitalWrite(DIR, LOW);
  for (int i = 0; i < 3200; i++) {
    digitalWrite(PUL, HIGH);
    delayMicroseconds(500);
    digitalWrite(PUL, LOW);
    delayMicroseconds(500);
  }
  delay(1000);
}
