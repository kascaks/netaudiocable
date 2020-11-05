#include <wdm.h>
#include <wsk.h>
#include "memops.h"
#include "pcfilterdescriptor.h"
#include "miniportwavert.h"
#include "miniportwavertstream.h"

NACMiniportWaveRT::NACMiniportWaveRT(PDEVICE_OBJECT deviceObject) : m_cRef(0), m_deviceObject(deviceObject), m_wskRegistrationRegistered(FALSE), m_wskSocket(nullptr) {
	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: NACMiniportWaveRT\n");
}

NACMiniportWaveRT::~NACMiniportWaveRT() {
	PIRP irp = nullptr;
	#pragma warning(disable:26494) //initialized with KeInitializeEvent when needed
	KEVENT event;

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: ~NACMiniportWaveRT\n");

	if (m_wskSocket != nullptr) {
		irp = IoAllocateIrp(1, FALSE);
		if (irp == nullptr) {
			DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: Failed to allocate memory to close socket\n");
		}
		else {
			KeInitializeEvent(&event, NotificationEvent, FALSE);
			IoSetCompletionRoutine(irp, CloseCompletionRoutine, &event, TRUE, TRUE, TRUE);
			if (((PWSK_PROVIDER_BASIC_DISPATCH)(m_wskSocket->Dispatch))->WskCloseSocket(m_wskSocket, irp) == STATUS_PENDING) {
				KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, nullptr);
			}
			if (!NT_SUCCESS(irp->IoStatus.Status)) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: WskCloseSocket failed: 0x%x\n", irp->IoStatus.Status);
				if (irp->IoStatus.Status == STATUS_PENDING) {
					IoCancelIrp(irp);
					KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, nullptr);
				}
			}
			IoFreeIrp(irp);
		}
	}
	if (m_wskRegistrationRegistered) {
		WskDeregister(&m_wskRegistration);
	}
}

/* IUnknown */
NTSTATUS NACMiniportWaveRT::QueryInterface(_In_ REFIID riid, _COM_Outptr_ PVOID* ppvObj) {
	if (!ppvObj) {
		return STATUS_INVALID_PARAMETER;
	}
	*ppvObj = nullptr;
	if (IsEqualGUIDAligned(riid, IID_IUnknown) || IsEqualGUIDAligned(riid, IID_IMiniport) || IsEqualGUIDAligned(riid, IID_IMiniportWaveRT)) {
		*ppvObj = this;
		this->AddRef();
		return STATUS_SUCCESS;
	}
	return STATUS_NOT_IMPLEMENTED;
}

ULONG NACMiniportWaveRT::AddRef() {
	return InterlockedIncrement(&m_cRef);
}

_Use_decl_annotations_
ULONG NACMiniportWaveRT::Release() {
	LONG cRef;
	cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0) {
		delete this;
	}
	return cRef;
}

/* IMiniport */
__declspec(nothrow) NTSTATUS __stdcall NACMiniportWaveRT::DataRangeIntersection(_In_ ULONG PinId, _In_ PKSDATARANGE ClientDataRange, _In_ PKSDATARANGE MyDataRange,
	_In_ ULONG OutputBufferLength, _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength) PVOID ResultantFormat, _Out_ PULONG ResultantFormatLength) {
	ULONG requiredSize;

	UNREFERENCED_PARAMETER(PinId);
	UNREFERENCED_PARAMETER(ResultantFormat);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: GetRangeIntersection\n");

	if (!IsEqualGUIDAligned(ClientDataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) {
		return STATUS_NOT_IMPLEMENTED;
	}

	requiredSize = sizeof(KSDATAFORMAT_WAVEFORMATEX);

	if (OutputBufferLength == 0) {
		*ResultantFormatLength = requiredSize;
		return STATUS_BUFFER_OVERFLOW;
	}
	else if (OutputBufferLength < requiredSize) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (((PKSDATARANGE_AUDIO)MyDataRange)->MaximumChannels != ((PKSDATARANGE_AUDIO)ClientDataRange)->MaximumChannels) {
		return STATUS_NO_MATCH;
	}

	// Let the class handler do the rest.
	return STATUS_NOT_IMPLEMENTED;
}

__declspec(nothrow) NTSTATUS __stdcall NACMiniportWaveRT::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* OutFilterDescriptor) {
	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: IMiniport::GetDescription\n");

	*OutFilterDescriptor = &SpeakerWaveMiniportFilterDescriptor;

	return STATUS_SUCCESS;
}

