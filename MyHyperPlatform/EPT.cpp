#include "EPT.h"
#include "Common.h"
#include "ASM.h"
#include "Log.h"
#include "Util.h"
#include "Performance.h"


EXTERN_C_START

//  64 λ�� EPT Entries��������������ַ��
//										   total 48 bits
// EPT Page map level 4 selector           9 bits 39 - 47
// EPT Page directory pointer selector     9 bits 30 - 38
// EPT Page directory selector             9 bits 21 - 39
// EPT Page table selector                 9 bits 12 - 20
// EPT Byte within page                   12 bits 0  - 11
// �������������ƫ�� - ȥ�õ���Ӧ������
// Get the highest 25 bits
static const auto kEptPxiShift = 39ull;
// Use 9 bits; 0b0000_0000_0000_0000_0000_0000_0001_1111_1111 - ��Ϊÿ��Index��Ч���ȶ�ֻ��9λ
static const auto kEptPtxMask = 0x1ffull;
// Get the highest 34 bits
static const auto kEptPpiShift = 30ull;
// Get the highest 43 bits
static const auto kEptPdiShift = 21ull;
// Get the highest 52 bits
static const auto kEptPtiShift = 12ull;

// ������64λ��ַ�����ַ��ʹ�ýṹ 
// Ԥ����� EPT entry����ʵ�����ֳ���Ԥ��ֵ��VMM����bug
static const auto EptNumberOfPreallocatedEntries = 50;
// 
static const auto EptNumberOfMaxVariableRangeMtrrs = 255; // MTRR �涨��Ϊ 96 ���ڴ淶Χ - ��� 255 �Լ��涨�� 

static const auto EptNumberOfFixedRangeMtrrs = 1 + 2 + 8; // 64K - 1 16K - 2 8K - 8

static const auto EptMtrrEntriesSize = EptNumberOfFixedRangeMtrrs + EptNumberOfMaxVariableRangeMtrrs;

// pshpack1.h poppack.h �������ǿ��ƽṹ��������ȵ�ͷ�ļ� - #pragma pack(push, 1)
#include <pshpack1.h>
typedef struct _MTRR_DATA_
{
	bool      Enabled;		//<! Whether this entry is valid
	bool      FixedMtrr;		//<! Whether this entry manages a fixed range MTRR
	UCHAR     Type;			//<! Memory Type (such as WB, UC)
	bool      Reserved1;		//<! Padding
	ULONG     Reserved2;		//<! Padding
	ULONG64   RangeBase;		//<! A base address of a range managed by this entry
	ULONG64   RangeEnd;		//<! An end address of a range managed by this entry
}MTRR_DATA, *PMTRR_DATA;
#include <poppack.h>
static_assert(sizeof(_MTRR_DATA_) == 24, "Size check");

// EPT ��ؽṹ���� PROCESSOR_DATA
struct EPT_DATA
{
	EPT_POINTER* EptPointer;
	EPT_COMMON_ENTRY* EptPm14;
	
	EPT_COMMON_ENTRY** PreallocatedEntries;	
	volatile long PreallocatedEntriesCount;
};

static ULONG64 EptAddressToPxeIndex(_In_ ULONG64 PhysicalAddress);
static ULONG64 EptAddressToPpeIndex(_In_ ULONG64 PhysicalAddress);
static ULONG64 EptAddressToPdeIndex(_In_ ULONG64 PhysicalAddress);
static ULONG64 EptAddressToPteIndex(_In_ ULONG64 PhysicalAddress);



//
static MTRR_DATA g_EptMtrrEntries[EptMtrrEntriesSize];
static UCHAR g_EptMtrrDefaultType;

static MEMORY_TYPE EptGetMemoryType(_In_ ULONG64 PhysicalAddress);

_When_(EptData == nullptr, _IRQL_requires_max_(DISPATCH_LEVEL))
static EPT_COMMON_ENTRY* EptConstructTables(_In_ EPT_COMMON_ENTRY* Table, _In_ ULONG TableLevel, _In_ ULONG64 PhysicalAddress, _In_opt_ EPT_DATA* EptData);

static void EptDestructTables(_In_ EPT_COMMON_ENTRY* Table, _In_ ULONG TableLevel);

_Must_inspect_result_ __drv_allocatesMem(Mem)
_When_(EptData == nullptr, _IRQL_requires_max_(DISPATCH_LEVEL))
static EPT_COMMON_ENTRY* EptAllocateEptEntry(_In_opt_ EPT_DATA* EptData);

