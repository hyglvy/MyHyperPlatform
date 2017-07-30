#pragma once

#include "ia32_type.h"

EXTERN_C_START


// ��һ��������ַ��Χ������һ���ַ���(����
// @param SearchBase ������ʼ��ַ 
// @param SearchSize ������Χ����
// @param Pattern �����������ַ���
// @param PatternSize �����ַ�������
// @return ��һ���������ĵ�ַ�����û�ҵ�����nullptr
void* UtilMemSearch(_In_ const void* SearchBase, _In_ SIZE_T SearchSize, _In_ const void* Pattern, _In_ SIZE_T PatternSize);

// �õ�һ���ں˵�������(����)��ַ
// @param ProcName ����������
// @return ���ŵ�ַ���߿�
void* UtilGetSystemProcAddress(_In_ const wchar_t* ProcName);

// ��ÿ����������ִ�лص�����
// @param CallbackRoutine ��Ҫִ�еĻص�����
// @prarm Context ����ص������Ĳ���
// @return ��ʵ�ֵ�ע��
_IRQL_requires_max_(APC_LEVEL) NTSTATUS UtilForEachProcessor(_In_ NTSTATUS(*CallbackRoutine)(void*), _In_opt_ void* Context);

_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS UtilInitialization(_In_ PDRIVER_OBJECT DriverObject);

_IRQL_requires_max_(PASSIVE_LEVEL) void UtilTermination();

// VMX ָ��������� - �� VMX �Ļ��ָ���װ�ɺ���
enum class VMX_STATUS : unsigned __int8
{
	kOk = 0,
	kErrorWithStatus,
	kErrorWithoutStatus
};

// �ṩ VmxStatus �� |= c=������
constexpr VMX_STATUS operator |= (_In_ VMX_STATUS lhs, _In_ VMX_STATUS rhs)
{
	return static_cast<VMX_STATUS>(static_cast<unsigned __int8>(lhs) | static_cast<unsigned __int8>(rhs));
}

// Available command numbers for VMCALL
enum class HYPERCALL_NUMBER : unsigned __int32 
{
	kTerminateVmm,            //!< Terminates VMM
	kPingVmm,                 //!< Sends ping to the VMM
	kGetSharedProcessorData,  //!< Terminates VMM
};

// ���������ַ��Χ
// @return ��Զ����ʧ��
const PHYSICAL_MEMORY_DESCRIPTOR* UtilGetPhysicalMemoryRanges();

// PA VA PFN ����֮����໥ת��
// PA -> PFN
// @pa ��Ҫ�õ�ҳ�����������ַ
// @return ҳ����� 
PFN_NUMBER UtilPfnFromPa(_In_ ULONG64 pa);
// PA -> VA
PVOID UtilVaFromPa(_In_ ULONG64 pa);

// ���������ַת���ĺ��� �Է�ҳģʽ��Ҫ�� ������ʹ��PTE��ģʽ��ʹ��
// VA -> PA
ULONG64 UtilPaFromVa(_In_ void* va);
// VA -> PFN
PFN_NUMBER UtilPfnFromVa(_In_ void* va);

// PFN -> PA
ULONG64 UtilPaFromPfn(_In_ PFN_NUMBER pfn);
// PFN -> VA
void* UtilVaFromPfn(_In_ PFN_NUMBER pfn);

// �������������ڴ�
// @param NumberOfBytes �����С
// @return ����õ����ڴ�Ļ���ַ
// @tips ������ڴ����ͨ�� UtilFreeContiguousMemory
_Must_inspect_result_ _IRQL_requires_max_(DISPATCH_LEVEL)
void* UtilAllocateContiguousMemory(_In_ SIZE_T NumberOfBytes);

// MSR ��������
ULONG_PTR UtilReadMsr(_In_ MSR msr);
ULONG64 UtilReadMsr64(_In_ MSR msr);

void UtilWriteMsr(_In_ MSR msr, _In_ ULONG_PTR Value);
void UtilWriteMsr64(_In_ MSR msr, _In_ ULONG64 Value);

// ��ȡ����Ӧ���ȵ� VMCS-field 
// @param Field  VMCS-field to read
// @return read value
ULONG_PTR UtilVmRead(_In_ VMCS_FIELD Field);

// ��ȡ����64λ�� VMCS
// @param Field ��ȡ��
ULONG64 UtilVmRead64(_In_ VMCS_FIELD Field);

// д�� VMCS ����
// @param Field ����д��� VMCS-Filed 
// @param FiledValue д���ֵ
// @return д����
VMX_STATUS UtilVmWrite(_In_ VMCS_FIELD Field, _In_ ULONG_PTR FieldValue);

// д�� 64 bits VMCS ����
VMX_STATUS UtilVmWrite64(_In_ VMCS_FIELD Field, _In_ ULONG64 FieldValue);

// ִ�� VMCALL - asm���װ
NTSTATUS UtilVmCall(_In_ HYPERCALL_NUMBER HypercallNumber, _In_opt_ void* Context);


// ����Ĵ�����ֵ
// @param AllRegiters Ҫ����ļĴ���
// @param StackPointer �ڵ��ú���֮ǰ��ջ��ַ
void UtilDumpGpRegisters(_In_ const ALL_REGISTERS* AllRegisters, _In_ ULONG_PTR StackPointer);


// ִ�� INVEPT ָ���ˢ EPT Entry ����
// @return INVEPT ָ��ؽ��
VMX_STATUS UtilInveptGlobal();

// Executes the INVVPID instruction (type 0)
// @return A result of the INVVPID instruction
VMX_STATUS UtilInvvpidIndividualAddress(_In_ USHORT Vpid, _In_ void* Address);

// Executes the INVVPID instruction (type 2)
// @return A result of the INVVPID instruction
VMX_STATUS UtilInvvpidAllContext();

/// Executes the INVVPID instruction (type 3)
/// @return A result of the INVVPID instruction
VMX_STATUS UtilInvvpidSingleContextExceptGlobal(_In_ USHORT Vpid);

// ��鵱ǰϵͳ�Ƿ��� 32λ�µ� PAE ��ҳģʽ
bool UtilIsX86PAE();

void UtilLoadPdptes(_In_ ULONG_PTR Cr3Value);

EXTERN_C_END

template<typename T>
constexpr bool UtilIsInBounds(_In_ const T& Value, _In_ const T& Min, _In_ const T& Max)
{
	return (Min <= Value) && (Value <= Max);
}


