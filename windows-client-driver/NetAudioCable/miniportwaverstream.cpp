#include "miniportwavertstream.h"

NACMiniportWaveRTStream::NACMiniportWaveRTStream(PDEVICE_OBJECT deviceObject, PPORTWAVERTSTREAM portStream, PWSK_SOCKET socket) : m_cRef(0), m_deviceObject(deviceObject), m_PortStream(portStream), m_wskSocket(socket),
m_playPosition(0), m_wfExt(nullptr), m_dmaMdl(nullptr), m_dmaBuffer(nullptr), m_dmaBufferSize(0), m_dmaMovementRate(0), m_thread(nullptr), m_threadState(STOPPED) {

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: MyMiniPortWaveRTStream\n");

	m_PortStream->AddRef();

	KeInitializeEvent(&m_threadStateChangeSync, SynchronizationEvent, FALSE);
	KeInitializeEvent(&m_threadStateChangedSync, SynchronizationEvent, FALSE);
}

NACMiniportWaveRTStream::~NACMiniportWaveRTStream() {
	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: ~MyMiniPortWaveRTStream\n");
	if (m_wfExt != nullptr) {
		ExFreePoolWithTag(m_wfExt, 'SRWM');
		m_wfExt = nullptr;
	}
	if (m_thread != nullptr) {
		m_threadState = TERMINATED;
		KeSetEvent(&m_threadStateChangeSync, IO_NO_INCREMENT, TRUE);
		KeWaitForSingleObject(m_thread, Executive, KernelMode, FALSE, nullptr);
		ObDereferenceObject(m_thread);
	}
	m_PortStream->Release();
	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: ~MyMiniPortWaveRTStream done\n");
}

/* IUnknown */
NTSTATUS NACMiniportWaveRTStream::QueryInterface(_In_ REFIID riid, _COM_Outptr_ PVOID* ppvObj) {
	if (!ppvObj) {
		return STATUS_INVALID_PARAMETER;
	}
	*ppvObj = nullptr;
	if (IsEqualGUIDAligned(riid, IID_IUnknown) || IsEqualGUIDAligned(riid, IID_IMiniportWaveRTStream)) {
		*ppvObj = this;
		this->AddRef();
		return STATUS_SUCCESS;
	}
	return STATUS_NOT_IMPLEMENTED;
}

ULONG NACMiniportWaveRTStream::AddRef() {
	return InterlockedIncrement(&m_cRef);
}

_Use_decl_annotations_
ULONG NACMiniportWaveRTStream::Release() {
	LONG cRef;
	cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0){
		delete this;
	}
	return cRef;
}

PWAVEFORMATEX NACMiniportWaveRTStream::GetWaveFormatEx(_In_  PKSDATAFORMAT pDataFormat) {
	PWAVEFORMATEX           pWfx = nullptr;

	if (pDataFormat && (IsEqualGUIDAligned(pDataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) &&
		(IsEqualGUIDAligned(pDataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) ||
			IsEqualGUIDAligned(pDataFormat->Specifier, KSDATAFORMAT_SPECIFIER_DSOUND)))) {
		pWfx = PWAVEFORMATEX(pDataFormat + 1);

		if (IsEqualGUIDAligned(pDataFormat->Specifier, KSDATAFORMAT_SPECIFIER_DSOUND)) {
			PKSDSOUND_BUFFERDESC    pwfxds;
			pwfxds = PKSDSOUND_BUFFERDESC(pDataFormat + 1);
			pWfx = &pwfxds->WaveFormatEx;
		}
	}
	return pWfx;
}

