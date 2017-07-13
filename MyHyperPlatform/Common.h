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

static const ULONG HyperPlatformCommonPoolTag = 'AazZ';

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
