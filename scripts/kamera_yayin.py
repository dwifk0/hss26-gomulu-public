#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
"""
HSS — Pi 5 kamera MJPEG yayini

IMX708'yi rpicam-vid ile MJPEG olarak alir ve HTTP uzerinden yayinlar.
Container ICINDE calistirilir (rpicam-vid host'ta yok), compose
network_mode: host oldugu icin port dogrudan Pi'nin agindan gorulur.

Kullanim (container icinde):
    python3 kamera_yayin.py [--port 8080] [--genislik 1280] [--yukseklik 720] [--fps 30]
                            [--dondur 0|90|180|270] [--kalite 85]

Kamera yan monte oldugu icin --dondur 90 kullanilir. Dondurme, JPEG'i cozup
yeniden kodlamayi gerektirir (cv2); --dondur 0 iken kareye hic dokunulmaz.

Izleme:
    tarayici     -> http://<TARET_IP>:8080/
    ham akis     -> http://<TARET_IP>:8080/stream.mjpg
    tek kare     -> http://<TARET_IP>:8080/kare.jpg
    OpenCV       -> cv2.VideoCapture("http://<TARET_IP>:8080/stream.mjpg")

YKI arayuzu (MIGFER_HSS_Arayuz) HardwareBackend icinden bu akisi okur.
"""
import argparse
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SINIR = b"hsskare"          # multipart sinir dizesi
JPEG_BAS = b"\xff\xd8"
JPEG_SON = b"\xff\xd9"


class KameraKaynagi:
    """rpicam-vid'i calistirir, stdout'tan JPEG kareleri ayiklar, sonuncuyu tutar."""

    def __init__(self, genislik, yukseklik, fps, dondur=0, kalite=85):
        self.genislik = genislik
        self.yukseklik = yukseklik
        self.fps = fps
        self.dondur = dondur % 360
        self.kalite = kalite
        self.kare = None
        self.kare_no = 0
        self._kilit = threading.Condition()
        self._dur = False
        self._proc = None
        # rpicam-vid yalnizca 180 dondurmeyi kendisi yapabilir (bedava).
        # 90/270 icin JPEG'i cozup dondurup yeniden kodlamak gerekir (cv2).
        self._donanimda = self.dondur == 180
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
            while not self._dur:
                # read() DEGIL read1(): read(65536) tam 65536 bayt dolana kadar
                # bloklar, bu da ~3,6 kare demek. Tuketici yalnizca en son kareyi
                # aldigi icin aradakiler cope gider ve 30 fps'lik kamera 8-9 fps
                # gorunur. read1() eldeki veriyle hemen doner. (Olculdu 2026-08-06:
                # read 8,5 fps -> read1 30,2 fps.)
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

            if self._proc:
                self._proc.kill()
            if not self._dur:
                print("rpicam-vid durdu, 2 sn sonra yeniden baslatiliyor...", file=sys.stderr)
                time.sleep(2)

    def _cevir(self, jpeg):
        """90/270 dondurme icin JPEG'i cozup dondurur ve yeniden kodlar."""
        if not self.dondur or self._donanimda:
            return jpeg                 # 0 veya 180: kareye hic dokunulmaz
        cv2 = self._cv2
        import numpy as np
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
        """Yeni kare gelene kadar bekler; (kare, kare_no) doner."""
        with self._kilit:
            if self.kare_no == son_gorulen:
                self._kilit.wait(zaman_asimi)
            return self.kare, self.kare_no

    def durdur(self):
        self._dur = True
        if self._proc:
            self._proc.kill()


class Istek(BaseHTTPRequestHandler):
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
        govde = f'{{"kare_no": {self.kaynak.kare_no}, "yayin": {str(self.kaynak.kare is not None).lower()}}}'.encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(govde)))
        self.end_headers()
        self.wfile.write(govde)

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


def main():
    a = argparse.ArgumentParser(description="HSS Pi 5 kamera MJPEG yayini")
    a.add_argument("--port", type=int, default=8080)
    a.add_argument("--genislik", type=int, default=1280)
    a.add_argument("--yukseklik", type=int, default=720)
    a.add_argument("--fps", type=int, default=30)
    a.add_argument("--dondur", type=int, default=0, choices=[0, 90, 180, 270],
                   help="kareyi saat yonunde dondur (kamera yan monteyse 90)")
    a.add_argument("--kalite", type=int, default=85,
                   help="dondurme sonrasi JPEG kalitesi (yalnizca --dondur ile)")
    p = a.parse_args()

    kaynak = KameraKaynagi(p.genislik, p.yukseklik, p.fps, p.dondur, p.kalite)
    kaynak.basla()
    Istek.kaynak = kaynak

    ip = socket.gethostbyname(socket.gethostname())
    donme = f", dondurme {p.dondur}°" if p.dondur else ""
    print(f"HSS kamera yayini  {p.genislik}x{p.yukseklik} @ {p.fps} fps{donme}")
    print(f"  tarayici : http://{ip}:{p.port}/")
    print(f"  akis     : http://{ip}:{p.port}/stream.mjpg")
    print(f"  tek kare : http://{ip}:{p.port}/kare.jpg")

    sunucu = ThreadingHTTPServer(("0.0.0.0", p.port), Istek)
    sunucu.daemon_threads = True
    try:
        sunucu.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        kaynak.durdur()


if __name__ == "__main__":
    main()
