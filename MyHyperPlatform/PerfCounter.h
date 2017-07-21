#pragma once

#include <fltKernel.h>

#define HYPERPLATFORM_PERFCOUNTER_P_JOIN2(x, y) x##y		// question ??? 
#define HYPERPLATFORM_PERFCOUNTER_P_JOIN1(x, y) \
	HYPERPLATFORM_PERFCOUNTER_P_JOIN2(x, y)

#define HYPERPLATFORM_PERFCOUNTER_P_JOIN(x, y) \
	HYPERPLATFORM_PERFCOUNTER_P_JOIN1(x, y)

#define HYPERPLATFORM_PERFCOUNTER_P_TO_STRING1(n) #n

#define HYPERPLATFORM_PERFCOUNTER_P_TO_STRING(n) \
	HYPERPLATFORM_PERFCOUNTER_P_TO_STRING1(n)

// ����һ�� PerfCounter ʵ����������δ������е�ʱ��
// @param Collector PerfCollector ʵ��
// @param QueryTimeRoutine ��������ʱ��ĺ���ָ��
// ����겻��ֱ��ʹ�ã�
// ����괴��һ�� PerfCounter ʵ��������Ϊ PerfObj_N ��N ��һ�������仯�����֣���0��ʼ���������뺯������Դ������������ꡣ
// PerfCounter ���Լ��Ĺ��캯����õ�����������������ִ��ʱ�䣬���������� Collector��
#define MYHYPERPLATFORM_PERFCOUNTER_MEASURE_TIME(Collector, QueryTimeRoutine)	\
	const PERF_COUNTER HYPERPLATFORM_PERFCOUNTER_P_JOIN(PerfObj_, __COUNTER__)(	\
		  (Collector), (QueryTimeRoutine),									    \
		  __FUNCTION__"("HYPERPLATFORM_PERFCOUNTER_P_TO_STRING(__LINE__) ")")


class PERF_COLLECTOR
{
public: 
	// ��� ��ǰ���������
	using INITIAL_OUTPUT_ROUTINE = void(_In_opt_ void* OutputContext);
	// ��� ������������
	using FINAL_OUTPUT_ROUTINE = void(_In_opt_ void* OutputContext);
	// ��� �������������
	using OUTPUT_ROUTINE = void(_In_ const char* LocationName,
							   _In_ ULONG64 TotalExecutionCount,
							   _In_ ULONG64 TotalElapsedTime,
						       _In_opt_ void* OutputContext);
	// Lock��������
	using LOCK_ROUTINE = void(_In_opt_ void* LockContext);

private:
	// �쳣����ֵ
	static const ULONG InvalidDataIndex = MAXULONG;
	// ����¼����
	static const ULONG MaxNumberOfDataEntries = 200;
	// ��¼ÿһ��λ�õ���Ϊ ���ݽṹ
	typedef struct _PERF_DATA_ENTRY_
	{
		// Ψһ��ʾ
		const char* Key;
		// �ܹ���ִ�д���
		ULONG64 TotalExecutionCount;	
		// �ܹ���ִ��ʱ��
		ULONG64 TotalElapsedTime;
	}PERF_DATA_ENTRY;

	// �ֲ��� - ��Ƕ��
	class SCOPED_LOCK
	{
	public: 
		SCOPED_LOCK(_In_ LOCK_ROUTINE* LockEnterRoutine, _In_ LOCK_ROUTINE* LockLeaveRoutine, _In_opt_ void* LockContext):
		m_EnterRoutine(LockEnterRoutine),  m_LeaveRoutine(LockLeaveRoutine), m_LockContext(LockContext)
		{
			// ִ�б����ĵĳ�ʼ��
			m_EnterRoutine(m_LockContext);
		}

		~SCOPED_LOCK()
		{
			m_LeaveRoutine(m_LockContext);
		}

	private:
		LOCK_ROUTINE* m_EnterRoutine;
		LOCK_ROUTINE* m_LeaveRoutine;
		void* m_LockContext;
	};
	// ˽�б�������

	INITIAL_OUTPUT_ROUTINE* m_InitialOutputRoutine;
	FINAL_OUTPUT_ROUTINE* m_FinalOutputRoutine;
	OUTPUT_ROUTINE* m_OutputRoutine;
	LOCK_ROUTINE* m_LockEnterRoutine;
	LOCK_ROUTINE* m_LockLeaveRoutine;
	void* m_LockContext;
	void* m_OutputContext;
	PERF_DATA_ENTRY m_Data[MaxNumberOfDataEntries];

	// Ĭ���������
	// @param OutputComtext ��Ч����
	static void NoOutputRoutine(_In_opt_ void* OutputContext)
	{
		UNREFERENCED_PARAMETER(OutputContext);
	}
	// Ĭ��������
	// @param LockContext ��Ч����
	static void NoLockRoutine(_In_opt_ void* LockContext)
	{
		UNREFERENCED_PARAMETER(LockContext);
	}

