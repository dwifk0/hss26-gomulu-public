#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
"""ESP32-S3 seri koprusu.

Kullanim:
    python3 esp32_bridge.py              # /dev/ttyACM0 (ESP32-S3)
    python3 esp32_bridge.py /dev/ttyUSB1 # baska port

DIKKAT: /dev/ttyUSB0 LiDAR'dir (TF02-Pro), ESP32 DEGIL. Yanlis porta
baglanirsan ekrana ikili cop akar ve komutlarin hicbir yere ulasmaz.
"""
import sys
import threading

import serial

import glob as _glob

# ESP32-S3 native USB: by-id ismi MAC'ten turer, tekildir; port numarasi
# (ttyACM0/ttyACM1) her cikar-tak islerinde degisir, ona guvenme.
_esp = sorted(_glob.glob("/dev/serial/by-id/*Espressif*"))
VARSAYILAN_PORT = _esp[0] if _esp else "/dev/ttyACM0"
BAUD = 115200


def oku(ser):
    while True:
        try:
            ham = ser.readline()
            if not ham:
                continue
            satir = ham.decode("utf-8", errors="replace").strip()
            if satir:
                print(f"[ESP32] {satir}")
        except Exception as e:
            print(f"[HATA-oku] {e}")
            break


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else VARSAYILAN_PORT
    if "ttyUSB0" in port:
        print("UYARI: /dev/ttyUSB0 LiDAR portu. ESP32 icin /dev/ttyACM0 kullan.")

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 1
    # DTR/RTS'i asagi cekmemek onemli: ESP32-S3'u resetlemesin.
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except Exception as e:
        print(f"Port acilamadi ({port}): {e}")
        print("Takili portlar icin: ls /dev/ttyACM* /dev/ttyUSB*")
        return

    print(f"Baglandi: {port} @ {BAUD}")
    print("Yazip Enter'a bas -> ESP32'ye gider. '?' komut listesi. Cikis: Ctrl+C\n")

    t = threading.Thread(target=oku, args=(ser,), daemon=True)
    t.start()
    try:
        while True:
            mesaj = input()
            ser.write((mesaj + "\n").encode("utf-8"))
    except (KeyboardInterrupt, EOFError):
        print("\nKapaniyor...")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
