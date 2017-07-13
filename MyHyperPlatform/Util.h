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

// MSR ��������
ULONG_PTR UtilReadMsr(_In_ MSR msr);
ULONG64 UtilReadMsr64(_In_ MSR msr);

void UtilWriteMsr(_In_ MSR msr, _In_ ULONG_PTR Value);
void UtilWriteMsr64(_In_ MSR msr, _In_ ULONG64 Value);

EXTERN_C_END

template<typename T>
constexpr bool UtilIsInBounds(_In_ const T& Value, _In_ const T& Min, _In_ const T& Max)
{
	return (Min <= Value) && (Value <= Max);
}


