#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
# TF02-Pro LiDAR okuma - mesafe ve sinyal gucu
import serial

# DIKKAT: eldeki iki CP2102'nin de seri numarasi "0001" - by-id ikisini
# ayirt edemiyor ve yanlis cihaza baglaniyor. Tek ayirt edici fiziksel yuva:
# by-path. Adaptoru baska USB girisine takarsan bu yol degisir.
PORT = "/dev/serial/by-path/platform-xhci-hcd.0-usb-0:2:1.0-port0"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)
print(f"TF02-Pro okunuyor: {PORT} @ {BAUD}  (Ctrl+C ile cik)\n")

try:
    while True:
        # Cerceve basligini bul: 0x59 0x59
        if ser.read(1) == b'\x59':
            if ser.read(1) == b'\x59':
                cerceve = ser.read(7)  # kalan 7 byte
                if len(cerceve) == 7:
                    dist = cerceve[0] + (cerceve[1] << 8)      # mesafe cm
                    strength = cerceve[2] + (cerceve[3] << 8)  # sinyal gucu
                    print(f"Mesafe: {dist:4d} cm   Sinyal: {strength:5d}", end="\r")
except KeyboardInterrupt:
    print("\nKapaniyor...")
finally:
    ser.close()