static EPT_COMMON_ENTRY* EptAllocateEptEntryFromPreAllocated(_In_ EPT_DATA* EptData);

_Must_inspect_result_ __drv_allocatesMem(Mem) _IRQL_requires_max_(DISPATCH_LEVEL)
static EPT_COMMON_ENTRY* EptAllocateEptEntryFromPool();

static void EptInitTableEntry(_In_ EPT_COMMON_ENTRY* Entry, _In_ ULONG TableLevel, _In_ ULONG64 PhysicalAddress);

static void EptFreeUnusedPreAllocatedEntries(_Pre_notnull_ __drv_allocatesMem(Mem) EPT_COMMON_ENTRY** PreallocatedEntries, _In_ long UsedCount);

static bool EptIsDeviceMemory(_In_ ULONG64 PhysicalAddress);
static EPT_COMMON_ENTRY* EptGetEptPtEntryByLevel(_In_ EPT_COMMON_ENTRY* Table, _In_ ULONG TableLevel, _In_ ULONG64 PhysicalAddress);

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(PAGE, EptIsEptAvailable)
#pragma alloc_text(PAGE, EptInitializeMtrrEntries)
#pragma alloc_text(PAGE, EptInitialization)
#endif


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
	if (!Ia32VmxEptVpidCap.fields.support_page_walk_length4			 || !Ia32VmxEptVpidCap.fields.support_write_back_memory_type  ||
		!Ia32VmxEptVpidCap.fields.support_invept				     || !Ia32VmxEptVpidCap.fields.support_single_context_invept   ||
		!Ia32VmxEptVpidCap.fields.support_all_context_invept		 || !Ia32VmxEptVpidCap.fields.support_invvpid			      ||
		!Ia32VmxEptVpidCap.fields.support_individual_address_invvpid || !Ia32VmxEptVpidCap.fields.support_single_context_invvpid  ||
		!Ia32VmxEptVpidCap.fields.support_all_context_invvpid		 || !Ia32VmxEptVpidCap.fields.support_single_context_retaining_globals_invvpid)	
		return false;
	
	return true;
}

// ���� EPT Pointer
_Use_decl_annotations_ ULONG64 EptGetEptPointer(EPT_DATA* EptData)
{
	return EptData->EptPointer->all;
}

