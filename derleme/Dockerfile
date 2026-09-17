# Hailo Dataflow Compiler 3.33.0 icin en kucuk calisan ortam.
#
# NEDEN KONTEYNER: DFC Python 3.10 istiyor, bu WSL'de 3.12 var. Ayrica DFC
# resmi olarak Ubuntu 20.04/22.04 destekliyor, sistemimiz 24.04. Konteyner
# ikisini de cozer ve ana sisteme hicbir sey bulastirmaz.
#
# Suite'in kendi install.sh'i onlarca GUI/TAPPAS bagimliligi da kuruyor;
# bize yalnizca DERLEME lazim, o yuzden sadece DFC + HailoRT kuruluyor.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONDONTWRITEBYTECODE=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3.10 python3.10-dev python3.10-venv python3-pip \
        build-essential graphviz graphviz-dev \
        libgl1 libglib2.0-0 libsm6 libxext6 libxrender1 \
        ca-certificates git wget \
    && rm -rf /var/lib/apt/lists/*

# SANAL ORTAM ZORUNLU — sistem pip'i ile KURMA.
# DFC, "kurulu surum muyum" sorusunu kendi yolunda `site-packages` kelimesi
# geciyor mu diye bakarak cevapliyor (hailo_sdk_common/paths_manager/paths.py:40).
# Ubuntu'nun sistem pip'i paketleri `dist-packages` altina koyar, dolayisiyla
# bu kontrol basarisiz olur; `join_hailo_tools_path` None doner ve derleyici
# ikilisini (hailo_tools/build/compiler) calistiran subprocess
# "TypeError: expected str... not NoneType" ile patlar. Kuantalama sorunsuz
# gecer, hata YALNIZCA derleme adiminda cikar.
# venv icinde yol .../lib/python3.10/site-packages olur ve kontrol saglanir.
RUN python3.10 -m venv /opt/venv
ENV PATH=/opt/venv/bin:$PATH
RUN pip install --no-cache-dir --upgrade "pip<25" setuptools wheel

# Wheel'ler suite arsivinden geliyor (artifacts/). 512 MB'lik DFC en son
# kopyalaniyor ki onceki katmanlar onbellekte kalsin.
WORKDIR /opt/hailo
COPY artifacts/hailort-4.23.0-cp310-cp310-linux_x86_64.whl /opt/hailo/
RUN pip install --no-cache-dir /opt/hailo/hailort-4.23.0-cp310-cp310-linux_x86_64.whl

COPY artifacts/hailo_dataflow_compiler-3.33.0-py3-none-linux_x86_64.whl /opt/hailo/
RUN pip install --no-cache-dir \
        /opt/hailo/hailo_dataflow_compiler-3.33.0-py3-none-linux_x86_64.whl

# Model Zoo: YOLOv8 icin hazir .alls (kuantalama tarifi) ve son islem
# yapilandirmalari burada. Cip uzerinde NMS bunlarla kuruluyor.
COPY model_zoo /opt/hailo/model_zoo
RUN pip install --no-cache-dir -e /opt/hailo/model_zoo || true

WORKDIR /is
CMD ["/bin/bash"]
