#include "Log.h"
#define NTSTRSAFE_NO_CB_FUNCTIONS
#include <ntstatus.h>
#include <Ntstrsafe.h>

#pragma prefast(disable : 30030)

EXTERN_C_START

//
// LogBuffer �����ҳ���� - �� LOG_BUFFER_INFO ���������黺��
// ����������Ⱥ󣬲��ټ�¼Log��Ϣ��
static const auto LogBufferSizeInPages = 16ul;
static const auto LogBufferSize = PAGE_SIZE * LogBufferSizeInPages;	// Buffer ��ʵ����
static const auto LogBufferUsableSize = LogBufferSize - 1;			// Buffer ������󳤶� - ��ȥ��β���� \0
// ˢ���߳� ���ʱ��
static const auto LogFlushIntervalMsec = 50;
static const ULONG LogPoolTag = 'log ';

// 
typedef struct _LOG_BUFFER_INFO_
{
	volatile char* LogBufferHead;
	volatile char* LogBufferTail;

	char* LogBufferOne;
	char* LogBufferTwo;

	SIZE_T LogMaxUsage;
	HANDLE LogFileHandle;
	KSPIN_LOCK SpinLock;
	ERESOURCE Resource;
	bool ResourceInitialized;
	volatile bool BufferFlushThreadShouldBeAlive;		// ����ˢ���߳����б���
	volatile bool BufferFlushThreadStarted;

	HANDLE	BufferFlushThreadHandle;
	wchar_t LogFilePath[100];
}LOG_BUFFER_INFO, *PLOG_BUFFER_INFO;

//
static auto g_LogDebugFlag = LogPutLevelDisable;
static LOG_BUFFER_INFO g_LogBufferInfo = { 0 };

// ���غ���Ԥ����
NTKERNELAPI UCHAR* NTAPI PsGetProcessImageFileName(_In_ PEPROCESS Process);


_IRQL_requires_max_(PASSIVE_LEVEL) static void LogFinalizeBufferInfo(_In_ LOG_BUFFER_INFO* LogBufferInfo);

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS LogInitializeBufferInfo(_In_ const wchar_t* LogFilePath, _Inout_ LOG_BUFFER_INFO* LogBufferInfo);

static bool LogIsLogNeeded(_In_ ULONG Level);

static bool LogIsDbgPrintNeeded();

static void LogDbgBreak();

static NTSTATUS LogMakePrefix(_In_ ULONG Level, _In_z_ const char* FunctionName, _In_z_ const char* LogMessage, _Out_ char* LogBuffer, _In_ SIZE_T LogBufferLength);

static const char* LogFindBaseFunctionName(_In_z_ const char* FunctionName);

static NTSTATUS LogPut(_In_z_ char* Message, _In_ ULONG Attribute);

static void LogDoDbgPrint(_In_z_ char * Message);

static bool LogIsLogFileEnabled(_In_ const LOG_BUFFER_INFO& LogBufferInfo);

static bool LogIsLogFileActivated(_In_ const LOG_BUFFER_INFO& LogBufferInfo);

static bool LogIsPrinted(char *Message);

static void LogSetPrintedBit(_In_z_ char *Message, _In_ bool on);

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS LogFlushLogBuffer(_Inout_ LOG_BUFFER_INFO* LogBufferInfo);

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS LogWriteMessageToFile(_In_z_ const char* Message, _In_ const LOG_BUFFER_INFO& LogBufferInfo);

static NTSTATUS LogBufferMessage(_In_z_ const char* Message,_Inout_ LOG_BUFFER_INFO* LogBufferInfo);

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS LogInitializeLogFile(_Inout_ LOG_BUFFER_INFO* LogBufferInfo); 

static KSTART_ROUTINE LogBufferFlushThreadRoutine;
static DRIVER_REINITIALIZE LogReinitializationRoutine;

_IRQL_requires_max_(PASSIVE_LEVEL) static NTSTATUS LogSleep(_In_ LONG Millsecond);

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(INIT, LogInitialization)
#pragma alloc_text(INIT, LogInitializeBufferInfo)
//#pragma alloc_text(INIT, LogRegisterReinitialization)

