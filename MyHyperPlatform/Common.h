#pragma once

#include <fltKernel.h>

#pragma prefast(disable : 30030)

#if !defined(MYHYPERPLATFORM_COMMON_DBG_BREAK)
// �жϵ�ǰ�Ƿ��е���������
// ���� 0 ��Ϊ���������ʹ��ʱ������һ������
#define MYHYPERPLATFORM_COMMON_DBG_BREAK()  \
		if (KD_DEBUGGER_NOT_PRESENT)        \
		{ }						            \
		else                                \
		{									\
			__debugbreak();					\
		}									\
		reinterpret_cast<void*>(0)
#endif

// ˵���ʹ���BUG
// @param TypeOfCheckBug bug����
// @param param1 KeBugCheckEx() ��һ����
// @param param2 KeBugCheckEx() �ڶ�����
// @param param3 KeBugCheckEx() ��������
#if !defined(MYHYPERPLATFORM_COMMON_BUG_CHECK)
#define MYHYPERPLATFORM_COMMON_BUG_CHECK(BugType, param1, param2, param3)	\
			MYHYPERPLATFORM_COMMON_DBG_BREAK();							    \
			const HYPERPLATFORM_BUG_CHECK code = (BugType);					\
			KeBugCheckEx(MANUALLY_INITIATED_CRASH, static_cast<ULONG>(code),\
						(param1), (param2), (param3))
#endif

// ���� | �ر� ȫ����Ϊ��¼
#define MYHYPERPLATFORM_PERFORMANCE_ENABLE_PERFCOUNTER 1

static const ULONG HyperPlatformCommonPoolTag = 'AazZ';

// BugCheck Type for #MYHYPERPLATFORM_COMMON_BUG_CHECK
enum class HYPERPLATFORM_BUG_CHECK : ULONG
{
	kUnspecified,                    //!< An unspecified bug occurred
	kUnexpectedVmExit,               //!< An unexpected VM-exit occurred
	kTripleFaultVmExit,              //!< A triple fault VM-exit occurred
	kExhaustedPreallocatedEntries,   //!< All pre-allocated entries are used
	kCriticalVmxInstructionFailure,  //!< VMRESUME or VMXOFF has failed
	kEptMisconfigVmExit,             //!< EPT misconfiguration VM-exit occurred
	kCritialPoolAllocationFailure,   //!< Critical pool allocation failed
};

// �ж��Ƿ��� 64 λϵͳ
// @return ��ϵͳ��64λ������ true 
constexpr bool IsX64() 
{
// constrexpr ������������ʾ�������س���
#if defined(_AMD64_)
	return true;
#else
	return false;
#endif
}

// ��鵱ǰ�Ƿ��� released ����״̬
// @return ��ǰ��relese�汾 ����true
constexpr bool IsReleaseBuild()
{
#if defined(DBG)
	return false;
#else
	return true;
#endif
}
