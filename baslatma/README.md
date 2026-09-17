# Başlatma scriptleri

Bunlar daha önce yalnızca çalıştıkları makinede duruyordu; tek bir disk arızası
hepsini götürürdü. Yedeğin gerçekten eksiksiz olması için depoya alındılar.

| Dosya | Nerede çalışır | Ne yapar |
|---|---|---|
| `ts_baslat.sh` | Pi | Taret sunucusunu elle yeniden başlatır |
| `hailo_kontrol.sh` | Pi | Hailo/NPU durum kontrolü |
| `start_yolo.sh` | Pi | Eski YOLO denemesi (systemd öncesi) |

**Normal işletimde bunlara gerek yok** — üç systemd servisi (`gomulu/systemd/`)
boot'ta her şeyi kaldırıyor. Bunlar elle müdahale ve teşhis içindir.

`ts_baslat.sh` süreçleri **port üzerinden** öldürür (`fuser -k 5005/tcp`);
`pkill -f` kullanmaz çünkü kendi komut satırını eşleyip çağıran kabuğu öldürür.
