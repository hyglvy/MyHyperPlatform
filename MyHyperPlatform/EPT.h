#pragma once

#include <fltKernel.h>

EXTERN_C_START


// ���EPT�����Ƿ�֧��
// @return ֧�֣�������
_IRQL_requires_max_(PASSIVE_LEVEL) bool EptIsEptAvailable();

// ��ȡ�洢���е�MTRR ��������EPT
_IRQL_requires_max_(PASSIVE_LEVEL) void EptInitializeMtrrEntries();

EXTERN_C_END