#pragma alloc_text(PAGE, LogInitializeLogFile)
#pragma alloc_text(PAGE, LogTermination)
#pragma alloc_text(PAGE, LogFinalizeBufferInfo)
#pragma alloc_text(PAGE, LogSleep)
#pragma alloc_text(PAGE, LogBufferFlushThreadRoutine)
#endif

////////////////////////////////////////////////////////////////
// ����ʵ��

_Use_decl_annotations_ NTSTATUS LogInitialization(ULONG Flag, const wchar_t* LogFilePath)
{
	PAGED_CODE();

	NTSTATUS NtStatus = STATUS_SUCCESS;
	g_LogDebugFlag = Flag;

	// ����LogFile - ���ָ��LogFilePath
	bool NeedReinitialization = false;
	if (LogFilePath)
	{
		NtStatus = LogInitializeBufferInfo(LogFilePath, &g_LogBufferInfo);
		if (NtStatus == STATUS_REINITIALIZATION_NEEDED)
			NeedReinitialization = true;	// ��Ҫ�ȴ��ڶ�ʱ���ʼ��
		else if (!NT_SUCCESS(NtStatus))
			return NtStatus;				// ʧ��
	}

	// ����Log
	NtStatus = MYHYPERPLATFORM_LOG_INFO("Log has been %sinitialized.", (NeedReinitialization ? "partially " : ""));
	if (!NT_SUCCESS(NtStatus))
		goto FAIL;

	// ���Log��Ϣ
	MYHYPERPLATFORM_LOG_DEBUG("Info=%p, Buffer=%p %p, File=%S", &g_LogBufferInfo, g_LogBufferInfo.LogBufferOne, g_LogBufferInfo.LogBufferTwo, LogFilePath);
	return (NeedReinitialization ? STATUS_REINITIALIZATION_NEEDED : STATUS_SUCCESS);	// ��ȷ�˳�

FAIL:
	if (LogFilePath)
		LogFinalizeBufferInfo(&g_LogBufferInfo);

	return NtStatus;
} 

