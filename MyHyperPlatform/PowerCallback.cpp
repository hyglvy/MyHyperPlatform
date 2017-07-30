#include "PowerCallback.h"
#include "Log.h"
#include "Common.h"
#include "VM.h"


EXTERN_C_START

static CALLBACK_FUNCTION PowerCallbackRoutine;

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(INIT, PowerCallbackInitialization)
#pragma alloc_text(PAGE, PowerCallbackTermination)
#pragma alloc_text(PAGE, PowerCallbackRoutine)
#endif

static PCALLBACK_OBJECT g_PC_CallbackObject = nullptr;	// PowerState �ص�����
static PVOID g_PC_Registration = nullptr;			    // �ص��������


_Use_decl_annotations_ NTSTATUS PowerCallbackInitialization()
{
	PAGED_CODE();

	UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Callback\\PowerState");
	OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(&Name, OBJ_CASE_INSENSITIVE);

	// �������ߴ�һ���ص�����
	// �������������Ǵ򿪻��Ǵ��� - FALSE ��
	// \\Callback\\PowerState \\Callback\\SetSystemTime ������ϵͳ�����õ� ����ֱ��ʹ�õĻص�����
	// �ػ���ʱ�򣬽��и�����մ��� - ���� Unload��
	auto NtStatus = ExCreateCallback(&g_PC_CallbackObject, &ObjectAttributes, FALSE, TRUE);
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	// �� PowerState �ص�����ע��ص�����
	// https://msdn.microsoft.com/EN-US/library/ff545534(v=VS.85,d=hv.2).aspx - ���� PowerState �Ļص�����Ҫ��
	g_PC_Registration = ExRegisterCallback(g_PC_CallbackObject, PowerCallbackRoutine, nullptr);
	if (!g_PC_Registration)
	{
		ObDereferenceObject(g_PC_CallbackObject);
		g_PC_CallbackObject = nullptr;
		
		return STATUS_UNSUCCESSFUL;
	}

	return NtStatus;
}

_Use_decl_annotations_ void PowerCallbackTermination()
{
	PAGED_CODE();

	if (g_PC_Registration)
	{
		ExUnregisterCallback(g_PC_Registration);
		g_PC_Registration = nullptr;
	}

	if (g_PC_CallbackObject)
	{
		ObDereferenceObject(g_PC_CallbackObject);
		g_PC_Registration = nullptr;
	}
}

// @param CallbackContext ����֧�ֵ������� - ������ע���ʱ�� ���� ExRegisterCallback �ĵ�������
// @param Argument1 PO_CB_XXX const ָ��ֵ ���ݴ���Ľṹ�岻ͬ ��ʾ��ͬ��״̬
// @prarm Argument2 TRUE / FALSE 
// https://msdn.microsoft.com/EN-US/library/ff545534(v=VS.85,d=hv.2).aspx ������������ĺ���
// ��ǰ�ص���ҪΪ�˴��� ˯�ߺ�����
_Use_decl_annotations_ static void PowerCallbackRoutine(PVOID CallbackContext, PVOID Argument1, PVOID Argument2)
{
	UNREFERENCED_PARAMETER(CallbackContext);
	PAGED_CODE();

	MYHYPERPLATFORM_LOG_DEBUG("PowerCallback %p %p", Argument1, Argument2);

	if (Argument1 != reinterpret_cast<void*>(PO_CB_SYSTEM_STATE_LOCK))
		return;

	MYHYPERPLATFORM_COMMON_DBG_BREAK();

	if (Argument2)
	{
		MYHYPERPLATFORM_LOG_INFO("Resume the system.");
		NTSTATUS NtStatus = VmInitialization();
		if (!NT_SUCCESS(NtStatus))
			MYHYPERPLATFORM_LOG_ERROR("Failed to re-virtualize processors. Please unload the driver.");
	}
	else
	{
		MYHYPERPLATFORM_LOG_INFO("Suspend thr system.");
		VmTermination();
	}
}

EXTERN_C_END
