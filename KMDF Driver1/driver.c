#include <ntddk.h>
#include "shared.h"

extern NTSTATUS PsLookupProcessByProcessId(
	HANDLE ProcessId, // Id процесса
	PEPROCESS* Process // куда закинуть 
);


extern NTSTATUS MmCopyVirtualMemory(
	PEPROCESS SourceProcess, // из какого процесса
	PVOID SourceAddress, // какие данные
	PEPROCESS TargetProcess, // в какой процесс
	PVOID TargetAddress, // по какому адресу
	SIZE_T BufferSize, // сколько байт копируется
	KPROCESSOR_MODE PreviousMode, // UserMode | KernelMoDe
	PSIZE_T ReturnSize // сколько байт было скопировано
);

UNICODE_STRING devName;
UNICODE_STRING symLink;

PSHARED_COMMAND_BUFFER OpenCommandBuffer = NULL;

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
	KdPrint(("[-] My Driver Unloaded!\n"));
}

NTSTATUS MyCreateCloseHandler(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS MyIoControlHandler(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	ULONG_PTR information = 0;

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;

	if (controlCode == IOCTL_INIT_SHARED_MEMORY) {
		PKERNEL_INIT_REQUEST request = (PKERNEL_INIT_REQUEST)Irp->AssociatedIrp.SystemBuffer;

		if (stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(KERNEL_INIT_REQUEST) && request) {
			if (request->Magic == REQ_MAGIC_VALUE) {
				OpenCommandBuffer = (PSHARED_COMMAND_BUFFER)request->SharedBufferPtr;
			
				KdPrint(("[+] Driver: Shared Memory linked at Address: 0x%llX\n", request->SharedBufferPtr));
				status = STATUS_SUCCESS;
				information = sizeof(KERNEL_INIT_REQUEST);
			}
		}
		else {
			status = STATUS_INFO_LENGTH_MISMATCH;
		}
	}
	else {
		status = STATUS_INVALID_DEVICE_REQUEST;
	}

	if (OpenCommandBuffer != NULL) {
		__try {
			if (OpenCommandBuffer->Magic == REQ_MAGIC_VALUE && OpenCommandBuffer->Command == STATUS_READY_READ) {
				PEPROCESS targetProcess = 0;
				status = PsLookupProcessByProcessId((HANDLE)OpenCommandBuffer->ProcessId, &targetProcess);

				if (NT_SUCCESS(status)) {
					SIZE_T bytesCopied;
					status = MmCopyVirtualMemory(
						targetProcess,
						(PVOID)OpenCommandBuffer->TargetAddress,
						PsGetCurrentProcess(), // Процесс вашего чита
						(PVOID)OpenCommandBuffer->UserBuffer,
						OpenCommandBuffer->Size,
						KernelMode,
						&bytesCopied
					);
					ObDereferenceObject(targetProcess);
					OpenCommandBuffer->Command = NT_SUCCESS(status) ? STATUS_COMPLETED : STATUS_FAILED;
				}
				else {
					OpenCommandBuffer->Command = STATUS_FAILED;
				}
			}
			else if (OpenCommandBuffer->Magic == REQ_MAGIC_VALUE && OpenCommandBuffer->Command == STATUS_READY_WRITE) {
				PEPROCESS targetProcess = 0;
				status = PsLookupProcessByProcessId((HANDLE)OpenCommandBuffer->ProcessId, &targetProcess);

				if (NT_SUCCESS(status)) {
					SIZE_T bytesCopied;
					status = MmCopyVirtualMemory(
						PsGetCurrentProcess(),
						(PVOID)OpenCommandBuffer->UserBuffer,
						targetProcess,
						(PVOID)OpenCommandBuffer->TargetAddress,
						OpenCommandBuffer->Size,
						KernelMode,
						&bytesCopied
					);
					ObDereferenceObject(targetProcess);
					OpenCommandBuffer->Command = NT_SUCCESS(status) ? STATUS_COMPLETED : STATUS_FAILED;
				}
				else {
					OpenCommandBuffer->Command = STATUS_FAILED;
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			OpenCommandBuffer = NULL;
		}
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = information;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);
	DriverObject->DriverUnload = DriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = MyCreateCloseHandler;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = MyCreateCloseHandler;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MyIoControlHandler;

	RtlInitUnicodeString(&devName, L"\\Device\\MyHyperHook");
	RtlInitUnicodeString(&symLink, L"\\DosDevices\\MyHyperHook");

	PDEVICE_OBJECT DeviceObject = NULL;
	NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status)) return status;

	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(DeviceObject);
		return status;
	}

	KdPrint(("[+] Debug Driver Loaded! Waiting for Shared Memory Init...\n"));
	return STATUS_SUCCESS;
}