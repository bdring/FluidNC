# Diagnostyka X/Y — 2026-11-07

Potwierdzona przyczyna po stronie konfiguracji: poprzednia konfiguracja
`Default (Test Drive no I/O)` miała `null_motor` dla wszystkich osi.
Firmware przyjmował ruchy, ale nie sterował fizycznymi wyjściami.

Wgrano przez USB/XMODEM osobny `config.xy-test.yaml`; odczyt z LittleFS
potwierdził identyczną treść pliku. Ustawiono `$Config/Filename=config.xy-test.yaml`
i zrestartowano płytkę. Log potwierdził:

- maszynę `TinyBee XY diagnostic`, magistralę I2SO i silnik krokowy I2S;
- X: STEP I2SO.1, DIR I2SO.2, DISABLE I2SO.0;
- Y: STEP I2SO.4, DIR I2SO.5, DISABLE I2SO.3;
- Z pozostaje `null_motor`; brak skonfigurowanego lasera/wrzeciona;
- start bez błędów, stan `Idle`.

Wykonano pojedynczą sekwencję diagnostyczną:

```gcode
$J=G91 G21 X0.25 F30
$J=G91 G21 X-0.25 F30
$J=G91 G21 Y0.25 F30
$J=G91 G21 Y-0.25 F30
```

Każda komenda otrzymała `ok`; po każdej oś raportowała oczekiwaną pozycję
i stan `Idle`. Na końcu `MPos:0.000,0.000,0.000`, bez alarmów.
Test używał tymczasowego 200 steps/mm, więc każdy ruch odpowiadał 50 impulsom.
Rzeczywisty dystans wymaga kalibracji; nie należy traktować go jako 0,25 mm
na konkretnej maszynie. Prędkość 30 jednostek/min, przyspieszenie 10,
limit prędkości 120. Wyłączenie sterowników po 250 ms bezczynności.
Homing i krańcówki nie są skonfigurowane, limity programowe wyłączone.

Log firmware nie potwierdza obrotu wału ani obecności napięcia silników.
Obserwacja fizycznego ruchu i zasilania 12–24 V pozostaje do potwierdzenia
przez użytkownika. Nie wykonywano pomiaru przebiegów STEP/DIR.

Logi: `xy-motion-test.log`, `xy-config-upload.log`, `serial-startup.log`.
Skrypt uploadu błędnie rozpoznał pojedyncze `ok` po zmianie nazwy configu;
późniejszy reset i odczyt `$Config/Filename` potwierdziły zapis ustawienia.

Źródło mapowania i napięcia zasilania:
https://github.com/makerbase-mks/MKS-TinyBee

Poprzedni plik `config.yaml` pozostaje na płytce; powrót do konfiguracji bez
napędów: `$Config/Filename=config.yaml` i restart płytki.

## Dłuższy test na prośbę użytkownika

Wysłano X +2/-2 i Y +2/-2 przy F60: po 400 impulsów, około 2 sekundy
na ruch. Każdy ruch zwrócił `ok`, przechodził Jog → Idle; końcowa pozycja
logiczna 0,0,0. Bez alarmów. Prądu sterowników nie zmieniano.
Log: `xy-motion-longer-test.log`. Fizyczny ruch nadal wymaga obserwacji.
