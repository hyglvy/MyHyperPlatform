#ifndef MYHYPERPLATFORM_LOG_H
#define MYHYPERPLATFORM_LOG_H

#include <fltKernel.h>

EXTERN_C_START

#define MYHYPERPLATFORM_LOG_PRINT(msg) DbgPrint("%s\r\n", msg);

#define MYHYPERPLATFORM_LOG_INFO(Format, ...) \
	LogPrint(LogLevelInfo, __FUNCTION__, (Format), __VA_ARGS__)

#define MYHYPERPLATFORM_LOG_DEBUG(Format, ...) \
	LogPrint(LogLevelDebug, __FUNCTION__, (Format), __VA_ARGS__)

#define MYHYPERPLATFORM_LOG_WARN(Format, ...) \
	LogPrint(LogLevelWarn, __FUNCTION__, (Format), __VA_ARGS__)

#define MYHYPERPLATFORM_LOG_ERROR(Format, ...) \
	LogPrint(LogLevelError, __FUNCTION__, (Format), __VA_ARGS__)


#define MYHYPERPLATFORM_LOG_DEBUG_SAFE(Format, ...) \
	LogPrint(LogLevelDebug | LogLevelOptSafe, __FUNCTION__, (Format), __VA_ARGS__)

#define MYHYPERPLATFORM_LOG_WARN_SAFE(Format, ...) \
	LogPrint(LogLevelWarn | LogLevelOptSafe, __FUNCTION__, (Format), __VA_ARGS__)

#define MYHYPERPLATFORM_LOG_ERROR_SAFE(Format, ...) \
	LogPrint(LogLevelError | LogLevelOptSafe, __FUNCTION__, (Format), __VA_ARGS__)

#define MYHYPERPLATFORM_LOG_INFO_SAFE(Format, ...) \
	LogPrint(LogLevelInfo | LogLevelOptSafe, __FUNCTION__, (Format), __VA_ARGS__)

// ����Log��Ϣ��д���ļ��������
static const auto LogLevelOptSafe = 0x1ul;

// Log Variables 
// �������ĸ�Log�ĵȼ� - ����Log�ȼ��������Ƿ��Ӧ��Ϣ
static const auto LogLevelDebug = 0x10ul;	
static const auto LogLevelInfo  = 0x20ul;
static const auto LogLevelWarn  = 0x40ul;
static const auto LogLevelError = 0x80ul;

// ������������Ƿֱ�ָ����Log��Ϣ�У��Ƿ����ָ����Ϣ�Ŀ��ƺ���
static const auto LogPutLevelDisable = 0x00ul;
static const auto LogOptDisableTime = 0x100ul;
static const auto LogOptDisableFunctionName = 0x200ul;
static const auto LogOptDisableProcessorNumber = 0x400ul;
static const auto LogOptDisableDbgPrint = 0x800ul;

// ��Ч���е�Log��Ϣ
static const auto LogPutLevelDebug = LogLevelError | LogLevelInfo | LogLevelDebug | LogLevelWarn;
// ��Ч INFO WARN ERROR ��Log��Ϣ
static const auto LogPutLevelInfo = LogLevelError | LogLevelWarn | LogLevelInfo;
// ��Ч WARN ERROR ��Log��Ϣ
static const auto LogPutLevelWarn = LogLevelError | LogLevelWarn;
// ��Ч ERROR ��Log��Ϣ
static const auto LogPutLevelError = LogLevelError;



// Log a message: һ�㶼��ͨ�� MYHYPERPLATFORM_LOG_*()����������
// @param Level ��Ϣ�ȼ�
// @param FunctionName ���ú�����
// @param Format Ҫ�������Ϣ
// @return �����ɹ����� STATUS_SUCCESS
// @see MYHYPERPLATFORM_LOG_DEBUG MYHYPERPLATFORM_LOG_SAFE
NTSTATUS LogPrint(_In_ ULONG Level, _In_z_ const char* FunctionName, _In_z_ _Printf_format_string_ const char* Format, ...);


// ��ʼ��Logϵͳ
// @param Flag  OR-ed ֵȥ����Log�ȼ��Ϳ�ѡ�� LogPutLevel* | LogOpt* (LogPutLevelDebug | LogOptDisableFunctionName
// @param FilaPath Log�ļ�·��
// @return �����ɹ�����STATUS_SUCCESS�� ���� STATUS_REINITIALIZATION_NEEDED�� ��������Ҫ���� LogRegisterReinitialization��������û��������ʼ����ɡ�
_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS LogInitialization(_In_ ULONG Flag, _In_opt_ const wchar_t* FilePath);

// ����Logϵͳ����UnloadDriver����,���ߴ�����
_IRQL_requires_max_(PASSIVE_LEVEL) void LogTermination();


EXTERN_C_END



#endif