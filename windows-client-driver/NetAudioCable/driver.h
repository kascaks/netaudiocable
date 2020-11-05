#ifndef _NAC_DRIVER_H_
#define _NAC_DRIVER_H_

#include <wdm.h>

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" DRIVER_UNLOAD DriverUnload;
extern "C" DRIVER_ADD_DEVICE AddDevice;
extern "C" NTSTATUS StartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList);

#endif
