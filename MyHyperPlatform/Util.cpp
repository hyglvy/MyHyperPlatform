#include "Util.h"
#include <intrin.h>
#include "Common.h"
#include "Log.h"
#include "ASM.h"

EXTERN_C_START

// �������ʹ�� RtlPcToFileHeader 
// ���������Win10 64λ�ϻ��������bug �����������������־λ�Ĵ���
static const auto UtilUseRtlPcToFileHeader = false;

// ����Ԥ���� - �������Ҫ�Լ��õ���ַ
NTKERNELAPI PVOID NTAPI RtlPcToFileHeader(_In_ PVOID PcValue, _Out_ PVOID* BaseOfImage);
using RtlPcToFileHeaderType = decltype(RtlPcToFileHeader);

_Must_inspect_result_ _IRQL_requires_max_(DISPATCH_LEVEL) NTKERNELAPI _When_(return != NULL, _Post_writable_byte_size_(NumberOfBytes))
PVOID MmAllocateContiguousNodeMemory(_In_ SIZE_T NumberOfBytes, _In_ PHYSICAL_ADDRESS LowestAcceptableAddress, _In_ PHYSICAL_ADDRESS HighestAcceptableAddress, 
								     _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple, _In_ ULONG Protect, _In_ NODE_REQUIREMENT PreferredNode);
using MmAllocateContiguousNodeMemoryType = decltype(MmAllocateContiguousNodeMemory);

// dt nt!_LDR_DATA_ENTRY
typedef struct _LDR_DATA_TABLE_ENTRY_
{
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;

	void* DllBase;
	void* EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	// ... ����ʵ�� ???
}LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

//////////////////////////////////////////////////////////////////////////
// ����Ԥ����
_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS UtilInitializePageTableVariables();

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS UtilInitializeRtlPcToFileHeader(_In_ PDRIVER_OBJECT DriverObject);

_Success_(return != nullptr) static PVOID NTAPI UtilUnsafePcToFileHeader(_In_ PVOID PcValue, _Out_ PVOID* BaseOfImage);

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS UtilInitializePhysicalMemoryRanges();

_IRQL_requires_max_(PASSIVE_LEVEL) static PPHYSICAL_MEMORY_DESCRIPTOR UtilBuildPhysicalMemoryRanges();


static PHYSICAL_MEMORY_DESCRIPTOR* g_UtilPhysicalMemoryRanges = nullptr;
static RtlPcToFileHeaderType* g_UtilRtlPcToFileHeader = nullptr;
static LIST_ENTRY* g_UtilPsLoadedModuleList = nullptr;
static MmAllocateContiguousNodeMemoryType* g_UtilMmAllocateContiguousNodeMemory = nullptr;

// EPTҳ����ĸ���Ļ���ַ
static ULONG_PTR g_UtilPXEBase = 0;
static ULONG_PTR g_UtilPPEBase = 0;
static ULONG_PTR g_UtilPDEBase = 0;
static ULONG_PTR g_UtilPTEBase = 0;

static ULONG_PTR g_UtilPXIShift = 0;
static ULONG_PTR g_UtilPPIShift = 0;
static ULONG_PTR g_UtilPDIShift = 0;
static ULONG_PTR g_UtilPTIShift = 0;

static ULONG_PTR g_UtilPXIMask = 0;
static ULONG_PTR g_UtilPPIMask = 0;
static ULONG_PTR g_UtilPDIMask = 0;
static ULONG_PTR g_UtilPTIMask = 0;

