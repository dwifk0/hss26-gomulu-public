#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
"""hailo_tespit.py cikti ayristirmasi — Hailo donanimi olmadan test.

Sadece saf mantigi sinar: batch ayiklama ve etiket yukleme. Hailo cagrilari
hic calistirilmaz.
"""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from hailo_tespit import HailoYolo, etiket_yukle, COCO


class Sahte(HailoYolo):
    """__init__'i atlayarak yalniz ayristirma mantigini kurar."""
    def __init__(self, sinif_sayisi):
        self.sinif_sayisi = sinif_sayisi
        self._uyarildi = False


def sinif_listesi(n, kutu_sayilari):
    """n sinifli NMS BY CLASS ciktisi uretir."""
    return [np.zeros((kutu_sayilari.get(i, 0), 5), dtype=np.float32) for i in range(n)]


gecti = basarisiz = 0


def esit(ad, bulunan, beklenen):
    global gecti, basarisiz
    if bulunan == beklenen:
        print(f"  OK   {ad}")
        gecti += 1
    else:
        print(f"  HATA {ad}: bulunan={bulunan} beklenen={beklenen}")
        basarisiz += 1


print("--- batch ayiklama ---")

# 1) COCO, batch'li — eski kod da bunu dogru yapiyordu
ham = [sinif_listesi(80, {4: 2})]
esit("80 sinif + batch", len(Sahte(80)._batch_ayikla(ham)), 80)

# 2) OZEL MODEL, batch'li — ESKI KODUN COKTUGU YER (4 != 80 -> batch atilmaz)
ham = [sinif_listesi(4, {1: 3})]
esit("4 sinif + batch", len(Sahte(4)._batch_ayikla(ham)), 4)

# 3) Ayni ama HEF sinif sayisini vermemis (None) — yapiya bakarak bulmali
esit("4 sinif + batch, sinif_sayisi bilinmiyor",
     len(Sahte(None)._batch_ayikla(ham)), 4)

# 4) Batch YOK, dogrudan sinif listesi — dokunulmamali
ham = sinif_listesi(4, {0: 1})
esit("4 sinif, batch yok", len(Sahte(4)._batch_ayikla(ham)), 4)

# 5) TEK sinifli model, batch yok, tam 1 kutu — en tehlikeli belirsizlik
ham = sinif_listesi(1, {0: 1})
esit("1 sinif / 1 kutu, batch yok", len(Sahte(1)._batch_ayikla(ham)), 1)

# 6) Tek sinifli model, batch VAR
ham = [sinif_listesi(1, {0: 2})]
esit("1 sinif + batch", len(Sahte(1)._batch_ayikla(ham)), 1)

# 7) Hicbir sinifta tespit yok (hepsi bos) — cokmemeli
ham = [sinif_listesi(4, {})]
esit("4 sinif + batch, hepsi bos", len(Sahte(4)._batch_ayikla(ham)), 4)


print("--- eski kodun davranisi (karsilastirma) ---")
def eski(ham):
    if len(ham) == 1 and len(ham[0]) == len(COCO):
        return ham[0]
    return ham
esit("ESKI kod 4 sinif + batch -> yanlis sonuc", len(eski([sinif_listesi(4, {1: 3})])), 1)


print("--- etiket yukleme ---")
d = tempfile.mkdtemp()

y = os.path.join(d, "duz.txt")
open(y, "w").write("# yarisma siniflari\niha\n\nf16\nhelikopter\nfuze\n")
esit("duz metin", etiket_yukle(y), ["iha", "f16", "helikopter", "fuze"])

y = os.path.join(d, "liste.json")
open(y, "w").write(json.dumps(["iha", "f16", "helikopter", "fuze"]))
esit("JSON liste", etiket_yukle(y), ["iha", "f16", "helikopter", "fuze"])

y = os.path.join(d, "sozluk.json")
open(y, "w").write(json.dumps({"2": "helikopter", "0": "iha", "10": "x", "1": "f16"}))
esit("JSON sozluk (indekse gore sirali)",
     etiket_yukle(y), ["iha", "f16", "helikopter", "x"])


print(f"\n{gecti} gecti, {basarisiz} basarisiz")
sys.exit(1 if basarisiz else 0)
