#pragma once

#include <fltKernel.h>

EXTERN_C_START

struct EPT_DATA;

// 
union EPT_COMMON_ENTRY
{
	ULONG64 all;
	struct {
		ULONG64 ReadAccess : 1;       //!< [0]
		ULONG64 WriteAccess : 1;      //!< [1]
		ULONG64 ExecuteAccess : 1;    //!< [2]
		ULONG64 MemoryType : 3;       //!< [3:5]
		ULONG64 Reserved1 : 6;         //!< [6:11]
		ULONG64 PhysicalAddress : 36;  //!< [12:48-1]
		ULONG64 Reserved2 : 16;        //!< [48:63]
	} fields;
};
static_assert(sizeof(EPT_COMMON_ENTRY) == 8, "Size check");

// ���EPT�����Ƿ�֧��
// @return ֧�֣�������
_IRQL_requires_max_(PASSIVE_LEVEL) bool EptIsEptAvailable();

// ��ȡ�洢���е�MTRR ��������EPT
_IRQL_requires_max_(PASSIVE_LEVEL) void EptInitializeMtrrEntries();

// �������� EPT �ṹ�� ���� Pre-Allocated ��ʼ�� EPT ҳ��ṹ
// 
_IRQL_requires_max_(PASSIVE_LEVEL) EPT_DATA* EptInitialization();

EXTERN_C_END