#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
xhost +local:docker
cd ~/pi-ai-kit-ubuntu
sudo docker compose up -d hailo-ubuntu-pi
sudo docker compose exec hailo-ubuntu-pi rpicam-hello -t 0 \
  --post-process-file /root/rpicam-apps/assets/hailo_yolov8_inference.json \
  --lores-width 640 --lores-height 640 --qt-preview
