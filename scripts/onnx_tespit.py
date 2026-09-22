#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
"""HSS — ONNX Runtime uzerinde YOLOv8 tespiti (Hailo'suz ARA COZUM)

NEDEN VAR: yarisma modeli `.pt` olarak geldi; Hailo NPU yalnizca `.hef` yukler
ve donusum Pi'de yapilamiyor. `.hef` hazir olana kadar model Pi'nin ISLEMCISINDE
kosar — yavastir ama zincir uctan uca gorunur olur.

    HIZ BEKLENTISI (Pi 5, 4 iplik):  640 girdi ~1,5-2,5 fps · 960 girdi ~0,7 fps
    Hailo ile ayni model 30 fps yapar. Bu dosya taret TAKIBI icin yeterli DEGIL,
    dogrulama ve gosterim icindir.

HailoYolo ile AYNI arayuzu sunar (genislik/yukseklik/siniflar/calistir/kapat),
boylece kamera_tespit.py motoru degistirince baska hicbir sey degismez ve
:8081/tespit ciktisi birebir ayni kalir — dashboard bunu ayirt edemez.

FARK: Hailo NMS'i cip uzerinde yapar, ONNX ciktisi HAMDIR. Kutu cozme ve NMS
burada, cv2.dnn.NMSBoxes ile yapiliyor.
"""
import sys

import numpy as np


