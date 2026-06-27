#pragma once

#ifndef _KERNEL_MODE
#include <Windows.h>
#endif 

#ifdef _KERNEL_MODE
#include <ntddk.h>
#endif 

#define REQ_MAGIC_VALUE 0xDEADC0DEADULL

enum eCommandStatus {
    STATUS_IDLE = 0,      
    STATUS_READY_READ,    
    STATUS_READY_WRITE,   
    STATUS_COMPLETED,     
    STATUS_FAILED         
};

typedef struct _SHARED_COMMAND_BUFFER {
    ULONG64 Magic;          // Должно быть равно REQ_MAGIC_VALUE
    ULONG   Command;        // Флаг из eCommandStatus
    ULONG   ProcessId;      // PID игры (цели)
    ULONG64 TargetAddress;  // Адрес в памяти игры
    ULONG64 UserBuffer;     // Адрес буфера в памяти нашего .exe (куда/откуда копировать)
    ULONG   Size;           // Сколько байт копировать
} SHARED_COMMAND_BUFFER, * PSHARED_COMMAND_BUFFER;

typedef struct _KERNEL_INIT_REQUEST {
    ULONG64 Magic;          // REQ_MAGIC_VALUE
    ULONG64 SharedBufferPtr;// Виртуальный адрес Code Cave в юзермоде
} KERNEL_INIT_REQUEST, * PKERNEL_INIT_REQUEST;

#define IOCTL_INIT_SHARED_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)