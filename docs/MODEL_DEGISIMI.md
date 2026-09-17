# Model değişim yordamı — `.hef`

Tespit hattı modelden bağımsızdır: model dosyası ve sınıf listesi değiştirilerek
yeni bir ağ devreye alınır. Bu belge o yordamı ve üzerinde durulması gereken
uyumluluk noktalarını anlatır.

## Ölçülen başarım

`.hef` olarak derlenen model Hailo-8L üzerinde:

```
model hazir [hailo] — girdi 640x640, esik 0.4, tespit olcegi 1/2, 4 sinif
{"fps": 30.0, "gecikme_ms": 24.5, "atlanan_kare": 0}
```

**30,0 fps · 24,5 ms · sıfır kare kaybı.** Aynı model Pi'nin işlemcisinde
(ONNX ile) 1,2 fps / 700 ms koşuyor — **25 kat** fark. Model dosyası 23,9 MB,
çip kaynağının %53–60'ını ve 4 bağlamı kullanıyor.

**Sürüm uyumu:** DFC 3.33 / HailoRT 4.23 ile derlenen dosya, Pi üzerindeki
**HailoRT 4.20.0** tarafından sorunsuz yüklenir; sürücü güncellemesi gerekmez.

Derleme tarifi ve tuzakları [`../derleme/`](../derleme/) altında
(Dockerfile, `.alls`, NMS yapılandırması, `README_derleme.md`).

## Kuantalama notu

Mevcut dosya 64 örneklik bir kalibrasyon kümesiyle ve optimizasyon seviyesi 0 ile
kuantalanmıştır. Dataflow Compiler, kalibrasyon kümesi 1024'ten küçükse ve GPU
yoksa seviyeyi kendiliğinden düşürür; bu durumda bias düzeltme ve ince ayar
adımları atlanır. `conv63` çıkış bitlerinin yarıdan fazla kısılması özellikle
uzaktaki küçük hedefleri etkileyen katmandır.

1024 görsellik kalibrasyon dizisiyle yeniden kuantalamak, aynı tarif üzerinden
tek adımdır; çalışan dosya yerinde durduğu için geri dönüş bedelsizdir.

> **16 Ağustos'ta kodda düzeltilen tuzak:** ayrıştırma eskiden sınıf sayısının
> **80 olduğunu varsayıyordu** (`len(ham[0]) == len(COCO)`). Özel model bu
> eşitliği tutturamayacağı için batch boyutu atılmıyor, döngüye tek eleman
> giriyor ve süreç **hiç hata vermeden sıfır tespit döndürüyordu**. Artık karar
> çıktının yapısına bakılarak veriliyor, sınıf sayısı modelden okunuyor.
> Regresyon testi: `scripts/test_ayristirma.py` (Hailo donanımı gerektirmez).

---

## 1. Yeni model için gereken üç şey

1. **`.hef` dosyası** — Hailo Model Zoo / Dataflow Compiler çıktısı.
   **`--hw-arch hailo8l` ile derlenmiş olmalı.** Kartımız AI HAT+ üzerinde
   **Hailo-8L**; düz Hailo-8 için derlenmiş dosya yüklenmez.
2. **Sınıf adları, eğitimdeki sırayla** — YOLO'da `data.yaml` içindeki `names`.
   Model indeks döndürür, ad yalnızca bu listeden gelir; **sıra kayarsa İHA'ya
   "füze" der.**
3. **NMS'in nerede olduğu** — çip üzerinde mi (`HAILO NMS BY CLASS`), yoksa ham
   çıktı mı? Kodumuz **çip üzerinde NMS'li** çıktıyı ayrıştırıyor. Ham çıktı
   gelirse ayrıştırıcı yeniden yazılmalı, bu ayrı bir iş.

**`.pt` geldiğinde:** dönüşüm Pi'de yapılamaz. Hailo Dataflow Compiler
**x86-64 Linux** ister ve kuantalama için birkaç yüz görsellik kalibrasyon
kümesi gerekir. Bu WSL'de de doğrudan çalışmaz. Dönüşüm bu yüzden ayrı bir
görev olarak planlanır; doğrudan `.hef` almak her zaman daha ucuzdur.

---

## 2. Dosyaları yerine koy

