#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
"""
HSS — kamera yayini + Hailo tespiti TEK SUREC

kamera_yayin.py ile hailo_tespit.py'nin birlesigi. Kareyi rpicam-vid'den bir kez
alir; JPEG'i oldugu gibi MJPEG olarak yayinlar, ayni kareyi kucultulmus cozerek
Hailo'ya verir. Ikisi arasinda HTTP/ffmpeg yok.

    IMX708 -> rpicam-vid -> JPEG kare
                              |-> :8080  MJPEG yayini (kareye hic dokunulmaz)
                              `-> :8081  Hailo YOLOv8 tespiti (JSON)

Iki port da eski scriptlerdekiyle ayni ve JSON alanlari birebir korundu; YKI
arayuzu (HSS_KAMERA / HSS_TESPIT) degistirilmeden calisir.

Kullanim (container ICINDE — rpicam-vid host'ta yok):
    python3 kamera_tespit.py [--dondur 180] [--esik 0.40] [--siniflar person,bottle]
                             [--port-kamera 8080] [--port-tespit 8081]
                             [--tespit-olcek 2] [--tespitsiz]

Uc noktalar:
    :8080/            tarayici onizleme          :8081/tespit   en guncel tespitler
    :8080/stream.mjpg ham MJPEG akisi            :8081/durum    saglik/istatistik
    :8080/kare.jpg    tek kare
    :8080/durum       kare sayaci

--- NEDEN TEK SUREC (2026-08-06 olcumleri, Pi 5, 1280x720) ---

1) Kaynakta kare kaybi (asil sorun, ayni duzeltme kamera_yayin.py'ye de islendi):
   stdout.read(65536) tam 65536 bayt dolana kadar bloklar = ~3,6 kare. Tuketici
   yalnizca en son kareyi aldigi icin aradakiler cope gider.
       read()  -> uretilen 30,3 fps, tuketiciye gorunen  8,5 fps
       read1() -> uretilen 30,0 fps, tuketiciye gorunen 30,2 fps

2) Tespit tarafinda cv2.VideoCapture + HTTP loopback tamamen kalkti. Kalan is:
       imdecode tam (1280x720)      5,17 ms      imdecode REDUCED_2   1,84 ms
       letterbox 1280x720           1,93 ms      letterbox 640x360    0,90 ms
       Hailo infer                 ~20    ms
   Hailo girdisi zaten 640x640 letterbox oldugu icin 1280x720'yi tam cozup sonra
   kucultmek bosa is: REDUCED_2 dogrudan 640x360 verir ve bu, tam kareden
   uretilecek olcegin ta kendisidir — bilgi kaybi yok. Kare basi ~4,4 ms CPU,
   30 fps'te ~%13 cekirdek kazanci.

Kutu koordinatlari kucultulmus karede hesaplanip TAM KARE olcegine geri
carpilir; disariya her zaman 1280x720 koordinatlari cikar (--tespit-olcek
degistirilse bile), yani arayuz tarafinda hicbir sey degismez.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SINIR = b"hsskare"          # multipart sinir dizesi
JPEG_BAS = b"\xff\xd8"
JPEG_SON = b"\xff\xd9"

# cv2'nin kucultulmus cozme bayraklari — 1 = tam kare
REDUCED = {}


# ============================ kamera kaynagi ============================

class KameraKaynagi:
    """rpicam-vid'i calistirir, stdout'tan JPEG kareleri ayiklar, sonuncuyu tutar."""

    def __init__(self, genislik, yukseklik, fps, dondur=0, kalite=85):
        self.genislik = genislik
        self.yukseklik = yukseklik
        self.fps = fps
        self.dondur = dondur % 360
        self.kalite = kalite
        self.kare = None                # JPEG bayt dizisi
        self.kare_no = 0
        self.olcum_fps = 0.0
        self._kilit = threading.Condition()
        self._dur = False
        self._proc = None
        # rpicam-vid yalnizca 180 dondurmeyi kendisi yapabilir (bedava).
        # 90/270 icin JPEG'i cozup dondurup yeniden kodlamak gerekir (cv2).
        self._donanimda = self.dondur == 180
        self._yeniden = False        # cozunurluk degisti -> beklemeden yeniden kur
        self._cv2 = None
        if self.dondur and not self._donanimda:
            import cv2
            self._cv2 = cv2
            self._kod = {
                90: cv2.ROTATE_90_CLOCKWISE,
                270: cv2.ROTATE_90_COUNTERCLOCKWISE,
            }[self.dondur]

    def komut(self):
        k = [
            "rpicam-vid",
            "-t", "0",
            "--codec", "mjpeg",
            "--width", str(self.genislik),
            "--height", str(self.yukseklik),
            "--framerate", str(self.fps),
            "--nopreview",
            "--flush",
            "-o", "-",
        ]
        if self._donanimda:
            k[1:1] = ["--rotation", "180"]
        return k

    def basla(self):
        threading.Thread(target=self._dongu, daemon=True).start()

    def yeniden_kur(self, genislik=None, yukseklik=None, fps=None):
        """Cozunurluk/fps'i calisma aninda degistirir.

        rpicam-vid'e cekim ayari canliyken verilemez; tek yol sureci yeniden
        baslatmak. Akis ~1 sn kesilir, sonra yeni olcude devam eder. Tuketiciler
        (YKI, tespit is parcacigi, konsol) kare boyutunu her karede okudugu icin
        degisimi kendiliginden karsilar.
        """
        if genislik:  self.genislik = int(genislik)
        if yukseklik: self.yukseklik = int(yukseklik)
        if fps:       self.fps = int(fps)
        self._yeniden = True
        if self._proc:
            self._proc.kill()          # _dongu yeni komutla hemen yeniden kurar
        return {"genislik": self.genislik, "yukseklik": self.yukseklik, "fps": self.fps}

    def _dongu(self):
        """rpicam-vid coker/kapanirsa yeniden baslatir."""
        while not self._dur:
            try:
                self._proc = subprocess.Popen(
                    self.komut(), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
                )
            except FileNotFoundError:
                print("HATA: rpicam-vid bulunamadi — bu script container ICINDE calismali.",
                      file=sys.stderr)
                return

            tampon = b""
            sayac, olcum_t0 = 0, time.time()
            while not self._dur:
                # read() DEGIL read1() — bkz. bas kisimdaki (1) numarali not.
                parca = self._proc.stdout.read1(65536)
                if not parca:
                    break
                tampon += parca

                # Tampondaki tam JPEG karelerini ayikla
                while True:
                    bas = tampon.find(JPEG_BAS)
                    if bas < 0:
                        # Ise yarar veri yok, tamponu kucult
                        if len(tampon) > 1 << 20:
                            tampon = tampon[-2:]
                        break
                    son = tampon.find(JPEG_SON, bas + 2)
                    if son < 0:
                        tampon = tampon[bas:]
                        break
                    self._yayinla(tampon[bas:son + 2])
                    tampon = tampon[son + 2:]
                    sayac += 1

                simdi = time.time()
                if simdi - olcum_t0 >= 2.0:
                    self.olcum_fps = sayac / (simdi - olcum_t0)
                    sayac, olcum_t0 = 0, simdi

            if self._proc:
                self._proc.kill()
            if self._yeniden:
                # Bilerek yeniden kuruyoruz; hata degil, bekleme de gerekmez.
                self._yeniden = False
                print(f"kamera yeniden kuruluyor: {self.genislik}x{self.yukseklik} "
                      f"@ {self.fps} fps", file=sys.stderr)
                continue
            if not self._dur:
                print("rpicam-vid durdu, 2 sn sonra yeniden baslatiliyor...", file=sys.stderr)
                time.sleep(2)

    def _cevir(self, jpeg):
        """90/270 dondurme icin JPEG'i cozup dondurur ve yeniden kodlar."""
        if not self.dondur or self._donanimda:
            return jpeg                 # 0 veya 180: kareye hic dokunulmaz
        cv2 = self._cv2
        goruntu = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        if goruntu is None:
            return jpeg                 # bozuk kare, oldugu gibi gecir
        goruntu = cv2.rotate(goruntu, self._kod)
        ok, kodlu = cv2.imencode(".jpg", goruntu,
                                 [int(cv2.IMWRITE_JPEG_QUALITY), self.kalite])
        return kodlu.tobytes() if ok else jpeg

    def _yayinla(self, jpeg):
        jpeg = self._cevir(jpeg)
        with self._kilit:
            self.kare = jpeg
            self.kare_no += 1
            self._kilit.notify_all()

    def bekle(self, son_gorulen, zaman_asimi=5.0):
        """Yeni kare gelene kadar bekler; (jpeg, kare_no) doner."""
        with self._kilit:
            if self.kare_no == son_gorulen:
                self._kilit.wait(zaman_asimi)
            return self.kare, self.kare_no

    def durdur(self):
        self._dur = True
        if self._proc:
            self._proc.kill()


