#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
"""
HSS — taret sunucusu: YKİ (TCP 5005) <-> ESP32-S3 (seri) köprüsü

YKİ arayüzündeki manuel kontroller (D-pad, ateş, acil durdur) bu sunucu
üzerinden gerçek tarete ulaşır; ESP32'nin 5 Hz telemetrisi de ters yönde
arayüze akar.

    YKI (Windows/PyQt6)  --JSON satir--> :5005 --seri--> ESP32-S3 --> TB6600
                         <--JSON satir--       <--seri--   (T ... telemetri)

Calistirma (Pi HOST'unda — container'da degil, ESP32 host'a bagli):
    python3 taret_sunucu.py [--port 5005] [--seri /dev/serial/by-id/...]

--- PROTOKOL (her iki yonde de satir basina bir JSON nesnesi) ---

YKI -> sunucu:
    {"tip":"jog","pan":-1..1,"tilt":-1..1}   surekli yenilenir; birakinca dur
    {"tip":"dur"}                            iki ekseni de durdur
    {"tip":"ates"}                           sarjoru surer (yasak alan reddedebilir)
    {"tip":"guc","ver":false}                ESP guc kesme yolu (yazilimsal acil durdur)
    {"tip":"estopreset"}                     acil stop mandalini ac
    {"tip":"ham","komut":"pan 120"}          firmware komutunu oldugu gibi gecir

sunucu -> YKI:
    {"tip":"telemetri","pan":...,"tilt":...,"estop":0,"hazir":1,"atis":1,
     "bus":..,"akim":..,"mesgul":0,"guc":1,"pan_norm":...,"yas":0.12}
    {"tip":"log","metin":"ATIS REDDEDILDI: ..."}

--- TASARIM NOTLARI ---

* **Jog'u sunucu ceviriyor, YKI degil.** Firmware'de hiz kipi yok; yalnizca
  "su aciya git" var (`pan <derece>`). Jog, hedefi surekli one itmek demek:
  hedef = anlik_aci + yon x ILERI_BAKIS. Anlik aciyi 5 Hz telemetriden burada
  biliyoruz, ag gidis-donusu beklemeye gerek kalmiyor.

* **Jog gozcusu (watchdog).** Jog paketi JOG_ZAMAN_ASIMI boyunca yenilenmezse
  otomatik `dur` gider. YKI cokerse, ag koparsa veya kullanici tusu basiliyken
  pencere kapanirsa taret kendiliginden durur. Bu bir emniyet ozelligi,
  optimizasyon degil.

* **Yazilimsal acil durdur = `guc kes`.** Firmware'de "yazilimdan acil stop
  tetikle" komutu YOK; fiziksel buton mandalli. Yerine ESP'nin guc kesme yolu
  (ARM rolesi, buton halkasina seri) kullanilir. Guvenlik yetkisi butonda
  kalir — bu yalnizca EK bir durdurma yolu.

* **Pan mutlak ve birikimli.** Firmware pan'i 0'a sarmadan sayar (tezgahta
  1626 derece gorulebilir). Telemetriye `pan_norm` (-180..180) eklenir; jog
  hedefi ise ham birikimli uzayda hesaplanir ki iki taraf ayrismasin.

* **DTR/RTS asagi cekilmez** — ESP32-S3'u resetler ve acil stop mandali
  yeniden kilitlenir.
"""
import argparse
import glob
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial

# Konsol sayfasi bu dosyanin yanindadir (repo ve Pi'de birlikte tasinir).
KONSOL_HTML = os.path.join(os.path.dirname(os.path.abspath(__file__)), "konsol.html")
# Birlesik konsol DENEME olarak "/2" yolundan yayinlanir. "/" uzerindeki
# uretim konsoluna dokunulmaz: ikisi yan yana calisir, geri donus icin
# sekmeyi kapatmak yeter, servisi yeniden baslatmak gerekmez.
KONSOL2_HTML = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "konsol_birlesik.html")
VARLIK_DIZIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "varlik")

BAUD = 115200
TELEMETRI_HZ = 5.0            # firmware 200 ms'de bir yolluyor
JOG_ZAMAN_ASIMI = 0.45        # sn — jog yenilenmezse otomatik dur
JOG_YENILE = 0.10             # sn — hedefi bu sikligla one it

# Telemetri gozcusu: firmware 5 Hz uretir, 3 sn sessizlik komutun kaybolmus
# olmasi demektir. 2 sn'de bir bakip 't 1' tekrarlanir.
TELEMETRI_GOZCU_ESIK = 3.0    # sn
TELEMETRI_GOZCU_ARALIK = 2.0  # sn
# Bu kadar sessizlikten sonra artik komut tekrari degil, portun kendisi supheli:
# USB kopmasi her zaman istisna firlatmiyor, port acik gorunup olu kalabiliyor.
SERI_YENIDEN_ESIK = 15.0      # sn
# Jog hedefinin anlik aciyi asma payi. RAMPADAN BUYUK olmali: firmware
# rampa = min(rampaAdim, mesafe/2) kuruyor, yani kruvaziyer hiza cikabilmek
# icin mesafe en az 2 x rampaAdim (800 adim -> pan'da ~18 deg) olmali. Eski
# 8 deg'lik pay rampanin yarisina bile yetmiyordu ve eksen hep yavas kaliyordu.
#
# Pay ayrica YAVASLAMA BOLGESINDEN uzak durmali. hizGuncelle() hedefe rampa
# kadar adim kalinca hizi dusurmeye basliyor (pan'da 800 adim = 9 deg) ve
# telemetri 5 Hz oldugu icin sunucunun gordugu konum tam hizda 13.5 deg'e
# kadar bayat olabiliyor. Pay bu ikisinin toplamindan bol bol buyuk secildi,
# yoksa eksen yavaslamaya girip sonra tekrar hizlaniyor ve jog duraksiyordu.
ILERI_BAKIS_PAN = 60.0        # derece
ILERI_BAKIS_TILT = 30.0
# Kalan yol ileri bakisin bu oranindan aza dusunce hedef uzatilir. Rampa artik
# ayni yonde korundugu icin sik tazelemenin bedeli yok; esik yuksek tutuldu ki
# kalan yol hicbir zaman yavaslama bolgesine inmesin.
JOG_UZAT_ESIK = 0.7