// ��ȡ���е�MTRR - �������Ӧ�� MTRR_DATA
// ����Ķ� http://blog.csdn.net/lightseed/article/details/4603383 
// intel �ֲ� Volume 3 11.11
_Use_decl_annotations_ void EptInitializeMtrrEntries()
{
	PAGED_CODE();

	int Index = 0;
	MTRR_DATA* MtrrEntries = g_EptMtrrEntries;

	// ��ȡ�ʹ洢Ĭ�ϵ��ڴ�����
	IA32_MTRR_DEFAULT_TYPE_MSR Ia32MtrrDefaultTypeMsr = { UtilReadMsr64(MSR::kIa32MtrrDefType) };
	g_EptMtrrDefaultType = Ia32MtrrDefaultTypeMsr.fields.DefaultMemoryType;

	// ��ȡ MTRR Capability �Ĵ���
	IA32_MTRR_CAPABILITIES_MSR Ia32MtrrCapabilitiesMsr = { UtilReadMsr64(MSR::kIa32MtrrCap) };
	MYHYPERPLATFORM_LOG_DEBUG(
		"MTRR Default=%lld, VariableCount=%lld, FixedSupported=%lld, FixedEnabled=%lld",
		Ia32MtrrDefaultTypeMsr.fields.DefaultMemoryType,
		Ia32MtrrCapabilitiesMsr.fields.VariableRangeCount,
		Ia32MtrrCapabilitiesMsr.fields.FixedRangeSupported,
		Ia32MtrrDefaultTypeMsr.fields.FixedMtrrsEnabled);

	// ��ȡ FIXED MTRR - �����Ӧ�� MTRR_ENTRIES
	//								�Ƿ�֧�� Fixed Range��							        �Ƿ����� Fixed Range?
	if (Ia32MtrrCapabilitiesMsr.fields.FixedRangeSupported && Ia32MtrrDefaultTypeMsr.fields.FixedMtrrsEnabled)
	{
		// Fixed �������ַ��ÿ����Χ�ĳ��ȶ��ǹ̶��ġ�
		static const auto k64kBase = 0x0;
		static const auto k64kManagedSize = 0x10000;
		static const auto k16kBase = 0x80000;
		static const auto k16kManagedSize = 0x4000;
		static const auto k4kBase = 0xC0000;
		static const auto k4kManagedSize = 0x1000;

		// FIXED_64K ���볤�� 0x10000
		ULONG64 offset = 0;
		IA32_MTRR_FIXED_RANGE_MSR FixedRange = { UtilReadMsr64(MSR::kIa32MtrrFix64k00000) };
		for (auto MemoryType : FixedRange.fields.types)
		{
			ULONG64 Base = k64kBase + offset;
			offset += k64kManagedSize;

			// ���� MTRR
			MtrrEntries[Index].Enabled = true;
			MtrrEntries[Index].FixedMtrr = true;
			MtrrEntries[Index].Type = MemoryType;
			MtrrEntries[Index].RangeBase = Base;
			MtrrEntries[Index].RangeEnd = Base + k64kManagedSize - 1;
			Index++;
		}
		NT_ASSERT(k64kBase + offset == k16kBase);

		// FIXED_16K ���볤�� 0x4000
		offset = 0;
		for (auto FixedMsr = static_cast<ULONG>(MSR::kIa32MtrrFix16k80000); FixedMsr <= static_cast<ULONG>(MSR::kIa32MtrrFix16kA0000); FixedMsr++)
		{
			// ��ȡ��Ӧ�� FIXED_MSR
			FixedRange.all = UtilReadMsr64(static_cast<MSR>(FixedMsr));
			// ÿ�� 16k ����
			for (auto MemoryType : FixedRange.fields.types)
			{
				//  16K ����
				ULONG64 Base = k16kBase + offset;
				offset += k16kManagedSize;

				// ���� MTRR_ENTRY
				MtrrEntries[Index].Enabled = true;
				MtrrEntries[Index].FixedMtrr = true;
				MtrrEntries[Index].Type = MemoryType;
				MtrrEntries[Index].RangeBase = Base;
				MtrrEntries[Index].RangeEnd = Base + k16kManagedSize - 1;	// ע����� - 1
				Index++;
			}
		}
		NT_ASSERT(k16kBase + offset == k4kBase);

		// FIX_4K - ���볤�� 0x1000
		offset = 0;
		for (auto FixedMsr = static_cast<ULONG>(MSR::kIa32MtrrFix4kC0000); FixedMsr <= static_cast<ULONG>(MSR::kIa32MtrrFix4kF8000); FixedMsr++)
		{
			FixedRange.all = UtilReadMsr64(static_cast<MSR>(FixedMsr));
			
			for (auto MemoryType : FixedRange.fields.types)
			{
				ULONG64 Base = k4kBase + offset;
				offset += k4kManagedSize;

				MtrrEntries[Index].Enabled = true;
				MtrrEntries[Index].FixedMtrr = true;
				MtrrEntries[Index].Type = MemoryType;
				MtrrEntries[Index].RangeBase = Base;
				MtrrEntries[Index].RangeEnd = Base + k4kManagedSize - 1;
				Index++;
			}
		}
		NT_ASSERT(k4kBase + offset == 0x100000);
	}

	// ��ȡ���� Variable-Range ���� MTRR_ENTRY
	// Variable-Range�Ĵ�����һ��(������) ��һ��ָʾ PHYSICAL_BASE �ڶ���ָʾ PHYSICAL_MASK 
	for (auto i = 0; i < Ia32MtrrCapabilitiesMsr.fields.VariableRangeCount; i++)
	{
		// ��ȡ��Ӧ�� MTRR mask���Ҽ���Ƿ���ʹ����
		const auto PhysicalMask = static_cast<ULONG>(MSR::kIa32MtrrPhysMaskN) + i * 2; // Mask - ÿ���еĵڶ����Ĵ��� 
		IA32_MTRR_PHYSICAL_MASK_MSR Ia32MtrrPhysicalMaskMsr = { UtilReadMsr64(static_cast<MSR>(PhysicalMask)) };
		if (!Ia32MtrrPhysicalMaskMsr.fields.valid)
			continue;

		// �õ� Variable-Range �ĳ��� 
		ULONG Length = 0;
		BitScanForward64(&Length, Ia32MtrrPhysicalMaskMsr.fields.phys_mask * PAGE_SIZE); // * PAGE_SIZE �൱�� << 3(������λ)
		

		const auto PhysicalBase = static_cast<ULONG>(MSR::kIa32MtrrPhysBaseN) + i * 2;
		IA32_MTRR_PHYSICAL_BASE_MSR Ia32MtrrPhysicalBaseMsr = { UtilReadMsr64(static_cast<MSR>(PhysicalBase)) };
		ULONG64 Base = Ia32MtrrPhysicalBaseMsr.fields.phys_base * PAGE_SIZE;
		ULONG64 End = Base + (1ull << Length) - 1;		// AddressWithin & Mask = Base & Mask -> ������Ӹ������� Mask Ӧ���ǽ���λ���ܱ仯��λ
												        // �������0, ���ҷ�Χ�϶��� 4K ����ġ���ô����ֻҪ�õ��ж���λ�Ǳ仯�ģ��ټ��� base ���� end 
														// ��ʵ���� * 2 ... 

		MtrrEntries[Index].Enabled = true;
		MtrrEntries[Index].FixedMtrr = false;
		MtrrEntries[Index].Type = Ia32MtrrPhysicalBaseMsr.fields.type;
		MtrrEntries[Index].RangeBase = Base;
		MtrrEntries[Index].RangeEnd = End;
		Index++;
	}
	
}