NTSTATUS __stdcall NACMiniportWaveRTStream::Init(_In_ ULONG Pin_, _In_ PKSDATAFORMAT DataFormat_) {
	NTSTATUS ntStatus = STATUS_SUCCESS;
	PWAVEFORMATEX pWfEx = nullptr;
	HANDLE thread;
	OBJECT_ATTRIBUTES objAttrs;

	ULONG minTimerRes;
	ULONG maxTimerRes;
	ULONG currTimerRes;

	UNREFERENCED_PARAMETER(Pin_);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: IMiniportWaveRTStream->Init\n");

	pWfEx = GetWaveFormatEx(DataFormat_);
	if (nullptr == pWfEx)
	{
		ntStatus = STATUS_INVALID_PARAMETER;
		goto done;
	}

	m_dmaMovementRate = pWfEx->nAvgBytesPerSec;

	m_wfExt = (PWAVEFORMATEXTENSIBLE)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(WAVEFORMATEX) + pWfEx->cbSize, 'SRWM');
	if (m_wfExt == nullptr)
	{
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to allocate buffer to store WaveFormatExtensible\n");
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}
	RtlCopyMemory(m_wfExt, pWfEx, sizeof(WAVEFORMATEX) + pWfEx->cbSize);

	KeQueryPerformanceCounter(&m_perfCounterFrequency);

	ExQueryTimerResolution(&minTimerRes, &maxTimerRes, &currTimerRes);
	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: Timer resolution info: min = %lu, max = %lu, curr = %lu\n", minTimerRes, maxTimerRes, currTimerRes);
	m_latency = minTimerRes;

	InitializeObjectAttributes(&objAttrs, nullptr, OBJ_KERNEL_HANDLE, nullptr, nullptr);
	ntStatus = PsCreateSystemThread(&thread, GENERIC_EXECUTE, &objAttrs, nullptr, nullptr, AudioTransferThreadRoutine, this);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to create transfer thread 0x%x\n", ntStatus);
		goto done;
	}
	ntStatus = ObReferenceObjectByHandle(thread, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID*)& m_thread, nullptr);
	ZwClose(thread);
	if (!NT_SUCCESS(ntStatus)) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to retrieve thread object 0x%x\n", ntStatus);
	}
	KeSetPriorityThread(m_thread, LOW_REALTIME_PRIORITY);

	KeWaitForSingleObject(&m_threadStateChangedSync, Executive, KernelMode, FALSE, nullptr);
	if (m_threadState != STOPPED) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to start transfer thread 0x%x\n", m_threadState);
		ntStatus = STATUS_UNSUCCESSFUL;
	}
done:
	if (!NT_SUCCESS(ntStatus)) {
		if (m_wfExt != nullptr) {
			ExFreePoolWithTag(m_wfExt, 'SRWM');
			m_wfExt = nullptr;
		}
	}
	return ntStatus;
}

NTSTATUS __stdcall  NACMiniportWaveRTStream::AllocateAudioBuffer(
	_In_ ULONG RequestedSize, _Out_ PMDL* AudioBufferMdl, _Out_ ULONG* ActualSize, _Out_ ULONG* OffsetFromFirstPage, _Out_ MEMORY_CACHING_TYPE* CacheType) {

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: AllocateAudioBuffer; RequestedSize = %lu\n", RequestedSize);

	if ((0 == RequestedSize) || (RequestedSize < m_wfExt->Format.nBlockAlign))
	{
		return STATUS_UNSUCCESSFUL;
	}

	RequestedSize -= RequestedSize % (m_wfExt->Format.nBlockAlign);

	PHYSICAL_ADDRESS highAddress;
	highAddress.HighPart = 0;
	highAddress.LowPart = MAXULONG;

	m_dmaMdl = m_PortStream->AllocatePagesForMdl(highAddress, RequestedSize);

	if (m_dmaMdl == nullptr) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to allocate audio MDL\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: Allocated audio buffer MDL of size %lu and offset %lu\n", MmGetMdlByteCount(m_dmaMdl), MmGetMdlByteOffset(m_dmaMdl));

	m_dmaBuffer = (PBYTE)m_PortStream->MapAllocatedPages(m_dmaMdl, MmCached);
	if (m_dmaBuffer == nullptr) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to map MDL to buffer\n");
		m_PortStream->FreePagesFromMdl(m_dmaMdl);
		m_dmaMdl = nullptr;
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	m_dmaBufferSize = RequestedSize;

	*AudioBufferMdl = m_dmaMdl;
	*ActualSize = RequestedSize;
	*OffsetFromFirstPage = 0;
	*CacheType = MmCached;

	return STATUS_SUCCESS;
}

void __stdcall NACMiniportWaveRTStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize) {
	UNREFERENCED_PARAMETER(BufferSize);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: FreeAudioBuffer\n");

	if (AudioBufferMdl != nullptr)
	{
		m_PortStream->UnmapAllocatedPages(m_dmaBuffer, AudioBufferMdl);
		m_dmaBuffer = nullptr;
		m_PortStream->FreePagesFromMdl(AudioBufferMdl);
	}

	m_dmaBufferSize = 0;
}

