#include "GlobalVariables.h"

// .CRT ��Ҫ����� ctors and dtors. ��ǰ����Ƕ��һ�� .CRT �ڵ� .rdata �ڡ�
// ���߻ᴥ��һ�����Ӿ���
#pragma comment(linker, "/merge:.CRT=.rdata")

// ���������� - ������ctors���� �ڱ���ʱ - ע�ⰴ����ĸ˳������
#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)

EXTERN_C_START

static const ULONG GlobalVariablePoolTag = 'asXX';

using DESTRUCTOR = void(__cdecl *)();	// ������ - ����ָ��

typedef struct _DESTRUCTOR_ENTRY_ 
{
	DESTRUCTOR        Destructor;	    // ������ - ����ָ��
	SINGLE_LIST_ENTRY ListEntry;		// ϵͳ�ṩ��Nextָ�롣����ʹ���������Լ��Ľṹ�����档ͬʱ����ϵͳ��API��������ά������
}DESTRUCTOR_ENTRY;


#ifdef ALLOC_PRAGMA	// ALLOC_PRAGM �����жϵ�ǰ�������Ƿ�֧��alloc_text
//alloc_text ���Խ�ָ���ĺ�������ָ���Ķ���
#pragma alloc_text(INIT, GlobalVariablesInitialization) // ���ڳ�ʼ����ɾͲ�����Ҫ�ĺ��������Բ���INIT��
#pragma alloc_text(INIT, atexit)
#pragma alloc_text(PAGE, GlobalVariablesTermination)	// PAGE - ��ҳ��
#endif

// ����ʼ�ͽ���ָ�����CRT��������
// https://docs.microsoft.com/zh-cn/cpp/c-runtime-library/crt-initialization
// �����������Ǳ�־��CRT����Ŀ�ʼ�ͽ��� - ��ζ���û�����ȫ�ֱ����ĳ�ʼ��һ������������֮ǰ
__declspec(allocate(".CRT$XCA")) static DESTRUCTOR g_GopCtorsBegin[1] = {};
__declspec(allocate(".CRT$XCZ")) static DESTRUCTOR g_GopCtorsEnd[1] = {};

// �洢 ������ ��ָ�룬���˳���ʱ��Ҫ��
static SINGLE_LIST_ENTRY gGopDtorsListHead = {};

// ��������κ�ȫ�ֱ�����Ҫ��ʼ�������뵽 g_GopCtorsBegin ��������ʹ�á��ڵ�ǰ�����δʹ�õ���
_Use_decl_annotations_ NTSTATUS GlobalVariablesInitialization()
{
	PAGED_CODE();

	for (auto ctor = g_GopCtorsBegin + 1; ctor < g_GopCtorsEnd; ctor++)
	{
		(*ctor)();
	}

	return STATUS_SUCCESS;
}
						
_Use_decl_annotations_ void GlobalVariablesTermination()
{
	PAGED_CODE();

	auto Entry = PopEntryList(&gGopDtorsListHead);
	while (Entry)
	{
		const auto Element = CONTAINING_RECORD(Entry, DESTRUCTOR_ENTRY, ListEntry);
		Element->Destructor();
		ExFreePoolWithTag(Element, GlobalVariablePoolTag);
		Entry = PopEntryList(&gGopDtorsListHead);
	}
}

// ע�������� - �������Ӧ����һ������������
_IRQL_requires_max_(PASSIVE_LEVEL) int __cdecl atexit(_In_ DESTRUCTOR Destructor)
{
	PAGED_CODE();
	const auto Element = reinterpret_cast<DESTRUCTOR_ENTRY*>(ExAllocatePoolWithTag(PagedPool, sizeof(DESTRUCTOR), GlobalVariablePoolTag));

	if (!Element)
		return 1;

	Element->Destructor = Destructor;
	// ����һ���ڵ���Listǰ��
	PushEntryList(&gGopDtorsListHead, &Element->ListEntry);
	return 0;
}

EXTERN_C_END