# ============================ tespit is parcacigi ============================

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
        self.atlanan = 0            # tespit yetisemedigi icin islenmeyen kare


class TespitIscisi:
    """Kaynaktan en guncel JPEG'i alir, kucultulmus cozer, Hailo'ya verir.

    Yetisemedigi kareleri ATLAR — taret icin en taze kare dogru olandir, kuyruk
    biriktirmek gecikmeyi buyutur. Atlanan kare sayisi /durum'da raporlanir.
    """

    def __init__(self, kaynak, durum, hef, esik, izinli, olcek, etiketler=None,
                 motor="hailo"):
        self.kaynak = kaynak
        self.durum = durum
        self.hef = hef                  # motor='onnx' ise .onnx yolu
        self.esik = esik
        self.izinli = izinli
        self.olcek = olcek
        self.etiketler = etiketler
        self.motor = motor
        self.yolo = None
        self._dur = False

    def basla(self):
        threading.Thread(target=self._dongu, daemon=True).start()

    def _dongu(self):
        import cv2

        try:
            print(f"model yukleniyor ({self.motor}): {self.hef}")
            if self.motor == "onnx":
                from onnx_tespit import OnnxYolo
                self.yolo = OnnxYolo(self.hef, self.etiketler)
            else:
                from hailo_tespit import HailoYolo
                self.yolo = HailoYolo(self.hef, self.etiketler)
            self.yolo.izinli_dogrula(self.izinli)
            print(f"model hazir [{self.motor}] — girdi "
                  f"{self.yolo.genislik}x{self.yolo.yukseklik}, "
                  f"esik {self.esik}, tespit olcegi 1/{self.olcek}, "
                  f"{len(self.yolo.siniflar)} sinif"
                  + (f", filtre: {sorted(self.izinli)}" if self.izinli else ", tum siniflar"))
            if self.motor == "onnx":
                print("NOT: ONNX motoru Pi ISLEMCISINDE kosar (~1-2 fps). Taret"
                      " takibi icin degil, dogrulama/gosterim icindir.", file=sys.stderr)
        except Exception as ex:
            # Motor acilmazsa kamera yayini AYAKTA KALMALI — YKI'nin goruntusu
            # tespit yuzunden olmesin. Hata /durum'dan okunur.
            mesaj = f"{type(ex).__name__}: {ex}"
            print(f"TESPIT MOTORU ({self.motor}) ACILAMADI, tespit kapali:", mesaj,
                  file=sys.stderr)
            with self.durum.kilit:
                self.durum.hata = mesaj
            return

        bayrak = REDUCED.get(self.olcek, cv2.IMREAD_COLOR)
        son_no, sayac, olcum_t0 = 0, 0, time.time()

        while not self._dur:
            jpeg, no = self.kaynak.bekle(son_no)
            if jpeg is None or no == son_no:
                continue
            atlanan = no - son_no - 1       # bu tur islenmeden gecen kareler
            son_no = no

            t0 = time.time()
            try:
                kare = cv2.imdecode(np.frombuffer(jpeg, np.uint8), bayrak)
                if kare is None:
                    continue                # bozuk kare, sonrakini bekle
                tespitler, boyut = self.yolo.calistir(kare, self.esik, self.izinli)
                # Kucultulmus karede bulunan kutulari tam kare olcegine tasi:
                # disariya her zaman 1280x720 koordinati cikar.
                if self.olcek != 1:
                    tespitler = [self._buyut(t) for t in tespitler]
                    boyut = (boyut[0] * self.olcek, boyut[1] * self.olcek)
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

            with self.durum.kilit:
                self.durum.tespitler = tespitler
                self.durum.kare_boyut = boyut
                self.durum.kare_no = no
                self.durum.fps = fps
                self.durum.gecikme_ms = gecikme
                self.durum.akis_bagli = self.kaynak.kare is not None
                self.durum.hata = hata
                self.durum.atlanan += max(0, atlanan)

    def _buyut(self, t):
        """Kutu/merkez degerlerini tam kare olcegine cevirir. 'sapma' normalize
        oldugu icin (kare merkezine gore -1..+1) olcekten etkilenmez."""
        o = self.olcek
        t["kutu"] = [k * o for k in t["kutu"]]
        t["merkez"] = [m * o for m in t["merkez"]]
        return t

    def durdur(self):
        self._dur = True
        if self.yolo is not None:
            self.yolo.kapat()


