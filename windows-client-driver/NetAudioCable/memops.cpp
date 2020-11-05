#include "memops.h"

PVOID operator new(size_t iSize,
	_When_((poolType& NonPagedPoolMustSucceed) != 0,
		__drv_reportError("Must succeed pool allocations are forbidden. "
			"Allocation failures cause a system crash"))
	POOL_TYPE       poolType,
	ULONG           tag) {
	PVOID result = ExAllocatePoolWithTag(poolType, iSize, tag);

	if (result) {
		RtlZeroMemory(result, iSize);
	}
	return result;
}


PVOID operator new(size_t iSize,
	_When_((poolType& NonPagedPoolMustSucceed) != 0,
		__drv_reportError("Must succeed pool allocations are forbidden. "
			"Allocation failures cause a system crash"))
	POOL_TYPE poolType) {
	PVOID result = ExAllocatePoolWithTag(poolType, iSize, 'DVSM');

	if (result) {
		RtlZeroMemory(result, iSize);
	}
	return result;
}

void __cdecl operator delete(PVOID pVoid, ULONG tag) {
	if (pVoid) {
		ExFreePoolWithTag(pVoid, tag);
	}
}

void __cdecl operator delete(_Pre_maybenull_ __drv_freesMem(Mem) PVOID pVoid, _In_ size_t cbSize)
{
	UNREFERENCED_PARAMETER(cbSize);

	if (pVoid) {
		ExFreePoolWithTag(pVoid, 'DVSM');
	}
}

void __cdecl operator delete[](_Pre_maybenull_ __drv_freesMem(Mem) PVOID pVoid, _In_ size_t cbSize) {
	UNREFERENCED_PARAMETER(cbSize);

	if (pVoid) {
		ExFreePoolWithTag(pVoid, 'DVSM');
	}
}

void __cdecl operator delete[](_Pre_maybenull_ __drv_freesMem(Mem) PVOID pVoid) {
	if (pVoid) {
		ExFreePoolWithTag(pVoid, 'DVSM');
	}
}