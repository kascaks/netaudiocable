#ifndef _NAC_MINIPORTWAVERT_H_
#define _NAC_MINIPORTWAVERT_H_

#include <portcls.h>
#include <wsk.h>

class NACMiniportWaveRT : public IMiniportWaveRT {
private:
	LONG m_cRef;

	PDEVICE_OBJECT m_deviceObject;
	const WSK_CLIENT_DISPATCH m_wskAppDispatch = {
			MAKE_WSK_VERSION(1,0),
			0,
			NULL
	};
	WSK_REGISTRATION m_wskRegistration;
	BOOL m_wskRegistrationRegistered;
	PWSK_SOCKET m_wskSocket;
public:
	NACMiniportWaveRT(PDEVICE_OBJECT pDeviceObject);
	virtual ~NACMiniportWaveRT();

	/* IUnknown */
	__declspec(nothrow) NTSTATUS __stdcall QueryInterface(_In_ REFIID riid, _COM_Outptr_ PVOID* ppvObject) override;
	__declspec(nothrow) ULONG __stdcall AddRef() override;
	_At_(this, __drv_freesMem(object))
	__declspec(nothrow) ULONG __stdcall Release() override;

	/* IMiniportWaveRT*/
	__declspec(nothrow) NTSTATUS __stdcall DataRangeIntersection(_In_ ULONG PinId, _In_ PKSDATARANGE ClientDataRange, _In_ PKSDATARANGE MyDataRange,
		_In_ ULONG OutputBufferLength, _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength) PVOID ResultantFormat, _Out_ PULONG ResultantFormatLength) override;
	__declspec(nothrow) NTSTATUS __stdcall GetDescription(_Out_ PPCFILTER_DESCRIPTOR* OutFilterDescriptor) override;
	__declspec(nothrow) NTSTATUS __stdcall Init(_In_ PUNKNOWN UnknownAdapter, _In_ PRESOURCELIST ResourceList, _In_ PPORTWAVERT Port) override;
	__declspec(nothrow) NTSTATUS __stdcall NewStream(_Out_ PMINIPORTWAVERTSTREAM* Stream, _In_ PPORTWAVERTSTREAM PortStream,
		_In_ ULONG Pin, _In_ BOOLEAN Capture, _In_ PKSDATAFORMAT DataFormat) override;
	__declspec(nothrow) NTSTATUS __stdcall GetDeviceDescription(_Out_ PDEVICE_DESCRIPTION DeviceDescription) override;
};

IO_COMPLETION_ROUTINE CloseCompletionRoutine;
IO_COMPLETION_ROUTINE BindCompletionRoutine;
IO_COMPLETION_ROUTINE SocketCompletionRoutine;

#endif