# ============================ HTTP: kamera (8080) ============================

class KameraIstek(BaseHTTPRequestHandler):
    kaynak = None                       # sinif duzeyinde, main'de atanir
    protocol_version = "HTTP/1.1"

    def log_message(self, bicim, *args):
        pass                            # erisim gunlugunu sustur

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._sayfa()
        elif self.path.startswith("/stream.mjpg"):
            self._akis()
        elif self.path.startswith("/kare.jpg"):
            self._tek_kare()
        elif self.path.startswith("/durum"):
            self._durum()
        elif self.path.startswith("/ayar"):
            self._ayar()
        else:
            self.send_error(404)

    def _sayfa(self):
        govde = (
            "<!doctype html><meta charset='utf-8'>"
            "<title>HSS Kamera</title>"
            "<style>body{background:#0b1017;color:#7fe7ff;font-family:Consolas,monospace;"
            "margin:0;display:flex;flex-direction:column;align-items:center;gap:12px;padding:16px}"
            "img{max-width:100%;border:1px solid #1d3a4d}</style>"
            "<h3>HSS &mdash; IMX708 canli yayin</h3>"
            "<img src='/stream.mjpg'>"
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(govde)))
        self.end_headers()
        self.wfile.write(govde)

    def _tek_kare(self):
        kare, _ = self.kaynak.bekle(-1)
        if kare is None:
            self.send_error(503, "kare yok")
            return
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(kare)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(kare)

    def _durum(self):
        govde = json.dumps({
            "kare_no": self.kaynak.kare_no,
            "yayin": self.kaynak.kare is not None,
            "fps": round(self.kaynak.olcum_fps, 1),
            "genislik": self.kaynak.genislik,
            "yukseklik": self.kaynak.yukseklik,
            "istenen_fps": self.kaynak.fps,
        }).encode()
        self._json_yaz(govde)

    def _json_yaz(self, govde):
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(govde)))
        self.send_header("Cache-Control", "no-store")
        # Konsol sayfasi baska porttan (8090) servis edildigi icin gerekli.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(govde)

    def _ayar(self):
        """/ayar?genislik=1280&yukseklik=720&fps=30 — canli cozunurluk degisimi."""
        from urllib.parse import parse_qs, urlparse
        q = parse_qs(urlparse(self.path).query)
        al = lambda ad: (q.get(ad, [None])[0])
        try:
            yeni = self.kaynak.yeniden_kur(al("genislik"), al("yukseklik"), al("fps"))
        except (TypeError, ValueError):
            self._json_yaz(b'{"hata":"gecersiz deger"}')
            return
        self._json_yaz(json.dumps(yeni).encode())

    def _akis(self):
        self.send_response(200)
        self.send_header("Age", "0")
        self.send_header("Cache-Control", "no-cache, private")
        self.send_header("Pragma", "no-cache")
        self.send_header("Content-Type",
                         "multipart/x-mixed-replace; boundary=" + SINIR.decode())
        self.end_headers()

        son = -1
        try:
            while True:
                kare, son = self.kaynak.bekle(son)
                if kare is None:
                    time.sleep(0.1)
                    continue
                self.wfile.write(b"--" + SINIR + b"\r\n")
                self.wfile.write(b"Content-Type: image/jpeg\r\n")
                self.wfile.write(b"Content-Length: " + str(len(kare)).encode() + b"\r\n\r\n")
                self.wfile.write(kare)
                self.wfile.write(b"\r\n")
        except (BrokenPipeError, ConnectionResetError):
            pass                        # istemci kapatti, normal