_Use_decl_annotations_ NTSTATUS UtilInitializePageTableVariables()
{
	PAGED_CODE();

#include "UtilPageConstants.h"

	// �õ�ϵͳ�汾 - �ж��Ƿ���Ҫҳ���ַ
	RTL_OSVERSIONINFOW OSVersionInfo = { sizeof(OSVersionInfo) };
	NTSTATUS NtStatus = RtlGetVersion(&OSVersionInfo);

	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	// �� Win10 14316 ֮�� ���������ҳ���ַ
	// ����32λϵͳ��win7֮ǰ����win10 14316 ֮ǰ��ֱ�Ӳ��ö�ֵ
	if (!IsX64() || OSVersionInfo.dwMajorVersion < 10 || OSVersionInfo.dwBuildNumber < 14316)
	{
		if (IsX64())
		{
			// EPTҳ����� �ĸ�����ַ��ֵ
			g_UtilPXEBase = UtilPXEBase;
			g_UtilPPEBase = UtilPPEBase;
			// ����ת����ַ
			g_UtilPXIShift = UtilPXIShift;
			g_UtilPPIShift = UtilPPIShift;
			// ������־λ�ĳ�ʼ��
			g_UtilPXIMask = UtilPXIMask;
			g_UtilPPIMask = UtilPPIMask;
		}
		if (UtilIsX86PAE())
		{
			// EPTҳ����� �ĸ�����ַ��ֵ
			g_UtilPXEBase = UtilPXEBase;
			g_UtilPPEBase = UtilPPEBase;
			// ����ת����ַ
			g_UtilPXIShift = UtilPXIShift;
			g_UtilPPIShift = UtilPPIShift;
			// ������־λ�ĳ�ʼ��
			g_UtilPXIMask = UtilPXIMask;
			g_UtilPPIMask = UtilPPIMask;
		}
		else
		{
			// EPTҳ����� �ĸ�����ַ��ֵ
			g_UtilPXEBase = UtilPXEBase;
			g_UtilPPEBase = UtilPPEBase;
			// ����ת����ַ
			g_UtilPXIShift = UtilPXIShift;
			g_UtilPPIShift = UtilPPIShift;
			// ������־λ�ĳ�ʼ��
			g_UtilPXIMask = UtilPXIMask;
			g_UtilPPIMask = UtilPPIMask;
		}

		return NtStatus;
	}
	
	// ���� Win10 14316 �Ժ�Ҫ�Լ����� ҳ���ַ
	// ͨ����MmGetVirtualForPhysical���±��������Ϳ��Եõ�PTE����ַ - �����������ת��
	const auto pfnMmGetVirtualForPhysical = UtilGetSystemProcAddress(L"MmGetVirtualForPhysical");
	if (!pfnMmGetVirtualForPhysical)
		return STATUS_PROCEDURE_NOT_FOUND;

	static const UCHAR PatternWin10x64[] = {
		0x48, 0x8b, 0x04, 0xd0,  // mov     rax, [rax+rdx*8]
		0x48, 0xc1, 0xe0, 0x19,  // shl     rax, 19h
		0x48, 0xba,              // mov     rdx, ????????`????????  ; PTE_BASE
	};

	auto Found = reinterpret_cast<ULONG_PTR>(UtilMemSearch(pfnMmGetVirtualForPhysical, 0x30, PatternWin10x64, sizeof(PatternWin10x64)));
	if (!Found)
		return STATUS_PROCEDURE_NOT_FOUND;

	Found += sizeof(PatternWin10x64);
	MYHYPERPLATFORM_LOG_DEBUG("Found a hard coded PTE_BASE at %016Ix", Found);

	// ����ȡֵ���õ�EPTҳ��ṹ��ַ
	const auto PTEBase = *reinterpret_cast<ULONG_PTR*>(Found);		// ȡ��PTEBase
	const auto Index   = (PTEBase >> UtilPXIShift) & UtilPXIMask;	
	const auto PDEBase = PTEBase | (Index << UtilPPIShift);
	const auto PPEBase = PDEBase | (Index << UtilPDIShift);
	const auto PXEBase = PPEBase | (Index << UtilPTIShift);

	g_UtilPXEBase = static_cast<ULONG_PTR>(PXEBase);
	g_UtilPPEBase = static_cast<ULONG_PTR>(PPEBase);
	g_UtilPDEBase = static_cast<ULONG_PTR>(PDEBase);
	g_UtilPTEBase = static_cast<ULONG_PTR>(PTEBase);

	g_UtilPXIShift = UtilPXIShift;
	g_UtilPPIShift = UtilPPIShift;
	g_UtilPDIShift = UtilPDIShift;
	g_UtilPTIShift = UtilPTIShift;

	g_UtilPXIMask = UtilPXIMask;
	g_UtilPPIMask = UtilPPIMask;
	g_UtilPDIMask = UtilPDIMask;
	g_UtilPTIMask = UtilPTIMask;

	return NtStatus;
}

