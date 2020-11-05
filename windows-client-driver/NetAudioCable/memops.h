#ifndef _NAC_MEMOPS_H_
#define _NAC_MEMOPS_H_

#include <wdm.h>

PVOID operator new(size_t iSize,
	_When_((poolType& NonPagedPoolMustSucceed) != 0,
		__drv_reportError("Must succeed pool allocations are forbidden. "
			"Allocation failures cause a system crash")) POOL_TYPE poolType, ULONG tag);


PVOID operator new(size_t iSize,
	_When_((poolType& NonPagedPoolMustSucceed) != 0,
		__drv_reportError("Must succeed pool allocations are forbidden. "
			"Allocation failures cause a system crash")) POOL_TYPE poolType);

void __cdecl operator delete(PVOID pVoid, ULONG tag);

void __cdecl operator delete(_Pre_maybenull_ __drv_freesMem(Mem) PVOID pVoid, _In_ size_t cbSize);

void __cdecl operator delete[](_Pre_maybenull_ __drv_freesMem(Mem) PVOID pVoid, _In_ size_t cbSize);

void __cdecl operator delete[](_Pre_maybenull_ __drv_freesMem(Mem) PVOID pVoid);

#endif