# ---------------------------------------------------------------- otonom takip
# Otonom surus eskiden YALNIZ masaustu YKI'sinde (es_hud/backend.py) yasiyordu;
# konsoldaki OTONOM dugmesi hicbir seye bagli degildi, sadece kendi gostergesini
# ceviriyordu. Dongü buraya tasindi: artik panelden acilabiliyor ve YKI acik
# olmasa da calisiyor. Isaretler 2026-08-19'da taret uzerinde OLCULDU:
#   pan +5 deg -> yatay sapma 0.538 -> 0.282  (hedef ortaya geldi)  => pan yonu +
#   tilt artinca hedef karede ASAGI gidiyor; el = -sapma_y oldugu icin  => tilt +
OTO_TESPIT_UCU = "http://127.0.0.1:8081/tespit"
# Gorus acisi da ayni olcumden: 5 deg pan yatay sapmayi 0.2555 degistirdi ->
# yarim HFOV ~19.6 deg. YKI'deki 54/34 degerleri olculene gore ~%40 buyuktu ve
# hatayi oldugundan buyuk gosterip kazanci sisiriyordu.
OTO_HFOV = 39.0               # derece
OTO_VFOV = 23.0               # derece
OTO_KAZANC = 0.55             # hatanin ne kadari uygulanir (1.0 asma/salinim)
OTO_OLU_BANT = 1.5            # derece — altinda komut yok (titremeyi keser)
OTO_MAKS = 10.0               # derece — tek komutta en fazla bu kadar don
OTO_PERIYOT = 0.15            # sn — seri hatti komutla bogmamak icin


def esp_portu():
    """ESP32-S3'un by-id yolu. Port numarasi (ttyACM0/1) cikar-tak ile degisir."""
    bulunan = sorted(glob.glob("/dev/serial/by-id/*Espressif*"))
    return bulunan[0] if bulunan else "/dev/ttyACM0"


# LiDAR ESP32'ye DEGIL Pi'ye bagli, o yuzden telemetriye burada karisiyor.
# by-id KULLANILAMAZ: eldeki iki CP2102'nin de seri numarasi "0001", by-id
# ikisini ayirt edemez ve yanlis cihaza baglanir. Tek ayirt edici fiziksel
# yuva -> by-path. Adaptor baska USB girisine takilirsa bu yol degisir.
LIDAR_PORT = "/dev/serial/by-path/platform-xhci-hcd.0-usb-0:2:1.0-port0"
LIDAR_BAUD = 115200


class Lidar:
    """TF02-Pro okuyucu. Cerceve: 0x59 0x59 + mesafe(2) + guc(2) + 3 bayt."""

    def __init__(self, port=LIDAR_PORT):
        self.port = port
        self.mesafe_cm = None
        self.sinyal = None
        self.son_t = 0.0
        self.hata = None
        self._dur = False

    @property
    def taze(self):
        return self.mesafe_cm is not None and time.time() - self.son_t < 1.0

    def basla(self):
        threading.Thread(target=self._dongu, daemon=True).start()

    def _dongu(self):
        while not self._dur:
            try:
                ser = serial.Serial(self.port, LIDAR_BAUD, timeout=1)
            except Exception as ex:
                self.hata = f"{type(ex).__name__}: {ex}"
                time.sleep(2.0)
                continue
            self.hata = None
            print(f"LiDAR baglandi: {self.port}")
            try:
                while not self._dur:
                    if ser.read(1) != b"\x59":
                        continue
                    if ser.read(1) != b"\x59":
                        continue
                    c = ser.read(7)
                    if len(c) != 7:
                        continue
                    self.mesafe_cm = c[0] + (c[1] << 8)
                    self.sinyal = c[2] + (c[3] << 8)
                    self.son_t = time.time()
            except Exception as ex:
                self.hata = f"{type(ex).__name__}: {ex}"
            finally:
                try:
                    ser.close()
                except Exception:
                    pass
            time.sleep(1.0)

    def durdur(self):
        self._dur = True


def aci_normal(a):
    """Birikimli aciyi -180..180 araligina indirir (yalnizca gosterim icin)."""
    a = a % 360.0
    return a - 360.0 if a > 180.0 else a


