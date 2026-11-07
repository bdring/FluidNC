# Wgranie na TinyBee — SUCCESS

Data: 2026-11-07.

- Port: `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` (`/dev/ttyUSB0`).
- Układ: ESP32-D0WD rev. 1.1, flash 8 MB, MAC AC:15:18:D0:21:A0.
- Przed zapisem odczytano pełne 8 388 608 bajtów pamięci do
  `device-backup/flash-before-upload.bin`; suma SHA256 w `device-backup/SHA256SUMS`.
- Wgrano bootloader 0x1000, partycje 0x8000, boot_app0 0xe000,
  firmware 0x10000, LittleFS 0x3d0000. Układ partycji i nagłówki 4 MB,
  zgodne z przygotowanymi artefaktami; reszta fizycznej pamięci niewykorzystana.
- esptool zakończył kodem 0 i potwierdził hash każdego z pięciu segmentów.
- Zrestartowano płytkę i odczytano start oraz odpowiedzi `$I`, `$SS`,
  `$Config/Filename` i `?`. Status końcowy `Idle`, bez błędów startowych.
- Wersja: `v4.x.x (main-22e2add7-dirty) (esp32-wifi)`.
- LittleFS zamontowany; aktywny `config.yaml`: `Default (Test Drive no I/O)`.
  Nie wgrano przykładu lasera K40 jako aktywnej konfiguracji maszyny.
- AP `FluidNC`, adres 192.168.0.1; HTTP port 80 uruchomiony.
  Domyślne hasło AP w źródłach: `12345678`.
- Nie łączono komputera do AP, więc nie wykonano testu strony w przeglądarce.
- Nie wykonano ruchów ani testów napędów/lasera.

Log zapisu: `upload.log`. Log uruchomienia i zapytań: `serial-startup.log`.
Raport kompilacji `BUILD-INFO.md` opisuje stan przed tym wgraniem.
