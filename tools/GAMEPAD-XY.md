# Pad PS5 → silniki X i Y przez FluidNC

Uruchom z katalogu projektu:

```bash
tools/run_gamepad_xy.sh          # Linux
```

```powershell
.\tools\run_gamepad_xy.ps1       # Windows (PowerShell)
```

Port płytki jest wykrywany automatycznie (CH340, USB VID `1a86`): na Linuksie
`/dev/serial/by-id/usb-1a86_*`, na Windowsie odpowiedni `COMx`. Gdy podłączonych
jest kilka takich adapterów, wskaż port ręcznie: `--port COM5` lub
`--port /dev/ttyUSB0`.

Lewy drążek poziomo steruje X, prawy poziomo steruje Y. Wychylenie określa
prędkość. Wycentruj oba drążki po uruchomieniu, aby uzbroić sterowanie.
Kółko anuluje jog; po jego puszczeniu wycentruj drążki, aby ponownie ruszyć.
Ctrl+C kończy program i anuluje jog. Odłączenie pada lub błąd komunikacji
kończy program z próbą anulowania ruchu. Przy zerwanym połączeniu z płytką
nie można potwierdzić zatrzymania; przyjęte krótkie odcinki mogą się wykonać.

## Jedno źródło prędkości i przyspieszeń

Zmieniasz je w `artifacts/mks-tinybee/config.xy-gamepad.yaml`, pod osiami
`axes.x` i `axes.y`:

- `max_rate_mm_per_min`: prędkość przy pełnym wychyleniu drążka;
- `acceleration_mm_per_sec2`: rampa zmiany prędkości i kierunku.

Po edycji lokalnego YAML-a wgraj go do plików FluidNC jako
`config.xy-gamepad.yaml`, uruchom firmware ponownie w stanie Idle,
a następnie uruchom ponownie program pada. Ten plik jest już ustawiony
jako aktywny. Nie trzeba kompilować firmware.

**Podczas sterowania Python odczytuje aktualne wartości z płytki** poprzez
`$/axes/x/...` i `$/axes/y/...`. Nie ma własnych kopii ani zapasowych wartości
prędkości i przyspieszeń. Przy starcie pokazuje źródło i odczytane parametry.
Jeśli nie można ich odczytać, program nie uruchamia ruchów.

`--dry-run` odczytuje te same pola z lokalnego YAML-a, bez otwierania USB.
Możesz wskazać inny plik przez `--config`:

```bash
tools/run_gamepad_xy.sh --dry-run
tools/run_gamepad_xy.sh --dry-run --config /sciezka/config.yaml
tools/run_gamepad_xy.sh --monitor
```

Monitor pokazuje surowe osie i przyciski pada, bez odczytywania konfiguracji
płytki. `--duration 10` ogranicza czas dowolnego trybu.

## Test silników bez pada

Gdy pad steruje, a silniki stoją, uruchom:

```bash
.venv-gamepad/bin/python tools/motor_test_xy.py              # Linux
.venv-gamepad\Scripts\python tools\motor_test_xy.py          # Windows
```

Skrypt wykonuje jog X i Y tam i z powrotem i pokazuje `MPos`. Długość, prędkość,
osie i liczbę powtórzeń zmienisz w stałych `TEST_*` na początku pliku albo
opcjami `--distance`, `--feed`, `--axes`, `--repeats`, `--port`. Jeśli `MPos` się zmienia, a silnik
stoi, problem jest sprzętowy: brak zasilania silników (samo USB zasila tylko
ESP32), sterowniki, kable silników lub prąd sterownika (Vref).

## Ustawienia samego pada

Na początku `gamepad_motor_controller_xy.py` pozostają:

| Zmienna | Znaczenie |
| --- | --- |
| `X_STICK_AXIS`, `Y_STICK_AXIS` | Indeksy osi pada; `None` = autowykrycie (patrz niżej) |
| `INVERT_X_STICK`, `INVERT_Y_STICK` | Odwrócenie kierunku danego drążka |
| `DEADZONE` | Martwa strefa wokół środka |
| `EXPO` | Krzywa reakcji; 1 liniowo, większa = dokładniej przy środku |
| `ENABLE_BUTTON` | Opcjonalny przycisk, który trzeba trzymać; `None` wyłącza wymaganie |
| `STOP_BUTTON` | Przycisk anulowania jog; `None` = autowykrycie (kółko PS5 / B Xbox) |
| `MAX_QUEUED_SEGMENTS`, `SEGMENT_SECONDS` | Głębokość kolejki i długość odcinka |
| `SERIAL_PORT`, `BAUD_RATE` | Port TinyBee (`None` = autowykrywanie CH340, można nadpisać `--port COM5`) i szybkość transmisji |

