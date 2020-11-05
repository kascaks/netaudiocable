#ifndef _NAC_MINIPORTWAVERTSTREAM_H_
#define _NAC_MINIPORTWAVERTSTREAM_H_

#include <portcls.h>
#include <wsk.h>

enum THREAD_STATE {
	STOPPED,
	ACQUIRED,
	PAUSED,
	RUNNING,
	TERMINATED
};

enum CONTROL_FLAG {
	STOP,
	ACQUIRE,
	PAUSE,
	START,
	DATASTART,
	DATAEND
};

struct ControlPacketHeader {
	const BYTE ControlHeaderCookie[4] = { 0x00, 0xff, 0x00, 0xff };
	CONTROL_FLAG ControlFlag = STOP;
};

class NACMiniportWaveRTStream : public IMiniportWaveRTStream {
	friend KSTART_ROUTINE AudioTransferThreadRoutine;
	friend NTSTATUS SendControlPacket(ControlPacketHeader* controlPacketHeader, PWSK_BUF wskBuf, PBYTE buffer, PIRP irp, PKEVENT sendEvent,
		PWSK_PROVIDER_DATAGRAM_DISPATCH dispatch, NACMiniportWaveRTStream* stream, PSOCKADDR addr, CONTROL_FLAG controlFlag);
private:
	LONG m_cRef;

	PDEVICE_OBJECT m_deviceObject;
	PPORTWAVERTSTREAM m_PortStream;
	PWSK_SOCKET m_wskSocket;

	ULONG m_playPosition;
	PWAVEFORMATEXTENSIBLE m_wfExt;
	PMDL m_dmaMdl;
	PBYTE m_dmaBuffer;
	ULONG m_dmaBufferSize;
	ULONG m_dmaMovementRate;

	PKTHREAD m_thread;
	THREAD_STATE m_threadState;
	KEVENT m_threadStateChangeSync;
	KEVENT m_threadStateChangedSync;

	ULONG m_latency; // how often data is sent, usually high boundary of system timer (cca 16ms) in 100ns units
	LARGE_INTEGER m_perfCounterFrequency; // system performance counter resolution (ticks per second)

public:
	NACMiniportWaveRTStream(PDEVICE_OBJECT deviceObject, PPORTWAVERTSTREAM portStream, PWSK_SOCKET socket);
	virtual ~NACMiniportWaveRTStream();

	/* IUnknown */
	virtual __declspec(nothrow) NTSTATUS __stdcall QueryInterface(_In_ REFIID riid, _COM_Outptr_ PVOID* ppvObject);
    virtual __declspec(nothrow) ULONG __stdcall AddRef();
    _At_(this, __drv_freesMem(object))
    virtual __declspec(nothrow) ULONG __stdcall Release();

	PWAVEFORMATEX GetWaveFormatEx(_In_  PKSDATAFORMAT pDataFormat);

	/* IMiniportWaveRTStream */
	NTSTATUS Init(_In_ ULONG Pin_, _In_ PKSDATAFORMAT DataFormat_);
	virtual NTSTATUS AllocateAudioBuffer(
		_In_ ULONG RequestedSize, _Out_ PMDL* AudioBufferMdl, _Out_ ULONG* ActualSize, _Out_ ULONG* OffsetFromFirstPage, _Out_ MEMORY_CACHING_TYPE* CacheType) override;
	virtual __declspec(nothrow) void __stdcall FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize);
	virtual __declspec(nothrow) NTSTATUS __stdcall GetClockRegister(KSRTAUDIO_HWREGISTER* Register);
	virtual __declspec(nothrow) void __stdcall GetHWLatency(KSRTAUDIO_HWLATENCY* hwLatency);
	virtual __declspec(nothrow) NTSTATUS __stdcall GetPosition(PKSAUDIO_POSITION Position);
	virtual __declspec(nothrow) NTSTATUS __stdcall GetPositionRegister(KSRTAUDIO_HWREGISTER* Register);
	virtual __declspec(nothrow) NTSTATUS __stdcall SetFormat(_In_ KSDATAFORMAT* DataFormat);
	virtual __declspec(nothrow) NTSTATUS __stdcall SetState(_In_ KSSTATE State);
};

IO_COMPLETION_ROUTINE SendCompletionRoutine;

#endif