# ============================ HTTP: tespit (8081) ============================

class TespitIstek(BaseHTTPRequestHandler):
    durum = None
    kaynak = None
    isci = None
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
        elif self.path.startswith("/ayar"):
            # /ayar?esik=0.25 — guven esigini calisma aninda degistirir.
            from urllib.parse import parse_qs, urlparse
            q = parse_qs(urlparse(self.path).query)
            try:
                e = float(q.get("esik", [""])[0])
            except (TypeError, ValueError):
                self._json({"hata": "gecersiz esik"})
                return
            if not 0.0 <= e <= 1.0:
                self._json({"hata": "esik 0..1 olmali"})
                return
            if self.isci is not None:
                self.isci.esik = e
            self._json({"esik": e})
        elif self.path.startswith("/durum"):
            with d.kilit:
                self._json({
                    "akis_bagli": d.akis_bagli,
                    "esik": self.isci.esik if self.isci else None,
                    "kare_no": d.kare_no,
                    "fps": round(d.fps, 1),
                    "kamera_fps": round(self.kaynak.olcum_fps, 1),
                    "atlanan_kare": d.atlanan,
                    "gecikme_ms": round(d.gecikme_ms, 1),
                    "tespit_sayisi": len(d.tespitler),
                    "hata": d.hata,
                })
        else:
            self.send_error(404)


