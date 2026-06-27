#include "shared.h"
#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>

using namespace std;

int MONEY;
ULONGLONG moneyOffset[7] = { 0x00498E28, 0x20, 0xf60, 0x230, 0xB8, 0xA8, 0x3F0 };

#pragma section(".mystash", read, write)

__declspec(allocate(".mystash")) SHARED_COMMAND_BUFFER MyHiddenBuffer;

PSHARED_COMMAND_BUFFER SharedMemory = &MyHiddenBuffer;

PVOID FindDataSectionCave(const char* moduleName, DWORD requiredSize) {
	HMODULE hMod = GetModuleHandleA(moduleName);
	if (!hMod) return nullptr;

	PBYTE baseAddress = reinterpret_cast<PBYTE>(hMod);
	PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(baseAddress);
	if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

	PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(baseAddress + dosHeader->e_lfanew);
	if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;

	PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
	for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
		if (strcmp(reinterpret_cast<const char*>(sectionHeader[i].Name), ".data") == 0) {
			PBYTE sectionStart = baseAddress + sectionHeader[i].VirtualAddress;
			DWORD virtualSize = sectionHeader[i].Misc.VirtualSize;
			DWORD rawSize = sectionHeader[i].SizeOfRawData;

			if (rawSize > virtualSize) {
				DWORD caveSize = rawSize - virtualSize;
				if (caveSize >= requiredSize) {
					return reinterpret_cast<PVOID>(sectionStart + virtualSize + 16);
				}
			}
		}
	}
	return nullptr;
}

DWORD GetCurrentProcessByName(const wstring& processName) {
	DWORD pid = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
	if (snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W processEntry;
		processEntry.dwSize = sizeof(processEntry);
		if (Process32FirstW(snapshot, &processEntry)) {
			do {
				if (processName == processEntry.szExeFile) {
					pid = processEntry.th32ProcessID;
					break;
				}
			} while (Process32NextW(snapshot, &processEntry));
		}
		CloseHandle(snapshot);
	}
	return pid;
}

uintptr_t GetModuleBase(DWORD pid, const wstring& moduleName) {
	uintptr_t baseAddress = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snapshot != INVALID_HANDLE_VALUE) {
		MODULEENTRY32W moduleEntry;
		moduleEntry.dwSize = sizeof(moduleEntry);
		if (Module32FirstW(snapshot, &moduleEntry)) {
			do {
				if (moduleName == moduleEntry.szModule) {
					baseAddress = reinterpret_cast<uintptr_t>(moduleEntry.modBaseAddr);
					break;
				}
			} while (Module32NextW(snapshot, &moduleEntry));
		}
		CloseHandle(snapshot);
	}
	return baseAddress;
}

template <size_t N>
BOOL WriteDriverMemory(HANDLE hDriver, DWORD pid, uintptr_t address, size_t size, uintptr_t valueBuffer, BYTE(&originalBytes)[N], uint32_t* originalBytesSize) {
	if (!SharedMemory) return FALSE;

	SharedMemory->Magic = REQ_MAGIC_VALUE;
	SharedMemory->Command = STATUS_READY_WRITE;
	SharedMemory->ProcessId = pid;
	SharedMemory->TargetAddress = address;
	SharedMemory->UserBuffer = (ULONG64)valueBuffer;
	SharedMemory->Size = (ULONG)size;

	DWORD bytesReturned = 0;
	DeviceIoControl(hDriver, IOCTL_INIT_SHARED_MEMORY, nullptr, 0, nullptr, 0, &bytesReturned, NULL);

	if (SharedMemory->Command == STATUS_COMPLETED) {
		if (originalBytesSize != nullptr) {
			*originalBytesSize = (uint32_t)size;
		}
		return TRUE;
	}

	return FALSE;
}

BOOL ReadDriverMemory(HANDLE hDriver, DWORD pid, uintptr_t address, size_t size, uintptr_t responseBuffer) {
	if (!SharedMemory) return FALSE;

	SharedMemory->Magic = REQ_MAGIC_VALUE;
	SharedMemory->Command = STATUS_READY_READ;
	SharedMemory->ProcessId = pid;
	SharedMemory->TargetAddress = address;
	SharedMemory->UserBuffer = (ULONG64)responseBuffer;
	SharedMemory->Size = (ULONG)size;

	DWORD bytesReturned = 0;
	DeviceIoControl(hDriver, IOCTL_INIT_SHARED_MEMORY, nullptr, 0, nullptr, 0, &bytesReturned, NULL);

	return SharedMemory->Command == STATUS_COMPLETED;
}