_Use_decl_annotations_ EPT_DATA* EptInitialization()
{
	PAGED_CODE();
	static const auto EptPageWalkLevel = 4ul;

	// ���� EPT_DATA
	const auto EptData = reinterpret_cast<EPT_DATA*>(ExAllocatePoolWithTag(NonPagedPool, sizeof(EPT_DATA), HyperPlatformCommonPoolTag));
	if (!EptData)
		return nullptr;

	RtlZeroMemory(EptData, sizeof(EPT_DATA));

	// ���� EPT_POINTER
	const auto EptPointer = reinterpret_cast<EPT_POINTER*>(ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, HyperPlatformCommonPoolTag));
	if (!EptPointer)
	{
		ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);
		return nullptr;
	}
	RtlZeroMemory(EptPointer, PAGE_SIZE);

	// ���� EPT_PM14 ���ҳ�ʼ�� EPT_POINTER
	const auto EptPm14 = reinterpret_cast<EPT_COMMON_ENTRY*>(ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, HyperPlatformCommonPoolTag));
	if (!EptPm14)
	{
		ExFreePoolWithTag(EptPointer, HyperPlatformCommonPoolTag);
		ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);
		return nullptr;
	}
	RtlZeroMemory(EptPm14, PAGE_SIZE);
	EptPointer->fields.MemoryType = static_cast<ULONG64>(EptGetMemoryType(UtilPaFromVa(EptPm14)));
	EptPointer->fields.PageWalkLength = EptPageWalkLevel - 1;
	EptPointer->fields.Pm14Address = UtilPfnFromPa(UtilPaFromVa(EptPm14));

	// ��ʼ�����е�EPT Entry - ������ɵ�EPT�ṹ
	const auto PhysicalMemoryRanges = UtilGetPhysicalMemoryRanges();
	for (auto RunIndex = 0ul; RunIndex < PhysicalMemoryRanges->NumberOfRuns; RunIndex++)
	{
		const auto Run = &PhysicalMemoryRanges->Run[RunIndex];
		const auto BaseAddr = Run->BasePage * PAGE_SIZE;	// �õ����ҳ����ʼ��ַ

		// ����Run�е�����ҳ - ����4��ҳ��
		for (auto PageIndex = 0ull; PageIndex < Run->PageCount; PageIndex++)
		{
			const auto PageAddr = BaseAddr + PageIndex * PAGE_SIZE;
			const auto EptPointerEntry = EptConstructTables(EptPm14, 4, PageAddr, nullptr);		// �������һҳ��ص�����EPT���� �Ӹߵ���

			if (!EptPointerEntry)
			{
				EptDestructTables(EptPm14, 4);
				ExFreePoolWithTag(EptPointer, HyperPlatformCommonPoolTag);
				ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);
				return nullptr;
			}
		}
	}

	// Ϊ APIC_BASE ��ʼ�� EPT Entry����Ϊ����ԭ��Ҫ�����������룬�������ϵͳ������
	const IA32_APIC_BASE_MSR ApicMsr = { UtilReadMsr64(MSR::kIa32ApicBase) };
	if (!EptConstructTables(EptPm14, 4, ApicMsr.fields.ApicBase * PAGE_SIZE, nullptr))
	{
		EptDestructTables(EptPm14, 4);
		ExFreePoolWithTag(EptPointer, HyperPlatformCommonPoolTag);
		ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);

		return nullptr;
	}

	// ���� preallocated entries
	const auto PreallocatedEntriesSize = sizeof(EPT_COMMON_ENTRY*) * EptNumberOfPreallocatedEntries;
	const auto PreallocatedEntries = reinterpret_cast<EPT_COMMON_ENTRY**>(ExAllocatePoolWithTag(NonPagedPool, PreallocatedEntriesSize, HyperPlatformCommonPoolTag));
	if (!PreallocatedEntries)
	{
		EptDestructTables(EptPm14, 4);
		ExFreePoolWithTag(EptPointer, HyperPlatformCommonPoolTag);
		ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);

		return nullptr;
	}

	// ��� preallocated entries 
	for (auto i = 0ul; i < EptNumberOfPreallocatedEntries; i++)
	{
		const auto EptEntry = EptAllocateEptEntry(nullptr);
		if (!EptEntry)
		{
			EptFreeUnusedPreAllocatedEntries(PreallocatedEntries, 0);
			EptDestructTables(EptPm14, 4);
			ExFreePoolWithTag(EptPointer, HyperPlatformCommonPoolTag);
			ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);

			return nullptr;
		}
		PreallocatedEntries[i] = EptEntry;
	}

	// ��ʼ�����
	EptData->EptPointer = EptPointer;
	EptData->EptPm14 = EptPm14;
	EptData->PreallocatedEntries = PreallocatedEntries;
	EptData->PreallocatedEntriesCount = 0;

	return EptData;
}

