# Operatör konsolu

`scripts/konsol_birlesik.html`

Operatör konsolu tek bir HTML dosyasıdır: derleme adımı, paket yöneticisi ve
çalışma zamanı bağımlılığı yok. Pi üzerinde `taret_sunucu.py` tarafından
servis edilir, tarayıcıdan açılır.

---

## Ne yapar

Tek kabuk altında dört görünüm, aralarında geçiş (`0`–`3` tuşları):

| Görünüm | İçerik |
|---|---|
| **Katman 0** | Donanım konsolunun düzeni + geliştirici bölümleri. Yalnız geliştirici modunda. |
| **Tuval** | Serbest yerleşimli pano. Sürüklenir, boyutlandırılır, üç kayıtlı düzen. |
| **Katman** | Kamera tam ekran, üstünde yüzen cam paneller (HUD). |
| **Kayıt** | Sinyal ağacı, küçük çoklu grafikler, ham örnek müfettişi. |

Dördü **aynı durum nesnesini** okur; görünüm değiştirmek veriyi değil sunumu
değiştirir. Emniyet ve komutlar tek yoldan (`komutCalistir`) geçer.

Ek olarak: 15 renk paleti (Apple + One UI), açık/koyu/oto tema, dinamik ada
bildirimleri, bölümleri seçilebilen manuel kontrol, pan ENA boşta sayacı.

---

## Geliştirici modu

Gizli giriş: başlığa 2 sn içinde **5 tıklama**, sonra **2 sn basılı tut**.
Ya da `Ctrl+Shift+D`. Hesap `admin`.

Bu kapı operatörün kazara geliştirici bölümlerine girmesini engeller; bir
güvenlik sınırı değildir. Tarayıcı tarafında tutulan hiçbir parola gizli
değildir — kaynağı açan görür.

**Bu depoda parola sabiti boş bırakılmıştır.** Gerçek doğrulama sunucu
tarafında yapılmalıdır: parola `taret_sunucu.py` içinde ortam değişkeninden
okunur, oturum çerezle taşınır, konsol yalnızca sunucudan gelen yetki
cevabına bakar.

---

## Tarayıcı tarafında dikkat edilen noktalar

- **`.gorunum` konumlandırmasını id seçicisiyle ezme.** `#gKatman{position:relative}`
  yazıldığında kutu akışa düşüyor, bütün çocukları `absolute` olduğu için
  yüksekliği sıfırlanıyor ve görünüm tamamen kayboluyor.
- **Her tik yeniden basılan bölüme düğme koyma.** Düğme `mousedown` ile
  `mouseup` arasında yok oluyor, tarayıcı `click` olayını hiç üretmiyor.
  Girdi ya da düğme içeren bölümler `statik:true` işaretlenir, yalnız değişen
  alanları `canli()` günceller.
- **Aynı `id`'yi iki görünümde birden kullanma.** Manuel kontrol hem panoda hem
  HUD'da açıktı; `getElementById` hep ilkini bulduğu için öteki panele yazılan
  değer kayboluyordu. Arama kendi kapsayıcısı içinde yapılır (`data-alan`).
- **Gizli görünümde `clientHeight` sıfırdır.** Sınır hesabı buna dayanıyorsa
  saklı panonun yerleşimi kırpılır. Ölçü almadan önce görünürlük kontrol edilir.

---