// ��ʼ������ĵڶ�����Ա LOG_BUFFER_INFO ����
_Use_decl_annotations_ static NTSTATUS LogInitializeBufferInfo(const wchar_t* LogFilePath, LOG_BUFFER_INFO* LogBufferInfo)
{
	PAGED_CODE();
	NT_ASSERT(LogFilePath);
	NT_ASSERT(LogBufferInfo);

	// ��ʼ��������
	KeInitializeSpinLock(&LogBufferInfo->SpinLock);

	// �����ַ���													  �õ� LogFilePath ����
	NTSTATUS NtStatus = RtlStringCchCopyW(LogBufferInfo->LogFilePath, RTL_NUMBER_OF_FIELD(LOG_BUFFER_INFO, LogFilePath), LogFilePath);
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	// ��ʼ��һ����Դ
	NtStatus = ExInitializeResourceLite(&LogBufferInfo->Resource);
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	LogBufferInfo->ResourceInitialized = true;

	// �������黺�� - ���ʧ�ܾ����� LogBufferInfo
	LogBufferInfo->LogBufferOne = reinterpret_cast<char*>(ExAllocatePoolWithTag(NonPagedPool, LogBufferSize, LogPoolTag));
	if (!LogBufferInfo->LogBufferOne)
	{
		LogFinalizeBufferInfo(LogBufferInfo);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	LogBufferInfo->LogBufferTwo = reinterpret_cast<char*>(ExAllocatePoolWithTag(NonPagedPool, LogBufferSize, LogPoolTag));
	if (!LogBufferInfo->LogBufferTwo)
	{
		LogFinalizeBufferInfo(LogBufferInfo);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// ��ʼ������Buffer
	// ���� Buffer�� һ������д��(LogFlushLogBuffer), һ���������(LogBufferMessage)��������ת��������
	RtlFillMemory(LogBufferInfo->LogBufferOne, LogBufferSize, 0xFF);
	LogBufferInfo->LogBufferOne[0] = '\0';
	LogBufferInfo->LogBufferOne[LogBufferSize - 1] = '\0';

	RtlFillMemory(LogBufferInfo->LogBufferTwo, LogBufferSize, 0xFF);
	LogBufferInfo->LogBufferTwo[0] = '\0';
	LogBufferInfo->LogBufferTwo[LogBufferSize - 1] = '\0';

	// д�� Buffer ʱʹ�õĶ���д�� LogBufferTail
	// ��ȡ Buffer ���� LogBufferHead ��ʼ��ȡ
	// ���л� ��ȡ��д��Buffer��ʱ��Ҳ��ǣ�浽�����������ı仯������� LogFlushLogBuffer
	LogBufferInfo->LogBufferHead = LogBufferInfo->LogBufferOne;
	LogBufferInfo->LogBufferTail = LogBufferInfo->LogBufferOne;

	// ������ʼ������
	NtStatus = LogInitializeLogFile(LogBufferInfo);
	if (NtStatus == STATUS_OBJECT_PATH_NOT_FOUND)	
		MYHYPERPLATFORM_LOG_INFO("The log file needs to be activated later.");	
	else if (!NT_SUCCESS(NtStatus))	// ʧ�� - ������Դ
		LogFinalizeBufferInfo(LogBufferInfo);

	return NtStatus;
}

// ��������LogFile�Ĳ��� - ��� LogBufferInfo
_Use_decl_annotations_ static void LogFinalizeBufferInfo(LOG_BUFFER_INFO* LogBufferInfo)
{
	PAGED_CODE();
	NT_ASSERT(LogBufferInfo);

	// �ر�LogBuffeˢ���߳�
	if (LogBufferInfo->BufferFlushThreadHandle)
	{
		LogBufferInfo->BufferFlushThreadShouldBeAlive = false;
		auto NtStatus = ZwWaitForSingleObject(LogBufferInfo->BufferFlushThreadHandle, FALSE, nullptr);
		if (!NT_SUCCESS(NtStatus))
			LogDbgBreak();

		ZwClose(LogBufferInfo->BufferFlushThreadHandle);
		LogBufferInfo->BufferFlushThreadHandle = nullptr;
	}

	// ���������
	if (LogBufferInfo->LogFileHandle)
	{
		ZwClose(LogBufferInfo->LogFileHandle);
		LogBufferInfo->LogFileHandle = nullptr;
	}

	if (LogBufferInfo->LogBufferTwo)
	{
		ExFreePoolWithTag(LogBufferInfo->LogBufferTwo, LogPoolTag);
		LogBufferInfo->LogBufferTwo = nullptr;
	}

	if (LogBufferInfo->LogBufferOne)
	{
		ExFreePoolWithTag(LogBufferInfo->LogBufferOne, LogPoolTag);
		LogBufferInfo->LogBufferOne = nullptr;
	}

	if (LogBufferInfo->ResourceInitialized)
	{
		ExDeleteResourceLite(&LogBufferInfo->Resource);
		LogBufferInfo->ResourceInitialized = false;
	}
}

// ������Log�������
_Use_decl_annotations_ NTSTATUS LogPrint(ULONG Level, const char* FunctionName, const char* Format, ...)
{
	NTSTATUS NtStatus = STATUS_SUCCESS;

	if (!LogIsLogNeeded(Level))
		return NtStatus;

	va_list Args;
	va_start(Args, Format);
	char LogMessage[412] = { 0 };
	// ����һ���ַ���
	NtStatus = RtlStringCchVPrintfA(LogMessage, RTL_NUMBER_OF(LogMessage), Format, Args);

	va_end(Args);
	if (!NT_SUCCESS(NtStatus))
	{
		LogDbgBreak();
		return NtStatus;
	}

	if (LogMessage[0] == '\0')
	{
		LogDbgBreak();
		return STATUS_INVALID_PARAMETER;
	}

	const auto PureLevel = Level & 0xF0;
	const auto Attribute = Level & 0x0F;

	// ���η���Log����Ϣ����Ӧ��С��512
	char Message[512] = { 0 };
	static_assert(RTL_NUMBER_OF(Message) <= 512, "On message should not exceed 512 bytes.");
	NtStatus = LogMakePrefix(PureLevel, FunctionName, LogMessage, Message, RTL_NUMBER_OF(Message));
	if (!NT_SUCCESS(NtStatus))
	{
		LogDbgBreak();
		return NtStatus;
	}
	// ��Message��Ϣ�����ļ� ���� ���
	NtStatus = LogPut(Message, Attribute);
	if (!NT_SUCCESS(NtStatus))
		LogDbgBreak();
	
	return NtStatus;
}

// ����Log��Ϣ
_Use_decl_annotations_ static NTSTATUS LogMakePrefix(ULONG Level, const char* FunctionName, const char* LogMessage, char* LogBuffer, SIZE_T LogBufferLength)
{
	// ����Level
	char const *LevelString = nullptr;
	switch (Level)
	{
		case LogLevelDebug:
			LevelString = "DBG\t";
			break;
		case LogLevelError:
			LevelString = "ERR\t";
			break;
		case LogLevelWarn:
			LevelString = "WRN\t";
			break;
		case LogLevelInfo:
			LevelString = "INF\t";
			break;
		default:
			return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS NtStatus = STATUS_SUCCESS;

	// ����ʱ���ַ���
	char TimeBuffer[20] = { 0 };
	if ((g_LogDebugFlag & LogOptDisableTime) == 0)
	{
		TIME_FIELDS TimeFields = { 0 };
		LARGE_INTEGER SystemTime = { 0 };
		LARGE_INTEGER LocalTime = { 0 };

		KeQuerySystemTime(&SystemTime);
		ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
		RtlTimeToTimeFields(&LocalTime, &TimeFields);

		NtStatus = RtlStringCchPrintfA(TimeBuffer, RTL_NUMBER_OF(TimeBuffer), "%02u:%02u:%02u.%03u\t", TimeFields.Hour, TimeFields.Minute, TimeFields.Second, TimeFields.Milliseconds);
		if (!NT_SUCCESS(NtStatus))
			return NtStatus;
	}

	// ���캯�����ַ���
	char FunctionNameBuffer[50] = { 0 };
	if ((g_LogDebugFlag & LogOptDisableFunctionName) == 0)
	{
		const auto BaseFunctionName = LogFindBaseFunctionName(FunctionName);
		NtStatus = RtlStringCchPrintfA(FunctionNameBuffer, RTL_NUMBER_OF(FunctionNameBuffer), "%-40s\t", BaseFunctionName);
		if (!NT_SUCCESS(NtStatus))
			return NtStatus;
	}

	// ���촦�����ַ���
	char ProcessorNumberBuffer[10] = { 0 };
	if ((g_LogDebugFlag & LogOptDisableProcessorNumber) == 0)
	{
		NtStatus = RtlStringCchPrintfA(ProcessorNumberBuffer, RTL_NUMBER_OF(ProcessorNumberBuffer), "#%lu\t", KeGetCurrentProcessorNumberEx(nullptr));
		if (!NT_SUCCESS(NtStatus))
			return NtStatus;
	}

	// �ϲ��ַ���

	NtStatus = RtlStringCchPrintfA(LogBuffer, LogBufferLength, "%s%s%s%5Iu\t%5Iu\t%-15s\t%s%s\r\n", TimeBuffer, LevelString, ProcessorNumberBuffer, reinterpret_cast<ULONG_PTR>(PsGetCurrentProcessId()),
		reinterpret_cast<ULONG_PTR>(PsGetCurrentThreadId()), PsGetProcessImageFileName(PsGetCurrentProcess()), FunctionNameBuffer, LogMessage);

	return NtStatus;
}

// �޸� __FUNCTION__ �꣬�õ�������ĺ�����
// namespace::class::function -> function
_Use_decl_annotations_ static const char * LogFindBaseFunctionName(const char * FunctionName)
{
	if (!FunctionName)
		return nullptr;
	
	auto Ptr = FunctionName;
	auto Name = FunctionName;

	while (*(Ptr++))
	{
		if (*Ptr == ':')
			Name = Ptr + 1;
	}

	return Name;
}

// ��¼��ڸ������Ժ��߳����
_Use_decl_annotations_ static NTSTATUS LogPut(char* Message, ULONG Attribute)
{
	NTSTATUS NtStatus = STATUS_SUCCESS;

	// ��ǰ�����а�������ȫ ���� IRQL �ȼ������ſ��Խ������
	auto DoDbgPrint = ((Attribute & LogLevelOptSafe) == 0 &&
		KeGetCurrentIrql() < CLOCK_LEVEL);

	// ��¼Log��Ϣ��Buffer�����ļ�
	auto& LogBufferInfo = g_LogBufferInfo;
	if (LogIsLogFileEnabled(LogBufferInfo))		// �ж��ļ�·���Ƿ���Ч
	{
		// �жϵ�ǰ�Ƿ����д���ļ�
		if (((Attribute & LogLevelOptSafe) == 0) && KeGetCurrentIrql() == PASSIVE_LEVEL && LogIsLogFileActivated(LogBufferInfo))
		{
#pragma warning(push)
#pragma warning(disable:28123)
			if (!KeAreAllApcsDisabled()) // ��������жϵ�ǰ�̵߳�IRQL�Ƿ� >= APC_LEVEL����ΪAPC_LEVEL��ʹ����APCʧЧ��������ڣ������档С�ڣ�����FALSE
			{
				LogFlushLogBuffer(&LogBufferInfo);		// ˢ�»���
				NtStatus = LogWriteMessageToFile(Message, LogBufferInfo);
			}
#pragma warning(pop)
		}
	}
	else
	{
		// ������Դ�ӡ - ���ô�ӡ��־λΪ��
		if (DoDbgPrint)
			LogSetPrintedBit(Message, true);
		// д�뵽 LogBuffeInfo ��
		NtStatus = LogBufferMessage(Message, &LogBufferInfo);
		LogSetPrintedBit(Message, false);
	}
	// ѡ�������
	if (DoDbgPrint)
		LogDoDbgPrint(Message);

	return NtStatus;
}

// @return ����true������Ҫ��ӡLog
_Use_decl_annotations_ bool LogIsLogNeeded(ULONG Level)
{
	return !!(g_LogDebugFlag & Level);
}

static void LogDbgBreak()
{
	if (!KD_DEBUGGER_NOT_PRESENT)
		__debugbreak();
}

_Use_decl_annotations_ static bool LogIsLogFileEnabled(const LOG_BUFFER_INFO& LogBufferInfo)
{
	if (LogBufferInfo.LogBufferOne)
	{
		NT_ASSERT(LogBufferInfo.LogBufferTwo);
		NT_ASSERT(LogBufferInfo.LogBufferHead);
		NT_ASSERT(LogBufferInfo.LogBufferTail);

		return true;
	}
	
	NT_ASSERT(!LogBufferInfo.LogBufferTwo);
	NT_ASSERT(!LogBufferInfo.LogBufferHead);
	NT_ASSERT(!LogBufferInfo.LogBufferTail);

	return false;
}

_Use_decl_annotations_ static bool LogIsLogFileActivated(const LOG_BUFFER_INFO& LogBufferInfo)
{
	if (LogBufferInfo.BufferFlushThreadShouldBeAlive)
	{
		NT_ASSERT(LogBufferInfo.BufferFlushThreadHandle);
		NT_ASSERT(LogBufferInfo.BufferFlushThreadHandle);

		return true;
	}

	NT_ASSERT(!LogBufferInfo.BufferFlushThreadHandle);
	NT_ASSERT(!LogBufferInfo.BufferFlushThreadHandle);

	return false;
}

// ˢ��Log���� - ���ɵ�Log����д�뵽Log�ļ��У������Ҫ��ӡ���ǡ�
_Use_decl_annotations_ static NTSTATUS LogFlushLogBuffer(LOG_BUFFER_INFO* LogBufferInfo)
{
	NT_ASSERT(LogBufferInfo);
	NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	NTSTATUS NtStatus = STATUS_SUCCESS;

	// �����ٽ��� - ����������Դ
	ExEnterCriticalRegionAndAcquireResourceExclusive(&LogBufferInfo->Resource);

	// ������������� 
	// https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/queued-spin-locks ���� Queued Spin Lock 
	// �����������ڶ�˶��̵߳Ļ����ϣ����Ӹ�Ч����֤�ǵ�һ������������ִ�С�
	// ������������Ϊ�˸ı� LogBufferHead ��ȫ
	KLOCK_QUEUE_HANDLE LockHandle = {};
	// �����л���д���ʱ���ʹ�á�
	// �ڶ�ȡ��ʱ���á���Ϊ��Ҫǣ�浽 Head �� Tail ���޸�
	KeAcquireInStackQueuedSpinLock(&LogBufferInfo->SpinLock, &LockHandle);

	// �õ���ǰ��д��Buffer
	const auto OldLogBuffer = const_cast<char*>(LogBufferInfo->LogBufferHead);
	if (OldLogBuffer[0])	// �ж��Ƿ�ʹ���ˣ����û��ʹ�þ�û���л�����Ҫ
	{
		// �л�д�� Buffer ��һ�� Buffer �ϡ�
		LogBufferInfo->LogBufferHead = (OldLogBuffer == LogBufferInfo->LogBufferOne)
			? LogBufferInfo->LogBufferTwo : LogBufferInfo->LogBufferOne;
		LogBufferInfo->LogBufferHead[0] = '\0';		// ��� Buffer
		LogBufferInfo->LogBufferTail = LogBufferInfo->LogBufferHead; // β���ص���ǰд�� Buffer �ײ���	
	}
	KeReleaseInStackQueuedSpinLock(&LockHandle);	

	// ������OldLogBufferд���ļ�
	IO_STATUS_BLOCK IoStatusBlock = { 0 };
	for (auto CurrentLogEntry = OldLogBuffer; CurrentLogEntry[0]; )
	{
		const auto IsPrinteOut = LogIsPrinted(CurrentLogEntry);
		LogSetPrintedBit(CurrentLogEntry, false);					// ���ñ�־λ - ��ʾ�Ѿ����

		// �õ����ȣ�д���ļ���
		const auto CurrentLogEntryLength = strlen(CurrentLogEntry);
		NtStatus = ZwWriteFile(LogBufferInfo->LogFileHandle, nullptr, nullptr, nullptr, &IoStatusBlock, CurrentLogEntry, static_cast<ULONG>(CurrentLogEntryLength), nullptr, nullptr);
		if (!NT_SUCCESS(NtStatus)) //  // It could happen when you did not register IRP_SHUTDOWN and call LogIrpShutdownHandler() and the system tried to log to a file after a file system was unmounted.
			LogDbgBreak();

		// �����Ҫ ��ӡ
		if (!IsPrinteOut)
			LogDoDbgPrint(CurrentLogEntry);

		CurrentLogEntry += CurrentLogEntryLength + 1;
	}
	OldLogBuffer[0] = '\0';

	ExReleaseResourceAndLeaveCriticalRegion(&LogBufferInfo->Resource);
	return NtStatus;
}

// ������黺���Ƿ�������
_Use_decl_annotations_ static bool LogIsPrinted(char *Message) 
{
	return (Message[0] & 0x80) != 0;
}

// �޸ı�־λ  - ��ֵ����ʾ�Ѿ�׼����� ���㣬�������Ż�ԭʼ
_Use_decl_annotations_ static void LogSetPrintedBit(char *Message, bool on) 
{
	if (on) {
		Message[0] |= 0x80;
	}
	else {
		Message[0] &= 0x7f;
	}
}

// Calls DbgPrintEx() while converting \r\n to \n\0
_Use_decl_annotations_ static void LogDoDbgPrint(char *Message) 
{
	if (!LogIsDbgPrintNeeded()) {
		return;
	}
	const auto LocationOfTail = strlen(Message) - 2;
	Message[LocationOfTail] = '\n';
	Message[LocationOfTail + 1] = '\0';
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "%s", Message);
}

// @return �����Ե��� DbgPrint ʱ��������
static bool LogIsDbgPrintNeeded()
{
	return (g_LogDebugFlag & LogOptDisableDbgPrint) == 0;
}

// ��Log��Ϣд�뵽�ļ���
_Use_decl_annotations_ static NTSTATUS LogWriteMessageToFile(const char* Message, const LOG_BUFFER_INFO& LogBufferInfo)
{
	NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	IO_STATUS_BLOCK IoStatusBlock = { 0 };
	NTSTATUS NtStatus = ZwWriteFile(LogBufferInfo.LogFileHandle, nullptr, nullptr, nullptr, &IoStatusBlock, const_cast<char*>(Message), static_cast<ULONG>(strlen(Message)), nullptr, nullptr);
	if (!NT_SUCCESS(NtStatus))
		LogDbgBreak();

	NtStatus = ZwFlushBuffersFile(LogBufferInfo.LogFileHandle, &IoStatusBlock);
	return NtStatus;
}

// ��Log��Ϣ���뵽������
_Use_decl_annotations_ static NTSTATUS LogBufferMessage(const char* Message, LOG_BUFFER_INFO* LogBufferInfo)
{
	NT_ASSERT(LogBufferInfo);

	// ���������� - ǣ�� Head Tail ��������
	KLOCK_QUEUE_HANDLE LockHandle = { 0 };
	const auto OldIRQL = KeGetCurrentIrql();
	
	if (OldIRQL < PASSIVE_LEVEL)
		KeAcquireInStackQueuedSpinLock(&LogBufferInfo->SpinLock, &LockHandle);
	else
		KeAcquireInStackQueuedSpinLockAtDpcLevel(&LogBufferInfo->SpinLock, &LockHandle);

	NT_ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

	// ����ǰ�� Logbuffer ��������
	SIZE_T UsedBufferSize = LogBufferInfo->LogBufferTail - LogBufferInfo->LogBufferHead;
	NTSTATUS NtStatus = RtlStringCchCopyA(const_cast<char*>(LogBufferInfo->LogBufferTail), LogBufferUsableSize - UsedBufferSize, Message);	// �����ĳ��� �ǻ�����ʹ�õĳ���

	// ���� LogBufferTail ���ܸ��� LogMaxUsage
	if (NT_SUCCESS(NtStatus))
	{
		const auto MessageLength = strlen(Message) + 1;
		LogBufferInfo->LogBufferTail += MessageLength;
		UsedBufferSize += MessageLength;

		if (UsedBufferSize > LogBufferInfo->LogMaxUsage)
			LogBufferInfo->LogMaxUsage = UsedBufferSize;
	}
	else
		LogBufferInfo->LogMaxUsage = LogBufferSize;		// ��ʾ�Ѿ����

	*LogBufferInfo->LogBufferTail = '\0';	// ÿ��д����ɣ��� 0��������ȡʱ����Ϊ�ָ�����

	if (OldIRQL < DISPATCH_LEVEL)
		KeReleaseInStackQueuedSpinLock(&LockHandle);
	else
		KeReleaseInStackQueuedSpinLockFromDpcLevel(&LockHandle);

	return NtStatus;
}

// ��ʼ�� LogBufferInfo ���й��ļ��ĳ�Ա  ���ҿ���ˢ���߳�
_Use_decl_annotations_ static NTSTATUS LogInitializeLogFile(LOG_BUFFER_INFO* LogBufferInfo)
{
	PAGED_CODE();

	// ����Ѿ������ - �˳�
	if (LogBufferInfo->LogFileHandle)
		return STATUS_SUCCESS;

	// ��ʼ��Log�ļ�
	UNICODE_STRING UniLogFilePath = { 0 };
	RtlInitUnicodeString(&UniLogFilePath, LogBufferInfo->LogFilePath);
	OBJECT_ATTRIBUTES ObjectAttributes = { 0 };
	InitializeObjectAttributes(&ObjectAttributes, &UniLogFilePath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);
	IO_STATUS_BLOCK IoStatusBlock = { 0 };

	// ���� / ���ļ� FILE_OPEN_IF
	NTSTATUS NtStatus = ZwCreateFile(&LogBufferInfo->LogFileHandle, FILE_APPEND_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, nullptr, FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, nullptr, 0);
	if (!NT_SUCCESS(NtStatus))
		return NtStatus;

	// ��ʼ��ˢ���߳�
	LogBufferInfo->BufferFlushThreadShouldBeAlive = true;
	NtStatus = PsCreateSystemThread(&LogBufferInfo->BufferFlushThreadHandle, GENERIC_ALL, nullptr, nullptr, nullptr, LogBufferFlushThreadRoutine, LogBufferInfo); // ��ˢ���߳������ɹ� - �޸� LogBufferInfo->BufferFlushThreadStarted
	if (!NT_SUCCESS(NtStatus))
	{
		ZwClose(LogBufferInfo->LogFileHandle);
		LogBufferInfo->LogFileHandle = nullptr;
		LogBufferInfo->BufferFlushThreadShouldBeAlive = false;
		return NtStatus;
	}

	// �ȴ����߳�������� - �ж� BufferFlushThreadStarted
	// Ŀ���߳��ڳ�ʼ����ɺ��޸ı���������ȴ�����ֵ���޸�
	while (!LogBufferInfo->BufferFlushThreadStarted)
		LogSleep(100);

	return NtStatus;
}

// ˯�ߺ���
_Use_decl_annotations_ static NTSTATUS LogSleep(LONG Millsecond)
{
	PAGED_CODE();

	LARGE_INTEGER LargeInteger = { 0 };
	LargeInteger.QuadPart = -(10000ll * Millsecond);	// 10000 LL(����Сдl)
	return KeDelayExecutionThread(KernelMode, FALSE, &LargeInteger);
}

// LogFileˢ���߳� - �� LogBufferInfo һ������߳� �� LogBufferInfo->BufferFlushThreadShouldBeAlive ��������
// ˢ���߳���Ҫ�� LogBuffer д�뵽 Log�ļ��� - ÿ�� LogFlushIntervalMsec ʱ�� ����ˢ��
_Use_decl_annotations_ static VOID LogBufferFlushThreadRoutine(void* StartContext)
{
	PAGED_CODE();
	NTSTATUS NtStatus = STATUS_SUCCESS;
	auto LogBufferInfo = reinterpret_cast<LOG_BUFFER_INFO*>(StartContext);
	LogBufferInfo->BufferFlushThreadStarted = true;				// ֪ͨ LogInitializeLogFile ��ˢ���߳��������

	MYHYPERPLATFORM_LOG_DEBUG("Log thread started (TID = %p).", PsGetCurrentThreadId());

	// BufferFlushThreadShouldBeAlive !!!
	while (LogBufferInfo->BufferFlushThreadShouldBeAlive)
	{
		NT_ASSERT(LogIsLogFileActivated(*LogBufferInfo));
		if (LogBufferInfo->LogBufferHead[0])
		{
			NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
			NT_ASSERT(!KeAreAllApcsDisabled());
			NtStatus = LogFlushLogBuffer(LogBufferInfo);

			// ��Ϊ��������(overall performance)ˢ���ļ���
			//���㴥���� KeBugCheck, ����Ҳ����ͨ�� LogBuffer �ָ� Log
		}
		LogSleep(LogFlushIntervalMsec);
	}

	PsTerminateSystemThread(NtStatus);
}

// ����Logϵͳ
_Use_decl_annotations_ void LogTermination()
{
	PAGED_CODE();
	MYHYPERPLATFORM_LOG_DEBUG("Finalizing... (Max log usage = %Iu/%lu bytes)", g_LogBufferInfo.LogMaxUsage, LogBufferSize);
	MYHYPERPLATFORM_LOG_INFO("Log termination.");

	g_LogDebugFlag = LogPutLevelDisable;	// �޸�ȫ�ֱ�־ - ��������κ�Log��Ϣ
	LogFinalizeBufferInfo(&g_LogBufferInfo);
}

_Use_decl_annotations_ void LogRegisterReinitialization(PDRIVER_OBJECT DriverObject)
{
	PAGED_CODE();

	IoRegisterBootDriverReinitialization(DriverObject, LogReinitializationRoutine, &g_LogBufferInfo);
	MYHYPERPLATFORM_LOG_INFO("The log file will be activated later.");
}

_Use_decl_annotations_ void static LogReinitializationRoutine(DRIVER_OBJECT* DriverObject, PVOID Context, ULONG Count)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(Count);

	NT_ASSERT(Context);

	auto LogBufferInfo = reinterpret_cast<LOG_BUFFER_INFO*>(Context);
	auto NtStatus = LogInitializeLogFile(LogBufferInfo);
	NT_ASSERT(NT_SUCCESS(NtStatus));

	if (NT_SUCCESS(NtStatus))
		MYHYPERPLATFORM_LOG_INFO("The log file has been activated.");

}

EXTERN_C_END

