#include "ComPortX86.h"
#include <iostream>
#include <conio.h>
#include "StdTimer.h"

#define MAX_DEVPATH_LENGTH 1024
extern StdTimer g_timer;

ComPortX86::ComPortX86(const char* pPort) : hSerial(INVALID_HANDLE_VALUE), Channel("com_win32") {
    DCB          dcb;
    BOOL         fSuccess;
    TCHAR        devicePath[MAX_DEVPATH_LENGTH];
    COMMTIMEOUTS commTimeout;

    if (pPort != NULL) {
        mbstowcs_s(NULL, devicePath, MAX_DEVPATH_LENGTH, pPort, strlen(pPort));
        hSerial = CreateFile(devicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (hSerial != INVALID_HANDLE_VALUE) {
        //  Initialize the DCB structure.
        SecureZeroMemory(&dcb, sizeof(DCB));
        dcb.DCBlength = sizeof(DCB);
        fSuccess      = GetCommState(hSerial, &dcb);
        if (!fSuccess) {
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
            return;
        }

        GetCommState(hSerial, &dcb);
        dcb.BaudRate = CBR_115200;  //  baud rate
        dcb.ByteSize = 8;           //  data size, xmit and rcv
        dcb.Parity   = NOPARITY;    //  parity bit
        dcb.StopBits = ONESTOPBIT;  //  stop bit
        dcb.fBinary  = TRUE;
        dcb.fParity  = TRUE;

        fSuccess = SetCommState(hSerial, &dcb);
        if (!fSuccess) {
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
            return;
        }

        GetCommTimeouts(hSerial, &commTimeout);
        commTimeout.ReadIntervalTimeout         = 1;
        commTimeout.ReadTotalTimeoutConstant    = 1;
        commTimeout.ReadTotalTimeoutMultiplier  = 1;
        commTimeout.WriteTotalTimeoutConstant   = 1;
        commTimeout.WriteTotalTimeoutMultiplier = 1;
        SetCommTimeouts(hSerial, &commTimeout);
    }
}

ComPortX86::ComPortX86() : Channel("com_win32"), hSerial(INVALID_HANDLE_VALUE) {}

ComPortX86::~ComPortX86() {}

int ComPortX86::read() {
    DWORD   dwBytesRead;
    uint8_t data;
    int     ret = -1;

    if (hSerial != INVALID_HANDLE_VALUE) {
        if (ReadFile(hSerial, &data, 1, &dwBytesRead, NULL) && dwBytesRead == 1) {
            ret = static_cast<int>(data);
        }
    } else {
        if (_kbhit()) {
            ret    = _getch();
            char c = static_cast<char>(ret);
            std::cout << c;
            if (c == 10 || c == 13)
                std::cout << "\n";
        }
    }
    return ret;
}

size_t ComPortX86::write(uint8_t c) {
    DWORD dwBytesWritten = 1;
    if (hSerial != INVALID_HANDLE_VALUE) {
        WriteFile(hSerial, &c, 1, &dwBytesWritten, NULL);
    } else {
        std::cout << c;
    }
    return dwBytesWritten;
}
