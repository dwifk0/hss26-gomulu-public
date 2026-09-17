#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
"""
HSS — Hailo-8L uzerinde YOLOv8 hedef tespiti

Kamera tek kaynak oldugu icin bu surec kameraya DOKUNMAZ: kamera_yayin.py'nin
yayinladigi MJPEG akisini tuketir, kareleri Hailo NPU'da YOLOv8s ile isler ve
sonuclari HTTP uzerinden JSON olarak sunar.

    IMX708 -> kamera_yayin.py (:8080 MJPEG) -> hailo_tespit.py (:8081 JSON)
                                                      |
                                            YKI arayuzu (Windows) ikisini de okur

Kullanim (container icinde):
    python3 hailo_tespit.py [--akis http://127.0.0.1:8080/stream.mjpg]
                            [--port 8081] [--hef /usr/share/hailo-models/yolov8s_h8l.hef]
                            [--esik 0.40] [--siniflar person,bottle,cup]

Uc noktalar:
    /tespit   -> en guncel tespitler (JSON)
    /durum    -> saglik/istatistik (JSON)

Model NMS'i cip uzerinde yapar (HAILO NMS BY CLASS, 80 sinif) — kutular hazir gelir.
"""
import argparse
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np

# COCO 80 sinif — HEF'in sinif sirasi
COCO = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush",
]


def etiket_yukle(yol):
    """Sinif adlarini dosyadan okur. Iki bicim kabul edilir:

      * duz metin — satir basina bir ad, bos satirlar ve '#' yorumlari atlanir
      * JSON      — ["iha", "f16", ...] listesi ya da {"0": "iha", ...} sozlugu
                    (sozlukte anahtar sinif indeksidir, siraya gore dizilir)

    Sira KRITIK: model sinif indeksi dondurur, ad yalnizca bu listeden gelir.
    Egitimdeki sirayla birebir ayni olmali (YOLO'da data.yaml'daki 'names').
    """
    with open(yol, "r", encoding="utf-8") as f:
        ham = f.read().strip()
    if ham.startswith("[") or ham.startswith("{"):
        veri = json.loads(ham)
        if isinstance(veri, dict):
            return [veri[k] for k in sorted(veri, key=lambda x: int(x))]
        return list(veri)
    return [s.strip() for s in ham.splitlines()
            if s.strip() and not s.strip().startswith("#")]


class AkisOkuyucu:
    """MJPEG akisini surekli okur, son kareyi tutar (eski kareler atilir)."""

    def __init__(self, url):
        self.url = url
        self.kare = None            # BGR ndarray
        self.kare_no = 0
        self.bagli = False
        self._kilit = threading.Lock()
        self._dur = False

    def basla(self):
        threading.Thread(target=self._dongu, daemon=True).start()

    def _dongu(self):
        import cv2
        while not self._dur:
            cap = cv2.VideoCapture(self.url)
            if not cap.isOpened():
                self.bagli = False
                print(f"akis acilamadi: {self.url} — 2 sn sonra tekrar", file=sys.stderr)
                time.sleep(2)
                continue
            self.bagli = True
            bos = 0
            while not self._dur:
                ok, kare = cap.read()
                if not ok:
                    bos += 1
                    if bos > 30:
                        break
                    time.sleep(0.02)
                    continue
                bos = 0
                with self._kilit:
                    self.kare = kare
                    self.kare_no += 1
            cap.release()
            self.bagli = False

    def son(self):
        with self._kilit:
            return (None, 0) if self.kare is None else (self.kare.copy(), self.kare_no)

    def durdur(self):
        self._dur = True