NTSTATUS __stdcall NACMiniportWaveRTStream::GetClockRegister(KSRTAUDIO_HWREGISTER* Register) {
	UNREFERENCED_PARAMETER(Register);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: GetClockRegister\n");

	return STATUS_NOT_IMPLEMENTED;
}

void __stdcall NACMiniportWaveRTStream::GetHWLatency(KSRTAUDIO_HWLATENCY* hwLatency) {
	UNREFERENCED_PARAMETER(hwLatency);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: GetHWLatency\n");

	// some estimations
	hwLatency->ChipsetDelay = 1000 * 10; // estimate of network delay (1ms)
	hwLatency->CodecDelay = 0;
	hwLatency->FifoSize = m_dmaMovementRate *  m_latency / 10000000; // data is sent when period elapses
}

NTSTATUS NACMiniportWaveRTStream::GetPosition(PKSAUDIO_POSITION Position) {

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: GetPosition\n");

	Position->PlayOffset = m_playPosition;
	Position->WriteOffset = m_playPosition;

	return STATUS_SUCCESS;
}

NTSTATUS __stdcall NACMiniportWaveRTStream::GetPositionRegister(KSRTAUDIO_HWREGISTER* Register) {
	UNREFERENCED_PARAMETER(Register);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: GetPositionRegister\n");

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS __stdcall NACMiniportWaveRTStream::SetFormat(_In_ KSDATAFORMAT* DataFormat) {
	UNREFERENCED_PARAMETER(DataFormat);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: SetFormat\n");

	return STATUS_NOT_SUPPORTED;
}

NTSTATUS __stdcall NACMiniportWaveRTStream::SetState(_In_ KSSTATE State) {
	NTSTATUS ntStatus = STATUS_SUCCESS;

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: SetState %d\n", State);

	switch (State) {
	case KSSTATE_STOP:
		m_threadState = STOPPED;
		KeSetEvent(&m_threadStateChangeSync, IO_NO_INCREMENT, TRUE);
		KeWaitForSingleObject(&m_threadStateChangedSync, Executive, KernelMode, FALSE, nullptr);
		if (m_threadState != STOPPED) {
			ntStatus = STATUS_UNSUCCESSFUL;
		}
		break;

	case KSSTATE_ACQUIRE:
		m_threadState = ACQUIRED;
		KeSetEvent(&m_threadStateChangeSync, IO_NO_INCREMENT, TRUE);
		KeWaitForSingleObject(&m_threadStateChangedSync, Executive, KernelMode, FALSE, nullptr);
		if (m_threadState != ACQUIRED) {
			ntStatus = STATUS_UNSUCCESSFUL;
		}
		break;

	case KSSTATE_PAUSE:
		m_threadState = PAUSED;
		KeSetEvent(&m_threadStateChangeSync, IO_NO_INCREMENT, TRUE);
		KeWaitForSingleObject(&m_threadStateChangedSync, Executive, KernelMode, FALSE, nullptr);
		if (m_threadState != PAUSED) {
			ntStatus = STATUS_UNSUCCESSFUL;
		}
		break;

	case KSSTATE_RUN:
		m_threadState = RUNNING;
		KeSetEvent(&m_threadStateChangeSync, IO_NO_INCREMENT, TRUE);
		KeWaitForSingleObject(&m_threadStateChangedSync, Executive, KernelMode, FALSE, nullptr);
		if (m_threadState != RUNNING) {
			ntStatus = STATUS_UNSUCCESSFUL;
		}
		break;
	}

	return ntStatus;
}

NTSTATUS SendControlPacket(ControlPacketHeader* controlPacketHeader, PWSK_BUF wskBuf, PBYTE buffer, PIRP irp, PKEVENT sendEvent,
		PWSK_PROVIDER_DATAGRAM_DISPATCH dispatch, NACMiniportWaveRTStream* stream, PSOCKADDR addr, CONTROL_FLAG controlFlag) {
	const SIZE_T HEADER_SIZE = sizeof(ControlPacketHeader);

	controlPacketHeader->ControlFlag = controlFlag;
	RtlCopyMemory(buffer, controlPacketHeader, HEADER_SIZE);
	wskBuf->Length = HEADER_SIZE;
	IoReuseIrp(irp, STATUS_SUCCESS);
	IoSetCompletionRoutine(irp, SendCompletionRoutine, sendEvent, TRUE, TRUE, TRUE);
	KeClearEvent(sendEvent);
	if (dispatch->WskSendTo(stream->m_wskSocket, wskBuf, 0, addr, 0, nullptr, irp) == STATUS_PENDING) {
		KeWaitForSingleObject(sendEvent, Executive, KernelMode, FALSE, nullptr);
	}
	return irp->IoStatus.Status;
}

VOID AudioTransferThreadRoutine(_In_ PVOID StartContext) {
	const SIZE_T HEADER_SIZE = sizeof(ControlPacketHeader);
	const SIZE_T MAX_DATA_SIZE = 4;
	const SIZE_T CONTROL_BUFFER_SIZE = HEADER_SIZE + MAX_DATA_SIZE;
	const SIZE_T MAX_PACKET_SIZE = 500;
	
	NTSTATUS ntStatus = STATUS_SUCCESS;
	NACMiniportWaveRTStream* stream = (NACMiniportWaveRTStream*)StartContext;
	PWSK_PROVIDER_DATAGRAM_DISPATCH dispatch = (PWSK_PROVIDER_DATAGRAM_DISPATCH)(stream->m_wskSocket->Dispatch);

	PIRP irp = nullptr;
	PBYTE buffer = nullptr;
	PMDL mdl = nullptr;
	ControlPacketHeader controlPacketHeader;

	LARGE_INTEGER timeout;
	LARGE_INTEGER perfCounter;
	LARGE_INTEGER perfCounterPrevious = { 0 };

	ULONGLONG elapsedTicks = 0;
	ULONGLONG byteDisplacement;
	ULONGLONG byteDisplacementCarry = 0;

	WSK_BUF wskBuf;
	SOCKADDR_IN addr;
	KEVENT sendEvent;
	THREAD_STATE currentState;
	BOOL stateChanged;

	irp = IoAllocateIrp(1, FALSE);
	if (irp == nullptr) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to allocate memory for Irp\n");
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}

	buffer = (PBYTE)ExAllocatePoolWithTag(NonPagedPoolNx, CONTROL_BUFFER_SIZE, 'ADUA');
	if (buffer == nullptr) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to allocate memory for buffer\n");
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}
	RtlZeroMemory(buffer, CONTROL_BUFFER_SIZE);

	mdl = IoAllocateMdl(buffer, CONTROL_BUFFER_SIZE, FALSE, FALSE, nullptr);
	if (mdl == nullptr) {
		DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to allocate memory for mdl\n");
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}
	MmBuildMdlForNonPagedPool(mdl);

	KeInitializeEvent(&sendEvent, NotificationEvent, FALSE);

	timeout.QuadPart = -1 * (LONGLONG)stream->m_latency; // should be around 16ms

	wskBuf.Mdl = mdl;
	wskBuf.Offset = 0;
	wskBuf.Length = 0;

	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_un_b.s_b1 = 192;
	addr.sin_addr.S_un.S_un_b.s_b2 = 168;
	addr.sin_addr.S_un.S_un_b.s_b3 = 0;
	addr.sin_addr.S_un.S_un_b.s_b4 = 94;
	addr.sin_port = 7459;

	stream->m_threadState = currentState = STOPPED;
	KeSetEvent(&stream->m_threadStateChangedSync, IO_NO_INCREMENT, FALSE);

	while (stream->m_threadState != TERMINATED) {
		stateChanged = false;
		ntStatus = KeWaitForSingleObject(&stream->m_threadStateChangeSync, Executive, KernelMode, FALSE, currentState == RUNNING ? &timeout : nullptr);

		if (currentState != stream->m_threadState) {
			stateChanged = true;
			DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: Changing state %d -> %d\n", currentState, stream->m_threadState);
		}

		if (stream->m_threadState == TERMINATED) {
			break;
		}
		
		if (currentState == STOPPED && stream->m_threadState == ACQUIRED) {
			ntStatus = SendControlPacket(&controlPacketHeader, &wskBuf, buffer, irp, &sendEvent, dispatch, stream, (PSOCKADDR)&addr, ACQUIRE);
			if (ntStatus != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send ACQUIRE control packet: %x\n", ntStatus);
			}
			else {
				currentState = ACQUIRED;
			}
		}
		else if (currentState == ACQUIRED && stream->m_threadState == PAUSED) {
			ntStatus = SendControlPacket(&controlPacketHeader, &wskBuf, buffer, irp, &sendEvent, dispatch, stream, (PSOCKADDR)&addr, PAUSE);
			if (ntStatus != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send ACQUIRE control packet: %x\n", ntStatus);
			}
			else {
				currentState = PAUSED;
			}
		}
		else if (currentState == PAUSED && stream->m_threadState == RUNNING) {
			ntStatus = SendControlPacket(&controlPacketHeader, &wskBuf, buffer, irp, &sendEvent, dispatch, stream, (PSOCKADDR)&addr, START);
			if (ntStatus != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send START control packet: %x\n", ntStatus);
			}
			else {
				perfCounterPrevious.QuadPart = 0;
				byteDisplacementCarry = 0;
				elapsedTicks = 0;

				currentState = RUNNING;
			}
		}
		else if (currentState == RUNNING && stream->m_threadState == PAUSE) {
			ntStatus = SendControlPacket(&controlPacketHeader, &wskBuf, buffer, irp, &sendEvent, dispatch, stream, (PSOCKADDR)&addr, PAUSE);
			if (ntStatus != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send PAUSE control packet: %x\n", ntStatus);
			}
			else {
				currentState = PAUSED;
			}
		}
		else if (currentState == PAUSED && stream->m_threadState == ACQUIRED) {
			ntStatus = SendControlPacket(&controlPacketHeader, &wskBuf, buffer, irp, &sendEvent, dispatch, stream, (PSOCKADDR)&addr, ACQUIRE);
			if (ntStatus != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send ACQUIRE control packet: %x\n", ntStatus);
			}
			else {
				currentState = ACQUIRED;
			}
		}
		else if (currentState == ACQUIRED && stream->m_threadState == STOPPED) {
			ntStatus = SendControlPacket(&controlPacketHeader, &wskBuf, buffer, irp, &sendEvent, dispatch, stream, (PSOCKADDR)&addr, STOP);
			if (ntStatus != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send STOP control packet: %x\n", ntStatus);
			}
			else {
				currentState = STOPPED;
			}
		}

		if (stateChanged) {
			stream->m_threadState = currentState;
			DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "NAC: State changed\n");
			KeSetEvent(&stream->m_threadStateChangedSync, IO_NO_INCREMENT, FALSE);
		}

		if(currentState == RUNNING){
			perfCounter = KeQueryPerformanceCounter(nullptr);
			/* After state set to RUN, there should be data in buffer. I did not find specification of how to find out how many, so I'll just assume there is enough for one period */
			if (perfCounterPrevious.QuadPart == 0) {
				byteDisplacement = ((ULONGLONG)stream->m_dmaMovementRate) * stream->m_latency / 10000000;
			}
			else {
				elapsedTicks = perfCounter.QuadPart - perfCounterPrevious.QuadPart;

				/*
				* elapsedTicks / frequency -> elapsed time in seconds
				* bytes to transfer = byteRate * elapsed time in seconds
				* bytes to transfer = byteRate * (elapsedTicks / frequency)
				*/
				byteDisplacement = (ULONGLONG)((stream->m_dmaMovementRate * elapsedTicks + byteDisplacementCarry) / stream->m_perfCounterFrequency.QuadPart);
				byteDisplacementCarry = (ULONGLONG)((stream->m_dmaMovementRate * elapsedTicks + byteDisplacementCarry) % stream->m_perfCounterFrequency.QuadPart);
			}

			byteDisplacement = byteDisplacement / 4 * 4; // to allign to frame (2 chan * 16bits per chan = 4B) boundary
			byteDisplacementCarry += (byteDisplacement % 4) * stream->m_perfCounterFrequency.QuadPart;

			DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: after %llu ticks to transfer %llu(carry %llu) from offset %lu\n", elapsedTicks, byteDisplacement, byteDisplacementCarry, stream->m_playPosition);

			if (byteDisplacement > stream->m_dmaBufferSize) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_WARNING_LEVEL, "NAC: Buffer overflow of %llu detected\n", byteDisplacement - stream->m_dmaBufferSize);
				byteDisplacement = stream->m_dmaBufferSize;
				byteDisplacementCarry = 0;
			}

			controlPacketHeader.ControlFlag = DATASTART;
			RtlCopyMemory(buffer, &controlPacketHeader, HEADER_SIZE);
			wskBuf.Length = HEADER_SIZE;
			RtlCopyMemory(buffer + HEADER_SIZE, &byteDisplacement, MAX_DATA_SIZE); // FIXME: size is not really known, should be handled better
			wskBuf.Length += MAX_DATA_SIZE;
			IoReuseIrp(irp, STATUS_SUCCESS);
			IoSetCompletionRoutine(irp, SendCompletionRoutine, &sendEvent, TRUE, TRUE, TRUE);
			KeClearEvent(&sendEvent);
			if (dispatch->WskSendTo(stream->m_wskSocket, &wskBuf, 0, (PSOCKADDR)& addr, 0, nullptr, irp) == STATUS_PENDING) {
				KeWaitForSingleObject(&sendEvent, Executive, KernelMode, FALSE, nullptr);
			}
			if (irp->IoStatus.Status != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send DATASTART control packet: %x\n", ntStatus);
			}

			while (byteDisplacement > 0) {
				SIZE_T chunk = min(byteDisplacement, stream->m_dmaBufferSize - stream->m_playPosition);
				chunk = min(chunk, MAX_PACKET_SIZE);
				
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: transfer %llu bytes from offset %lu\n", chunk, stream->m_playPosition);

				// FIXME don't rely on fact that there is only single MDL page
				wskBuf.Mdl = stream->m_dmaMdl;
				wskBuf.Offset = stream->m_playPosition;
				wskBuf.Length = chunk;

				IoReuseIrp(irp, STATUS_SUCCESS);
				IoSetCompletionRoutine(irp, SendCompletionRoutine, &sendEvent, TRUE, TRUE, TRUE);
				KeClearEvent(&sendEvent);
				if (dispatch->WskSendTo(stream->m_wskSocket, &wskBuf, 0, (PSOCKADDR)& addr, 0, nullptr, irp) == STATUS_PENDING) {
					KeWaitForSingleObject(&sendEvent, Executive, KernelMode, FALSE, nullptr);
				}
				if (irp->IoStatus.Status != STATUS_SUCCESS) {
					DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send data packet: %x\n", ntStatus);
				}
				
				byteDisplacement -= chunk;
				stream->m_playPosition = (stream->m_playPosition + (ULONG)chunk) % stream->m_dmaBufferSize;
			}

			// FIXME: should be set before each transmit
			wskBuf.Offset = 0;
			wskBuf.Mdl = mdl;

			controlPacketHeader.ControlFlag = DATAEND;
			RtlCopyMemory(buffer, &controlPacketHeader, HEADER_SIZE);
			wskBuf.Length = HEADER_SIZE;
			IoReuseIrp(irp, STATUS_SUCCESS);
			IoSetCompletionRoutine(irp, SendCompletionRoutine, &sendEvent, TRUE, TRUE, TRUE);
			KeClearEvent(&sendEvent);
			if (dispatch->WskSendTo(stream->m_wskSocket, &wskBuf, 0, (PSOCKADDR)& addr, 0, nullptr, irp) == STATUS_PENDING) {
				KeWaitForSingleObject(&sendEvent, Executive, KernelMode, FALSE, nullptr);
			}
			if (irp->IoStatus.Status != STATUS_SUCCESS) {
				DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "NAC: Failed to send DATAEND control packet: %x\n", ntStatus);
			}

			perfCounterPrevious = perfCounter;
		}
	}

done:
	if (mdl != nullptr) {
		IoFreeMdl(mdl);
		mdl = nullptr;
	}
	if (buffer != nullptr) {
		ExFreePoolWithTag(buffer, 'OUA');
		buffer = nullptr;
	}
	if (irp != nullptr) {
		IoFreeIrp(irp);
		irp = nullptr;
	}
	stream->m_threadState = TERMINATED;
	KeSetEvent(&stream->m_threadStateChangedSync, IO_NO_INCREMENT, FALSE);
	PsTerminateSystemThread(ntStatus);
}

NTSTATUS SendCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_TRACE_LEVEL, "NAC: SendCompletionRoutine\n");

	KeSetEvent((PRKEVENT)Context, IO_NO_INCREMENT, FALSE);

	return STATUS_MORE_PROCESSING_REQUIRED;
}