// ���߳�ʼ������
NTSTATUS UtilInitialization(PDRIVER_OBJECT DriverObject)
{
	PAGED_CODE();
	NTSTATUS NtStatus = UtilInitializePageTableVariables();
	MYHYPERPLATFORM_LOG_DEBUG("PXE at %016Ix, PPE at %016Ix, PDE at %016Ix, PTE at %016Ix", g_UtilPXEBase, g_UtilPPEBase, g_UtilPDEBase, g_UtilPTEBase);
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	NtStatus = UtilInitializeRtlPcToFileHeader(DriverObject);
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	NtStatus = UtilInitializePhysicalMemoryRanges();
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;
	
	g_UtilMmAllocateContiguousNodeMemory = reinterpret_cast<MmAllocateContiguousNodeMemoryType*>(UtilGetSystemProcAddress(L"MmAllocateContiguousNodeMemory"));

	return NtStatus;
}

// �������ú��� 
_Use_decl_annotations_ void UtilTermination()
{
	PAGED_CODE();

	if (g_UtilPhysicalMemoryRanges)
		ExFreePoolWithTag(g_UtilPhysicalMemoryRanges, HyperPlatformCommonPoolTag);
	
	return;
}

_Use_decl_annotations_ void* UtilMemSearch(const void* SearchBase, SIZE_T SearchSize, const void* Pattern, SIZE_T PatternSize)
{
	if (PatternSize > SearchSize)
		return nullptr;

	auto BaseAddr = static_cast<const char*>(SearchBase);
	// �������� SearchSize - PatternSize ���һ�������������һ��Pattern��������
	for (SIZE_T i = 0; i <= SearchSize - PatternSize; i++)
	{
		// RtlCompareMemory �������ص�����ȵĳ��� (�ӵ�һ���ֽڿ�ʼ
		if (RtlCompareMemory(Pattern, &BaseAddr[i], PatternSize) == PatternSize)
			return const_cast<char*>(&BaseAddr[i]);
	}

	return nullptr;
}

// �Լ�ʵ�ֵ��ں˰� GetProcAddress
_Use_decl_annotations_ void* UtilGetSystemProcAddress(const wchar_t* ProcName)
{
	PAGED_CODE();

	UNICODE_STRING UniProcName = {};
	RtlInitUnicodeString(&UniProcName, ProcName);	

	return MmGetSystemRoutineAddress(&UniProcName);
}

// �жϵ�ǰ���͵ķ�ҳģʽ
bool UtilIsX86PAE()
{
	return (!IsX64() && CR4{ __readcr4() }.fields.pae);
}

// ���ֵõ� RtlPcToFileHeader �ķ���
_Use_decl_annotations_ static NTSTATUS UtilInitializeRtlPcToFileHeader(PDRIVER_OBJECT DriverObject)
{
	PAGED_CODE();
	// �������ʹ�� - ֱ�ӵõ���ַ
	if (UtilUseRtlPcToFileHeader)
	{
		const auto RtlPcToFileHeaderFunc = UtilGetSystemProcAddress(L"RtlPcToFileHeader");
		if (RtlPcToFileHeaderFunc)
		{
			g_UtilRtlPcToFileHeader = reinterpret_cast<RtlPcToFileHeaderType*>(RtlPcToFileHeaderFunc);
			return STATUS_SUCCESS;
		}
	}

	// ����ʹ��ϵͳԭ���� �Լ�ʵ��һ���ٵ� - �����а�ȫ����
#pragma warning(push)
#pragma warning(disable : 28175)
	auto Module = reinterpret_cast<PLDR_DATA_TABLE_ENTRY>(DriverObject->DriverSection);
#pragma warning(pop)

	g_UtilPsLoadedModuleList = Module->InLoadOrderLinks.Flink;
	g_UtilRtlPcToFileHeader = UtilUnsafePcToFileHeader;

	return STATUS_SUCCESS;
}