class Taret:
    """ESP32 seri baglantisi: telemetriyi ayristirir, komutlari yollar."""

    def __init__(self, port, lidar=None, otomatik_port=False):
        self.port = port
        # ESP32 sonradan takilabilir. Port otomatik bulunduysa her yeniden
        # baglanma denemesinde YENIDEN cozulur — yoksa aciliista bulunan
        # yedek yol (/dev/ttyACM0) kalir ve cihaz by-id ile gelince kacirilir.
        self.otomatik_port = otomatik_port
        self.lidar = lidar
        self.ser = None
        self.telemetri = {}
        self.son_telemetri_t = 0.0
        self.baglanma_t = 0.0             # portun en son acildigi an
        self._yeniden_ac = False          # gozcu -> okuma dongusu bayragi
        self._baglanma_uyarisi = False    # ESP yokken gunlugu doldurmasin
        self.dinleyiciler = []            # callable(dict) — bagli istemciler
        # Konsol sayfasi yoklama ile calistigi icin gunluk satirlari numarali
        # bir halkada tutulur; sayfa "en son gordugum numara" der, yalnizca
        # sonrasini alir. Boylece satir kacirmadan tekrar da gormez.
        self.gunluk = deque(maxlen=300)   # (no, metin)
        self.gunluk_no = 0
        self._gunluk_kilidi = threading.Lock()
        self._yaz_kilidi = threading.Lock()
        self._dur = False

    def gunluk_ekle(self, metin):
        with self._gunluk_kilidi:
            self.gunluk_no += 1
            self.gunluk.append((self.gunluk_no, metin))

    def gunluk_sonrasi(self, bilinen):
        with self._gunluk_kilidi:
            yeni = [m for no, m in self.gunluk if no > bilinen]
            return yeni, self.gunluk_no

    def baglan(self):
        self.seri_kapat()             # tekrar cagrilabilir olmali
        s = serial.Serial()
        s.port = self.port
        s.baudrate = BAUD
        s.timeout = 1
        # DTR=True / RTS=False. ESP32-S3'un USB-JTAG/serial birimi belirli
        # DTR-RTS bilesimlerinde cipi RESETLER; olculdu (2026-08-06):
        # dtr=False ile port acilinca pan 1626 -> 0 dondu ve acil stop mandali
        # yeniden kilitlendi. dtr=True ile durum korunuyor (hss_komut.py de
        # boyle kullaniyor). Reset SADECE gorsel bir aksama degil: mutlak
        # enkoder olmadigi icin pan sifir referansi kaybolur.
        s.dtr = True
        s.rts = False
        s.open()
        self.ser = s
        # Pi ile birlikte ESP de yeni acilmis olabilir (systemd boot'ta ikisini
        # ayni anda kaldiriyor). 0.3 sn cip banner'ini bitirmeye yetmiyordu.
        time.sleep(1.0)
        s.reset_input_buffer()
        s.reset_output_buffer()
        # Bas satir: ESP'nin girdi tamponunda yarim kalmis bir satir varsa
        # "t 1" onun sonuna yapisip birlesik bir komut oluyor ve YUTULUYOR.
        # 2026-08-07 reboot testinde tam bu olustu: ESP "bilinmeyen komut:
        # 'adim ureteci 500t'" dedi ve telemetri hic acilmadi.
        self.gonder("")
        self.telemetri_ac()
        # Sessizlik bu andan itibaren olculur. Soguk aciliista son_telemetri_t
        # 0.0 oldugu icin fark epoch'un kendisi cikiyor ve gozcu ilk tikinde
        # portu kapatiyordu — ESP daha ilk paketini yollayamadan.
        self.baglanma_t = time.time()
        print(f"ESP32 baglandi: {self.port}")

    def telemetri_ac(self):
        """5 Hz telemetriyi acar. Acikca 't 1' — 't' de ayni isi goruyor ama
        niyet belirsiz kaliyor.

        `telemetriAcik` firmware'de HER RESETTE false basliyor ve NVS'e
        yazilmiyor. Tek seferlik gonderim yeterli degil: komut kaybolursa
        butun sistem olu gorunur (telemetri yok -> `hazir` yok -> jog ve
        otonom surus calismaz). Gozcu icin bkz. _oku_dongu.
        """
        self.gonder("t 1")

    def gonder(self, komut):
        if self.ser is None:
            return False
        with self._yaz_kilidi:
            try:
                self.ser.write((komut.strip() + "\n").encode("utf-8"))
                self.ser.flush()
                return True
            except Exception as ex:
                print(f"[seri-yazma] {ex}", file=sys.stderr)
                return False

    def basla(self):
        threading.Thread(target=self._oku_dongu, daemon=True).start()
        threading.Thread(target=self._telemetri_gozcusu, daemon=True).start()

    def _telemetri_gozcusu(self):
        """Seri port acik ama telemetri akmiyorsa 't 1' komutunu tekrarlar.

        2026-08-07 reboot testinde gerekli oldugu goruldu: acilista gonderilen
        tek komut ESP'nin girdi tamponundaki artikla birlesip yutuldu, sunucu
        "ESP32 baglandi" yazdi ama telemetri hic gelmedi ve sistem dis
        gozlemciye tamamen olu gorundu. Kayip komutun sebebini kovalamak
        yerine durumu izleyip duzeltiyoruz - hangi sebeple kaybolursa kaybolsun
        sistem kendini toparlar.
        """
        while not self._dur:
            time.sleep(TELEMETRI_GOZCU_ARALIK)
            if self.ser is None:
                continue
            # Sessizlik, "son telemetri" ile "portun acildigi an"in GEC olanindan
            # olculur. Yalnizca son_telemetri_t'ye bakmak iki yerde patliyordu:
            # soguk aciliista (deger 0.0) ve gercek bir kopmadan sonraki yeniden
            # baglanmada (deger eski) — ikisinde de gozcu portu aninda kapatiyor,
            # baglanti hic kurulamiyordu.
            ref = max(self.son_telemetri_t, self.baglanma_t)
            if not ref:
                continue
            sessizlik = time.time() - ref
            if sessizlik > SERI_YENIDEN_ESIK:
                # 't 1' tekrarlari da fayda etmedi: port acik GORUNUYOR ama olu
                # olabilir (USB kopmasi her zaman istisna firlatmiyor). Portu
                # kapat, okuma dongusu yeniden acsin.
                print(f"[gozcu] {sessizlik:.0f} sn telemetri yok — seri port "
                      "yeniden aciliyor", file=sys.stderr)
                # Portu BU iplikten kapatma: okuma dongusu readline() icinde
                # bloke ve port altindan cekilince pyserial
                # "'NoneType' object cannot be interpreted as an integer"
                # firlatiyor. Bayragi birak, readline 1 sn'de zaman asimina
                # ugrayip kendi kapatsin.
                self._yeniden_ac = True
            elif sessizlik > TELEMETRI_GOZCU_ESIK:
                print("[gozcu] telemetri gelmiyor, 't 1' tekrarlaniyor",
                      file=sys.stderr)
                self.telemetri_ac()

    def seri_kapat(self):
        """Portu kapatir; okuma dongusu bir sonraki turda yeniden acar."""
        s, self.ser = self.ser, None
        if s is not None:
            try: s.close()
            except Exception: pass

    def _oku_dongu(self):
        while not self._dur:
            # YENIDEN BAGLANMA. Onceden baglan() yalnizca aciliista bir kez
            # cagriliyordu: ESP32 USB'den bir an kopunca (ya da ttyACM0 ->
            # ttyACM1 diye yeniden numaralaninca) bu dongu olu tanitici uzerinde
            # sonsuza kadar hata basiyor, port HIC yeniden acilmiyordu.
            # 2026-08-07'de olculdu: sunucu 12 dakika `bagli: false` kaldi ve
            # ancak elle yeniden baslatilinca duzeldi. LiDAR sinifi bunu zaten
            # dogru yapiyordu; ESP yolu tutarsiz kalmis.
            if self._yeniden_ac:
                self._yeniden_ac = False
                self.seri_kapat()         # kapatma hep BU iplikte
            if self.ser is None:
                if self.otomatik_port:
                    self.port = esp_portu()
                try:
                    self.baglan()
                except Exception as ex:
                    # ESP hic takili degilken bu her 2 sn'de bir basilir ve
                    # gunlugu doldurur; ilk hatadan sonra sussun, cihaz gelince
                    # "ESP32 baglandi" satiri zaten gorunur.
                    if not self._baglanma_uyarisi:
                        self._baglanma_uyarisi = True
                        print(f"[seri-baglanma] {ex}", file=sys.stderr)
                        print("[seri-baglanma] ESP32 beklenecek; konsol ve TCP "
                              "acik kaliyor, hareket komutlari uygulanmayacak",
                              file=sys.stderr)
                    time.sleep(2.0)
                    continue
                self._baglanma_uyarisi = False
            try:
                ham = self.ser.readline()
            except Exception as ex:
                print(f"[seri-okuma] {ex} — port kapatiliyor, yeniden baglanilacak",
                      file=sys.stderr)
                self.seri_kapat()
                time.sleep(1.0)
                continue
            if not ham:
                continue
            satir = ham.decode("utf-8", errors="replace").strip()
            if not satir:
                continue
            if satir.startswith("T "):
                self._telemetri_ayristir(satir)
            else:
                # Firmware'in insan okuyacagi cevaplari (RED:, ATIS ... ) YKI'nin
                # olay gunlugune dusurulur; operator neden reddedildigini gorur.
                self.gunluk_ekle(satir)
                self._yayinla({"tip": "log", "metin": satir})

    def _telemetri_ayristir(self, satir):
        """'T pan=1.5 tilt=0 estop=0 ...' -> sozluk. Bilinmeyen alanlar korunur."""
        d = {}
        for parca in satir[2:].split():
            if "=" not in parca:
                continue
            ad, _, deger = parca.partition("=")
            try:
                d[ad] = float(deger) if "." in deger else int(deger)
            except ValueError:
                d[ad] = deger
        if "pan" in d:
            d["pan_norm"] = aci_normal(d["pan"])
        # LiDAR ESP32'de degil Pi'de; ayni pakete burada karistiriliyor ki
        # tuketici tek kaynaktan okusun.
        if self.lidar is not None:
            d["lidar_cm"] = self.lidar.mesafe_cm if self.lidar.taze else None
            d["lidar_sinyal"] = self.lidar.sinyal if self.lidar.taze else None
        self.telemetri = d
        self.son_telemetri_t = time.time()
        paket = {"tip": "telemetri"}
        paket.update(d)
        self._yayinla(paket)

    def _yayinla(self, paket):
        for gonder in list(self.dinleyiciler):
            try:
                gonder(paket)
            except Exception:
                pass                       # kopan istemciyi sunucu temizler

    def durdur(self):
        self._dur = True
        if self.ser:
            try:
                self.gonder("dur")
                self.ser.close()
            except Exception:
                pass