_Use_decl_annotations_ static MEMORY_TYPE EptGetMemoryType(ULONG64 PhysicalAddress)
{
	// Ĭ�� MTRR ��û������
	UCHAR ResultType = MAXUCHAR;

	// Ѱ�������ض������ַ��MTRR
	for (const auto MtrrEntry : g_EptMtrrEntries)
	{
		// �ҵ������һ��
		if (!MtrrEntry.Enabled)
			break;

		// �ж���������ַ�Ƿ������ MTRR ������Χ��
		if (!UtilIsInBounds(PhysicalAddress, MtrrEntry.RangeBase, MtrrEntry.RangeEnd))	
			continue;	// ������������Χ Ѱ����һ��

		// ���һ�� FixedMtrr �������ڴ����� - ֱ�ӷ���
		if (MtrrEntry.FixedMtrr)
		{
			ResultType = MtrrEntry.Type;
			break;
		}

		// �����һ�� VariableMtrr ������Ҫ�����жϣ���Ϊ Fixed Ҳ�������������� Fixed�������ȼ�����
		if (MtrrEntry.Type == static_cast<UCHAR>(MEMORY_TYPE::kUncacheable))
		{
			// �����һ�� UC �ڴ����� - ���ټ���Ѱ�ң���Ϊ��ӵ����ߵ�Ȩ�ޡ�
			ResultType = MtrrEntry.Type;
			break;
		}

		if (ResultType == static_cast<UCHAR>(MEMORY_TYPE::kWriteThrough) || MtrrEntry.Type == static_cast<UCHAR>(MEMORY_TYPE::kWriteThrough))
		{
			if (ResultType == static_cast<UCHAR>(MEMORY_TYPE::kWriteBack))
			{
				// ���������MTRR������������ڴ����򡣲���һ���� WT ��һ���� WB - ʹ��WT
				// ���ǻ���Ҫ����Ѱ����һ�� MTRRs��ָ���ڴ�������UC
				ResultType = static_cast<UCHAR>(MEMORY_TYPE::kWriteThrough);
				continue;
			}
		}
		
		// ������涼������ - ˵����һ��δ�����MTRR�ڴ�����
		ResultType = MtrrEntry.Type;
	}

	// ���û���ҵ���Ӧ������ MTRRs ʹ��Ĭ��ֵ
	if (ResultType == MAXUCHAR)
	{
		ResultType = g_EptMtrrDefaultType;
	}

	return static_cast<MEMORY_TYPE>(ResultType);
}