// �Լ���д��һ�� PcToFileHeader - ����û������ PsLoadedModuleSpinLock
// Ҳ�͵�����������ǲ���ȫ�ģ��п����ڵ���ʱ������ģ����ء������²���ȫ�����������
_Use_decl_annotations_ static PVOID NTAPI UtilUnsafePcToFileHeader(PVOID PcValue, PVOID* BaseOfImage)
{
	// ��������ַ �������ں˷�Χ��  ֱ�Ӵ���
	if (PcValue < MmSystemRangeStart)
		return nullptr;

	//������ǰ���ص�����ģ�� - �ж�Ŀ���ַ�Ƿ���ģ���ڲ�
	const auto Head = g_UtilPsLoadedModuleList;
	for (auto Current = Head->Flink; Current != Head; Current = Current->Flink)
	{
		const auto Module = CONTAINING_RECORD(Current, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
		const auto DriverEnd = reinterpret_cast<void*>(reinterpret_cast<ULONG_PTR>(Module->DllBase) + Module->SizeOfImage);

		if (UtilIsInBounds(PcValue, Module->DllBase, DriverEnd))
		{
			*BaseOfImage = Module->DllBase;
			return Module->DllBase;
		}
	}

	return nullptr;
}

// ��ʼ������ҳ�淶Χ
_Use_decl_annotations_ static NTSTATUS UtilInitializePhysicalMemoryRanges()
{
	PAGED_CODE();
	// �õ�ҳ�淶Χ
	const auto Ranges = UtilBuildPhysicalMemoryRanges();
	if (!Ranges)
		return STATUS_UNSUCCESSFUL;

	g_UtilPhysicalMemoryRanges = Ranges;
	// ����ҳ�� �����Ϣ
	for (auto i = 0ul; i < Ranges->NumberOfRuns; ++i)
	{
		// ����ҳ�淶Χ
		const auto BaseAddr = static_cast<ULONG64>(Ranges->Run[i].BasePage) * PAGE_SIZE;
		MYHYPERPLATFORM_LOG_DEBUG("Physical Memory Ranges: %01611x - %01611x", BaseAddr, BaseAddr + Ranges->Run[i].PageCount * PAGE_SIZE);
		// ����ҳ�泤��
		const auto PhysicalMemorySize = static_cast<ULONG64>(Ranges->NumberOfPages) * PAGE_SIZE;
		MYHYPERPLATFORM_LOG_DEBUG("Physical Memory Total: %llu KB", PhysicalMemorySize / 1024);
	}

	return STATUS_SUCCESS;
}

// ����һ�� PHYSICAL_MEMORY_DESCRIPTOR �ṹ��
_Use_decl_annotations_ static PHYSICAL_MEMORY_DESCRIPTOR* UtilBuildPhysicalMemoryRanges()
{
	PAGED_CODE();

	const auto PhysicalMemoryRanges = MmGetPhysicalMemoryRanges();
	if (!PhysicalMemoryRanges)
		return nullptr;

	PFN_COUNT NumberOfRuns = 0;
	PFN_NUMBER NumberOfPages = 0;

	for (; ; ++NumberOfRuns)
	{
		const auto Range = &PhysicalMemoryRanges[NumberOfRuns];
		if (!Range->BaseAddress.QuadPart && !Range->NumberOfBytes.QuadPart)
			break;

		NumberOfPages += static_cast<PFN_NUMBER>(BYTES_TO_PAGES(Range->NumberOfBytes.QuadPart));
	}

	if (NumberOfRuns == 0)
	{
		ExFreePoolWithTag(PhysicalMemoryRanges, 'hPmM'); // �����־λ��ȷ���� ?
		return nullptr;
	}
	
	const auto MemoryBlockSize = sizeof(PHYSICAL_MEMORY_DESCRIPTOR) + sizeof(PHYSICAL_MEMORY_RUN) * (NumberOfRuns - 1);
	const auto PhyiscalMemoryBlock = reinterpret_cast<PPHYSICAL_MEMORY_DESCRIPTOR>(ExAllocatePoolWithTag(NonPagedPool, MemoryBlockSize, HyperPlatformCommonPoolTag));
	if (!PhyiscalMemoryBlock)
	{
		ExFreePoolWithTag(PhysicalMemoryRanges, 'hPmM');
		return nullptr;
	}
	RtlZeroMemory(PhyiscalMemoryBlock, MemoryBlockSize);

	PhyiscalMemoryBlock->NumberOfPages = NumberOfPages;
	PhyiscalMemoryBlock->NumberOfRuns = NumberOfRuns;

	for (auto RunIndex = 0ul; RunIndex < NumberOfRuns; RunIndex++)
	{
		auto CurrentRun = &PhyiscalMemoryBlock->Run[RunIndex];
		auto CurrentBlock = &PhysicalMemoryRanges[RunIndex];

		CurrentRun->BasePage = static_cast<ULONG_PTR>(UtilPfnFromPa(CurrentBlock->BaseAddress.QuadPart));
		CurrentRun->PageCount = static_cast<ULONG_PTR>(BYTES_TO_PAGES(CurrentBlock->NumberOfBytes.QuadPart));
	}

	ExFreePoolWithTag(PhysicalMemoryRanges, 'hPmM');
	return PhyiscalMemoryBlock;
}

// �����ַ �����ַ �����ַҳ�� ���໥ת��
// PA -> PFN
_Use_decl_annotations_ PFN_NUMBER UtilPfnFromPa(ULONG64 pa)
{
	return static_cast<PFN_NUMBER>(pa >> PAGE_SHIFT);
}
// PA -> VA
_Use_decl_annotations_ PVOID UtilVaFromPa(ULONG64 pa)
{
	PHYSICAL_ADDRESS PhysicalAddress = { 0 };
	PhysicalAddress.QuadPart = pa;
	return MmGetVirtualForPhysical(PhysicalAddress);
}
// VA -> PA
_Use_decl_annotations_ ULONG64 UtilPaFromVa(void* va)
{
	const auto  pa = MmGetPhysicalAddress(va);
	return pa.QuadPart;
}
// VA -> PFN
_Use_decl_annotations_ PFN_NUMBER UtilPfnFromVa(void* va)
{
	return UtilPfnFromPa(UtilPaFromVa(va));
}
// PFN -> PA
_Use_decl_annotations_ ULONG64 UtilPaFromPfn(PFN_NUMBER pfn)
{
	return pfn << PAGE_SHIFT;
}
// PFN -> VA
_Use_decl_annotations_ void* UtilVaFromPfn(PFN_NUMBER pfn)
{
	return UtilVaFromPa(UtilPaFromPfn(pfn));
}

// MSR ��ȡ����
_Use_decl_annotations_ ULONG_PTR UtilReadMsr(MSR msr)
{
	return static_cast<ULONG_PTR>(__readmsr(static_cast<unsigned long>(msr)));
}

_Use_decl_annotations_ ULONG64 UtilReadMsr64(MSR msr)
{
	return __readmsr(static_cast<unsigned long>(msr));
}

_Use_decl_annotations_ void UtilWriteMsr(MSR msr, ULONG_PTR Value)
{
	__writemsr(static_cast<unsigned long>(msr), Value);

	return;
}

_Use_decl_annotations_ void UtilWriteMsr64(MSR msr, ULONG64 Value)
{
	__writemsr(static_cast<unsigned long>(msr), Value);

	return;
}

// �����д�����ִ��һ��ָ���Ļص������� PSSIVE_LEVEL
// �����ɹ����� STATUS_SUCCESS��ֻ�е����лص����� STATUS_SUCCESS ��������ɹ�������κ�һ���ص�ʧ�ܣ��������ٵ��������ص��������������ֵ��
_Use_decl_annotations_ NTSTATUS UtilForEachProcessor(NTSTATUS(*CallbackRoutine)(void*), void* Context)
{
	PAGED_CODE();
	// ���ػ�Ծ�Ĵ������������ض�������
	const auto NumberOfProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	// ����ÿ�������� ִ�лص�
	for (ULONG ProcessorIndex = 0; ProcessorIndex < NumberOfProcessors; ProcessorIndex++)
	{
		PROCESSOR_NUMBER ProcessorNumber = { 0 };
		NTSTATUS NtStatus = KeGetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);
		if (!NT_SUCCESS(NtStatus))
			return NtStatus;

		// ת����ǰ������
		GROUP_AFFINITY GroupAffinity = { 0 };
		GroupAffinity.Group = ProcessorNumber.Group;
		GroupAffinity.Mask = 1ull << ProcessorNumber.Number;

		GROUP_AFFINITY PreviousAffinity = { 0 };
		KeSetSystemGroupAffinityThread(&GroupAffinity, &PreviousAffinity);	// ת��

		NtStatus = CallbackRoutine(Context);

		KeRevertToUserGroupAffinityThread(&PreviousAffinity);
		if (!NT_SUCCESS(NtStatus))
			return NtStatus;
	}

	return STATUS_SUCCESS;
}


