#include <initguid.h>
#include <portcls.h>
#include <punknown.h>
#include <ntdef.h>
#include <wdm.h>
#include <ntddk.h>
#include <ntstrsafe.h>
#include "driver.h"
#include "memops.h"
#include "miniportwavert.h"

PDRIVER_UNLOAD gPCDriverUnloadRoutine = NULL;

#pragma code_seg("INIT")
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPathName) {
	NTSTATUS ntStatus;

	PAGED_CODE()

		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: DriverEntry\n");

	ntStatus = PcInitializeAdapterDriver(DriverObject, RegistryPathName, AddDevice);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: PcInitializeAdapterDriver failed, 0x%x", ntStatus);
	}
	else {
		gPCDriverUnloadRoutine = DriverObject->DriverUnload;
		DriverObject->DriverUnload = DriverUnload;
	}

	return ntStatus;
}

#pragma code_seg("PAGE")
void DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {

	PAGED_CODE()

		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: DriverUnload\n");

	if (DriverObject != nullptr) {
		if (gPCDriverUnloadRoutine != nullptr) {
			gPCDriverUnloadRoutine(DriverObject);
		}
	}

	return;
}

#pragma code_seg("PAGE")
NTSTATUS AddDevice(_In_ PDRIVER_OBJECT  DriverObject, _In_ PDEVICE_OBJECT  PhysicalDeviceObject) {
	NTSTATUS ntStatus;

	PAGED_CODE()

		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: AddDevice\n");

	ntStatus = PcAddAdapterDevice(DriverObject, PhysicalDeviceObject, StartDevice, 1, PORT_CLASS_DEVICE_EXTENSION_SIZE + sizeof(PWSK_SOCKET));
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: PcAddAdapterDevice failed, 0x%x\n", ntStatus);
	}

	return ntStatus;
}

#pragma code_seg("PAGE")
NTSTATUS StartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList) {
	NTSTATUS ntStatus = STATUS_SUCCESS;
	PPORT port = nullptr;
	NACMiniportWaveRT* miniport = nullptr;

	PAGED_CODE()

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: StartDevice\n");

//	PWSK_SOCKET pMyExtensionData = (PWSK_SOCKET)((PBYTE)DeviceObject->DeviceExtension +
//		PORT_CLASS_DEVICE_EXTENSION_SIZE);

	ntStatus = PcNewPort(&port, CLSID_PortWaveRT);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: PcNewPort failed, 0x%x\n", ntStatus);
		goto done;
	}

	miniport = new(NonPagedPoolNx, 'RWMN') NACMiniportWaveRT(DeviceObject);
	if (miniport == NULL) {
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: new NACMiniportWaveRT failed\n");
		goto done;
	}
	miniport->AddRef();

	ntStatus = port->Init(DeviceObject, Irp, miniport, NULL, ResourceList);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: port init failed, 0x%x\n", ntStatus);
		goto done;
	}
	ntStatus = PcRegisterSubdevice(DeviceObject, L"NetAudioCableOut", port);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: PcRegisterSubdevice failed, 0x%x\n", ntStatus);
		goto done;
	}

done:
	if (miniport != nullptr) {
		miniport->Release();
	}
	if (port != nullptr) {
		port->Release();
	}

	return ntStatus;
}