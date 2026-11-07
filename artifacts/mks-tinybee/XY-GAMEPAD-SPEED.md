# Profil pada 1200 mm/min

Powód: zmiana MAX_X/Y_SPEED_MM_MIN w Pythonie nie przekraczała limitu
120 mm/min w aktywnym `config.xy-test.yaml`.

Wgrano osobny `config.xy-gamepad.yaml`: XY 1200 mm/min, 100 mm/s².
Z i mapowanie pinów bez zmian. Poprzedni profil pozostał na płytce.
Odczyt wgranego pliku porównano z kopią lokalną; ustawienie aktywnego
pliku i restart powiodły się. Log `xy-gamepad-config-upload.log` potwierdza
start z nowym profilem, I2S bez błędów i końcowy Idle. Nie wysyłano ruchów.

Skrypt: zachowano wybrane przez użytkownika 1200 mm/min, podniesiono rampy
z 3 do 40 mm/s² i kolejkę z 4 do 6 segmentów po 40 ms. Dodano odczyt
limitów firmware przy starcie i odmowę jazdy przy ich przekroczeniu.
13 testów hosta przeszło; YAML przeszedł walidację schematu.

Dodatkowy test nowego odczytu limitów na fizycznej płytce nie doszedł do
skutku: port zajmował już program pada (blokada exclusive). Nie przerywano
tej sesji. Aby wczytać zmienione stałe i kod, uruchom program ponownie.

Powrót do poprzednich limitów: w Idle ustaw
`$Config/Filename=config.xy-test.yaml` i uruchom firmware ponownie.
W Pythonie dopasuj prędkości do 120 i rampy do najwyżej 10 mm/s²
(pierwotnie 3). Kroki/mm pozostają diagnostyczne, bez kalibracji mechaniki.

## Aktualizacja: przyspieszenie 500 mm/s²

Na polecenie użytkownika ustawiono w aktywnym `config.xy-gamepad.yaml`
`axes.x.acceleration_mm_per_sec2` oraz `axes.y.acceleration_mm_per_sec2`
na 500. Plik zwalidowano, wgrano i porównano odczyt z kopią lokalną.
Po restarcie odczyty runtime obu osi zwróciły `500.000`, a stan końcowy
to Idle. Log: `xy-gamepad-accel500-upload.log`. Nie wysyłano ruchów.
W skrypcie użytkownik ustawił już rampy 500 oraz prędkości 500 mm/min;
zachowano te wartości. Limit prędkości na płytce nadal wynosi 1200 mm/min.

## Diagnostyka zgłoszonego spowolnienia

W skrypcie zastano maksymalną prędkość 500 mm/min i przyspieszenie
500 mm/s². Potwierdzono na płytce limity 1200 mm/min i 500 mm/s².
Porównano na X ruchy +4/-4 jednostki: pojedyncza komenda i strumień
segmentów przy 500, następnie przy 1200. Końcowe MPos: 0,0,0, stan Idle;
port zamknięto i przywrócono maskę raportów. Log: `xy-speed-diagnostic.log`.

Zmierzony czas do raportu Idle: pojedyncza komenda 500 — 0,509 s,
strumień 500 — 0,554 s, pojedyncza 1200 — 0,264 s, strumień 1200 —
0,366 s. Obejmuje start, hamowanie i odpytywanie stanu. Potwierdzenia
komend: około 10–17 ms. Raporty FS i MPos potwierdzają szybsze generowanie
ruchu przy 1200, ale nie mierzą rzeczywistej prędkości wału silnika.
Na starcie strumienia widoczne jest dodatkowe zwolnienie; nie ustalono
jeszcze jego dokładnej przyczyny. Nie ma dowodu na stałe dławienie do 500
przez konfigurację firmware.

Przywrócono w Pythonie MAX_X/Y_SPEED_MM_MIN=1200, zachowano rampy 500.
Przy starcie wypisywane są osobno limity firmware i zadane maksimum pada.
Testy ramp mają stałe nastawy testowe, aby użytkownik mógł zmieniać swoje
parametry bez zmieniania znaczenia testów. 13 testów hosta przeszło.
Firmware i pliku YAML w tej diagnozie nie zmieniano.