Skrypt obsługuje pady PS5 (DualSense) i Xbox na Linuksie i Windowsie.
Typ pada jest rozpoznawany po nazwie, a `GAMEPAD_INDEX = None` wybiera pierwszy
rozpoznany pad. Indeksy osi i przycisku stop pochodzą z mapowania SDL
GameController, które zna surowy układ pada na danym systemie. Gdy SDL nie zna
pada, używany jest zapasowy układ dla pary system + pad:

| System | Pad | X | Y | Stop |
| --- | --- | --- | --- | --- |
| Linux | PS5 (hid-playstation) | 0 | 3 | 1 (kółko) |
| Linux | Xbox (xpad) | 0 | 3 | 1 (B) |
| Windows | PS5 (HIDAPI SDL) | 0 | 2 | 1 (kółko) |
| Windows | Xbox (XInput) | 0 | 3 | 1 (B) |

Nieznany pad dostaje pierwotny układ skryptu (0, 3, stop 1). Na Windowsie
skrypt wyłącza RawInput (`SDL_JOYSTICK_RAWINPUT=0`), bo bez okna pad zwracałby
same zera. Przy starcie wypisywany jest wykryty typ i źródło mapowania.
Ręcznie ustawione `X_STICK_AXIS`, `Y_STICK_AXIS`, `STOP_BUTTON` mają pierwszeństwo.

## Zachowanie ruchu

Każdy drążek ma osobną rampę. Zmiana kierunku Y nie zeruje zadanej
prędkości X, nie resetuje sterownika ani nie anuluje obu osi. Powrót
jednego drążka do środka wyhamowuje tę oś. Oba drążki w środku anulują jog.
Posuw XY jest normą wektora prędkości, więc równoczesna jazda obu osi
nie dzieli zadanej prędkości jednego silnika przez dwa.

FluidNC ma wspólny planer: na zmianach kierunku może zwolnić obie osie.
Skrypt nie zapewnia niezależnych generatorów prędkości w firmware.
Kolejka jest ograniczana raportem wolnych bloków `Bf`, komendy wymagają
potwierdzeń `ok`. Nominalny czas segmentów nie jest gwarantowanym czasem
zatrzymania — dochodzi bufor kroków firmware i hamowanie. Po zmianie
limitów program sprawdza, czy kolejka daje zapas na drogę hamowania,
i wypisuje uwagę, jeżeli może ograniczać prędkość.

Otwieranie USB/CH340 może zrestartować ESP i chwilowo rozłączyć panel.
Program czeka na start i wymaga Idle; nie odblokowuje alarmów.
Podczas pracy steruj z jednego programu. Skrypt tymczasowo ustawia
`$Report/Status=3` i przy wyjściu przywraca odczytaną wartość.
Po SIGKILL lub utracie zasilania może pozostać 3; to zakres raportów.
Kroki/mm w tym profilu są diagnostyczne, bez kalibracji mechaniki.

## Instalacja i testy

Linux:

```bash
python3 -m venv .venv-gamepad
.venv-gamepad/bin/python -m pip install -r tools/requirements-gamepad.txt
.venv-gamepad/bin/python -m unittest discover -s tools/testing -p test_gamepad_xy.py -v
```

Windows (PowerShell):

```powershell
py -3 -m venv .venv-gamepad
.venv-gamepad\Scripts\python -m pip install -r tools\requirements-gamepad.txt
.venv-gamepad\Scripts\python -m unittest discover -s tools\testing -p test_gamepad_xy.py -v
```

Jeśli PowerShell blokuje skrypty `.ps1`, uruchom
`.venv-gamepad\Scripts\python tools\gamepad_motor_controller_xy.py` bezpośrednio.

Źródła protokołu: [jogging GRBL](https://github.com/gnea/grbl/blob/master/doc/markdown/jogging.md),
lokalne `FluidNC/src/Jog.cpp`, `Planner.cpp`, `Report.cpp`.
Inspiracja: `RoboticArm/tools/gamepad_motor_controller_xy.py`.