class HailoYolo:
    """Cip uzerinde NMS'li YOLOv8 (.hef). Girdi UINT8 NHWC 640x640x3.

    Sinif sayisi MODELDEN okunur, koda gomulu degildir — yarisma modeli
    80 yerine birkac sinifla gelecek. Adlar `etiketler` ile verilir; verilmezse
    80 sinifli modelde COCO, digerinde sayisal indeks kullanilir.
    """

    def __init__(self, hef_yolu, etiketler=None):
        from hailo_platform import (VDevice, HEF, ConfigureParams, HailoStreamInterface,
                                    InputVStreamParams, OutputVStreamParams, FormatType)
        self._hp = __import__("hailo_platform")

        self.hef = HEF(hef_yolu)
        self.hedef = VDevice()
        cfg = ConfigureParams.create_from_hef(
            self.hef, interface=HailoStreamInterface.PCIe)
        self.ag = self.hedef.configure(self.hef, cfg)[0]
        self.ag_params = self.ag.create_params()

        self.giris_params = InputVStreamParams.make(self.ag, format_type=FormatType.UINT8)
        self.cikis_params = OutputVStreamParams.make(self.ag, format_type=FormatType.FLOAT32)

        gbilgi = self.hef.get_input_vstream_infos()[0]
        self.giris_adi = gbilgi.name
        self.yukseklik, self.genislik = gbilgi.shape[0], gbilgi.shape[1]

        self._uyarildi = False          # yapi uyarisi kare basina basilmasin

        # --- sinif sayisi: modelin kendisinden ---
        # NMS BY CLASS ciktisinda sinif sayisi vstream bilgisinde yazar. Iki
        # yerden denenir; ikisi de vermezse None kalir ve calistir() yapiya
        # bakarak karar verir (bkz. _batch_ayikla).
        self.sinif_sayisi = None
        try:
            cbilgi = self.hef.get_output_vstream_infos()[0]
            nms = getattr(cbilgi, "nms_shape", None)
            if nms is not None and getattr(nms, "number_of_classes", None):
                self.sinif_sayisi = int(nms.number_of_classes)
            elif getattr(cbilgi, "shape", None):
                self.sinif_sayisi = int(cbilgi.shape[0])
        except Exception as ex:
            print(f"UYARI: sinif sayisi HEF'ten okunamadi ({type(ex).__name__}: {ex}),"
                  " yapiya bakilarak cikarilacak", file=sys.stderr)

        # --- sinif adlari ---
        if etiketler:
            self.siniflar = list(etiketler)
            if self.sinif_sayisi and len(self.siniflar) != self.sinif_sayisi:
                print(f"UYARI: etiket dosyasinda {len(self.siniflar)} ad var ama model"
                      f" {self.sinif_sayisi} sinif donduruyor — adlar KAYMIS olabilir,"
                      " egitimdeki sirayla ayni oldugunu dogrula", file=sys.stderr)
        elif self.sinif_sayisi is None or self.sinif_sayisi == len(COCO):
            self.siniflar = list(COCO)
        else:
            # Ozel model ama etiket verilmemis: adlar yerine indeks. Tespit
            # CALISIR, yalnizca isimler sayisal olur — sessiz sifir tespit YOK.
            self.siniflar = [str(i) for i in range(self.sinif_sayisi)]
            print(f"UYARI: model {self.sinif_sayisi} sinifli (COCO degil) ama --etiketler"
                  " verilmedi; sinif adlari yerine indeks kullanilacak", file=sys.stderr)

        # Inference baglamini BIR KEZ kur ve acik tut. Her karede yeniden kurmak
        # cok pahali (16 fps'e kadar dusuruyordu) — kalici baglamla ~2 kat hizli.
        from hailo_platform import InferVStreams
        self._boru_cm = InferVStreams(self.ag, self.giris_params, self.cikis_params)
        self._boru = self._boru_cm.__enter__()
        self._aktif_cm = self.ag.activate(self.ag_params)
        self._aktif_cm.__enter__()

    def kapat(self):
        for cm in (getattr(self, "_aktif_cm", None), getattr(self, "_boru_cm", None)):
            if cm is not None:
                try:
                    cm.__exit__(None, None, None)
                except Exception:
                    pass

    def _hazirla(self, kare):
        """Kareyi 640x640'a en-boy koruyarak yerlestirir (letterbox). RGB dondurur."""
        import cv2
        y, g = kare.shape[:2]
        olcek = min(self.genislik / g, self.yukseklik / y)
        yg, yy = int(round(g * olcek)), int(round(y * olcek))
        kucuk = cv2.resize(kare, (yg, yy), interpolation=cv2.INTER_LINEAR)
        tuval = np.full((self.yukseklik, self.genislik, 3), 114, dtype=np.uint8)
        ust, sol = (self.yukseklik - yy) // 2, (self.genislik - yg) // 2
        tuval[ust:ust + yy, sol:sol + yg] = kucuk
        return cv2.cvtColor(tuval, cv2.COLOR_BGR2RGB), olcek, sol, ust

    @staticmethod
    def _sinif_listesi_mi(x):
        """x, sinif basina kutu dizilerinden olusan bir liste mi?

        Sinif listesinde her eleman (n,5)'lik bir DIZIDIR; kutu listesinde ise
        her eleman 5 elemanli tek satirdir. Ayrimi bu yapiyor.
        """
        try:
            if len(x) == 0:
                return False
            for e in x:                          # ilk BOS OLMAYAN elemana bak
                d = np.asarray(e)
                if d.size == 0:
                    continue
                return d.ndim == 2 and d.shape[-1] == 5
            return True                          # hepsi bos -> sinif listesi
        except Exception:
            return False

    def _batch_ayikla(self, ham):
        """Ciktinin basindaki batch boyutunu (1) atar.

        Eski kod bunu `len(ham[0]) == 80` diye COCO'ya bakarak yapiyordu: ozel
        modelde bu esitlik tutmaz, batch atilmaz, donguye TEK eleman girer ve
        surec hic hata vermeden SIFIR tespit dondurur. Artik karar sinif
        sayisina degil ciktinin YAPISINA bakiyor.
        """
        if len(ham) == 1 and self._sinif_listesi_mi(ham[0]):
            return ham[0]
        return ham

    def izinli_dogrula(self, izinli):
        """--siniflar ile verilen adlar modelde var mi?

        Sessiz sifir tespitin ikinci kaynagi: systemd biriminde eski modelin
        sinif adi kalirsa (ornegin 'person') filtre her seyi eler.
        """
        if not izinli:
            return
        bilinmeyen = sorted(set(izinli) - set(self.siniflar))
        if bilinmeyen:
            print(f"UYARI: --siniflar'daki {bilinmeyen} bu modelde YOK — bu adlarla"
                  f" hicbir tespit gecmez. Modelin siniflari: {self.siniflar}",
                  file=sys.stderr)

    def calistir(self, kare, esik, izinli=None):
        """Kareyi isler, tespit sozlukleri listesi dondurur."""
        girdi, olcek, sol, ust = self._hazirla(kare)
        y, g = kare.shape[:2]

        sonuc = self._boru.infer({self.giris_adi: np.expand_dims(girdi, axis=0)})

        # NMS BY CLASS cikti yapisi: [batch][N sinif] -> her sinif (n,5) dizi,
        # satirlar [y_min, x_min, y_max, x_max, skor], 640x640 tuvaline gore 0..1.
        ham = list(sonuc.values())[0]
        ham = self._batch_ayikla(ham)

        if not self._uyarildi and self.sinif_sayisi and len(ham) != self.sinif_sayisi:
            self._uyarildi = True
            print(f"UYARI: cikti {len(ham)} sinif tasiyor ama HEF {self.sinif_sayisi}"
                  " diyor — cikti yapisi beklenenden farkli, tespitler eksik olabilir",
                  file=sys.stderr)

        tespitler = []
        for sinif_no, kutular in enumerate(ham):
            if kutular is None or len(kutular) == 0:
                continue
            ad = (self.siniflar[sinif_no] if sinif_no < len(self.siniflar)
                  else str(sinif_no))
            if izinli and ad not in izinli:
                continue
            for k in kutular:
                skor = float(k[4])
                if skor < esik:
                    continue
                # Hailo NMS: [y_min, x_min, y_max, x_max, skor], 0..1 normalize (640x640 tuvalinde)
                ymin, xmin, ymax, xmax = (float(k[0]), float(k[1]), float(k[2]), float(k[3]))
                # letterbox'i geri al -> orijinal kare koordinatlari
                x1 = (xmin * self.genislik - sol) / olcek
                x2 = (xmax * self.genislik - sol) / olcek
                y1 = (ymin * self.yukseklik - ust) / olcek
                y2 = (ymax * self.yukseklik - ust) / olcek
                x1, x2 = max(0.0, x1), min(float(g), x2)
                y1, y2 = max(0.0, y1), min(float(y), y2)
                if x2 <= x1 or y2 <= y1:
                    continue
                mx, my = (x1 + x2) / 2, (y1 + y2) / 2
                tespitler.append({
                    "sinif": ad,
                    "guven": round(skor, 3),
                    "kutu": [round(x1), round(y1), round(x2), round(y2)],
                    "merkez": [round(mx), round(my)],
                    # kare merkezine gore normalize sapma (-1..+1) — taret komutu icin
                    "sapma": [round((mx - g / 2) / (g / 2), 4),
                              round((my - y / 2) / (y / 2), 4)],
                })
        tespitler.sort(key=lambda t: t["guven"], reverse=True)
        return tespitler, (g, y)


