#!/bin/bash
# SPDX-License-Identifier: LicenseRef-dwifk0-All-Rights-Reserved
# Copyright (C) 2026 Ahmet Efe Nezli
xhost +local:docker
cd ~/pi-ai-kit-ubuntu
sudo docker compose up -d hailo-ubuntu-pi
sudo docker compose exec hailo-ubuntu-pi rpicam-hello -t 0 \
  --post-process-file /root/rpicam-apps/assets/hailo_yolov8_inference.json \
  --lores-width 640 --lores-height 640 --qt-preview
