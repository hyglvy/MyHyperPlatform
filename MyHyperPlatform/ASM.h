#pragma once

#include "ia32_type.h"

EXTERN_C_START

// ��VM��ʼ����һ���װ
// @param VmInitializationRoutine ����VMX-mode����ں���
// @param Context Context����
// @return true ������ִ�гɹ�
bool _stdcall AsmInitializeVm(_In_ void(*VmInitializationRoutine)(_In_ ULONG_PTR, _In_ ULONG_PTR, _In_opt_ void*), _In_opt_ void* Context);


EXTERN_C_END