class OnnxYolo:
    """Ultralytics YOLOv8 ONNX ciktisi: (1, 4+nc, N) — ham, NMS'siz."""

    def __init__(self, model_yolu, etiketler=None, iplik=4, iou=0.45):
        import onnxruntime as ort

        se = ort.SessionOptions()
        se.intra_op_num_threads = iplik
        se.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        self.oturum = ort.InferenceSession(model_yolu, se,
                                           providers=["CPUExecutionProvider"])
        self.iou = iou

        g = self.oturum.get_inputs()[0]
        self.giris_adi = g.name
        # NCHW: [batch, 3, yukseklik, genislik]. Dinamik eksen None/str gelirse
        # 640 varsayilir — ultralytics disa aktarimi normalde sabit boyutludur.
        y, x = g.shape[2], g.shape[3]
        self.yukseklik = int(y) if isinstance(y, int) else 640
        self.genislik = int(x) if isinstance(x, int) else 640

        # Sinif sayisi cikti seklinden: (1, 4+nc, N)
        c = self.oturum.get_outputs()[0]
        self.sinif_sayisi = None
        try:
            orta = c.shape[1]
            if isinstance(orta, int) and orta > 4:
                self.sinif_sayisi = int(orta) - 4
        except Exception:
            pass

        if etiketler:
            self.siniflar = list(etiketler)
            if self.sinif_sayisi and len(self.siniflar) != self.sinif_sayisi:
                print(f"UYARI: etiket dosyasinda {len(self.siniflar)} ad var ama model"
                      f" {self.sinif_sayisi} sinif donduruyor — adlar KAYMIS olabilir",
                      file=sys.stderr)
        elif self.sinif_sayisi:
            self.siniflar = [str(i) for i in range(self.sinif_sayisi)]
            print(f"UYARI: {self.sinif_sayisi} sinifli model, --etiketler verilmedi;"
                  " sinif adlari yerine indeks kullanilacak", file=sys.stderr)
        else:
            self.siniflar = []

    def kapat(self):
        self.oturum = None

    def izinli_dogrula(self, izinli):
        """--siniflar filtresindeki adlar modelde var mi? (HailoYolo ile ayni)"""
        if not izinli:
            return
        bilinmeyen = sorted(set(izinli) - set(self.siniflar))
        if bilinmeyen:
            print(f"UYARI: --siniflar'daki {bilinmeyen} bu modelde YOK — bu adlarla"
                  f" hicbir tespit gecmez. Modelin siniflari: {self.siniflar}",
                  file=sys.stderr)

    def _hazirla(self, kare):
        """Letterbox + NCHW float32 0..1 RGB. HailoYolo._hazirla'nin ONNX esi."""
        import cv2
        y, g = kare.shape[:2]
        olcek = min(self.genislik / g, self.yukseklik / y)
        yg, yy = int(round(g * olcek)), int(round(y * olcek))
        kucuk = cv2.resize(kare, (yg, yy), interpolation=cv2.INTER_LINEAR)
        tuval = np.full((self.yukseklik, self.genislik, 3), 114, dtype=np.uint8)
        ust, sol = (self.yukseklik - yy) // 2, (self.genislik - yg) // 2
        tuval[ust:ust + yy, sol:sol + yg] = kucuk
        rgb = cv2.cvtColor(tuval, cv2.COLOR_BGR2RGB)
        x = rgb.astype(np.float32) / 255.0
        return np.ascontiguousarray(x.transpose(2, 0, 1)[None]), olcek, sol, ust

    def calistir(self, kare, esik, izinli=None):
        """Kareyi isler, HailoYolo.calistir ile AYNI yapida liste dondurur."""
        import cv2
        girdi, olcek, sol, ust = self._hazirla(kare)
        y, g = kare.shape[:2]

        cikti = self.oturum.run(None, {self.giris_adi: girdi})[0]

        # (1, 4+nc, N) -> (N, 4+nc). Bazi disa aktarimlar zaten (1, N, 4+nc) verir.
        ham = np.squeeze(cikti, 0) if cikti.ndim == 3 else cikti
        if ham.shape[0] < ham.shape[1]:
            ham = ham.T                       # (4+nc, N) -> (N, 4+nc)

        kutular_xywh = ham[:, :4]
        skorlar = ham[:, 4:]
        if skorlar.size == 0:
            return [], (g, y)

        en_iyi = skorlar.argmax(axis=1)
        en_iyi_skor = skorlar[np.arange(len(skorlar)), en_iyi]
        tut = en_iyi_skor >= esik
        if not tut.any():
            return [], (g, y)

        kutular_xywh, en_iyi, en_iyi_skor = (kutular_xywh[tut], en_iyi[tut],
                                             en_iyi_skor[tut])

        # cx,cy,w,h (girdi tuvali pikseli) -> x1,y1,x2,y2 (ORIJINAL kare pikseli)
        cx, cy, gw, gh = (kutular_xywh[:, 0], kutular_xywh[:, 1],
                          kutular_xywh[:, 2], kutular_xywh[:, 3])
        x1 = (cx - gw / 2 - sol) / olcek
        x2 = (cx + gw / 2 - sol) / olcek
        y1 = (cy - gh / 2 - ust) / olcek
        y2 = (cy + gh / 2 - ust) / olcek
        x1, x2 = np.clip(x1, 0, g), np.clip(x2, 0, g)
        y1, y2 = np.clip(y1, 0, y), np.clip(y2, 0, y)

        # NMS SINIF BASINA — Hailo'nun cip uzerinde yaptigi isin ayni sekilde
        # yapilmasi sart, yoksa ust uste binen farkli sinif kutulari birbirini eler.
        tespitler = []
        for sn in np.unique(en_iyi):
            ad = self.siniflar[sn] if sn < len(self.siniflar) else str(sn)
            if izinli and ad not in izinli:
                continue
            m = en_iyi == sn
            kutu_listesi = np.stack([x1[m], y1[m], x2[m] - x1[m], y2[m] - y1[m]], 1)
            skor_listesi = en_iyi_skor[m]
            secim = cv2.dnn.NMSBoxes(kutu_listesi.tolist(), skor_listesi.tolist(),
                                     float(esik), float(self.iou))
            if len(secim) == 0:
                continue
            sx1, sy1, sx2, sy2 = x1[m], y1[m], x2[m], y2[m]
            for i in np.array(secim).flatten():
                a, b, c_, d = (float(sx1[i]), float(sy1[i]),
                               float(sx2[i]), float(sy2[i]))
                if c_ <= a or d <= b:
                    continue
                mx, my = (a + c_) / 2, (b + d) / 2
                tespitler.append({
                    "sinif": ad,
                    "guven": round(float(skor_listesi[i]), 3),
                    "kutu": [round(a), round(b), round(c_), round(d)],
                    "merkez": [round(mx), round(my)],
                    # kare merkezine gore normalize sapma (-1..+1) — taret komutu icin
                    "sapma": [round((mx - g / 2) / (g / 2), 4),
                              round((my - y / 2) / (y / 2), 4)],
                })

        tespitler.sort(key=lambda t: t["guven"], reverse=True)
        return tespitler, (g, y)