Container `/home/pi/hss_workspace` dizinini `/hss_workspace` olarak bağlıyor
(`pi_kurulum/docker-compose.override.yaml`), yani host'a koymak yeterli:

```bash
# Pi'de
mkdir -p ~/hss_workspace/modeller
# .hef ve etiket dosyasını buraya kopyala (scp ile)
```

Etiket dosyası — `~/hss_workspace/modeller/siniflar.txt`, satır başına bir ad,
**eğitimdeki sırayla**:

```
iha
f16
helikopter
fuze
```

JSON liste (`["iha", "f16", ...]`) veya indeks sözlüğü (`{"0": "iha", ...}`)
de kabul edilir; `#` ile başlayan satırlar yorumdur.

---

## 3. Önce elle dene, servisi sonra değiştir

Servisi bozmadan, çalışan kameranın yanında denemek en güvenlisi değil —
**Hailo'yu aynı anda tek süreç kullanabilir**, ikincisi
`HAILO_OUT_OF_PHYSICAL_DEVICES (74)` ile düşer. Bu yüzden önce servisi durdur:

```bash
sudo systemctl stop hss-kamera
sudo docker exec -it pi-ai-kit-ubuntu-hailo-ubuntu-pi-1 \
  python3 -u /hss_workspace/scripts/kamera_tespit.py --dondur 180 \
    --hef /hss_workspace/modeller/yarisma.hef \
    --etiketler /hss_workspace/modeller/siniflar.txt
```

**Açılış satırında şunu görmelisin:**

```
model hazir — girdi 640x640, esik 0.4, tespit olcegi 1/2, 4 sinif, tum siniflar
```

Sınıf sayısı beklediğin rakam değilse dur, ilerleme.

**Basılabilecek uyarılar ve anlamları:**

| Uyarı | Anlamı |
|---|---|
| `etiket dosyasinda N ad var ama model M sinif donduruyor` | Etiket listesi modelle uyuşmuyor — **adlar kaymış**, düzeltmeden devam etme |
| `model N sinifli (COCO degil) ama --etiketler verilmedi` | Tespit çalışır ama adlar `0`, `1`, `2`… olur |
| `--siniflar'daki [...] bu modelde YOK` | Filtre her şeyi eliyor; eski sınıf adı kalmış |
| `cikti N sinif tasiyor ama HEF M diyor` | Çıktı yapısı beklenenden farklı, muhtemelen NMS çip üzerinde değil |

Sonra tespit geliyor mu diye bak:

```bash
curl -s http://<TARET_IP>:8081/tespit | head -c 400
```

`tespitler` dizisi dolu ve `sinif` alanları gerçek adlar mı? Konsoldan da
görülür: `http://<pi>:8090/2` → Tuval, kutular görüntünün üstünde çizilir.

---

## 4. Servisi kalıcı olarak güncelle

Denemeden sonra `systemd/hss-kamera.service` içindeki `ExecStart` satırına iki
argümanı ekle:

```
ExecStart=/usr/bin/docker exec pi-ai-kit-ubuntu-hailo-ubuntu-pi-1 \
          python3 -u /hss_workspace/scripts/kamera_tespit.py --dondur 180 \
          --hef /hss_workspace/modeller/yarisma.hef \
          --etiketler /hss_workspace/modeller/siniflar.txt
```

```bash
sudo systemctl daemon-reload
sudo systemctl start hss-kamera
journalctl -u hss-kamera -n 30
```

**`docker compose` çağırırken `--build` yapma** — imajdaki `hailo-all` gider.
Süreçleri `pkill -f` ile öldürme, port üzerinden öldür (`fuser -k 8080/tcp`).

---

## 5. Model değişince gözden geçirilecek yerler

- **Eşik.** `--esik 0.40` COCO ağırlıkları için seçilmişti. Özel model daha az
  sınıfla daha keskin güven dağılımı verir; yeniden ayarlanması gerekebilir.
- **Otonom takip hedef sınıfı.** Konsol/YKİ tarafında sınıf adına göre bir
  seçim varsa yeni adlarla güncellenmeli.
- **Menzil.** Hedefler 0–15 m'de ve maket boyutunda; 640×640 letterbox'ta küçük
  hedefin kaç piksel kaldığına bak. Tespit zayıfsa `--tespit-olcek 1` (tam
  çözünürlük) denenebilir, bedeli kare başına ~4,4 ms CPU.