	// ���ص�ǰλ LOCATION_NAME ��ȷ������
	// @param Key ��Ҫ�õ�������LOCATION_NAME 
	// @return    �����õ���������
	// ������Ѿ����ڵ�Data��δ���ҵ����������Զ����������LOCATION��Data�С����û���ҵ����������ʧ�ܡ����� InvalidDataIndex (MAXULONG)
	ULONG GetPerfDataIndex(_In_ const char* Key)
	{
		if (!Key)
			return false;

		for (auto i = 0; i < MaxNumberOfDataEntries; i++)
		{
			if (m_Data[i].Key == Key)	// ����ȽϷ�ʽ ?
				return i;

			// ����ҵ��˿սڵ� - ˵������ʧ�ܡ� ֱ����ӽ�ȥ
			if (m_Data[i].Key == nullptr)
			{
				m_Data[i].Key = Key;
				return i;
			}
		}

		return InvalidDataIndex;
	}

public:
	// ��ʼ������ 5������ָ�� ����������ָ��
	void Initialize(_In_ OUTPUT_ROUTINE* OutputRoutine, _In_opt_ INITIAL_OUTPUT_ROUTINE* InitialOutputRoutine = NoOutputRoutine, _In_opt_ FINAL_OUTPUT_ROUTINE* FinalOutputRoutine = NoOutputRoutine,
		_In_opt_ LOCK_ROUTINE* LockEnterRoutine = NoLockRoutine, _In_opt_ LOCK_ROUTINE* LockLeaveRoutine = NoLockRoutine, _In_opt_ void* LockContext = nullptr, _In_opt_ void* OutputContext = nullptr)
	{
		m_InitialOutputRoutine = InitialOutputRoutine;
		m_FinalOutputRoutine = FinalOutputRoutine;
		m_OutputRoutine = OutputRoutine;
		m_LockEnterRoutine = LockEnterRoutine;
		m_LockLeaveRoutine = LockLeaveRoutine;
		m_LockContext = LockContext;
		m_OutputContext = OutputContext;
		memset(m_Data, 0, sizeof(m_Data));
	}

	// ������ �������¼����Ϊ��¼
	void Terminate()
	{
		if (m_Data[0].Key)
			m_InitialOutputRoutine(m_OutputContext);
		
		for (auto i = 0; i < MaxNumberOfDataEntries; i++)
		{
			if (m_Data[i].Key == nullptr)
				break;

			m_OutputRoutine(m_Data[i].Key, m_Data[i].TotalExecutionCount, m_Data[i].TotalElapsedTime, m_OutputContext);
		}

		if (m_Data[0].Key)
			m_FinalOutputRoutine(m_OutputContext);
	}

	// ������Ϊ����
	bool AddData(_In_ const char* LocationName, _In_ ULONG64 ElapsedTime)
	{
		SCOPED_LOCK Lock(m_LockEnterRoutine, m_LockLeaveRoutine, m_LockContext);
		const auto DataIndex = GetPerfDataIndex(LocationName);
		if (DataIndex == InvalidDataIndex)
			return false;

		m_Data[DataIndex].TotalElapsedTime += ElapsedTime;
		m_Data[DataIndex].TotalExecutionCount++;

		return true;
	}
};

class PERF_COUNTER
{
public:
	using QUERY_TIME_ROUTINE = ULONG64();

	// ͨ��QueryTimeRoutine�õ���ǰʱ��
	// @param Collector ��ʵ�� ���洢��Ϊ����
	// @param QueryTimeRoutine ʱ���ѯ����ָ��
	// @param LocationName ���ᱻ��¼�ĺ�����
	// �����ʹ�� #HYPERPLATFORM_PERFCOUNTER_MEASURE_TIME() ������������ʵ��
	PERF_COUNTER(_In_ PERF_COLLECTOR* Collector, _In_opt_ QUERY_TIME_ROUTINE* QueryTimeRoutine, _In_ const char* LocationName):
	m_Collector(Collector), m_QueryTimeRoutine(QueryTimeRoutine ? QueryTimeRoutine : RdTsc), m_LocationName(LocationName), m_BeforeTime(m_QueryTimeRoutine())
	{
	}

	~PERF_COUNTER()
	{
		if (m_Collector)
		{
			// ����ʱ�� = ��ǰʱ��� - ��ǰʱ���
			const auto ElapsedTime = m_QueryTimeRoutine() - m_BeforeTime;
			m_Collector->AddData(m_LocationName, ElapsedTime);
		}
	}

private:
	// // ���ش�����ʱ���
	static ULONG64 RdTsc()
	{
		// ���ش�����ʱ���
		return __rdtsc();
	}

	PERF_COLLECTOR* m_Collector;
	QUERY_TIME_ROUTINE* m_QueryTimeRoutine;
	const char* m_LocationName;
	const ULONG64 m_BeforeTime;

};