class JogSurucu:
    """D-pad girdisini firmware'in anladigi 'su aciya git' komutlarina cevirir.

    Firmware'de hiz kipi olmadigi icin jog = hedefi surekli one itmek. Yon
    birakilinca ve JOG_ZAMAN_ASIMI dolunca `dur` gider (gozcu).
    """

    def __init__(self, taret):
        self.taret = taret
        self.pan_yon = 0.0
        self.tilt_yon = 0.0
        self.son_jog_t = 0.0
        self.suruyor = False
        self._dur = False
        self._hedef = {"pan": None, "tilt": None}   # eksene en son verilen hedef
        self._yon = {"pan": 0.0, "tilt": 0.0}       # o hedefin yonu

    def jog(self, pan, tilt):
        self.pan_yon = max(-1.0, min(1.0, float(pan)))
        self.tilt_yon = max(-1.0, min(1.0, float(tilt)))
        self.son_jog_t = time.time()

    def _hedefleri_unut(self):
        self._hedef["pan"] = self._hedef["tilt"] = None
        self._yon["pan"] = self._yon["tilt"] = 0.0

    def dur(self):
        self.pan_yon = self.tilt_yon = 0.0
        self._hedefleri_unut()
        if self.suruyor:
            self.taret.gonder("dur")
            self.suruyor = False

    def iptal(self):
        """Yon vektorunu sifirlar ama 'dur' GONDERMEZ.

        Otonom surus devraliyorken kullanilir: hemen ardindan bir hareket
        komutu gidecegi icin araya 'dur' atmak rampayi bosuna kirar ve taret
        her dongude sarsilir. Gozcu de susar cunku yon artik sifir.
        """
        self.pan_yon = self.tilt_yon = 0.0
        self._hedefleri_unut()
        self.suruyor = False

    def basla(self):
        threading.Thread(target=self._dongu, daemon=True).start()

    def _dongu(self):
        while not self._dur:
            time.sleep(JOG_YENILE)
            simdi = time.time()

            # Gozcu: jog yenilenmiyorsa (YKI coktu / ag koptu) durdur.
            if (self.pan_yon or self.tilt_yon) and \
                    simdi - self.son_jog_t > JOG_ZAMAN_ASIMI:
                self.dur()
                continue

            if not (self.pan_yon or self.tilt_yon):
                continue

            tel = self.taret.telemetri
            if not tel or tel.get("hazir") != 1:
                continue                   # acil stop / guc kesik: komut yollama

            # Iki eksen ayni anda surulebiliyor. Biri birakildiginda firmware'de
            # EKSEN BAZLI durdurma yok - `dur` ikisini birden yumusak frenler.
            # Kalan eksen ayni tikta yeniden hedeflenince eksenHedefle()
            # "ayni yonde devam" gorup rampasini KORUYOR, o yuzden gorunur bir
            # kesinti olmuyor.
            if ((not self.pan_yon and self._hedef["pan"] is not None) or
                    (not self.tilt_yon and self._hedef["tilt"] is not None)):
                self.taret.gonder("dur")
                self._hedefleri_unut()

            self._surdur("pan", self.pan_yon, tel.get("pan", 0.0), ILERI_BAKIS_PAN)
            self._surdur("tilt", self.tilt_yon, tel.get("tilt", 0.0), ILERI_BAKIS_TILT)

    def _surdur(self, eksen, yon, anlik, ileri_bakis):
        """Jog hedefini yalnizca GEREKTIGINDE tazeler.

        Firmware her hedef komutunda rampayi bastan kuruyor (eksenHedefle:
        hareketAdim=0, artis=artisBas). Her tikta yeniden hedeflemek bu yuzden
        ekseni surekli baslangic hizina dusuruyor, jog takila takila
        ilerliyordu. Burada hedef ancak uc durumda yenilenir: hic verilmemisse,
        yon degismisse, ya da kalan yol bitmek uzereyse.
        """
        if not yon:
            self._hedef[eksen] = None
            self._yon[eksen] = 0.0
            return
        hedef = self._hedef[eksen]
        if (hedef is None or self._yon[eksen] != yon
                or abs(hedef - anlik) < ileri_bakis * JOG_UZAT_ESIK):
            hedef = anlik + yon * ileri_bakis
            self.taret.gonder(f"{eksen} {hedef:.3f}")
            self._hedef[eksen] = hedef
            self._yon[eksen] = yon
        self.suruyor = True

    def durdur(self):
        self._dur = True