// ����ͳ�ʼ��һ��ҳ������� EPT Entryies - �ݹ鹹��
_Use_decl_annotations_ static EPT_COMMON_ENTRY* EptConstructTables(EPT_COMMON_ENTRY* Table, ULONG TableLevel, ULONG64 PhysicalAddress, EPT_DATA* EptData)
{
	switch (TableLevel)
	{
		case 4:
		{
			// ���� PML4T
			const auto PxeIndex = EptAddressToPxeIndex(PhysicalAddress);
			const auto EptPm14Entry = &Table[PxeIndex];						// PML4 Entry
			if (!EptPm14Entry->all)	// ���û�������
			{
				// ������һ���� PDPT (Ҳ������һ���ı���
				const auto EptPdpt = EptAllocateEptEntry(EptData);
				if (!EptPdpt)
					return nullptr;

				// �� ��һ����ַ���и�ֵ
				EptInitTableEntry(EptPm14Entry, TableLevel, UtilPaFromVa(EptPdpt));
			}

			// �ݹ鹹����һ��
			return EptConstructTables(reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(EptPm14Entry->fields.PhysicalAddress)), TableLevel - 1, PhysicalAddress, EptData);
		}		
		case 3:
		{
			// ���� PDPT
			const auto PpeIndex = EptAddressToPpeIndex(PhysicalAddress);
			const auto EptPdptEntry = &Table[PpeIndex];
			if (!EptPdptEntry->all)
			{
				const auto EptPdt = EptAllocateEptEntry(EptData);
				if (!EptPdt)
					return nullptr;

				EptInitTableEntry(EptPdptEntry, TableLevel, UtilPaFromVa(EptPdt));
			}

			return EptConstructTables(reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(EptPdptEntry->fields.PhysicalAddress)), TableLevel - 1, PhysicalAddress, EptData);
		}			
		case 2:
		{
			// ���� PDT
			const auto PdeIndex = EptAddressToPdeIndex(PhysicalAddress);
			const auto EptPdtEntry = &Table[PdeIndex];

			if (!EptPdtEntry->all)
			{
				const auto EptPt = EptAllocateEptEntry(EptData);
				if (!EptPt)
					return nullptr;

				EptInitTableEntry(EptPdtEntry, TableLevel, UtilPaFromVa(EptPt));
			}

			return EptConstructTables(reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(EptPdtEntry->fields.PhysicalAddress)), TableLevel - 1, PhysicalAddress, EptData);
		}			
		case 1:
		{
			// ���� PT
			const auto PteIndex = EptAddressToPteIndex(PhysicalAddress);
			const auto EptPtEntry = &Table[PteIndex];
			NT_ASSERT(!EptPtEntry->all);		// PTE �����Ѿ��������ڴ�
			EptInitTableEntry(EptPtEntry, TableLevel, PhysicalAddress);

			return EptPtEntry;
		}			
		default:
		{
			MYHYPERPLATFORM_COMMON_DBG_BREAK();
			return nullptr;
		}		
	}
}

// �ͷ����е�EPT Entryͨ���������е�EPT
_Use_decl_annotations_ static void EptDestructTables(EPT_COMMON_ENTRY* Table, ULONG TableLevel)
{
	for (auto i = 0ul; i < 512; i++)
	{
		const auto Entry = Table[i];
		if (Entry.fields.PhysicalAddress)
		{
			// �õ���һ������׵�ַ
			const auto SubTable = reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(Entry.fields.PhysicalAddress));

			switch (TableLevel)
			{
				// 4 �� 3 ���µݹ�Ѱ��
				case 4:
				case 3:
					EptDestructTables(SubTable, TableLevel - 1);
					break;
				// 2 �ͷ� PTE
				case 2:
					ExFreePoolWithTag(SubTable, HyperPlatformCommonPoolTag);
					break;
				default:
					MYHYPERPLATFORM_COMMON_DBG_BREAK();
					break;
			}
		}
	}

	ExFreePoolWithTag(Table, HyperPlatformCommonPoolTag);
}

// ����һ���µ� EPT Entry ͨ����������ߴ�Ԥ������ȡ��һ��
_Use_decl_annotations_ static EPT_COMMON_ENTRY* EptAllocateEptEntry(EPT_DATA* EptData)
{
	if (EptData)
		return EptAllocateEptEntryFromPreAllocated(EptData);
	else
		return EptAllocateEptEntryFromPool();
}

// ȡһ���µ� EPT_COMMON_ENTRY ��Ԥ����
_Use_decl_annotations_ static EPT_COMMON_ENTRY * EptAllocateEptEntryFromPreAllocated(EPT_DATA * EptData)
{
	const auto Count = InterlockedIncrement(&EptData->PreallocatedEntriesCount);
	if (Count > EptNumberOfPreallocatedEntries)
	{
		MYHYPERPLATFORM_COMMON_BUG_CHECK(HYPERPLATFORM_BUG_CHECK::kExhaustedPreallocatedEntries, Count, reinterpret_cast<ULONG_PTR>(EptData), 0);
	}
	return EptData->PreallocatedEntries[Count - 1];
}

