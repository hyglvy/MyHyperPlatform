#include "EPT.h"
#include "Common.h"
#include "ASM.h"
#include "Log.h"
#include "Util.h"
#include "Performance.h"


EXTERN_C_START

// ������64λ��ַ�����ַ��ʹ�ýṹ 
// Ԥ����� EPT entry����ʵ�����ֳ���Ԥ��ֵ��VMM����bug
static const auto EptNumberOfPreallocatedEntries = 50;
// 
static const auto EptNumberOfMaxVariableRangeMtrrs = 255;

static const auto EptNumberOfFixedRangeMtrrs = 1 + 2 + 8;

static const auto EptMtrrEntriesSize = EptNumberOfFixedRangeMtrrs + EptNumberOfMaxVariableRangeMtrrs;

// pshpack1.h poppack.h �������ǿ��ƽṹ��������ȵ�ͷ�ļ� - #pragma pack(push, 1)
#include <pshpack1.h>
typedef struct _MTRR_DATA_
{
	bool    enabled;		//<! Whether this entry is valid
	bool    fixedMtrr;		//<! Whether this entry manages a fixed range MTRR
	UCHAR   type;			//<! Memory Type (such as WB, UC)
	bool    reserved1;		//<! Padding
	ULONG   reserved2;		//<! Padding
	ULONG64 range_base;		//<! A base address of a range managed by this entry
	ULONG   reange_end;		//<! An end address of a range managed by this entry
}MTRR_DATA, *PMTRR_DATA;
#include <poppack.h>
static_assert(sizeof(_MTRR_DATA_) == 24, "Size check");


#if defined(ALLOC_PRAGMA)
#pragma alloc_text(PAGE, EptIsEptAvailable)
#pragma alloc_text(PAGE, EptInitializeMtrrEntries)
#endif

//
static MTRR_DATA g_EptMtrrEntries[EptMtrrEntriesSize];
static UCHAR g_EptMtrrDefaultType;


// ���EPT�����Ƿ�֧�� - ���������⻯ 2.5.12
_Use_decl_annotations_ bool EptIsEptAvailable()
{
	PAGED_CODE();

	// ���ȶ����ܷ��ȡ IA32_VMX_EPT_VPID_CAP �Ĵ���Ҳ����Ҫ���
	// �������ı�־λ: 
	// bit 6: �Ƿ�֧��4��ҳ��
	// bit 14: �Ƿ�֧��WB����
	// 

	IA32_VMX_EPT_VPID_CAP Ia32VmxEptVpidCap = { UtilReadMsr64(MSR::kIa32VmxEptVpidCap) };
	if (!Ia32VmxEptVpidCap.fields.support_page_walk_length4			 || !Ia32VmxEptVpidCap.fields.support_write_back_memory_type ||
		!Ia32VmxEptVpidCap.fields.support_invept				     || !Ia32VmxEptVpidCap.fields.support_single_context_invept  ||
		!Ia32VmxEptVpidCap.fields.support_all_context_invept		 || !Ia32VmxEptVpidCap.fields.support_invvpid			     ||
		!Ia32VmxEptVpidCap.fields.support_individual_address_invvpid || !Ia32VmxEptVpidCap.fields.support_single_context_invept  ||
		!Ia32VmxEptVpidCap.fields.support_all_context_invvpid		 || !Ia32VmxEptVpidCap.fields.support_single_context_retaining_globals_invvpid)	
		return false;
	
	return true;
}