// ������мĴ���
_Use_decl_annotations_ void UtilDumpGpRegisters(const ALL_REGISTERS* AllRegisters, ULONG_PTR StackPointer)
{
	const auto CurrentIrql = KeGetCurrentIrql();
	if (CurrentIrql < DISPATCH_LEVEL)
		KeRaiseIrqlToDpcLevel();

#if defined(_AMD64_)
	MYHYPERPLATFORM_LOG_DEBUG_SAFE(
		"Context at %p: "
		"rax= %016Ix rbx= %016Ix rcx= %016Ix "
		"rdx= %016Ix rsi= %016Ix rdi= %016Ix "
		"rsp= %016Ix rbp= %016Ix "
		" r8= %016Ix  r9= %016Ix r10= %016Ix "
		"r11= %016Ix r12= %016Ix r13= %016Ix "
		"r14= %016Ix r15= %016Ix efl= %08Ix",
		_ReturnAddress(), AllRegisters->gp.ax, AllRegisters->gp.bx, AllRegisters->gp.cx,
		AllRegisters->gp.dx, AllRegisters->gp.si, AllRegisters->gp.di, StackPointer,
		AllRegisters->gp.bp, AllRegisters->gp.r8, AllRegisters->gp.r9, AllRegisters->gp.r10,
		AllRegisters->gp.r11, AllRegisters->gp.r12, AllRegisters->gp.r13, AllRegisters->gp.r14,
		AllRegisters->gp.r15, AllRegisters->flags.all);
#else
	MYHYPERPLATFORM_LOG_DEBUG_SAFE(
		"Context at %p: "
		"eax= %08Ix ebx= %08Ix ecx= %08Ix "
		"edx= %08Ix esi= %08Ix edi= %08Ix "
		"esp= %08Ix ebp= %08Ix efl= %08x",
		_ReturnAddress(), AllRegisters->gp.ax, AllRegisters->gp.bx, AllRegisters->gp.cx,
		AllRegisters->gp.dx, AllRegisters->gp.si, AllRegisters->gp.di, StackPointer,
		AllRegisters->gp.bp, AllRegisters->flags.all);
#endif

	if (CurrentIrql < DISPATCH_LEVEL)
		KeLowerIrql(CurrentIrql);
}