// ����һ���µ� EPT_COMMON_ENTRY
_Use_decl_annotations_ static EPT_COMMON_ENTRY* EptAllocateEptEntryFromPool()
{
	static const auto AllocSize = 512 * sizeof(EPT_COMMON_ENTRY);
	static_assert(AllocSize == PAGE_SIZE, "Size Check");

	const auto Entry = reinterpret_cast<EPT_COMMON_ENTRY*>(ExAllocatePoolWithTag(NonPagedPool, AllocSize, HyperPlatformCommonPoolTag));
	if (!Entry)
		return nullptr;

	RtlZeroMemory(Entry, AllocSize);
	return Entry;
}

// ��ʼ��һ�� EPT entry ʹ�� "pass through" ����
_Use_decl_annotations_ static void EptInitTableEntry(EPT_COMMON_ENTRY* Entry, ULONG TableLevel, ULONG64 PhysicalAddress)
{
	// PhysicalAddress  Ҳ������һ���Ļ���ַ
	Entry->fields.ReadAccess = true;
	Entry->fields.WriteAccess = true;
	Entry->fields.ExecuteAccess = true;
	Entry->fields.PhysicalAddress = UtilPfnFromPa(PhysicalAddress);
	
	// ���һ��ҳ��ָʾ�ڴ�����
	if (TableLevel == 1)
		Entry->fields.MemoryType = static_cast<ULONG64>(EptGetMemoryType(PhysicalAddress));
	
}

// �����ַת������
_Use_decl_annotations_ static ULONG64 EptAddressToPxeIndex(ULONG64 PhysicalAddress)
{
	const auto Index = (PhysicalAddress >> kEptPxiShift) & kEptPtxMask;
	return Index;
}

_Use_decl_annotations_ static ULONG64 EptAddressToPpeIndex(ULONG64 PhysicalAddress)
{
	const auto Index = (PhysicalAddress >> kEptPpiShift) & kEptPtxMask;
	return Index;
}

_Use_decl_annotations_ static ULONG64 EptAddressToPdeIndex(ULONG64 PhysicalAddress)
{
	const auto Index = (PhysicalAddress >> kEptPdiShift) & kEptPtxMask;
	return Index;
}

_Use_decl_annotations_ static ULONG64 EptAddressToPteIndex(ULONG64 PhysicalAddress)
{
	const auto Index = (PhysicalAddress >> kEptPtiShift) & kEptPtxMask;
	return Index;
}

// �ͷ�����û��ʹ�õ�Ԥ���� Entries �Ѿ�ʹ�õĿ� EptDestructTables()
_Use_decl_annotations_ static void EptFreeUnusedPreAllocatedEntries(EPT_COMMON_ENTRY** PreallocatedEntries, long UsedCount)
{
	for (auto i = UsedCount; i < EptNumberOfPreallocatedEntries; i++)
	{
		if (!PreallocatedEntries[i])
			break;

#pragma warning(push)
#pragma warning(disable : 6001)
		ExFreePoolWithTag(PreallocatedEntries[i], HyperPlatformCommonPoolTag);
#pragma warning(pop)
	}
	ExFreePoolWithTag(PreallocatedEntries, HyperPlatformCommonPoolTag);
}

// �ͷ�����������ڴ� 
_Use_decl_annotations_ void EptTermination(EPT_DATA* EptData)
{
	MYHYPERPLATFORM_LOG_DEBUG("Used pre-allocated = %2d / %2d", EptData->PreallocatedEntriesCount, EptNumberOfPreallocatedEntries);

	EptFreeUnusedPreAllocatedEntries(EptData->PreallocatedEntries, EptData->PreallocatedEntriesCount);

	EptDestructTables(EptData->EptPm14, 4);

	ExFreePoolWithTag(EptData->EptPointer, HyperPlatformCommonPoolTag);
	ExFreePoolWithTag(EptData, HyperPlatformCommonPoolTag);
}

