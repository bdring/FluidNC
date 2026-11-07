# WebUI — diagnostyka błędu Idle

Komunikat użytkownika to opis `error:8` w WebUI: komenda wymaga stanu
Idle. Nie jest to komunikat o awarii przeglądarki ani o sterowaniu wrzecionem.

Sprawdzono źródła `Settings.cpp`, `WebUI/WebCommands.cpp` i spakowany
`FluidNC/data/index.html.gz`: [ESP444]RESTART jest blokowane przez
notIdleOrAlarm podczas Jog/Cycle/Hold. Nie usuwano tej blokady.

Test [ESP444]RESTART przez USB: ok, SW_CPU_RESET, poprawne uruchomienie
I2S, X/Y, LittleFS, Wi-Fi i HTTP, następnie Idle. Bez panic/watchdog.

Podczas odtwarzania przez użytkownika monitor zarejestrował:
Idle → Jog na Y (do -6.435 jednostek logicznych) → ALARM:3 Abort Cycle
→ Alarm. To reset/przerwanie w trakcie ruchu, bez pełnego ponownego
startu ESP32 w tym momencie. Nie zarejestrowano treści odrzuconej
komendy HTTP, więc przypisanie error:8 do konkretnej komendy wymagałoby
logu konsoli przeglądarki; stan Jog wyjaśnia działanie blokady.

Otwieranie portu USB na tym zestawie powoduje restart ESP32 i może
chwilowo rozłączyć panel. Po zakończeniu diagnostyki port zamknięto.

Logi: webui-debug-serial.log, webui-restart-test.log,
webui-live-status.log, webui-recovery.log.

Powrót do obsługi: odświeżyć panel i sprawdzić Idle. Kończyć jog przyciskiem
anulowania ruchu, poczekać na Idle, potem wykonywać restart/zmiany ustawień.
Po resecie podczas ruchu nie traktować współrzędnych jako potwierdzonej
pozycji fizycznej maszyny. Parametry config.xy-test.yaml nadal są testowe.
