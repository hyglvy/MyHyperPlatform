// https://msdn.microsoft.com/en-us/library/windows/hardware/hh920402(v=vs.85).aspx
// Ϊ�˱���һ���򵥶��������� ����������win8��֮ǰ����������Ӧ��ʹ�������ѡ��
// �������Ϊ�˵������������Զ�̬֧�ֶ���汾��windows�������� - �������� ExInitializeDriverRuntime
#ifndef POOL_NX_OPTIN
#define POOL_NX_OPTIN 1
#endif

#include "Common.h"
#include "Log.h"
#include "GlobalVariables.h"
#include "PowerCallback.h"
#include "Util.h"
#include "Performance.h"
#include "HotplugCallback.h"
#include "VM.h"

EXTERN_C_START

// ����Ԥ����
DRIVER_UNLOAD DriverUnload;
BOOLEAN IsSupportedOS();

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegisterPath)
{
	NTSTATUS NtStatus = STATUS_UNSUCCESSFUL;
	BOOLEAN NeedReinitialization = FALSE;
	
	UNREFERENCED_PARAMETER(RegisterPath);
	UNREFERENCED_PARAMETER(DriverObject);
	// LogFile ������ʼ��
	static const wchar_t LogFilePath[] = L"\\SystemRoot\\HyperPlatform.log";
	static const unsigned long LogLevel = (IsReleaseBuild()) ? LogPutLevelInfo  | LogOptDisableFunctionName :
															   LogPutLevelDebug | LogOptDisableFunctionName;

	// UnloadDriver
	DriverObject->DriverUnload = DriverUnload;

	// ���� NX �Ƿ�ҳ�ڴ��
	ExInitializeDriverRuntime(DrvRtPoolNxOptIn);	// ��������������κ��ڴ��������֮ǰ

	// ��ʼ�� Log ����
	NtStatus = LogInitialization(LogLevel, LogFilePath);
	if (NtStatus == STATUS_REINITIALIZATION_NEEDED)
		NeedReinitialization = TRUE;
	else if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	// ���ϵͳ�Ƿ�֧��
	if (IsSupportedOS())
	{
		LogTermination();
		return NtStatus;
	}

	//  ��ʼ��ȫ�ֱ���
	NtStatus = GlobalVariablesInitialization();
	if (!NT_SUCCESS(NtStatus))
	{
		LogTermination();
		return NtStatus;
	}

	// ��ʼ����Ϊ���� ?
	NtStatus = PerfInitialization();
	if (!NT_SUCCESS(NtStatus)) {
		GlobalVariablesTermination();
		LogTermination();
		return NtStatus;
	}

	// ��ʼ�����ߺ���
	NtStatus = UtilInitialization(DriverObject);
	if (!NT_SUCCESS(NtStatus)) {
		PerfTermination();
		GlobalVariablesTermination();
		LogTermination();
		return NtStatus;
	}

	// ��ʼ����Դ�ص�����
	NtStatus = PowerCallbackInitialization();
	if (!NT_SUCCESS(NtStatus)) {
		UtilTermination();
		PerfTermination();
		GlobalVariablesTermination();
		LogTermination();
		return NtStatus;
	}

	// ��ʼ���Ȳ�κ���
	NtStatus = HotplugCallbackInitialization();
	if (!NT_SUCCESS(NtStatus)) {
		PowerCallbackTermination();
		UtilTermination();
		PerfTermination();
		GlobalVariablesTermination();
		LogTermination();
		return NtStatus;
	}

	// ���⻯���д�����
	NtStatus = VmInitialization();
	if (!NT_SUCCESS(NtStatus)) {
		HotplugCallbackTermination();
		PowerCallbackTermination();
		UtilTermination();
		PerfTermination();
		GlobalVariablesTermination();
		LogTermination();
		return NtStatus;
	}

	// �����Ҫ��ע���س�ʼ������Ϊlog����
	//if (NeedReinitialization)
	//	LogRegisterReinitialization(DriverObject);

	MYHYPERPLATFORM_LOG_PRINT("The VM has been installed.");
	return NtStatus;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
}

// ���ϵͳ�Ƿ�֧��
BOOLEAN IsSupportedOS()
{
	PAGED_CODE();

	RTL_OSVERSIONINFOW OsVersionInfo = { 0 };
	NTSTATUS NtStatus = STATUS_UNSUCCESSFUL;

	NtStatus = RtlGetVersion(&OsVersionInfo);
	if (!NT_SUCCESS(NtStatus))
		return FALSE;
	// 6 - Windows Vista --- Windows 8.1
	// 10 - Windows 10
	if (OsVersionInfo.dwMajorVersion != 6 && OsVersionInfo.dwMajorVersion != 10)
		return FALSE;

	if (IsX64() && (ULONG_PTR)MmSystemRangeStart != 0x80000000)
		return FALSE;

	return TRUE;
}

EXTERN_C_END