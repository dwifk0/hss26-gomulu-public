# Ateşleme Zinciri — Mimari

> ## ⚠ Yüksek gerilim
>
> Bu sistem yüzlerce volt seviyesinde bir darbe kondansatörü bankası içerir ve
> depoladığı enerji insan için ölümcül mertebededir. Dolu kondansatörler besleme
> kesildikten sonra da tehlikelidir; boşaltıldıktan sonra **dielektrik geri
> kazanımı** ile kendiliğinden birkaç on volta geri çıkabilirler.
>
> **Bu belge bir yapım tarifi değildir.** Parça numaraları, kondansatör
> değerleri, bobin ve namlu ölçüleri bu depoda bilinçli olarak yayımlanmamıştır.
> Anlatılan şey, yüksek gerilimli ve yazılımla durdurulamayan bir aktüatörün bir
> gömülü sisteme nasıl **emniyetli biçimde bağlandığıdır**.

---

## Tasarımın belirleyici kararı: namlu ile denetleyici bağımsız

Sistemin en belirleyici kararı, ateşleme zincirinin mikrodenetleyiciden
**ayrılmasıdır**. Kademelerin anahtarlama kapıları firmware'e hiç girmez; her
kademe kendi optik kapısıyla kendi anahtarını tetikler. Denetleyicinin atış
üzerindeki tek yetkisi **şarjörü sürmek** — yani mermi vermek — ve şarj hattını
yönetmektir.

Bu, kaybedilmiş bir yetenek değil, bilinçli bir sadeleştirmedir: yüksek gerilim
anahtarlarının tetiklenmesi yazılım zamanlamasına değil, optik kapının fiziğine
bırakılır. Yazılımın yapabileceği en kötü hata bile bir kademeyi yanlış anda
tetikleyemez.

Kararın üç doğrudan sonucu var ve üçü de sistemin geri kalanını şekillendirir:

1. **Kilit, ateşleme anahtarını değil şarjörü kilitler.** Yazılımın hâlâ yetkili
   olduğu tek nokta orasıdır.
2. **Mermi namluya girdikten sonra yazılım atışı durduramaz.** Zincir kendi
   kendine tamamlanır.
3. **Kademeler arası geçiş süresi ölçülemez.** Ölçüm için gerekli hatlar
   denetleyiciye bağlı değildir.

---

## Neden kilit bir adım önde

Ateşleme anahtarı olarak kullanılan tristör (SCR) **mandallı** bir elemandır:
tetiklendikten sonra denetleyici onu kapatamaz. Deşarj akımı doğal olarak sıfırı
geçince kendi kendine söner; freewheel diyotları bu akımı şekillendirir. Yani
kontrol edilen tek şey **tetikleme anıdır**, süre ve kapanış devrenin kendi
fiziğine aittir.

Atışa yasak sektör kilidi bu yüzden zincirin **bir adım öncesine** konulmuştur.
Namlu ağzı yasak bir sektöre bakarken kilitlenen şey anahtar değil, mermi verme
hareketidir — çünkü orası, kilidin gerçekten uygulanabildiği son noktadır.

Bu, yazılım emniyetinin genel kuralının somut hâli: **kilidi, geri alınamaz
adımın öncesine koy.** Geri alınamaz adımın üstüne konan kilit bir kilit değil,
temennidir.

---

## Optik kapı

Her kademede bir IR verici ile bir fototransistör namlunun iki yanına bakar ve
optik bir kapı kurar. Mermi arayı geçerken fototransistör iletime girer,
anahtar tetiklenir, kondansatör enerjisi bobine boşalır ve mermi hızlanarak bir
sonraki kademeye gider.

Kademelerin birbirini senkronize etmesi gerekmez: her kademe, merminin kendi
konumunu görerek tetiklenir. Hızlanma arttıkça geçiş süreleri kısalır ve sistem
bunu kendiliğinden takip eder — hiçbir yerde zamanlama tablosu tutulmaz.

Fototransistör kenarı yavaştır; doğrudan bir kesme girişine verilirse tek
geçişte birden çok tetikleme üretebilir. Kenarın komparatörle keskinleştirilmesi
ve histerezis verilmesi, bu mimaride zamanlama hassasiyetini belirleyen noktadır.

---

## Ölçüm izolasyonu

Yüksek gerilim hattının gerilimi, izole fark yükselticisi üzerinden okunur;
düşük gerilim tarafı ile yüksek gerilim tarafı arasında galvanik bağ kurulmaz.

Buradaki kritik nokta beslemededir: izole yükselticinin yüksek gerilim tarafı
**ayrı ve izole bir besleme** ister. Aynı beslemeyi iki taraftan çekmek
izolasyonu o anda öldürür — bileşen yerinde durur, işlevi kalmaz. Bu, şemaya
bakarken en kolay atlanan ve en pahalıya patlayan hatalardan biridir.

Çıkış differential'dır ve tepe değeri 3,3 V mantık seviyesinin üstündedir;
denetleyicinin ADC'sine doğrudan bağlanırsa pin yanar. Araya harici bir ADC
konulur.

---

## Kablajın mimariye etkisi

Anahtar kapısına giden hatlar **ekranlı olmak zorundadır**. Deşarj akımının tam
yanından geçiyorlar ve kapı hattına binen bir gürültü darbesi **istenmeyen
ateşleme** demektir. Bu, kablo seçiminin bir konfor meselesi değil, emniyet
gereksinimi olduğu birkaç yerden biri.

Deşarj halkası mümkün olduğunca kısa ve gidiş-dönüş telleri sıkı yan yana
çekilir: halka endüktansı düştükçe akım daha hızlı yükselir. Uzun ve ayrık
çekilmiş bir halka, hiçbir hata vermeden sistemin performansını düşürür.

---

## Çalışma öncesi kural

Kart üzerinde çalışmadan önce, her seferinde, üç adım birden:

1. Şarjı kes (ARM rölesi)
2. Aktif boşaltmayı tetikle
3. Boşaltma çubuğuyla elle boşalt **ve voltmetreyle doğrula**

Üçünden birine tek başına güvenilmez. Tek elle çalışılır, yanında ikinci bir
kişi bulunur. Uzun süre kapalı kalacaksa kondansatör uçları kısa devre teliyle
bağlı bırakılır.

Emniyet donanımının listesi: [`MALZEME_LISTESI.md`](MALZEME_LISTESI.md) §2 ve §7.
