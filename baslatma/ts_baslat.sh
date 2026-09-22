#!/bin/bash
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
# taret sunucusunu yeniden baslatir. pkill -f KULLANMA: kendi komut satirini
# esler ve cagiran kabugu oldurur. Port uzerinden oldurmek kesin ve guvenli.
fuser -k 5005/tcp >/dev/null 2>&1
sleep 1
setsid nohup python3 -u /home/pi/hss_workspace/scripts/taret_sunucu.py \
       > /tmp/ts.log 2>&1 < /dev/null &
sleep 5
echo "--- log ---"; cat /tmp/ts.log
echo "--- port ---"; ss -ltn | grep 5005 || echo "DINLEMIYOR"