// ��ȡ���е�MTRR  -�������Ӧ�� MTRR_DATA
_Use_decl_annotations_ void EptInitializeMtrrEntries()
{
	PAGED_CODE();

	int Index = 0;
	MTRR_DATA* MtrrEntries = g_EptMtrrEntries;

	// ��ȡ�ʹ洢Ĭ�ϵ��ڴ�����
	IA32_MTRR_DEFAULT_TYPE_MSR Ia32MtrrDefaultTypeMsr = { UtilReadMsr64(MSR::kIa32MtrrDefType) };
	g_EptMtrrDefaultType = Ia32MtrrDefaultTypeMsr.fields.default_mtemory_type;

	// ��ȡ MTRR ����
	IA32_MTRR_CAPABILITIES_MSR Ia32MtrrCapabilitiesMsr = { UtilReadMsr64(MSR::kIa32MtrrCap) };
	MYHYPERPLATFORM_LOG_DEBUG(
		"MTRR Default=%lld, VariableCount=%lld, FixedSupported=%lld, FixedEnabled=%lld",
		Ia32MtrrDefaultTypeMsr.fields.default_mtemory_type,
		Ia32MtrrCapabilitiesMsr.fields.variable_range_count,
		Ia32MtrrCapabilitiesMsr.fields.fixed_range_supported,
		Ia32MtrrDefaultTypeMsr.fields.fixed_mtrrs_enabled);

	// ��ȡ FIXED MTRR - �����Ӧ�� MTRR_ENTRIES
	if (Ia32MtrrCapabilitiesMsr.fields.fixed_range_supported && Ia32MtrrDefaultTypeMsr.fields.fixed_mtrrs_enabled)
	{
		static const auto k64kBase = 0x0;
		static const auto k64kManagedSize = 0x10000;
		static const auto k16kBase = 0x80000;
		static const auto k16kManagedSize = 0x4000;
		static const auto k4kBase = 0xC0000;
		static const auto k4kManagedSize = 0x1000;

		// FIXED_64K
		ULONG64 offset = 0;
		IA32_MTRR_FIXED_RANGE_MSR FixedRange = { UtilReadMsr64(MSR::kIa32MtrrFix64k00000) };
		for (auto MemoryType : FixedRange.fields.types)
		{
			// ÿһ��Entry ��Ӧ 64K(0x10000) ����
			
			ULONG64 Base = k64kBase + offset;
			offset += k64kManagedSize;

			// ���� MTRR
			MtrrEntries[Index].enabled = true;
			MtrrEntries[Index].fixedMtrr = true;
			MtrrEntries[Index].type = MemoryType;
			MtrrEntries[Index].range_base = Base;
			MtrrEntries[Index].reange_end = Base + k64kManagedSize - 1;
			Index++;
		}
		NT_ASSERT(k64kBase + offset == k16kBase);

		// FIXED_16K
		offset = 0;
		for (auto FixedMsr = static_cast<ULONG>(MSR::kIa32MtrrFix16k80000); FixedMsr <= static_cast<ULONG>(MSR::kIa32MtrrFix16kA0000); FixedMsr++)
		{
			// ��ȡ��Ӧ��FIXED_MSR
			FixedRange.all = UtilReadMsr64(static_cast<MSR>(FixedMsr));

			for (auto MemoryType : FixedRange.fields.types)
			{
				//  16K ����
				ULONG64 Base = k16kBase + offset;
				offset += k16kManagedSize;

				// ���� MTRR_ENTRY
				MtrrEntries[Index].enabled = true;
				MtrrEntries[Index].fixedMtrr = true;
				MtrrEntries[Index].type = MemoryType;
				MtrrEntries[Index].range_base = Base;
				MtrrEntries[Index].reange_end = Base + k16kManagedSize - 1;
				Index++;
			}
		}
		NT_ASSERT(k16kBase + offset == k4kBase);

		// FIX_4K
		offset = 0;
		for (auto FixedMsr = static_cast<ULONG>(MSR::kIa32MtrrFix4kC0000); FixedMsr <= static_cast<ULONG>(MSR::kIa32MtrrFix4kF8000); FixedMsr++)
		{
			FixedRange.all = UtilReadMsr64(static_cast<MSR>(FixedMsr));
			for (auto MemoryType : FixedRange.fields.types)
			{
				ULONG Base = k4kBase + offset;
				offset += k4kManagedSize;

				MtrrEntries[Index].enabled = true;
				MtrrEntries[Index].fixedMtrr = true;
				MtrrEntries[Index].type = MemoryType;
				MtrrEntries[Index].range_base = Base;
				MtrrEntries[Index].reange_end = Base + k4kManagedSize - 1;
				Index++;
			}
		}
		NT_ASSERT(k4kBase + offset == 0x100000);
	}

	// ��ȡ���� Variable-Range ���� MTRR_ENTRY
	// Variable-Range�Ĵ�����һ�� ��һ��ָʾ PHYSICAL_BASE �ڶ���ָʾ PHYSICAL_MASK 
	for (auto i = 0; i < Ia32MtrrCapabilitiesMsr.fields.variable_range_count; i++)
	{
		// ��ȡ��Ӧ�� MTRR mask���Ҽ���Ƿ���ʹ����
		const auto PhysicalMask = static_cast<ULONG>(MSR::kIa32MtrrPhysMaskN) + i * 2; // Maskÿ���еĵڶ����Ĵ��� 
		IA32_MTRR_PHYSICAL_MASK_MSR Ia32MtrrPhysicalMaskMsr = { UtilReadMsr64(static_cast<MSR>(PhysicalMask)) };
		if (!Ia32MtrrPhysicalMaskMsr.fields.valid)
			continue;

		// �õ� MTRR ��Ϣ�ĳ���
		ULONG Length = 0;
		BitScanForward64(&Length, Ia32MtrrPhysicalMaskMsr.fields.phys_mask * PAGE_SIZE);

		const auto PhysicalBase = static_cast<ULONG>(MSR::kIa32MtrrPhysBaseN) + i * 2;
		IA32_MTRR_PHYSICAL_BASE_MSR Ia32MtrrPhysicalBaseMsr = { UtilReadMsr64(static_cast<MSR>(PhysicalBase)) };
		ULONG64 Base = Ia32MtrrPhysicalBaseMsr.fields.phys_base * PAGE_SIZE;
		ULONG64 End = Base + (1ull << Length) - 1;

		MtrrEntries[Index].enabled = true;
		MtrrEntries[Index].fixedMtrr = false;
		MtrrEntries[Index].type = Ia32MtrrPhysicalBaseMsr.fields.type;
		MtrrEntries[Index].range_base = Base;
		MtrrEntries[Index].reange_end = End;
		Index++;
	}
...00
}


EXTERN_C_END