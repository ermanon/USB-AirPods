# v1.1.0 doğrulama kaydı

Tarih: 2026-11-01. Donanım: Pico 2 W ve AirPods 4 ANC. Firmware işareti: case-battery-anc-v1.

## Geçen kontroller

- Pico SDK 2.1.1 / Arm GNU 14.2.Rel1 ile firmware derlemesi.
- 2515 ARM kontrolü: ses toparlanması, kuyruk/IRQ sahipliği, zaman aşımı, paket sınırları, kutu/kimlik ve codec pazarlığı.
- Native pil/ANC ayrıştırıcı testleri: kesilmiş/bozuk bildirimler, kısmi güncellemeler, geçersiz değerler, bağlantıda sıfırlama, sorgu kimlikleri ve ANC oturum/mod sınırları.
- 23 Windows protokol kontrolü.
- Pico flash yükleme ve picotool doğrulaması OK.
- Gerçek USB sorgularından pil, ANC ve cihaz yanıtları.
- ANC komutları için kuyruğa alma yanıtı; ayrı alınan kulaklık bildirimleriyle önce gürültü engelleme, ardından Off onayı.
- Kullanıcı onayı: uygulamada pil yüzdeleri/ANC modu görünüyor; uygulamadan Off'a geçiş çalışıyor; iki kulaklık ve mikrofon temiz çalışıyor.

## Sınırlar

Kutu pilinin bildirilmediği durum bilinmiyor olarak gösterildi. ANC modları yalnızca bu bağlantıda kulaklığın bildirdiği modlarla sınırlı. Önceki kutudan dönüş testinde tek taraflı sessizlik ve yaklaşık 15 saniyelik otomatik toparlanma görüldü; kalıcı çözüm iddia edilmiyor. Yeni pil/ANC entegrasyonunda tekrarlı menzil/kutu döngüleri ve uzun süreli dayanıklılık test edilmedi.

Ham günlükler, cihaz adresleri ve eski deneme firmware'leri bu temiz sürüme dahil edilmedi. App EXE yanına sınırlı boyutlu companion-status.log ve bir döndürülmüş kopya yazabilir; bunlar yalnızca yerel tanılama içindir ve Git'e dahil edilmez.
