# FluidNC dla MKS TinyBee

## Wynik: SUCCESS

- Firmware: kompilacja i linkowanie zakończone kodem 0; czas 392,58 s.
- LittleFS/WebUI: `pio run -e wifi -t buildfs`, kod 0; czas 3,24 s.
- `firmware.bin`: 1 905 792 bajty; partycja 1 966 080 bajtów.
- Rozmiar sekcji raportowany przez PlatformIO: 1 905 640 bajtów (96,9%).
- Statyczne RAM raportowane przez PlatformIO: 68 352 bajty; nie obejmuje
  zapotrzebowania dynamicznego podczas pracy.
- esptool: ESP32, DIO, 80 MHz, nagłówek 4 MB; checksum i hash poprawne.
- `littlefs.bin`: 196 608 bajtów; zawiera WebUI, favicon i fabryczny
  `config.yaml` typu `Default (Test Drive no I/O)`, bez sterowania napędami.
- Dołączono `firmware.elf` do późniejszej diagnostyki oraz sumy SHA256.
- `firmware.factory.bin` zawiera bootloader, partycje, boot_app0 i aplikację,
  z początkiem obrazu pod adresem 0x0. Nie zawiera LittleFS ani konfiguracji K40.

Sprawdzono kompilację i format obrazu. Działanie na fizycznej płytce będzie
można potwierdzić dopiero po wgraniu i odczytaniu logu startowego.

## Źródła i środowisko

Build z lokalnego repozytorium `https://github.com/Looczky/FluidNC_MKSTinyBee`,
commit `22e2add7`. To build tego forka, nie oficjalna paczka wydania FluidNC.
Wersja generowana przez repo: `v4.x.x (main-22e2add7-dirty)`.
Repo nie ma dostępnego taga wersji; oznaczenie `dirty` wynika z zastanej zmiany
śledzonego `.gitignore`. Zastane zmiany użytkownika pozostawiono.

Polecenie kompilacji:

```sh
/home/luki/.platformio/penv/bin/pio run -e wifi
```

Wybrano `wifi` / `esp32dev` dla klasycznego ESP32, z obsługą I2S używaną
przez TinyBee. Nie wybierać środowiska ESP32-S3 ani RP2040.

Zainstalowane narzędzia i zależności użyte podczas kompilacji:

- PlatformIO Core 6.2.0.post1; `requirements.txt` deklaruje 6.1.*.
- pioarduino Espressif32 55.3.39, commit platformy `cbc3349`.
- Arduino-ESP32 3.3.9 / biblioteki ESP-IDF 5.5.4.
- Xtensa GCC 14.2.0+20260121; esptool 5.3.0.
- TMCStepper 0.7.3; JsonStreamingParser 1.0.5 (`ddb90a2`).
- OLED 4.4.1; AsyncTCP 3.5.0; ESPAsyncWebServer 3.12.1 (`75dafb4`).

Potrzebne są Git, Python, PlatformIO oraz dostęp do pobrania pakietów
kompilatora/frameworka/bibliotek. W tym środowisku były już zainstalowane.
PlatformIO wymaga zapisu do `~/.platformio` poza katalogiem projektu.
Istniejący symlink `.platformio-home` wskazuje na nieobecną ścieżkę z macOS;
ten build korzysta z `/home/luki/.platformio`.

Producent podaje 8 MB flash. Build używa standardowego układu 4 MB
(`min_littlefs.csv`), który mieści się także w module 8 MB. Dodatkowa pamięć
pozostaje niewykorzystana. Rzeczywistą pojemność sprawdzimy przed flashowaniem.

## Konfiguracja maszyny

`MKS_TinyBee_1_XYZAB.reference.yaml` to niezmieniona kopia wskazanego przykładu.
Przechodzi walidację schematu z tego repozytorium. Walidacja nie potwierdza
zgodności z okablowaniem ani parametrów konkretnej maszyny.

Przykład opisuje laser K40: aktywne XYZ, zakomentowane A/B, silnik krokowy
I2S_STATIC, magistrala I2SO: BCK GPIO25, DATA GPIO27, WS GPIO26.
Sterowanie laserem: PWM GPIO13, enable I2SO.16. Komentarz przy enable mówi
FAN1, ale mapa producenta i komentarz na końcu YAML wskazują HBED dla I2SO.16;
FAN1 to I2SO.19. Przed użyciem trzeba sprawdzić fizyczne podłączenie.

Przed uruchomieniem napędów trzeba dopasować liczbę osi, sterowniki,
mikrokroki, steps/mm, kierunki, prędkości, przyspieszenia, krańcówki, homing,
zakresy ruchu oraz typ i podłączenie lasera/wrzeciona. Nie trzeba do tego
ponownie kompilować firmware: `config.yaml` jest osobnym plikiem.

## Późniejsze wgrywanie

Do pierwszej instalacji potrzebne będą firmware, bootloader, tablica partycji,
boot_app0 i system plików LittleFS z WebUI oraz dopasowany config.yaml.
Przed zapisem sprawdzimy port USB, typ układu, pojemność flash i istniejące dane.
Samo `firmware.bin` nie stanowi kompletnej pierwszej instalacji.

Adresy dla wybranego układu partycji (ESP32):

| Plik | Adres |
| --- | --- |
| bootloader.bin | 0x1000 |
| partitions.bin | 0x8000 |
| boot_app0.bin | 0xe000 |
| firmware.bin | 0x10000 |
| littlefs.bin | 0x3d0000 |

Pełny zapis obrazu LittleFS zastępuje istniejące pliki na płytce.
Na tym etapie nie wykonano flashowania ani testu na sprzęcie.

## Dokumentacja źródłowa

- https://github.com/bdring/fluidnc-wiki-content/blob/main/installation.md
- https://github.com/makerbase-mks/MKS-TinyBee
- https://github.com/bdring/fluidnc-config-files/blob/main/official/MKS_TinyBee_1_XYZAB.yaml
- Lokalne: `platformio.ini`, `platformio_override.ini`, `build-release.py`,
  `min_littlefs.csv`.
