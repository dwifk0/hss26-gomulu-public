#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
"""Tespit ANINDA kareyi kaydeder — kanit toplama.

Surekli kare alir, modele verir; bir sey bulursa kutuyu cizip diske yazar.
Boylece "model neye baktiginda ne dedi" sorusu tahminle degil kanitla cevaplanir.
"""
import sys
import time
import urllib.request

import cv2
import numpy as np

sys.path.insert(0, "/hss_workspace/scripts")
from onnx_tespit import OnnxYolo

SURE = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
ESIK = 0.25
KLASOR = "/hss_workspace/modeller/yakalanan"
import os
os.makedirs(KLASOR, exist_ok=True)
for f in os.listdir(KLASOR):
    os.remove(os.path.join(KLASOR, f))

etiket = [l.strip() for l in open("/hss_workspace/modeller/siniflar.txt")
          if l.strip() and not l.startswith("#")]
y = OnnxYolo("/hss_workspace/modeller/migfer_v2_best.onnx", etiket)
print("yakalayici basladi, sure", SURE, "sn", flush=True)

no, bitis, bakilan = 0, time.time() + SURE, 0
while time.time() < bitis and no < 12:
    try:
        ham = urllib.request.urlopen("http://127.0.0.1:8080/kare.jpg", timeout=10).read()
        kare = cv2.imdecode(np.frombuffer(ham, np.uint8), cv2.IMREAD_COLOR)
        if kare is None:
            continue
    except Exception as ex:
        print("kare alinamadi:", ex, flush=True)
        time.sleep(1)
        continue
    bakilan += 1
    r, _ = y.calistir(kare, ESIK)
    if not r:
        continue
    no += 1
    print(f"[{no}] {[(t['sinif'], t['guven'], t['kutu']) for t in r]}", flush=True)
    for t in r:
        x1, y1, x2, y2 = t["kutu"]
        cv2.rectangle(kare, (x1, y1), (x2, y2), (0, 0, 255), 4)
        cv2.putText(kare, "%s %.2f" % (t["sinif"], t["guven"]),
                    (x1, max(34, y1 - 12)), cv2.FONT_HERSHEY_SIMPLEX, 1.3, (0, 0, 255), 3)
    cv2.imwrite("%s/%02d.jpg" % (KLASOR, no), kare)

print(f"bitti — {bakilan} kare bakildi, {no} tespitli kare kaydedildi", flush=True)