/* IMiniportWaveRT */
__declspec(nothrow) NTSTATUS __stdcall NACMiniportWaveRT::Init(_In_ PUNKNOWN UnknownAdapter, _In_ PRESOURCELIST ResourceList, _In_ PPORTWAVERT Port) {
	NTSTATUS ntStatus;
	WSK_PROVIDER_NPI wskProviderNpi;
	BOOL wskProviderNpiRegistered = FALSE;
	WSK_CLIENT_NPI wskClientNpi;
	PWSK_SOCKET wskSocket = nullptr;
	PIRP irp = nullptr;
	KEVENT event;
	LARGE_INTEGER timeout;

	UNREFERENCED_PARAMETER(UnknownAdapter);
	UNREFERENCED_PARAMETER(ResourceList);
	UNREFERENCED_PARAMETER(Port);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: IMiniportWaveRT::Init\n");

	wskClientNpi.ClientContext = nullptr;
	wskClientNpi.Dispatch = &m_wskAppDispatch;
	ntStatus = WskRegister(&wskClientNpi, &m_wskRegistration);
	if (ntStatus != STATUS_SUCCESS) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: WskRegister failed: 0x%x\n", ntStatus);
		goto done;
	}
	m_wskRegistrationRegistered = TRUE;

	ntStatus = WskCaptureProviderNPI(&m_wskRegistration, 1000, &wskProviderNpi);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: WskCaptureProviderNPI failed: 0x%x\n", ntStatus);
		goto done;
	}
	wskProviderNpiRegistered = TRUE;

	irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: Failed to allocate Irp\n");
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}

	timeout.QuadPart = 10000000; // *100ns -> 1s

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	IoSetCompletionRoutine(irp, SocketCompletionRoutine, &event, TRUE, TRUE, TRUE);
	if (wskProviderNpi.Dispatch->WskSocket(wskProviderNpi.Client, AF_INET, SOCK_DGRAM, IPPROTO_UDP, WSK_FLAG_DATAGRAM_SOCKET, nullptr, nullptr, nullptr, nullptr, nullptr, irp) == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout);
	}
	ntStatus = irp->IoStatus.Status;
	if (ntStatus != STATUS_SUCCESS) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: WskSocket failed 0x%x\n", ntStatus);
		if (ntStatus == STATUS_PENDING) {
			IoCancelIrp(irp);
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, nullptr);
		}
		goto done;
	}

	wskSocket = (PWSK_SOCKET)irp->IoStatus.Information;

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = 0;
	addr.sin_port = 0;

	IoReuseIrp(irp, STATUS_SUCCESS);
	IoSetCompletionRoutine(irp, BindCompletionRoutine, &event, TRUE, TRUE, TRUE);
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	if (((PWSK_PROVIDER_DATAGRAM_DISPATCH)(wskSocket->Dispatch))->WskBind(wskSocket, (PSOCKADDR)& addr, 0, irp) == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout);
	}
	ntStatus = irp->IoStatus.Status;
	if (ntStatus != STATUS_SUCCESS) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: WskBind failed 0x%x\n", ntStatus);
		if (ntStatus == STATUS_PENDING) {
			IoCancelIrp(irp);
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, nullptr);
		}
		goto done;
	}

	m_wskSocket = wskSocket;