// ���������ڴ淶Χ
const PHYSICAL_MEMORY_DESCRIPTOR* UtilGetPhysicalMemoryRanges()
{
	return g_UtilPhysicalMemoryRanges;
}

// ���� ���������ڴ�
_Use_decl_annotations_ void* UtilAllocateContiguousMemory(SIZE_T NumberOfBytes)
{
	PHYSICAL_ADDRESS HighestAcceptableAddress = { 0 };
	HighestAcceptableAddress.QuadPart = -1;

	if (g_UtilMmAllocateContiguousNodeMemory)
	{
		// ���� NX �����ڴ�
		PHYSICAL_ADDRESS LowestAcceptableAddress = { 0 };
		PHYSICAL_ADDRESS BoundaryAddressMultiple = { 0 };
		
		return g_UtilMmAllocateContiguousNodeMemory(NumberOfBytes, LowestAcceptableAddress, HighestAcceptableAddress, BoundaryAddressMultiple, PAGE_READWRITE, MM_ANY_NODE_OK);
	}
	else
	{
#pragma warning(push)
#pragma warning(disable : 30029)
		return MmAllocateContiguousMemory(NumberOfBytes, HighestAcceptableAddress);
#pragma warning(pop)
	}
}

VMX_STATUS UtilInveptGlobal()
{
	// 2.6.7.1 
	// ����ӳ�����
	INV_EPT_DESCRIPTOR InvEptDescriptor = { 0 };
	return static_cast<VMX_STATUS>(AsmInvept(INV_EPT_TYPE::kGlobalInvalidation, &InvEptDescriptor));
}