class Durum:
    """Paylasilan son tespit sonucu."""

    def __init__(self):
        self.kilit = threading.Lock()
        self.tespitler = []
        self.kare_boyut = (0, 0)
        self.kare_no = 0
        self.fps = 0.0
        self.gecikme_ms = 0.0
        self.akis_bagli = False
        self.hata = None


class Istek(BaseHTTPRequestHandler):
    durum = None
    protocol_version = "HTTP/1.1"

    def log_message(self, bicim, *args):
        pass

    def _json(self, govde):
        ham = json.dumps(govde, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(ham)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(ham)

    def do_GET(self):
        d = self.durum
        if self.path.startswith("/tespit"):
            with d.kilit:
                self._json({
                    "kare_no": d.kare_no,
                    "genislik": d.kare_boyut[0],
                    "yukseklik": d.kare_boyut[1],
                    "fps": round(d.fps, 1),
                    "gecikme_ms": round(d.gecikme_ms, 1),
                    "tespitler": d.tespitler,
                })
        elif self.path.startswith("/durum"):
            with d.kilit:
                self._json({
                    "akis_bagli": d.akis_bagli,
                    "kare_no": d.kare_no,
                    "fps": round(d.fps, 1),
                    "gecikme_ms": round(d.gecikme_ms, 1),
                    "tespit_sayisi": len(d.tespitler),
                    "hata": d.hata,
                })
        else:
            self.send_error(404)


def main():
    a = argparse.ArgumentParser(description="HSS — Hailo YOLOv8 tespit sunucusu")
    a.add_argument("--akis", default="http://127.0.0.1:8080/stream.mjpg")
    a.add_argument("--port", type=int, default=8081)
    a.add_argument("--hef", default="/usr/share/hailo-models/yolov8s_h8l.hef")
    a.add_argument("--esik", type=float, default=0.40)
    a.add_argument("--siniflar", default="",
                   help="virgulle ayrilmis sinif adlari (filtre); bos = hepsi")
    a.add_argument("--etiketler", default="",
                   help="sinif adi dosyasi (satir basina bir ad ya da JSON liste)."
                        " Ozel modelde SART; verilmezse 80 sinifta COCO, digerinde"
                        " sayisal indeks kullanilir")
    p = a.parse_args()

    izinli = {s.strip() for s in p.siniflar.split(",") if s.strip()} or None
    etiketler = etiket_yukle(p.etiketler) if p.etiketler else None

    okuyucu = AkisOkuyucu(p.akis)
    okuyucu.basla()

    print(f"HEF yukleniyor: {p.hef}")
    yolo = HailoYolo(p.hef, etiketler)
    yolo.izinli_dogrula(izinli)
    print(f"model hazir — girdi {yolo.genislik}x{yolo.yukseklik}, esik {p.esik},"
          f" {len(yolo.siniflar)} sinif"
          + (f", filtre: {sorted(izinli)}" if izinli else ", tum siniflar"))

    durum = Durum()
    Istek.durum = durum
    sunucu = ThreadingHTTPServer(("0.0.0.0", p.port), Istek)
    sunucu.daemon_threads = True
    threading.Thread(target=sunucu.serve_forever, daemon=True).start()
    print(f"tespit ucu : http://0.0.0.0:{p.port}/tespit")

    son_no, sayac, olcum_t0 = 0, 0, time.time()
    try:
        while True:
            kare, no = okuyucu.son()
            if kare is None or no == son_no:
                time.sleep(0.005)
                continue
            son_no = no

            t0 = time.time()
            try:
                tespitler, boyut = yolo.calistir(kare, p.esik, izinli)
                hata = None
            except Exception as ex:
                tespitler, boyut, hata = [], (0, 0), f"{type(ex).__name__}: {ex}"
                print("tespit hatasi:", hata, file=sys.stderr)
                time.sleep(0.5)
            gecikme = (time.time() - t0) * 1000

            sayac += 1
            simdi = time.time()
            fps = sayac / (simdi - olcum_t0) if simdi > olcum_t0 else 0.0
            if simdi - olcum_t0 >= 2.0:
                sayac, olcum_t0 = 0, simdi

            with durum.kilit:
                durum.tespitler = tespitler
                durum.kare_boyut = boyut
                durum.kare_no = no
                durum.fps = fps
                durum.gecikme_ms = gecikme
                durum.akis_bagli = okuyucu.bagli
                durum.hata = hata
    except KeyboardInterrupt:
        pass
    finally:
        okuyucu.durdur()
        yolo.kapat()


if __name__ == "__main__":
    main()