template <size_t N>
uintptr_t GetPointerWithOffsets(HANDLE hDriver, DWORD pid, ULONGLONG(&offsets)[N], uintptr_t moduleBase) {
	uintptr_t finalAddress;
	int i;
	finalAddress = moduleBase + offsets[0];

	for (i = 1; i < (int)N - 1; i++) {
		ReadDriverMemory(hDriver, pid, finalAddress, sizeof(uintptr_t), (uintptr_t)&finalAddress);
		finalAddress += offsets[i];
	}
	ReadDriverMemory(hDriver, pid, finalAddress, sizeof(uintptr_t), (uintptr_t)&finalAddress);
	finalAddress += offsets[i];

	return finalAddress;
}

int main()
{
	BYTE originalBytes[16] = { 0 };
	uint32_t originalBytesSize = 0;
	string move;
	BOOL success = FALSE;
	DWORD pid;
	uintptr_t moduleBase;
	uintptr_t pMoney = 0;
	wstring processName = L"Skul.exe";
	wstring moduleName = L"mono-2.0-bdwgc.dll";

	cout << "[*] Client started. Initializing allocated memory section.." << endl;

	RtlSecureZeroMemory(SharedMemory, sizeof(SHARED_COMMAND_BUFFER));
	SharedMemory->Magic = REQ_MAGIC_VALUE;
	SharedMemory->Command = STATUS_IDLE;

	cout << "[+] Shared Memory structure allocated inside .exe section at: 0x" << hex << (uint64_t)SharedMemory << dec << endl;

	cout << "[*] Trying to connect to debug driver.." << endl;
	HANDLE hDriver = CreateFileA(
		"\\\\.\\MyHyperHook",
		GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
	);

	if (hDriver == INVALID_HANDLE_VALUE) {
		cout << "[-] Failed to connect to driver! Error code: " << GetLastError() << endl;
		system("pause");
		return 1;
	}
	cout << "[+] Connected to debug driver!" << endl;

	KERNEL_INIT_REQUEST initReq;
	initReq.Magic = REQ_MAGIC_VALUE;
	initReq.SharedBufferPtr = reinterpret_cast<ULONG64>(SharedMemory);

	DWORD bytesReturned;
	success = DeviceIoControl(hDriver, IOCTL_INIT_SHARED_MEMORY, &initReq, sizeof(initReq), &initReq, sizeof(initReq), &bytesReturned, NULL);
	if (!success) {
		cout << "[-] Failed to initialize Shared Memory link in kernel!" << endl;
		CloseHandle(hDriver);
		system("pause");
		return 1;
	}
	cout << "[+] Kernel successfully linked to our .data cave address!" << endl;

	pid = GetCurrentProcessByName(processName);
	moduleBase = GetModuleBase(pid, moduleName);
	cout << "PID: " << pid << "\n" << "ModuleBase: " << hex << moduleBase << dec << endl;

	if (pid == 0 || moduleBase == 0) {
		cout << "[-] Game or Mono module not found! Please run the game first." << endl;
		CloseHandle(hDriver);
		system("pause");
		return 1;
	}

	pMoney = GetPointerWithOffsets(hDriver, pid, moneyOffset, moduleBase);
	cout << "[+] Final Money Address calculated: 0x" << hex << pMoney << dec << endl;

	while (cin >> move) {
		if (move == "Z" || move == "z") {
			break;
		}
		if (move == "R" || move == "r") {
			success = ReadDriverMemory(hDriver, pid, pMoney, sizeof(int), (uintptr_t)&MONEY);
			if (success) {
				cout << "[+] Read executed successfully via .data cave!" << endl;
				cout << "[*] Current Money: " << MONEY << endl;
			}
			else {
				cout << "[-] Read failed!" << endl;
			}
		}
		if (move == "W" || move == "w") {
			cout << "Enter new money: ";
			int newMoney;
			cin >> newMoney;

			ReadDriverMemory(hDriver, pid, pMoney, sizeof(int), (uintptr_t)originalBytes);

			success = WriteDriverMemory(hDriver, pid, pMoney, sizeof(int), (uintptr_t)&newMoney, originalBytes, &originalBytesSize);
			if (success) {
				cout << "[*] Money value was changed via .data cave" << endl;
				cout << "[*] Old money value: " << *(int*)originalBytes << endl;
			}
			else 
			{ 
				cout << "[-] Write failed!" << endl; 
			}
		}
	}
	CloseHandle(hDriver);
	cout << "[*] Connection closed." << endl;
	system("pause");
	return 0;
}