class OtonomSurucu:
    """Tespit sapmasini taret hareketine ceviren kapali dongu.

    `:8081/tespit` ucundan en yuksek guvenli hedefi alir, nisangahtan sapmayi
    dereceye cevirir ve MUTLAK aci komutu yollar. Firmware tek dogruluk
    kaynagidir: `hazir` degilse hicbir sey gonderilmez.

    ELLE SURUS ONCELIKLIDIR: jog tuslarina basildigi anda otonom o tik'i atlar,
    boylece operator her zaman devralabilir.
    """

    def __init__(self, taret, jog):
        self.taret = taret
        self.jog = jog
        self.acik = False
        self.son_hedef = None      # ("drone", 0.53) — panoda gostermek icin
        self._dur = False

    def ac(self, acik):
        self.acik = bool(acik)
        if not self.acik:
            self.son_hedef = None
            self.jog.dur()         # otonom kapatilinca eksenler yumusak dursun

    def basla(self):
        threading.Thread(target=self._dongu, daemon=True).start()

    def durdur(self):
        self._dur = True

    def _tespitler(self):
        try:
            with urllib.request.urlopen(OTO_TESPIT_UCU, timeout=0.5) as r:
                return json.loads(r.read().decode("utf-8")).get("tespitler") or []
        except Exception:
            return []              # kamera servisi yoksa otonom sessizce bekler

    def _dongu(self):
        while not self._dur:
            time.sleep(OTO_PERIYOT)
            if not self.acik:
                continue
            if self.jog.pan_yon or self.jog.tilt_yon:
                continue           # operator surerken karisma
            tel = self.taret.telemetri
            if not tel or tel.get("hazir") != 1:
                continue           # acil stop / guc kesik

            hedefler = self._tespitler()
            if not hedefler:
                self.son_hedef = None
                continue
            h = max(hedefler, key=lambda x: x.get("guven", 0.0))
            try:
                sx, sy = h["sapma"]
            except (KeyError, TypeError, ValueError):
                continue
            self.son_hedef = (h.get("sinif", "?"), round(h.get("guven", 0.0), 2))

            # Sapma -> derece. el'in isareti ters: karede ASAGI olan hedef
            # (sy > 0) tilt'in AZALMASINI ister.
            dp = (sx * (OTO_HFOV / 2.0)) * OTO_KAZANC
            dt = (-sy * (OTO_VFOV / 2.0)) * OTO_KAZANC
            if abs(dp) < OTO_OLU_BANT:
                dp = 0.0
            if abs(dt) < OTO_OLU_BANT:
                dt = 0.0
            if dp == 0.0 and dt == 0.0:
                continue           # hedef zaten nisangahta
            dp = max(-OTO_MAKS, min(OTO_MAKS, dp))
            dt = max(-OTO_MAKS, min(OTO_MAKS, dt))

            # Aci MUTLAK ve HAM (birikimli) olmali — `pan_norm` degil.
            self.jog.iptal()       # jog gozcusu araya `dur` atmasin
            if dp:
                self.taret.gonder(f"pan {tel.get('pan', 0.0) + dp:.3f}")
            if dt:
                self.taret.gonder(f"tilt {tel.get('tilt', 0.0) + dt:.3f}")


