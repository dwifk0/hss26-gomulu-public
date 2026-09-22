// SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
// Copyright (C) 2026 Ahmet Efe Nezli
// Pull-up direncini ESP32'nin dahili pull-down'u (nominal ~45k) ile olcer:
//   V = 3.3 * Rpd / (Rpu + Rpd)  ->  Rpu = Rpd * (3.3 - V) / V
// Ayrica bir pini asagi cekerken DIGERINI de okur: besleme mi cokuyor,
// yoksa hatlar mi birbirine bagli, onu ayirir.

#define A 10
#define B 11
#define RPD 45000.0
#define VCC 3.30

void hepsiSerbest() { pinMode(A, INPUT); pinMode(B, INPUT); delay(5); }

float mv(int p) { return analogReadMilliVolts(p); }

void olc(int hedef, int oteki, const char *adH, const char *adO) {
  hepsiSerbest();
  pinMode(oteki, INPUT);
  pinMode(hedef, INPUT_PULLDOWN);
  delay(10);
  float vh = mv(hedef), vo = mv(oteki);
  float rpu = (vh > 10) ? RPD * (VCC * 1000.0 - vh) / vh : -1;
  hepsiSerbest();

  Serial.printf("\n%s dahili pull-down ile:\n", adH);
  Serial.printf("  %s = %.0f mV -> pull-up ~%.1f k\n", adH, vh, rpu / 1000.0);
  Serial.printf("  %s = %.0f mV (bu sirada)\n", adO, vo);
  if (vo < 2900)
    Serial.printf("  -> %s de dustu: ortak dugum/besleme etkileniyor\n", adO);
  else
    Serial.printf("  -> %s etkilenmedi: hatlar bagimsiz\n", adO);
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  analogSetAttenuation(ADC_11db);
  Serial.println("\n=== PULL-UP DEGERI VE KUPLAJ ===");
  Serial.println("(ADC 11db ~3167 mV'ta doyuyor, ustu ayirt edilemez)");

  hepsiSerbest();
  Serial.printf("\nbosta: GPIO10 = %.0f mV, GPIO11 = %.0f mV\n", mv(A), mv(B));

  olc(A, B, "GPIO10", "GPIO11");
  olc(B, A, "GPIO11", "GPIO10");

  // ikisini birden asagi cek - besleme tamamen coker mu?
  hepsiSerbest();
  pinMode(A, INPUT_PULLDOWN); pinMode(B, INPUT_PULLDOWN);
  delay(10);
  Serial.printf("\nikisi birden pull-down: GPIO10 = %.0f mV, GPIO11 = %.0f mV\n", mv(A), mv(B));
  hepsiSerbest();

  Serial.println("\n=== bitti ===");
}

void loop() { delay(5000); Serial.println("bosta"); }