// invvpid ָ��һ��������ִ��ģʽ
_Use_decl_annotations_ VMX_STATUS UtilInvvpidIndividualAddress(USHORT Vpid, void* Address)
{
	INV_VPID_DESCRIPTOR InvVpidDescriptor = { 0 };
	InvVpidDescriptor.Vpid = Vpid;
	InvVpidDescriptor.LinearAddress = reinterpret_cast<ULONG64>(Address);

	return static_cast<VMX_STATUS>(AsmInvvpid(INV_VPID_TYPE::kIndividualAddressInvalidation, &InvVpidDescriptor));
}
// ������ ȫ��ˢ��
VMX_STATUS UtilInvvpidAllContext()
{
	INV_VPID_DESCRIPTOR InvVpidDescriptor = { 0 };	
	return static_cast<VMX_STATUS>(AsmInvvpid(INV_VPID_TYPE::kAllContextInvalidation, &InvVpidDescriptor));
}

_Use_decl_annotations_ VMX_STATUS UtilVmWrite(VMCS_FIELD Field, ULONG_PTR FieldValue)
{
	return static_cast<VMX_STATUS>(__vmx_vmwrite(static_cast<size_t>(Field), FieldValue));
}

_Use_decl_annotations_ VMX_STATUS UtilVmWrite64(VMCS_FIELD Field, ULONG64 FieldValue)
{
#if defined(_AMD64_)
	return UtilVmWrite(Field, FieldValue);
#else
	// ��32λ����������64λ��ʱ������ - ��������� ÿ���������������
	NT_ASSERT(UtilIsInBounds(Field, VMCS_FIELD::kIoBitmapA, VMCS_FIELD::kHostIa32PerfGlobalCtrlHigh));
	NT_ASSERT((static_cast<ULONG>(Field) % 2) == 0);	// Ҫд��ı�ſ϶���һ��ż�� - Ҳ���Ǵ�����������ĵ�һ����ʼд

	ULARGE_INTEGER Value64 = { 0 };
	Value64.QuadPart = FieldValue;

	const auto VmxStatus = UtilVmWrite(Field, Value64.LowPart);
	if (VmxStatus != VMX_STATUS::kOk)
		return VmxStatus;

	return UtilVmWrite(static_cast<VMCS_FIELD>(static_cast<ULONG>(Field) + 1), Value64.HighPart);
#endif
}