# ============================ main ============================

def sunucu_baslat(port, sinif):
    s = ThreadingHTTPServer(("0.0.0.0", port), sinif)
    s.daemon_threads = True
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s


def main():
    import cv2
    REDUCED.update({
        1: cv2.IMREAD_COLOR,
        2: cv2.IMREAD_REDUCED_COLOR_2,
        4: cv2.IMREAD_REDUCED_COLOR_4,
        8: cv2.IMREAD_REDUCED_COLOR_8,
    })

    a = argparse.ArgumentParser(description="HSS — kamera yayini + Hailo tespiti tek surecte")
    a.add_argument("--port-kamera", type=int, default=8080)
    a.add_argument("--port-tespit", type=int, default=8081)
    a.add_argument("--genislik", type=int, default=1280)
    a.add_argument("--yukseklik", type=int, default=720)
    a.add_argument("--fps", type=int, default=30)
    a.add_argument("--dondur", type=int, default=0, choices=[0, 90, 180, 270],
                   help="kareyi saat yonunde dondur (kamera ters monteyse 180)")
    a.add_argument("--kalite", type=int, default=85,
                   help="dondurme sonrasi JPEG kalitesi (yalnizca --dondur 90/270 ile)")
    a.add_argument("--hef", default="/usr/share/hailo-models/yolov8s_h8l.hef",
                   help="model yolu — motor'a gore .hef ya da .onnx")
    a.add_argument("--motor", default="hailo", choices=["hailo", "onnx"],
                   help="hailo = NPU (30 fps); onnx = Pi islemcisi (~1-2 fps,"
                        " .hef hazir olana kadar ARA COZUM)")
    a.add_argument("--esik", type=float, default=0.40)
    a.add_argument("--siniflar", default="",
                   help="virgulle ayrilmis sinif adlari (filtre); bos = hepsi")
    a.add_argument("--etiketler", default="",
                   help="sinif adi dosyasi (satir basina bir ad ya da JSON liste)."
                        " Yarisma modeli takilinca SART")
    a.add_argument("--tespit-olcek", type=int, default=2, choices=[1, 2, 4, 8],
                   help="tespit icin kareyi 1/N cozer (2 = 640x360, Hailo icin yeterli)")
    a.add_argument("--tespitsiz", action="store_true",
                   help="yalnizca kamera yayini; Hailo hic acilmaz")
    p = a.parse_args()

    izinli = {s.strip() for s in p.siniflar.split(",") if s.strip()} or None

    etiketler = None
    if p.etiketler:
        from hailo_tespit import etiket_yukle
        etiketler = etiket_yukle(p.etiketler)

    kaynak = KameraKaynagi(p.genislik, p.yukseklik, p.fps, p.dondur, p.kalite)
    kaynak.basla()

    durum = Durum()
    KameraIstek.kaynak = kaynak
    TespitIstek.durum = durum
    TespitIstek.kaynak = kaynak

    isci = None
    if not p.tespitsiz:
        isci = TespitIscisi(kaynak, durum, p.hef, p.esik, izinli, p.tespit_olcek,
                            etiketler, p.motor)
        TespitIstek.isci = isci
        isci.basla()

    sunucular = [sunucu_baslat(p.port_kamera, KameraIstek)]
    if not p.tespitsiz:
        sunucular.append(sunucu_baslat(p.port_tespit, TespitIstek))

    ip = socket.gethostbyname(socket.gethostname())
    donme = f", dondurme {p.dondur}°" if p.dondur else ""
    print(f"HSS kamera+tespit  {p.genislik}x{p.yukseklik} @ {p.fps} fps{donme}")
    print(f"  tarayici : http://{ip}:{p.port_kamera}/")
    print(f"  akis     : http://{ip}:{p.port_kamera}/stream.mjpg")
    print(f"  tek kare : http://{ip}:{p.port_kamera}/kare.jpg")
    if not p.tespitsiz:
        print(f"  tespit   : http://{ip}:{p.port_tespit}/tespit")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        for s in sunucular:
            s.shutdown()
        if isci is not None:
            isci.durdur()
        kaynak.durdur()


if __name__ == "__main__":
    main()
