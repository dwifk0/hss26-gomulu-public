#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
"""ESP32-S3'e komut gonderip cevabi toplayan tek atimlik yardimci.

Kullanim:
    python3 hss_komut.py "durum"
    python3 hss_komut.py --bekle 3 "estop" "durum"
"""
import sys
import time

import serial

import glob as _glob

# ESP32-S3 native USB: by-id ismi MAC'ten turer, tekildir; port numarasi
# (ttyACM0/ttyACM1) her cikar-tak islerinde degisir, ona guvenme.
_esp = sorted(_glob.glob("/dev/serial/by-id/*Espressif*"))
PORT = _esp[0] if _esp else "/dev/ttyACM0"
BAUD = 115200


def main():
    argv = sys.argv[1:]
    bekle = 1.5
    if argv and argv[0] == "--bekle":
        bekle = float(argv[1])
        argv = argv[2:]

    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = 0.2
    ser.dtr = True
    ser.rts = False
    ser.open()
    time.sleep(0.3)
    ser.reset_input_buffer()

    for komut in argv:
        print(f">>> {komut}")
        ser.write((komut + "\n").encode())
        ser.flush()
        son = time.time() + bekle
        while time.time() < son:
            ham = ser.readline()
            if ham:
                satir = ham.decode("utf-8", errors="replace").rstrip()
                if satir:
                    print(f"    {satir}")
                son = time.time() + 0.4
    ser.close()


if __name__ == "__main__":
    main()
