#pragma once

#include <fltKernel.h>

EXTERN_C_START

// �������еĹ�������ע�����е�������
// @return �����ɹ�����STATUS_SUCCESS 
_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS GlobalVariablesInitialization();

// �������е�������
_IRQL_requires_max_(PASSIVE_LEVEL) void GlobalVariablesTermination();


EXTERN_C_END