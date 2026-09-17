# HSS systemd servisleri

Pi 5 yeniden başladığında Hailo container'ı, kamera+tespit süreci ve taret
köprüsü kendiliğinden ayağa kalksın diye. Öncesinde hiçbiri otomatik
başlamıyordu; sahada tek bir elektrik kesintisi bütün sistemi durduruyordu.

## Kurulum

    sudo cp *.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable --now hss-hailo hss-taret hss-kamera

## Zincir

    hss-hailo   docker compose up -d --no-build   (container)
       ↑ Requires
    hss-kamera  container İÇİNDE kamera_tespit.py  → 8080 MJPEG, 8081 tespit
    hss-taret   host'ta taret_sunucu.py            → 5005 TCP, 8090 konsol

`hss-kamera` yalnız başına başlatılsa bile `hss-hailo`'yu kendisi kaldırır.
`hss-taret` host tarafında koşar çünkü ESP32 ve LiDAR host USB portlarında.

## İki tuzak

**`--build` KULLANMA.** İmajdaki `hailo-all` paketi yeniden kurulumda gidiyor.
Servis bilerek `--no-build` ile yazıldı.

**Kamerayı `pkill -f` ile öldürme.** `docker exec` SIGTERM'i içerideki sürece
geçirmiyor; kalan süreç Hailo'yu tutuyor ve yeniden başlatma
`HAILO_OUT_OF_PHYSICAL_DEVICES (74)` ile düşüyor. `ExecStopPost` bu yüzden
port üzerinden (`fuser -k 8080/tcp`) öldürüyor — kesin ve kendi kabuğunu
vurmuyor.
