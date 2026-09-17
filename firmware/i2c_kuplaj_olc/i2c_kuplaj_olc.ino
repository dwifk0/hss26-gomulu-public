// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ahmet Efe Nezli
// GPIO8-GPIO9 arasindaki kuplajin CINSINI olcer.
// digitalRead sadece 0/1 der; ADC gercek gerilimi verir ve neden oldugunu ayirir:
//   ~0-150 mV  -> gercek bakir kisa devre (multimetre oterdi)
//   ~500-800mV -> diyot uzerinden (cip ESD diyodu / beslemesiz modul)
//   ~1-2 V     -> direncli kuplaj ya da cokmus besleme

#define A 8
#define B 9

void olc(int suren, int dinleyen) {
  pinMode(dinleyen, INPUT);
  analogSetAttenuation(ADC_11db);
  delay(5);
  int bosta = analogReadMilliVolts(dinleyen);

  pinMode(suren, OUTPUT); digitalWrite(suren, LOW);
  delay(2);
  int surulurken = analogReadMilliVolts(dinleyen);
  int dijital = digitalRead(dinleyen);
  pinMode(suren, INPUT);          // hemen birak, hatti yorma
  delay(5);
  int sonra = analogReadMilliVolts(dinleyen);

  Serial.printf("\nGPIO%d LOW surulurken GPIO%d:\n", suren, dinleyen);
  Serial.printf("  bosta %d mV -> surulurken %d mV (dijital %d) -> birakinca %d mV\n",
                bosta, surulurken, dijital, sonra);

  if (surulurken < 150)
    Serial.println("  -> GERCEK KISA DEVRE (ayni bakir net)");
  else if (surulurken >= 400 && surulurken <= 900)
    Serial.println("  -> DIYOT KUPLAJI: modulun VCC'si gelmiyor, hatlar cipin ESD");
  else if (surulurken > 900)
    Serial.println("  -> direncli/besleme kuplaji, tam kisa devre degil");
  else
    Serial.println("  -> ara deger, belirsiz");
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("\n=== GPIO8/GPIO9 KUPLAJ CINSI ===");

  pinMode(A, INPUT); pinMode(B, INPUT);
  analogSetAttenuation(ADC_11db);
  delay(10);
  Serial.printf("bosta: GPIO8 = %d mV, GPIO9 = %d mV\n",
                analogReadMilliVolts(A), analogReadMilliVolts(B));

  olc(A, B);
  olc(B, A);

  // ucuncu kontrol: birini HIGH surersek digeri takip ediyor mu?
  pinMode(B, INPUT);
  pinMode(A, OUTPUT); digitalWrite(A, HIGH);
  delay(2);
  int hi = analogReadMilliVolts(B);
  pinMode(A, INPUT);
  Serial.printf("\nGPIO8 HIGH surulurken GPIO9 = %d mV\n", hi);

  Serial.println("\n=== bitti ===");
}

void loop() { delay(5000); Serial.println("bosta"); }
