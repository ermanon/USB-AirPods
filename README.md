# USB AirPods

Raspberry Pi Pico 2 W üzerinden AirPods için USB ses çıkışı, mikrofon ve Windows tepsi uygulaması.

## Güncel sürüm: v1.1.0

Bu depo, birlikte çalıştığı cihaz üzerinde doğrulanan **case-battery-anc-v1** firmware'i ve Windows uygulamasını içerir.

- 48 kHz stereo ses çıkışı ve aynı anda mikrofon kullanımı.
- Windows tepsisinden sol/sağ/kutu pil bilgisi ve ANC kontrolü.
- Tepsi simgesine tıklayınca görev çubuğunun üzerinde, sağ altta açılan pencere.
- Kapalı kutuda bağlantıyı kapatma, kapak açılışında yeniden bağlantı hazırlığı ve menzil kaybında otomatik toparlanma.

## İndir ve çalıştır

[Son sürümün indirme sayfası](https://github.com/ermanon/USB-AirPods/releases/latest)

Dosyalar ayrıca [downloads](downloads) klasöründe bulunur:

1. Pico 2 W'yi **BOOTSEL'e basılı tutarak** USB'ye bağlayın.
2. **PicoW_USB_BT_Audio.uf2** dosyasını görünen yükleme sürücüsüne kopyalayın; Pico yeniden başlar.
3. AirPods'u bağlayın. Bağlantı yokken kısa BOOTSEL basışı seçili cihaza bağlanmayı dener; uzun basıp bırakmak cihaz aramasını başlatır. Tarama ilk uygun ses cihazını seçebilir; eşleştirme sırasında diğer ses cihazlarını eşleştirme modunda bırakmayın.
4. **USBAirPods.exe** dosyasını çalıştırın. Windows 10/11 ve .NET Framework 4.x gerektirir; kurulum veya yönetici yetkisi istemez.
5. Windows ses çıkışı/girişinde Pico'nun USB ses aygıtını seçin. Tepsi simgesine tıklayarak durum ekranını açın.

Bilgisayarın Bluetooth adaptörü kullanılmaz. AirPods'un kendi yazılımı değiştirilmez.

## Pil ve ANC

Pil sorguları kulaklığa ek istek göndermek yerine Pico'nun aldığı bildirimleri okur. Kutu pili her zaman bildirilmez; alınmayan veya eski bilgi **bilinmiyor** olarak gösterilir.

Yalnızca kulaklığın mevcut bağlantıda bildirdiği ANC modları etkinleşir. Bir modu ilk kez kullanmak için kulaklığın kendi kontrolüyle o moda geçin. Uygulama, komutun kuyruğa alınmasını başarı saymaz; kulaklıktan mod onayı bekler. Yeniden bağlantıda mod doğrulaması sıfırlanır.

USB connection menüsü seri port izlemeyi seçer; AirPods Bluetooth bağlantısını kesen bir menü değildir. Başka günlük uygulaması COM portunu kullanıyorsa kapatın.

## Doğrulama ve sınırlar

AirPods 4 ANC üzerinde kullanıcı tarafından pil/ANC görüntüleme, uygulamadan ANC kapatma ve iki kulaklıkta temiz ses/mikrofon doğrulandı. Firmware derlemesi, pil/ANC ayrıştırıcı testleri, **2.515 ARM kontrolü** ve **23 Windows protokol kontrolü** geçti. Bkz. [VALIDATION.md](VALIDATION.md).

Proje deneyseldir. Önceki testlerde ilk bağlantıda tek kulaklık sessizliği ve yaklaşık 15 saniyelik toparlanma görüldü; kalıcı olarak giderildiği iddia edilmiyor. Pil/ANC eklenen sürümde uzun süreli kullanım ve tekrarlı menzil/kutu testleri yapılmadı. Mikrofonun ham protokol zamanlaması araştırılıyor; decoder 48 kHz raporluyor. Akışın açık görünmesi, duyulabilir sesin kanıtı değildir.

## Kaynak koddan derleme

Firmware: Pico SDK **2.1.1**, Arm GNU **14.2.Rel1**, CMake ve Ninja. PICO_SDK_PATH ortam değişkenini SDK dizinine ayarlayın.

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -g"
cmake --build build --parallel 4
```

Windows uygulaması:

```powershell
powershell -ExecutionPolicy Bypass -File windows-tray/build.ps1
powershell -ExecutionPolicy Bypass -File windows-tray/test.ps1
```

Firmware testleri ve bağımlılıkları [tests/README.md](tests/README.md) içinde açıklanır. İndirme dosyalarının özetleri [downloads/SHA256SUMS.txt](downloads/SHA256SUMS.txt) içindedir.

## Kaynaklar ve lisanslar

[USBPods-Pico2W](https://github.com/wasdwasd0105/USBPods-Pico2W) ve [iki yönlü ses uygulaması](https://github.com/han-um/USBPods-Pico2W-2-way-audio-implement) üzerine geliştirilmiştir. AACP protokol çalışmaları için [LibrePods](https://github.com/librepods-org/librepods) topluluğuna teşekkürler. FDK-AAC ve LDAC kitaplıklarının lisansları kendi dizinlerindedir; proje lisansları [LICENSE](LICENSE) ve [LICENSE.Apache-2.0](LICENSE.Apache-2.0) dosyalarında korunmuştur.
