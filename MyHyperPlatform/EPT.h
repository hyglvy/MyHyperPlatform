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

// 检查EPT机制是否支持
// @return 支持，返回真
_IRQL_requires_max_(PASSIVE_LEVEL) bool EptIsEptAvailable();

// 读取存储所有的MTRR 用来纠正EPT
_IRQL_requires_max_(PASSIVE_LEVEL) void EptInitializeMtrrEntries();

// 构造申请 EPT 结构体 申请 Pre-Allocated 初始化 EPT 页表结构
// 
_IRQL_requires_max_(PASSIVE_LEVEL) EPT_DATA* EptInitialization();

// 得到 EPTP 指针
ULONG64 EptGetEptPointer(_In_ EPT_DATA* EptData);

// 销毁 EPT 所有相关结构体
void EptTermination(_In_ EPT_DATA* EptData);

/// Handles VM-exit triggered by EPT violation
/// @param ept_data   EptData to get an EPT pointer
_IRQL_requires_min_(DISPATCH_LEVEL) void EptHandleEptViolation(_In_ EPT_DATA* EptData);

// 得到一个 物理地址 的 EPT Entry
EPT_COMMON_ENTRY* EptGetEptPtEntry(_In_ EPT_DATA* EptData,_In_ ULONG64 PhysicalAddress);

EXTERN_C_END