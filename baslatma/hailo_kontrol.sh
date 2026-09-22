#!/bin/bash
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
# HSS — Hailo AI HAT+ saglik kontrolu ve onarimi (Raspberry Pi 5, host'ta calistirilir)
#
#   ./hailo_kontrol.sh          durumu raporlar
#   ./hailo_kontrol.sh --onar   eksikleri gidermeye calisir
#
# Arka plan: 2026-07-28'de kernel 6.8.0-1057 -> 1060 guncellemesinde surucu
# kayboldu, cunku elle "make install" ile kurulmustu. Artik DKMS'e kayitli
# (AUTOINSTALL=yes), yani kernel guncellemelerinde otomatik derleniyor.
# hailo-all ise container ICINDE kurulu (upstream Dockerfile onu bilerek disliyor);
# imaja islendigi icin normal yeniden baslatmalarda kalici.

set -u
ONAR=0
[[ "${1:-}" == "--onar" ]] && ONAR=1

KAP=pi-ai-kit-ubuntu-hailo-ubuntu-pi-1
IMAJ=pi-ai-kit-ubuntu-hailo-ubuntu-pi:latest
hata=0

bilgi() { printf '  %-34s %s\n' "$1" "$2"; }
basarili() { printf '  \e[32m✓\e[0m %-32s %s\n' "$1" "$2"; }
basarisiz() { printf '  \e[31m✗\e[0m %-32s %s\n' "$1" "$2"; hata=1; }

echo "HSS — Hailo durum raporu"
echo

# 1) PCI cihazi
if lspci 2>/dev/null | grep -qi hailo; then
    basarili "PCI cihazi" "$(lspci | grep -i hailo | head -1 | cut -c1-60)"
else
    basarisiz "PCI cihazi" "gorunmuyor — kart oturmamis olabilir"
fi

# 2) Kernel basliklari (DKMS derlemesi icin sart)
if [[ -d /lib/modules/$(uname -r)/build ]]; then
    basarili "Kernel basliklari" "$(uname -r)"
else
    basarisiz "Kernel basliklari" "eksik: linux-headers-$(uname -r)"
    if (( ONAR )); then
        echo "    -> kuruluyor..."
        sudo apt-get install -y "linux-headers-$(uname -r)"
    fi
fi

# 3) DKMS kaydi
if dkms status 2>/dev/null | grep -q "hailo_pci.*$(uname -r).*installed"; then
    basarili "DKMS" "$(dkms status | grep hailo_pci | head -1)"
else
    basarisiz "DKMS" "hailo_pci bu kernel icin kayitli degil"
    if (( ONAR )); then
        echo "    -> DKMS'e kuruluyor..."
        ( cd ~/hailort-drivers/linux/pcie && sudo make install_dkms )
    fi
fi

# 4) Modul yuklu mu
if lsmod | grep -q hailo_pci; then
    basarili "Cekirdek modulu" "hailo_pci yuklu"
else
    basarisiz "Cekirdek modulu" "hailo_pci yuklu degil"
    (( ONAR )) && { echo "    -> modprobe..."; sudo modprobe hailo_pci; }
fi

# 5) Cihaz dugumu
if [[ -e /dev/hailo0 ]]; then
    basarili "Cihaz dugumu" "/dev/hailo0"
else
    basarisiz "Cihaz dugumu" "/dev/hailo0 yok"
fi

# 6) Firmware
if [[ -e /lib/firmware/hailo/hailo8_fw.bin ]]; then
    basarili "Firmware" "/lib/firmware/hailo/hailo8_fw.bin"
else
    basarisiz "Firmware" "hailo8_fw.bin eksik — download_firmware.sh calistir"
fi

# 7) Container ayakta mi
if sudo docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${KAP}$"; then
    basarili "Container" "$KAP calisiyor"

    # 8) Container icinde hailort
    if sudo docker exec "$KAP" sh -c 'command -v hailortcli' >/dev/null 2>&1; then
        surum=$(sudo docker exec "$KAP" hailortcli fw-control identify 2>/dev/null \
                | tr -d '\000' | grep -a "Firmware Version" | head -1)
        basarili "Container hailort" "${surum:-kurulu}"
    else
        basarisiz "Container hailort" "hailo-all kurulu degil (imaj yeniden mi kuruldu?)"
        if (( ONAR )); then
            echo "    -> container icinde hailo-all kuruluyor (birkac dakika)..."
            sudo docker exec "$KAP" bash -c "apt-get update -qq && apt-get install -y hailo-all" \
                && echo "    -> imaja isleniyor (kalici olsun diye)..." \
                && sudo docker commit "$KAP" "$IMAJ"
        fi
    fi
else
    basarisiz "Container" "$KAP calismiyor"
    (( ONAR )) && { echo "    -> baslatiliyor..."; ( cd ~/pi-ai-kit-ubuntu && sudo docker compose up -d hailo-ubuntu-pi ); }
fi

echo
if (( hata )); then
    (( ONAR )) || echo "Onarmak icin: $0 --onar"
    exit 1
fi
echo "Hailo saglikli."