done:
	if (ntStatus != STATUS_SUCCESS) {
		if (wskSocket != nullptr) {
			IoReuseIrp(irp, STATUS_SUCCESS);
			IoSetCompletionRoutine(irp, CloseCompletionRoutine, &event, TRUE, TRUE, TRUE);
			KeInitializeEvent(&event, NotificationEvent, FALSE);
			if (((PWSK_PROVIDER_BASIC_DISPATCH)(wskSocket->Dispatch))->WskCloseSocket(wskSocket, irp) == STATUS_PENDING) {
				KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout);
			}
			if (!NT_SUCCESS(irp->IoStatus.Status)) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: WskCloseSocket failed: 0x%x\n", irp->IoStatus.Status);
				if (irp->IoStatus.Status == STATUS_PENDING) {
					IoCancelIrp(irp);
					KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, nullptr);
				}
			}
		}
	}
	if (irp != nullptr) {
		IoFreeIrp(irp);
	}
	if (wskProviderNpiRegistered) {
		WskReleaseProviderNPI(&m_wskRegistration);
	}
	if (ntStatus != STATUS_SUCCESS) {
		if (m_wskRegistrationRegistered) {
			WskDeregister(&m_wskRegistration);
			m_wskRegistrationRegistered = FALSE;
		}
	}
	return ntStatus;
}

NTSTATUS NACMiniportWaveRT::NewStream(_Out_ PMINIPORTWAVERTSTREAM* Stream, _In_ PPORTWAVERTSTREAM PortStream,
	_In_ ULONG Pin, _In_ BOOLEAN Capture, _In_ PKSDATAFORMAT DataFormat) {
	NTSTATUS ntStatus = STATUS_SUCCESS;
	NACMiniportWaveRTStream* stream = nullptr;

	// FIXME: determine whether Capture can be true (does description specify that we can only stream?)
	UNREFERENCED_PARAMETER(Capture);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "XXX: NewStream\n");

	*Stream = nullptr;

	if (m_wskSocket == nullptr) {
		ntStatus = STATUS_INVALID_DEVICE_STATE;
		goto done;
	}

	stream = new(NonPagedPoolNx, 'RWMN') NACMiniportWaveRTStream(m_deviceObject, PortStream, m_wskSocket);
	if (stream == nullptr) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: Failed to allocate new stream\n");
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}
	stream->AddRef();

	ntStatus = stream->Init(Pin, DataFormat);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "XXX: MyMiniportWaveRTStream->Init failed 0x%x\n", ntStatus);
		goto done;
	}

	*Stream = PMINIPORTWAVERTSTREAM(stream);
	(*Stream)->AddRef();

done:
	if (stream != nullptr) {
		stream->Release();
	}
	return ntStatus;
}

NTSTATUS NACMiniportWaveRT::GetDeviceDescription(_Out_ PDEVICE_DESCRIPTION DeviceDescription) {

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: GetDeviceDescription\n");

	RtlZeroMemory(DeviceDescription, sizeof(DEVICE_DESCRIPTION));

	DeviceDescription->Master = TRUE;
	DeviceDescription->ScatterGather = TRUE;
	DeviceDescription->Dma32BitAddresses = TRUE;
	DeviceDescription->InterfaceType = PCIBus;
	DeviceDescription->MaximumLength = 0xFFFFFFFF;

	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS BindCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: BindCompletionRoutine\n");

	KeSetEvent(static_cast<PRKEVENT>(Context), IO_NO_INCREMENT, FALSE);

	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS CloseCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: CloseCompletionRoutine\n");

	KeSetEvent((PRKEVENT)Context, IO_NO_INCREMENT, FALSE);

	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS SocketCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "XXX: SocketCompletionRoutine\n");

	KeSetEvent((PRKEVENT)Context, IO_NO_INCREMENT, FALSE);

	return STATUS_MORE_PROCESSING_REQUIRED;
}