// ���� EPT volation - VM-exit
_Use_decl_annotations_ void EptHandleEptViolation(EPT_DATA* EptData)
{
	const EPT_VIOLATION_QUALIFICATION ExitQualification = { UtilVmRead(VMCS_FIELD::kExitQualification) };

	const auto FaultPa = UtilVmRead64(VMCS_FIELD::kGuestPhysicalAddress);
	const auto FaultVa = reinterpret_cast<void*>(ExitQualification.fields.ValidGuestLinearAddress ? UtilVmRead(VMCS_FIELD::kGuestLinearAddress) : 0);

	if (ExitQualification.fields.EptReadable || ExitQualification.fields.EptWriteable || ExitQualification.fields.EptExecutable)
	{
		MYHYPERPLATFORM_COMMON_DBG_BREAK();
		MYHYPERPLATFORM_LOG_ERROR_SAFE("[UNK1] VA = %p, PA = %016llx", FaultVa, FaultPa);
		return;
	}

	const auto EptEntry = EptGetEptPtEntry(EptData, FaultPa);
	if (EptEntry && EptEntry->all)
	{
		MYHYPERPLATFORM_COMMON_DBG_BREAK();
		MYHYPERPLATFORM_LOG_ERROR_SAFE("[UNK2] VA = %p, PA = %016llx", FaultVa, FaultPa);
		return;
	}

	// û�еõ� EptEntry�� �϶��� Device Memory
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();
	if (!IsReleaseBuild())
		NT_VERIFY(EptIsDeviceMemory(FaultPa));

	EptConstructTables(EptData->EptPm14, 4, FaultPa, EptData);

	UtilInveptGlobal();
}

// ���ݸ����������ַ������ EPTEntry
_Use_decl_annotations_ EPT_COMMON_ENTRY* EptGetEptPtEntry(EPT_DATA* EptData, ULONG64 PhysicalAddress)
{
	return EptGetEptPtEntryByLevel(EptData->EptPm14, 4, PhysicalAddress);
}

_Use_decl_annotations_ static EPT_COMMON_ENTRY* EptGetEptPtEntryByLevel(EPT_COMMON_ENTRY* Table, ULONG TableLevel, ULONG64 PhysicalAddress)
{
	if (!Table)
		return nullptr;

	switch (TableLevel)
	{
		case 4:
		{
			// PML4
			const auto PxeIndex = EptAddressToPxeIndex(PhysicalAddress);
			const auto EptPml4Entry = &Table[PxeIndex];
			if (!EptPml4Entry->all)
				return nullptr;

			return EptGetEptPtEntryByLevel(reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(EptPml4Entry->fields.PhysicalAddress)), TableLevel - 1, PhysicalAddress);
		}
		case 3:
		{
			// PDPT
			const auto PpeIndex = EptAddressToPpeIndex(PhysicalAddress);
			const auto EptPdptEntry = &Table[PpeIndex];
			if (!EptPdptEntry->all) 
				return nullptr;
			
			return EptGetEptPtEntryByLevel(reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(EptPdptEntry->fields.PhysicalAddress)), TableLevel - 1, PhysicalAddress);
		}
		case 2:
		{
			// PDT
			const auto PdeIndex = EptAddressToPdeIndex(PhysicalAddress);
			const auto EptPdtEntry = &Table[PdeIndex];
			if (!EptPdtEntry->all)
				return nullptr;
			
			return EptGetEptPtEntryByLevel(reinterpret_cast<EPT_COMMON_ENTRY*>(UtilVaFromPfn(EptPdtEntry->fields.PhysicalAddress)), TableLevel - 1, PhysicalAddress);
		}
		case 1:
		{
			const auto PteIndex = EptAddressToPteIndex(PhysicalAddress);
			const auto EptPtEntry = &Table[PteIndex];
			return EptPtEntry;
		}
		default:
			MYHYPERPLATFORM_COMMON_DBG_BREAK();
			return nullptr;
	}
}

// �ж�һ�������ַ�Ƿ����豸�ڴ� (which could not have a corresponding PFN entry)
_Use_decl_annotations_ static bool EptIsDeviceMemory(ULONG64 PhysicalAddress) 
{
	const auto PhysicalAddrRanges = UtilGetPhysicalMemoryRanges();
	for (auto i = 0ul; i < PhysicalAddrRanges->NumberOfRuns; ++i) 
	{
		const auto CurrentRun = &PhysicalAddrRanges->Run[i];
		const auto BaseAddr = static_cast<ULONG64>(CurrentRun->BasePage) * PAGE_SIZE;
		const auto EndAddr = BaseAddr + CurrentRun->PageCount * PAGE_SIZE - 1;

		if (UtilIsInBounds(PhysicalAddress, BaseAddr, EndAddr))
			return false;	
	}
	return true;
}

EXTERN_C_END