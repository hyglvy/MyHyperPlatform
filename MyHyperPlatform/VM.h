#pragma once

#include <fltKernel.h>

EXTERN_C_START

_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS VmInitialization();

// �������⻯
_IRQL_requires_max_(PASSIVE_LEVEL) void VmTermination();

// ���⻯һ���ض��Ĵ�����
// @param ProcNum   A processor number to virtualize
// @return �ɹ�ִ�з��� STATUS_SUCCESS
// The processor 0 must have already been virtualized, or it fails.
_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS VmHotplugCallback(const PROCESSOR_NUMBER& ProcNum);

EXTERN_C_END