class Sunucu:
    """TCP 5005 — satir basina bir JSON nesnesi, cok istemcili."""

    def __init__(self, taret, jog, port=5005):
        self.taret = taret
        self.jog = jog
        self.otonom = None                 # main() baglar
        self.port = port
        self.istemciler = {}               # sock -> adres
        self._kilit = threading.Lock()

    def calis(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("0.0.0.0", self.port))
        s.listen(5)
        print(f"taret sunucusu dinliyor: 0.0.0.0:{self.port}")
        self.taret.dinleyiciler.append(self.yayinla)
        while True:
            baglanti, adres = s.accept()
            baglanti.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with self._kilit:
                self.istemciler[baglanti] = adres
            print(f"YKI baglandi: {adres[0]}:{adres[1]}")
            threading.Thread(target=self._istemci, args=(baglanti, adres),
                             daemon=True).start()

    def yayinla(self, paket):
        ham = (json.dumps(paket, ensure_ascii=False) + "\n").encode("utf-8")
        with self._kilit:
            hedefler = list(self.istemciler)
        for sock in hedefler:
            try:
                sock.sendall(ham)
            except Exception:
                self._kapat(sock)

    def _kapat(self, sock):
        with self._kilit:
            adres = self.istemciler.pop(sock, None)
        if adres:
            print(f"YKI ayrildi: {adres[0]}:{adres[1]}")
            # Baglanti koptuysa taret hareket halinde kalmasin.
            self.jog.dur()
        try:
            sock.close()
        except Exception:
            pass

    def _istemci(self, sock, adres):
        tampon = b""
        try:
            while True:
                parca = sock.recv(4096)
                if not parca:
                    break
                tampon += parca
                while b"\n" in tampon:
                    satir, _, tampon = tampon.partition(b"\n")
                    if satir.strip():
                        self._komut(satir.decode("utf-8", errors="replace"))
        except Exception:
            pass
        finally:
            self._kapat(sock)

    def _komut(self, satir):
        try:
            self.komut_uygula(json.loads(satir))
        except Exception:
            return

    def nisan(self, pan, tilt):
        """Mutlak aci komutu (otonom takip icin). Ham/birikimli derece bekler."""
        tel = self.taret.telemetri
        if not tel or tel.get("hazir") != 1:
            return          # tek dogruluk kaynagi firmware; istemci taklit etmez
        if pan is not None:
            self.taret.gonder(f"pan {float(pan):.3f}")
        if tilt is not None:
            self.taret.gonder(f"tilt {float(tilt):.3f}")

    def komut_uygula(self, p):
        """TCP (YKI) ve HTTP (konsol) ayni yoldan gecsin diye ortak."""
        tip = p.get("tip")

        if tip == "otonom":
            # Panodaki OTONOM dugmesi. `ac` yoksa mevcut durumu tersine cevir.
            if self.otonom is not None:
                self.otonom.ac(not self.otonom.acik if p.get("ac") is None
                               else p.get("ac"))
            return

        # Elle surus otonomu devre disi birakir: operator devraldi demektir.
        if tip in ("jog", "dur", "nisan") and self.otonom is not None \
                and self.otonom.acik:
            self.otonom.ac(False)

        if tip == "jog":
            self.jog.jog(p.get("pan", 0.0), p.get("tilt", 0.0))
        elif tip == "dur":
            self.jog.dur()
        elif tip == "ates":
            self.taret.gonder("ates")
        elif tip == "guc":
            # Yazilimsal acil durdur: ESP guc kesme yolu (ARM rolesi).
            self.jog.dur()
            self.taret.gonder("guc ver" if p.get("ver") else "guc kes")
        elif tip == "nisan":
            # Otonom takip. Aci MUTLAK ve HAM (birikimli) derecedir — telemetrideki
            # `pan` alaniyla ayni olcek, `pan_norm` DEGIL. Normalize deger
            # gonderilirse tezgahta 1626 -> 66 gibi devasa bir siçrama olur.
            self.jog.iptal()          # jog gozcusu araya girip hareketi kesmesin
            self.nisan(p.get("pan"), p.get("tilt"))
        elif tip == "estopreset":
            self.taret.gonder("estop reset")
        elif tip == "ham":
            komut = str(p.get("komut", "")).strip()
            if komut:
                self.taret.gonder(komut)


_pi_gecmis = {"cpu": None, "net": None, "disk": None, "t": 0.0}


def _pi_cpu():
    """Cekirdek basina kullanim yuzdesi — /proc/stat farkindan."""
    simdi = {}
    try:
        with open("/proc/stat") as f:
            for l in f:
                if l.startswith("cpu") and len(l) > 3 and l[3].isdigit():
                    p = l.split()
                    simdi[p[0]] = [int(x) for x in p[1:]]
    except OSError:
        return []
    onceki, _pi_gecmis["cpu"] = _pi_gecmis["cpu"], simdi
    if not onceki:
        return []                       # ilk cagri: fark yok
    cikti = []
    for ad in sorted(simdi, key=lambda a: int(a[3:])):
        a, b = onceki.get(ad), simdi[ad]
        if not a:
            continue
        top = sum(b) - sum(a)
        bos = (b[3] + b[4]) - (a[3] + a[4])     # idle + iowait
        cikti.append(round(100.0 * (top - bos) / top, 1) if top > 0 else 0.0)
    return cikti


def _ilk_sayi(yol, bol=1.0):
    try:
        with open(yol) as f:
            return float(f.read().split()[0]) / bol
    except (OSError, ValueError, IndexError):
        return None


