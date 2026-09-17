#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
"""Canli kareyi al, modele ver, kutulari cizip kaydet. Eszamanli teshis."""
import sys
import urllib.request

import cv2
import numpy as np

sys.path.insert(0, "/hss_workspace/scripts")
from onnx_tespit import OnnxYolo

ham = urllib.request.urlopen("http://127.0.0.1:8080/kare.jpg", timeout=15).read()
kare = cv2.imdecode(np.frombuffer(ham, np.uint8), cv2.IMREAD_COLOR)
print("kare boyutu:", kare.shape)

etiket = [l.strip() for l in open("/hss_workspace/modeller/siniflar.txt")
          if l.strip() and not l.startswith("#")]
y = OnnxYolo("/hss_workspace/modeller/migfer_v2_best.onnx", etiket)

for esik in (0.40, 0.25, 0.10):
    r, _ = y.calistir(kare, esik)
    print("esik", esik, "->", [(t["sinif"], t["guven"], t["kutu"]) for t in r])

r, _ = y.calistir(kare, 0.25)
for t in r:
    x1, y1, x2, y2 = t["kutu"]
    cv2.rectangle(kare, (x1, y1), (x2, y2), (0, 0, 255), 4)
    cv2.putText(kare, "%s %.2f" % (t["sinif"], t["guven"]),
                (x1, max(34, y1 - 12)), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 0, 255), 3)
cv2.imwrite("/hss_workspace/modeller/isaretli.jpg", kare)
print("isaretli.jpg kaydedildi,", len(r), "kutu")
