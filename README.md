# USB AirPods

USB audio playback, microphone support, and a Windows tray companion for AirPods through a Raspberry Pi Pico 2 W.

## Current release: v1.1.0

This repository contains the **case-battery-anc-v1** firmware and Windows companion, verified together on physical hardware.

- 48 kHz stereo playback with simultaneous microphone use.
- Left, right, and case battery status and ANC control from the Windows tray.
- A dashboard that opens above the taskbar at the lower-right when you click the tray icon.
- Closed-case disconnection, lid-open reconnection preparation, and automatic recovery after range loss.

## Windows companion

![Windows companion dashboard showing battery cards and ANC controls](docs/images/windows-dashboard.png)

*Sample readings.* Click the tray icon for battery status and ANC controls.

## Connection

![Windows PC connected by USB to Pico 2 W, then Bluetooth to AirPods](docs/images/hardware-connection.png)

Connect the PC to the Pico over **USB**, then pair AirPods with the Pico over **Bluetooth (BT)**. [Photo credits](docs/images/ATTRIBUTION.md).

## Standard Bluetooth adapter vs. USB AirPods

**With the microphone on, standard Bluetooth Classic switches to voice-call audio. USB AirPods keeps stereo playback running.**

### Standard Bluetooth Classic

![Bluetooth HFP: 8 or 16 kHz mono speaker and microphone paths](docs/images/bluetooth-hfp-two-way.png)

### USB AirPods

![USB AirPods: 48 kHz stereo speaker path and 64 kHz mono USB mic interface, with decoder-rate caveat](docs/images/usb-airpods-two-way.png)

- **Standard Bluetooth Classic (HFP):** both speaker and microphone use mono call audio: **8 kHz narrowband** or **16 kHz wideband**, depending on the negotiated codec. This is the familiar switch from stereo music to voice-call sound when the headset microphone opens. These rates are documented by [Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio).
- **USB AirPods:** playback stays at **48 kHz stereo** while the microphone uses a separate AACP/AAC-ELD path. The firmware presents a **64 kHz mono USB microphone interface** to Windows (`USB_MIC_SAMPLE_RATE` in `src/tinyusb/usb_descriptors.h`). The decoder reports **48 kHz** and native microphone timing is still under investigation, so the USB interface rate must not be read as proven 64 kHz recording fidelity.

This comparison is about Bluetooth Classic HFP, not all Bluetooth devices. Compatible [Windows LE Audio systems](https://support.microsoft.com/en-us/windows/hardware/bluetooth/configuring-bluetooth-le-audio-quality-settings-on-windows-11) can also retain stereo with an active mic. Sampling rates alone are not a measured sound-quality score; no microphone-quality, latency, or range benchmark is claimed.

## Download and run

[Download the latest release](https://github.com/ermanon/USB-AirPods/releases/latest)

The files are also available in [downloads](downloads):

1. Hold **BOOTSEL** while connecting the Pico 2 W to USB.
2. Copy **PicoW_USB_BT_Audio.uf2** to the bootloader drive. The Pico restarts automatically.
3. Connect your AirPods. When disconnected, a short BOOTSEL press attempts to reconnect to the selected device; a long press followed by release starts discovery. Discovery may select the first compatible audio device, so keep other audio devices out of pairing mode during setup.
4. Run **USBAirPods.exe**. Requires Windows 10/11 and .NET Framework 4.x; no installation or administrator privileges are needed.
5. Select the Pico USB audio device as the Windows output/input. Click the tray icon to open the dashboard.

The computer's Bluetooth adapter is not used. The AirPods' own firmware is not modified.

## Battery and ANC

Battery queries read notifications cached by the Pico rather than sending additional requests to the headphones. The case does not always report its battery; missing or stale information is shown as **unknown**.

Only ANC modes reported by the headphones during the current connection are enabled. To enable a mode for the first time, select it using the headphones' own controls. Queuing a command does not count as success: the app waits for a mode notification from the headphones. Mode verification resets on reconnection.

The **USB connection** menu selects serial monitoring; it does not disconnect the AirPods Bluetooth link. Close other logging applications that have the COM port open.

## Validation and limitations

On AirPods 4 ANC, the user confirmed battery/ANC display, switching ANC off from the app, and clean playback on both earbuds with microphone operation. Firmware compilation, battery/ANC parser tests, **2,515 ARM checks**, and **23 Windows protocol checks** passed. See [VALIDATION.md](VALIDATION.md).

This project is experimental. Earlier tests showed one earbud remaining silent after an initial connection and recovery taking approximately 15 seconds; no permanent fix is claimed. Long-duration use and repeated range/case cycles were not retested for the battery/ANC integration. Raw microphone protocol timing remains under investigation; the decoder reports 48 kHz. An active stream flag does not prove audible playback.

## Build from source

Firmware requires Pico SDK **2.1.1**, Arm GNU **14.2.Rel1**, CMake, and Ninja. Set the PICO_SDK_PATH environment variable to your SDK directory.

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -g"
cmake --build build --parallel 4
```

Windows companion:

```powershell
powershell -ExecutionPolicy Bypass -File windows-tray/build.ps1
powershell -ExecutionPolicy Bypass -File windows-tray/test.ps1
```

Firmware tests and dependencies are described in [tests/README.md](tests/README.md). Download checksums are in [downloads/SHA256SUMS.txt](downloads/SHA256SUMS.txt).

## Credits and licenses

Built on [USBPods-Pico2W](https://github.com/wasdwasd0105/USBPods-Pico2W) and the [two-way audio implementation](https://github.com/han-um/USBPods-Pico2W-2-way-audio-implement). Thanks to the [LibrePods](https://github.com/librepods-org/librepods) community for its AACP protocol work. FDK-AAC and LDAC licenses are retained in their respective directories; project licenses are preserved in [LICENSE](LICENSE) and [LICENSE.Apache-2.0](LICENSE.Apache-2.0).