// ��CE3 ���� PDPTE
_Use_decl_annotations_ void UtilLoadPdptes(ULONG_PTR Cr3Value)
{
	const auto CurCr3 = __readcr3();

	__writecr3(Cr3Value);

	PDPTR_REGISTER Pdptrs[4] = { 0 };
	for (auto i = 0; i < 4; i++)
	{
		const auto PdptrAddr = g_UtilPDEBase + i * PAGE_SIZE;	// PDE ֮����ʵ�ǽ������е�
		Pdptrs[i].fields.Present = true;
		Pdptrs[i].fields.PageDirectoryPhysicalAddr = UtilPaFromVa(reinterpret_cast<void*>(PdptrAddr));
	}

	__writecr3(CurCr3);
	UtilVmWrite64(VMCS_FIELD::kGuestPdptr0, Pdptrs[0].all);
	UtilVmWrite64(VMCS_FIELD::kGuestPdptr0, Pdptrs[1].all);
	UtilVmWrite64(VMCS_FIELD::kGuestPdptr0, Pdptrs[2].all);
	UtilVmWrite64(VMCS_FIELD::kGuestPdptr0, Pdptrs[3].all);
}

// ��ȡ VMCS ��ֵ
_Use_decl_annotations_ ULONG_PTR UtilVmRead(VMCS_FIELD Field)
{
	size_t FieldValue = 0;
	const auto VmxStatus = static_cast<VMX_STATUS>(__vmx_vmread(static_cast<size_t>(Field), &FieldValue));
	
	if (VmxStatus != VMX_STATUS::kOk)
	{
		MYHYPERPLATFORM_COMMON_BUG_CHECK(HYPERPLATFORM_BUG_CHECK::kCriticalVmxInstructionFailure, static_cast<ULONG_PTR>(VmxStatus), static_cast<ULONG_PTR>(Field), 0);
	}

	return FieldValue;
}

_Use_decl_annotations_ ULONG64 UtilVmRead64(VMCS_FIELD Field)
{
#if defined(_AMD64_)
	return UtilVmRead(Field);
#else
	// ֻ�� x86 �¶�ȡ 64 bit �����Ҫ���д���
	// Ҫ������ȡ���� 32 bit �Լ�����
	NT_ASSERT(UtilIsInBounds(Field, VMCS_FIELD::kIoBitmapA, VMCS_FIELD::kHostIa32PerfGlobalCtrlHigh));	// �ж�������Ƿ���һ��˫ 32 bit ����
	NT_ASSERT((static_cast<ULONG>(Field) % 2) == 0);													// Ҫ��ȡ�϶���һ��ż�� - Ҳ���Ǵ�����������ĵ�һ����ʼ��

	ULARGE_INTEGER Value64 = { 0 };
	Value64.LowPart = UtilVmRead(Field);
	Value64.HighPart = UtilVmRead(static_cast<VMCS_FIELD>(static_cast<ULONG>(Field) + 1));
	
	return Value64.QuadPart;
#endif
}

_Use_decl_annotations_ NTSTATUS UtilVmCall(HYPERCALL_NUMBER HypercallNumber, void* Context)
{
	__try
	{
		const auto VmxStatus = static_cast<VMX_STATUS>(AsmVmxCall(static_cast<ULONG>(HypercallNumber), Context));

		return (VmxStatus == VMX_STATUS::kOk) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		const auto NtStatus = GetExceptionCode();
		MYHYPERPLATFORM_COMMON_DBG_BREAK();
		MYHYPERPLATFORM_LOG_WARN_SAFE("Exception thrown (code %08x)", NtStatus);
		return NtStatus;
	}
}

EXTERN_C_END