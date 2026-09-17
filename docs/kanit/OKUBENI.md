# Kanıt görselleri

**`2026-08-19_atolye_karesi.jpg`** — Pi kamerasından canlı alınan kare
(`:8080/stream.mjpg`, 1280×720, `--dondur 180` uygulanmış hâli). Üç hedef de
karede: mavi F-16 solda mengenede, kırmızı dron ortada, kırmızı füze sağda,
altta namlu. Bu karede tam hassasiyetli `.pt` (conf 0,01):
`fuze` **0,826** · `drone` **0,750** · `f16` **hiçbir şey**.

**`2026-08-19_f16_siyah_zeminde.jpg`** — aynı karedeki F-16, mavi baskın
piksellere göre kabaca kesilip siyah tuvale gömülmüş hâli. Aynı model bu
görselde `f16` **0,451** veriyor. İki görsel arasındaki tek fark çevre;
teşhisin dayanağı budur.

## Neden burada duruyor

Model f16'yı "bilmiyor" değil: aynı ağırlıklar, aynı eşik, aynı uçak — tek
değişen çevre. Atölye karesinde F-16 mavi bir mengenenin üstünde, arkasında
mavi-gri makine gövdesi ve duvar var; kontur arka plana karışıyor. Siyah zemine
alındığında aynı nesne 0,451 ile çıkıyor.

Buradan çıkan sonuç, eşiği düşürmek ya da modeli suçlamak değil: **eğitim
kümesinin arka plan çeşitliliği yetersiz.** Hedefler eğitimde ağırlıklı olarak
temiz/kontrast zeminde görülmüş, sahada ise dağınık bir atölye zemini var.
Çözüm veri tarafında — zor negatif ve karışık zeminli örnek eklemek.

İki görsel bu depoda, iddianın kendi başına doğrulanabilmesi için duruyor.