def pi_kaynak():
    """Pi 5 kaynak durumu — hepsi /proc ve /sys'ten, harici komut yok.

    Olculemeyen alan None doner; konsol onu '—' basar. Uydurma deger
    dondurmek, alani hic dondurmemekten kotu: ekran gercek sistemin
    ustunde duruyor.
    """
    simdi = time.time()
    gecen = max(0.001, simdi - (_pi_gecmis["t"] or simdi))
    _pi_gecmis["t"] = simdi
    d = {"cekirdek": _pi_cpu(), "gpu": None, "hailo": None}

    # --- bellek
    bel = {}
    try:
        with open("/proc/meminfo") as f:
            for l in f:
                ad, _, deger = l.partition(":")
                bel[ad] = int(deger.split()[0]) // 1024      # MB
    except (OSError, ValueError):
        pass
    d["ramTop"] = bel.get("MemTotal")
    d["ram"] = (bel["MemTotal"] - bel["MemAvailable"]) if "MemAvailable" in bel else None
    d["swapTop"] = bel.get("SwapTotal")
    d["swap"] = (bel["SwapTotal"] - bel["SwapFree"]) if "SwapFree" in bel else None

    # --- disk
    try:
        st = os.statvfs("/")
        d["diskTop"] = int(st.f_blocks * st.f_frsize / 1048576)
        d["disk"] = int((st.f_blocks - st.f_bfree) * st.f_frsize / 1048576)
    except OSError:
        d["diskTop"] = d["disk"] = None

    # --- disk okuma/yazma hizi (sektor = 512 bayt)
    oku = yaz = 0
    try:
        with open("/proc/diskstats") as f:
            for l in f:
                p = l.split()
                if len(p) > 9 and p[2].startswith(("mmcblk", "nvme", "sd")) and not p[2][-1].isdigit():
                    oku += int(p[5]); yaz += int(p[9])
    except (OSError, ValueError):
        pass
    onc = _pi_gecmis["disk"]; _pi_gecmis["disk"] = (oku, yaz)
    if onc:
        d["diskOku"] = round((oku - onc[0]) * 512 / 1048576 / gecen, 2)
        d["diskYaz"] = round((yaz - onc[1]) * 512 / 1048576 / gecen, 2)
    else:
        d["diskOku"] = d["diskYaz"] = None

    # --- sicaklik / frekans / kisilma
    d["sicaklik"] = _ilk_sayi("/sys/class/thermal/thermal_zone0/temp", 1000.0)
    if d["sicaklik"] is not None:
        d["sicaklik"] = round(d["sicaklik"], 1)
    f = _ilk_sayi("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 1000.0)
    d["freq"] = int(f) if f else None
    d["kisik"] = 0
    try:
        cikti = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True,
                               text=True, timeout=1).stdout
        d["kisik"] = 1 if cikti.strip().endswith(("1", "2", "4", "8")) else 0
    except Exception:
        d["kisik"] = None

    # --- yuk ve ayakta kalma
    try:
        with open("/proc/loadavg") as fh:
            d["yuk"] = [float(x) for x in fh.read().split()[:3]]
    except (OSError, ValueError):
        d["yuk"] = None
    ay = _ilk_sayi("/proc/uptime")
    d["ayakta"] = int(ay) if ay else None

    # --- ag
    rx = tx = 0
    try:
        with open("/proc/net/dev") as fh:
            for l in fh:
                if ":" not in l:
                    continue
                ad, _, kalan = l.partition(":")
                ad = ad.strip()
                if ad == "lo":
                    continue
                p = kalan.split()
                rx += int(p[0]); tx += int(p[8])
    except (OSError, ValueError, IndexError):
        pass
    onc = _pi_gecmis["net"]; _pi_gecmis["net"] = (rx, tx)
    if onc:
        d["agRx"] = round((rx - onc[0]) / 1048576 / gecen, 2)
        d["agTx"] = round((tx - onc[1]) / 1048576 / gecen, 2)
    else:
        d["agRx"] = d["agTx"] = None

    # --- surecler: en cok CPU yiyen 10 tanesi; bizimkiler servis adiyla
    d["surec"] = []
    ETIKET = (("kamera_tespit", "hss-kamera"), ("taret_sunucu", "hss-taret"),
              ("hailo", "hss-hailo"))
    try:
        ps = subprocess.run(["ps", "-eo", "pid,pcpu,rss,user:12,comm,args",
                             "--sort=-pcpu"],
                            capture_output=True, text=True, timeout=2).stdout.splitlines()[1:]
        for l in ps[:10]:
            p = l.split(None, 5)
            if len(p) < 6:
                continue
            args = p[5]
            servis = ""
            ad = p[4]
            for iz, sv in ETIKET:
                if iz in args:
                    servis, ad = sv, iz + (".py" if iz != "hailo" else "")
                    break
            d["surec"].append({"pid": int(p[0]), "ad": ad, "servis": servis,
                               "kul": p[3], "cpu": float(p[1]),
                               "ram": int(p[2]) // 1024})
    except Exception:
        pass
    return d


class KonsolHTTP(BaseHTTPRequestHandler):
    """Donanim konsolu: tarayicidan acilan teshis ekrani (konsol.html).

    Ayri bir istemci kurulumu istemedigimiz icin (laptop, telefon, tablet fark
    etmesin) WebSocket yerine duz yoklama kullaniliyor: sayfa ~7 Hz `/veri`
    cekiyor, telemetri zaten 5 Hz uretiliyor. Komutlar `/komut`a POST edilir ve
    TCP 5005'tekiyle AYNI islevden gecer — iki arayuz ayrisamaz.
    """
    taret = None
    jog = None
    sunucu = None
    otonom = None
    protocol_version = "HTTP/1.1"

    def log_message(self, *_a):
        pass

    def _gonder(self, govde, tur="application/json; charset=utf-8", kod=200):
        if isinstance(govde, str):
            govde = govde.encode("utf-8")
        self.send_response(kod)
        self.send_header("Content-Type", tur)
        self.send_header("Content-Length", str(len(govde)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(govde)

    def _sayfa(self, dosya):
        try:
            with open(dosya, "rb") as f:
                self._gonder(f.read(), "text/html; charset=utf-8")
        except OSError:
            self._gonder(os.path.basename(dosya) + " bulunamadi",
                         "text/plain; charset=utf-8", 404)

    def do_GET(self):
        yol = self.path.split("?")[0]
        if yol in ("/", "/index.html"):
            self._sayfa(KONSOL_HTML)
        elif yol in ("/2", "/2/"):
            self._sayfa(KONSOL2_HTML)
        elif yol.startswith("/varlik/"):
            # Takimin amblemleri. Yalnizca dosya ADI alinir (dizin gecisi yok)
            # ve yalnizca .png servis edilir.
            ad = os.path.basename(yol[len("/varlik/"):])
            if not ad.endswith(".png"):
                self._gonder("{}", kod=404); return
            try:
                with open(os.path.join(VARLIK_DIZIN, ad), "rb") as f:
                    self._gonder(f.read(), "image/png")
            except OSError:
                self._gonder("{}", kod=404)
        elif yol == "/pi":
            self._gonder(json.dumps(pi_kaynak(), ensure_ascii=False))
        elif yol == "/veri":
            self._gonder(json.dumps(self._veri(), ensure_ascii=False))
        else:
            self._gonder("{}", kod=404)

    def do_POST(self):
        if self.path.split("?")[0] != "/komut":
            self._gonder("{}", kod=404)
            return
        n = int(self.headers.get("Content-Length", 0) or 0)
        ham = self.rfile.read(n).decode("utf-8", errors="replace") if n else ""
        try:
            self.sunucu.komut_uygula(json.loads(ham))
        except Exception:
            pass
        self._gonder('{"ok":true}')

    def _veri(self):
        t = self.taret
        yas = time.time() - t.son_telemetri_t if t.son_telemetri_t else 999.0
        # Sayfa en son gordugu gunluk numarasini yollar; yalnizca yenisi doner.
        try:
            bilinen = int(self.path.partition("gunluk=")[2] or 0)
        except ValueError:
            bilinen = 0
        yeni, no = t.gunluk_sonrasi(bilinen)
        return {
            "bagli": yas < 2.0,
            "yas": round(yas, 2),
            "telemetri": t.telemetri,
            "gunluk": yeni,
            "gunluk_no": no,
            # Otonom durumu SUNUCUDAN gelir; konsol kendi bayragini tutmaz,
            # yoksa iki arayuz ayrisir (biri "otonom" derken oteki "elle").
            "otonom": bool(self.otonom.acik) if self.otonom else False,
            "oto_hedef": self.otonom.son_hedef if self.otonom else None,
        }


def main():
    a = argparse.ArgumentParser(description="HSS taret sunucusu (YKI <-> ESP32)")
    a.add_argument("--port", type=int, default=5005)
    a.add_argument("--seri", default=None, help="ESP32 seri portu (varsayilan: by-id)")
    a.add_argument("--lidar", default=LIDAR_PORT, help="TF02-Pro portu (by-path!)")
    a.add_argument("--lidarsiz", action="store_true", help="LiDAR okuma")
    a.add_argument("--web-port", type=int, default=8090,
                   help="donanim konsolu HTTP portu (0 = kapat)")
    p = a.parse_args()

    lidar = None
    if not p.lidarsiz:
        lidar = Lidar(p.lidar)
        lidar.basla()

    taret = Taret(p.seri or esp_portu(), lidar=lidar,
                  otomatik_port=p.seri is None)
    try:
        taret.baglan()
    except Exception as ex:
        # ESP32 YOKKEN DE AYAGA KALK. Eskiden burada `return 1` vardi: konsol
        # (:8090) ve TCP (5005) ayni surecin icinde oldugu icin ESP takili
        # degilken DASHBOARD HIC ACILMIYORDU. systemd Restart=always ile
        # saniyede bir yeniden deniyor, 16 Agustos'ta yeniden baslatma sayaci
        # 356'ya ulasmisti; kamera ve tespit calisirken konsol goruntulenemiyordu.
        #
        # Yeniden baglanma zaten _oku_dongu icinde var ve dogru calisiyor:
        # ser None ise baglan(), hata olursa 2 sn bekle, tekrar dene. Yani
        # tek yapmamiz gereken pes etmemek — ESP sonradan takilinca kendiliginden
        # baglanir.
        #
        # EMNIYET: `hazir=1` gorulmedikce hicbir hareket komutu uygulanmiyor ve
        # telemetri gelmedigi icin `bagli` false kalir. ESP'siz sunucu bu yuzden
        # "sanki calisiyormus gibi" gorunmez, sadece izlenebilir olur.
        print(f"ESP32 acilamadi: {ex}", file=sys.stderr)
        print("Takili portlar: ls /dev/serial/by-id/", file=sys.stderr)
        print("ESP32 BEKLENIYOR — konsol ve TCP aciliyor, cihaz takilinca "
              "kendiliginden baglanacak.", file=sys.stderr)
    taret.basla()

    jog = JogSurucu(taret)
    jog.basla()

    otonom = OtonomSurucu(taret, jog)
    otonom.basla()

    sunucu = Sunucu(taret, jog, p.port)
    sunucu.otonom = otonom

    if p.web_port:
        KonsolHTTP.taret, KonsolHTTP.jog, KonsolHTTP.sunucu = taret, jog, sunucu
        KonsolHTTP.otonom = otonom
        web = ThreadingHTTPServer(("0.0.0.0", p.web_port), KonsolHTTP)
        web.daemon_threads = True
        threading.Thread(target=web.serve_forever, daemon=True).start()
        print(f"donanim konsolu    : http://<pi>:{p.web_port}/")

    try:
        sunucu.calis()
    except KeyboardInterrupt:
        print("\nKapaniyor...")
    finally:
        jog.durdur()
        taret.durdur()
        if lidar is not None:
            lidar.durdur()
    return 0


if __name__ == "__main__":
    sys.exit(main())
