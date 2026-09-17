# HSS yarışma modeli — Hailo-8L derleme notları

`migfer.alls` yorum satırı **içeremez** — Hailo'nun model betiği ayrıştırıcısı
`//` veya `#` kabul etmiyor, ilk karakterde `ParseException` veriyor. Açıklama
bu yüzden burada duruyor.

## Tarifin satır satır anlamı

```
normalization1 = normalization([0.0, 0.0, 0.0], [255.0, 255.0, 255.0])
```
Normalizasyon **çip üzerinde**. Girdi 0–255 UINT8 gelir, bölme donanımda yapılır.
Pi tarafında ek bir ölçekleme gerekmiyor; `kamera_tespit.py` zaten UINT8 besliyor.
Kalibrasyon dizisi de bu yüzden **ham 0–255** olmalı, 0–1'e bölünmüş değil.

```
change_output_activation(conv42, sigmoid)
change_output_activation(conv53, sigmoid)
change_output_activation(conv63, sigmoid)
```
Sınıf dallarının çıkışına sigmoid. Ultralytics bunu ONNX grafiğinde Detect
başlığının içinde yapıyor; grafiği o başlıktan **önce** kestiğimiz için burada
geri ekleniyor. Unutulursa güven değerleri ham logit olarak çıkar ve eşik
karşılaştırması tamamen anlamsızlaşır.

```
nms_postprocess("/is/migfer_nms_config.json", meta_arch=yolov8, engine=cpu)
```
Kutu çözme + NMS. `engine=cpu`, NMS'i HailoRT'nin ana işlemcide yapması demek;
çıktı yine **NMS BY CLASS** biçiminde gelir ve `hailo_tespit.py` tam bunu
ayrıştırır.

## Katman adları neden Hailo'nunkiyle aynı

Modelimiz de yolov8s olduğu için ayrıştırıcı katmanları birebir aynı
numaralandırdı: `conv41/42` (stride 8), `conv52/53` (16), `conv62/63` (32).
Dolayısıyla Hailo'nun hazır `yolov8s.alls` tarifi olduğu gibi geçerli. Tek
değişiklik NMS yapılandırmasında: **`classes: 4`** (80 yerine).

## Çıkış düğümleri

ONNX grafiği Detect başlığından önce kesiliyor:

```
/model.22/cv2.0/cv2.0.2/Conv   /model.22/cv3.0/cv3.0.2/Conv
/model.22/cv2.1/cv2.1.2/Conv   /model.22/cv3.1/cv3.1.2/Conv
/model.22/cv2.2/cv2.2.2/Conv   /model.22/cv3.2/cv3.2.2/Conv
```

`cv2.*` kutu dalı (64 kanal = 16×4 DFL), `cv3.*` sınıf dalı (**4 kanal**).
Ayrıştırıcı bu düğümleri kendisi de öneriyor ("NMS structure of yolov6 or
equivalent was detected").

## Komut sırası

```bash
hailo parser onnx migfer_v2_best.onnx --hw-arch hailo8l --har-path migfer.har \
  --end-node-names <yukarıdaki 6 düğüm>          # etkileşimli soru sorar: stdin'e "n" ver
hailo optimize migfer.har --hw-arch hailo8l \
  --calib-set-path kalib_256.npy --model-script migfer.alls \
  --output-har-path migfer_opt.har
hailo compiler migfer_opt.har --hw-arch hailo8l --output-dir .
```

**Tuzak:** `hailo ... | tail` yazarsan `set -e` hatayı yakalayamaz (çıkış kodu
`tail`'den gelir) ve sonraki adım boşuna koşar. `set -o pipefail` şart.

## ⚠ TUZAK: DFC sanal ortam DIŞINDA derleyemez

Sistem pip'i ile kurarsan **kuantalama sorunsuz geçer, derleme adımı**
şu hatayla düşer:

```
TypeError: expected str, bytes or os.PathLike object, not NoneType
```

Sebep `hailo_sdk_common/paths_manager/paths.py:40`:

```python
self._is_release = "site-packages" in os.path.dirname(os.path.dirname(hailo_sdk_common.origin))
```

DFC "kurulu sürüm müyüm" sorusunu **kendi yolunda `site-packages` kelimesi
geçiyor mu** diye bakarak cevaplıyor. Ubuntu'nun sistem pip'i paketleri
`dist-packages` altına koyar → kontrol başarısız → `join_hailo_tools_path()`
`None` döner → derleyici ikilisini (`hailo_tools/build/compiler`, 49 MB, paket
içinde **mevcut**) çalıştıran `subprocess.Popen` boş yolla patlar.

**Çözüm:** `python3.10 -m venv /opt/venv` içine kur. venv'de yol
`.../lib/python3.10/site-packages` olur ve kontrol sağlanır. Hailo'nun kendi
`install.sh`'i zaten sanal ortam kurduğu için bu hata onların akışında hiç
görünmüyor.

Hata mesajı sebebi hiç ele vermiyor; yığın izini `traceback.print_exc()` ile
açmadan kök nedene ulaşmak mümkün değil.

## Sürüm uyumu — açık risk

Derleyici **DFC 3.33.0 / HailoRT 4.23.0**, Pi'de ise **HailoRT 4.20.0** var.
Arşivde 4.20'ye denk gelen suite yayında değildi. Üretilen `.hef` Pi'ye
yüklenmezse `hailort-pcie-driver_4.23.0_all.deb` indirildi ve hazır —
ama Pi'yi yükseltmek konteynerdeki çalışma zamanını da kapsıyor, son çare.
