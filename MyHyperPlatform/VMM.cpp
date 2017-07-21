#include "VMM.h"
#include <intrin.h>
#include "ASM.h"
#include "Common.h"
#include "EPT.h"
#include "Log.h"
#include "Util.h"
#include "Performance.h"


EXTERN_C_START


// �����Ƿ��¼ VM-Exit
static const long kVmmEnableRecordVmExit = false;
// ÿ��������Ӧ�ü�¼����
static const long kVmmNumberOfRecords = 100;
// ֧�ּ�¼���ٸ�����������Ϣ
static const long kVmmNumberOfProcessors = 2;

struct VMM_INITIAL_STACK
{
	GP_REGISTER GpRegister;
	ULONG_PTR   Reserved;
	PROCESSOR_DATA* ProcessorData;
};

struct GUEST_CONTEXT
{
	union 
	{
		VMM_INITIAL_STACK* Stack;
		GP_REGISTER* GpRegister;
	};

	FLAG_REGISTER Flag;
	ULONG_PTR Ip;
	ULONG_PTR Cr8;
	KIRQL Irql;
	bool VmContinue;
};
#if defined(_AMD64_)
static_assert(sizeof(GUEST_CONTEXT) == 40, "Size check");
#else
static_assert(sizeof(GUEST_CONTEXT) == 20, "Size check");
#endif

// VM-exit �¼��������ļ�¼
struct VM_EXIT_HISTORY
{
	GP_REGISTER GpRegister;
	ULONG_PTR Ip;
	VM_EXIT_INFORMATION ExitReason;
	ULONG_PTR ExitQualification;
	ULONG_PTR InstructionInfo;
};

DECLSPEC_NORETURN void __stdcall VmmVmxFailureHandler(_Inout_ ALL_REGISTERS* AllRegisters);
bool __stdcall VmmVmExitHandler(_Inout_ VMM_INITIAL_STACK *Stack);
static void VmmHandleVmExit(_Inout_ GUEST_CONTEXT* GuestContext);

static void VmmDumpGuestState();

static void VmmHandleException(_Inout_ GUEST_CONTEXT* GuestContext);
DECLSPEC_NORETURN static void VmmHandleTripleFault(_Inout_ GUEST_CONTEXT* GuestContext);
static void VmmHandleCpuid(_Inout_ GUEST_CONTEXT* GuestContext);
static void VmmHandleInvalidateInternalCaches(_Inout_ GUEST_CONTEXT* GuestContext);
static void VmmHandleInvalidateTlbEntry(_Inout_ GUEST_CONTEXT* GuestContext);
static void VmmHandleRdtsc(_Inout_ GUEST_CONTEXT* GuestContext);

static ULONG_PTR* VmmSelectRegister(_In_ ULONG Index,_In_ GUEST_CONTEXT* GuestContext);

// ����ı��������������м�¼�ͷ���bugʱ���м��
static ULONG g_VmmNextHistroyIndex[kVmmNumberOfProcessors];
static VM_EXIT_HISTORY g_VmmVmExitHistroy[kVmmNumberOfProcessors][kVmmNumberOfRecords];	// Ϊ kVmmNumberOfProcessors ����������¼ kVmmNumberOfRecords ����¼


// �� AsmVmExitHandler ���� - ���� VM-exit
#pragma warning(push)
#pragma warning(disable : 28167)
_Use_decl_annotations_ bool __stdcall VmmVmExitHandler(VMM_INITIAL_STACK *Stack)
{
	// ���� guest ������ 
	const auto GuestIrql = KeGetCurrentIrql();
	const auto GuestCr8 = IsX64() ? __readcr8() : 0;
	// ���� IRQL - ??? Ϊɶ����
	if (GuestIrql < DISPATCH_LEVEL)
		KeRaiseIrqlToDpcLevel();

	NT_ASSERT(Stack->Reserved == MAXULONG_PTR);	
	GUEST_CONTEXT GuestContext = { Stack, UtilVmRead(VMCS_FIELD::kGuestRflags), UtilVmRead(VMCS_FIELD::kGuestRip), GuestCr8, GuestIrql, true };
	GuestContext.GpRegister->sp = UtilVmRead(VMCS_FIELD::kGuestRsp);

	// ����ʵ�ʴ�����
	VmmHandleVmExit(&GuestContext);

	// ���VM���󣬲���ִ�� - ˢ�»���
	if (!GuestContext.VmContinue)
	{
		UtilInveptGlobal();
		UtilInvvpidAllContext();
	}
	// �ظ� IRQL
	if (GuestContext.Irql < DISPATCH_LEVEL)
		KeLowerIrql(GuestContext.Irql);

	// ���� CR8 ???
	if (IsX64())
	{
		__writecr8(GuestContext.Cr8);
	}

	return GuestContext.VmContinue;
}

// Handle VMRESUME or VMXOFF failure. Fatal error.
_Use_decl_annotations_ void __stdcall VmmVmxFailureHandler(ALL_REGISTERS* AllRegisters)
{
	UNREFERENCED_PARAMETER(AllRegisters);
	
}

//  �ַ� VM-exit ������Ĵ�����
// 3.10 P247 VM-exit ��Ϣ���ֶ�
_Use_decl_annotations_ static void VmmHandleVmExit(GUEST_CONTEXT* GuestContext)
{
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();

	const VM_EXIT_INFORMATION ExitReason = { static_cast<ULONG32>(UtilVmRead(VMCS_FIELD::kVmExitReason)) };

	if (kVmmEnableRecordVmExit)
	{
		// ��¼ VM-Exit ��Ϣ
		const auto Processor = KeGetCurrentProcessorNumberEx(nullptr);
		auto& Index = g_VmmNextHistroyIndex[Processor];
		auto& Histroy = g_VmmVmExitHistroy[Processor][Index];

		Histroy.GpRegister = *GuestContext->GpRegister;
		Histroy.Ip = GuestContext->Ip;
		Histroy.ExitReason = ExitReason;
		Histroy.ExitQualification = UtilVmRead(VMCS_FIELD::kExitQualification);
		Histroy.InstructionInfo = UtilVmRead(VMCS_FIELD::kVmxInstructionInfo);

		Index++;
		// ����Ѿ���¼�����ˣ���ô��һ����¼���ǵ�һ����¼
		if (Index == kVmmNumberOfRecords)
			Index = 0;
	}

	// switc �˳�ԭ�� - ���д���
	switch (ExitReason.fields.reason)
	{
		case VMX_EXIT_REASON::kExceptionOrNmi:
			VmmHandleException(GuestContext);
			break;
		case VMX_EXIT_REASON::kTripleFault:
			VmmHandleTripleFault(GuestContext);
			break;
		case VMX_EXIT_REASON::kCpuid:
			VmmHandleCpuid(GuestContext);
			break;
		case VMX_EXIT_REASON::kInvd:
			VmmHandleInvalidateInternalCaches(GuestContext);
			break;
		case VMX_EXIT_REASON::kInvlpg:
			VmmHandleInvalidateTlbEntry(GuestContext);
			break;
		case VMX_EXIT_REASON::kRdtsc:
			VmmHandleRdtsc(GuestContext);
			break;
		case VMX_EXIT_REASON::kCrAccess:
			VmmHandleCrAccess(GuestContext);
			break;
		case VMX_EXIT_REASON::kDrAccess:
			VmmpHandleDrAccess(GuestContext);
			break;
		case VMX_EXIT_REASON::kIoInstruction:
			VmmpHandleIoPort(GuestContext);
			break;
		case VMX_EXIT_REASON::kMsrRead:
			VmmpHandleMsrReadAccess(GuestContext);
			break;
		case VMX_EXIT_REASON::kMsrWrite:
			VmmpHandleMsrWriteAccess(GuestContext);
			break;
		case VMX_EXIT_REASON::kMonitorTrapFlag:
			VmmpHandleMonitorTrap(GuestContext);
			break;
		case VMX_EXIT_REASON::kGdtrOrIdtrAccess:
			VmmpHandleGdtrOrIdtrAccess(GuestContext);
			break;
		case VMX_EXIT_REASON::kLdtrOrTrAccess:
			VmmpHandleLdtrOrTrAccess(GuestContext);
			break;
		case VMX_EXIT_REASON::kEptViolation:
			VmmpHandleEptViolation(GuestContext);
			break;
		case VMX_EXIT_REASON::kEptMisconfig:
			VmmpHandleEptMisconfig(GuestContext);
			break;
		case VMX_EXIT_REASON::kVmcall:
			VmmpHandleVmCall(GuestContext);
			break;
		case VMX_EXIT_REASON::kVmclear:
		case VMX_EXIT_REASON::kVmlaunch:
		case VMX_EXIT_REASON::kVmptrld:
		case VMX_EXIT_REASON::kVmptrst:
		case VMX_EXIT_REASON::kVmread:
		case VMX_EXIT_REASON::kVmresume:
		case VMX_EXIT_REASON::kVmwrite:
		case VMX_EXIT_REASON::kVmoff:
		case VMX_EXIT_REASON::kVmon:
			VmmHandleVmx(GuestContext);
			break;
		case VMX_EXIT_REASON::kRdtscp:
			VmmpHandleRdtscp(GuestContext);
			break;
		case VMX_EXIT_REASON::kXsetbv:
			VmmpHandleXsetbv(GuestContext);
			break;
		default:
			VmmpHandleUnexpectedExit(GuestContext);
			break;
	}
}

// VM �������ж�
_Use_decl_annotations_ static void VmmHandleException(GUEST_CONTEXT* GuestContext)
{
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();
	// ��ȡ�ж���Ϣ
	// 3.10.2 ֱ�������¼�����Ϣ�ֶ�
	
	const VM_EXIT_INTERRUPTION_INFORMATION_FIELD VmExitInterruptionInformationField = { static_cast<ULONG32>(UtilVmRead(VMCS_FIELD::kVmExitIntrInfo)) };
	const auto InterruptionType = static_cast<INTERRUPTION_TYPE>(VmExitInterruptionInformationField.field.InterruptionType);
	const auto Vector = static_cast<INTERRUPTION_VECTOR>(VmExitInterruptionInformationField.field.Vector);

	if (InterruptionType == INTERRUPTION_TYPE::kHardwareException)
	{
		// �����Ӳ���ж� - һ��Ҫ�ַ��쳣
		// Ӳ���ն�
		if (Vector == INTERRUPTION_VECTOR::kPageFaultException)
		{
			// #PF �쳣
			const PAGEFAULT_ERROR_CODE FaultCode = { static_cast<ULONG32>(UtilVmRead(VMCS_FIELD::kVmExitIntrErrorCode)) };
			const auto FaultAddress = UtilVmRead(VMCS_FIELD::kExitQualification);

			VmmInjectInterruption(InterruptionType, Vector, true, FaultCode.all);
			MYHYPERPLATFORM_LOG_INFO_SAFE("GuestIp= %016Ix, #PF Fault= %016Ix Code= 0x%2x", GuestContext->Ip, FaultAddress, FaultCode.all);
			// ???
			AsmWriteCR2(FaultAddress);
		}
		else if (Vector == INTERRUPTION_VECTOR::kGeneralProtectionException)
		{
			// #GP
			const auto ErrorCode = static_cast<ULONG32>(UtilVmRead(VMCS_FIELD::kVmExitIntrErrorCode));
			VmmInjectInterruption(InterruptionType, Vector, true, ErrorCode);
			MYHYPERPLATFORM_LOG_INFO_SAFE("GuestIp= %016Ix, #GP Code= 0x%2x", GuestContext->Ip, ErrorCode);
		}
		else
			MYHYPERPLATFORM_COMMON_BUG_CHECK(HYPERPLATFORM_BUG_CHECK::kUnspecified, 0, 0, 0);
	}
	else if (InterruptionType == INTERRUPTION_TYPE::kSoftwareException)
	{
		if (Vector == INTERRUPTION_VECTOR::kBreakpointException)
		{
			// #BP
			VmmInjectInterruption(InterruptionType, Vector, false, 0);
			MYHYPERPLATFORM_LOG_INFO_SAFE("GuestIp = %016Ix, #BP ", GuestContext->Ip);
			UtilVmWrite(VMCS_FIELD::kVmEntryInstructionLen, 1);
		}
		else
			MYHYPERPLATFORM_COMMON_BUG_CHECK(HYPERPLATFORM_BUG_CHECK::kUnspecified, 0, 0, 0);
	}
	else
		MYHYPERPLATFORM_COMMON_BUG_CHECK(HYPERPLATFORM_BUG_CHECK::kUnspecified, 0, 0, 0);
}

// ��ȡ����� �ͻ��˵����� VMCS Field
static void VmmDumpGuestState()
{
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest EsSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestEsSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest CsSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCsSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SsSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSsSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest DsSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestDsSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest FsSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestFsSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest GsSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestGsSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest LdtrSelector = %016Ix", UtilVmRead(VMCS_FIELD::kGuestLdtrSelector));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest TrSelector   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestTrSelector));

	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Ia32Debugctl = %016llx", UtilVmRead64(VMCS_FIELD::kGuestIa32Debugctl));

	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest EsLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestEsLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest CsLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCsLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SsLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSsLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest DsLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestDsLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest FsLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestFsLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest GsLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestGsLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest LdtrLimit    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestLdtrLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest TrLimit      = %016Ix", UtilVmRead(VMCS_FIELD::kGuestTrLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest GdtrLimit    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestGdtrLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest IdtrLimit    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestIdtrLimit));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest EsArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestEsArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest CsArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCsArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SsArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSsArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest DsArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestDsArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest FsArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestFsArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest GsArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestGsArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest LdtrArBytes  = %016Ix", UtilVmRead(VMCS_FIELD::kGuestLdtrArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest TrArBytes    = %016Ix", UtilVmRead(VMCS_FIELD::kGuestTrArBytes));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SysenterCs   = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSysenterCs));

	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Cr0          = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCr0));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Cr3          = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCr3));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Cr4          = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCr4));

	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest EsBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestEsBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest CsBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestCsBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SsBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSsBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest DsBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestDsBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest FsBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestFsBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest GsBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestGsBase));

	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest LdtrBase     = %016Ix", UtilVmRead(VMCS_FIELD::kGuestLdtrBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest TrBase       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestTrBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest GdtrBase     = %016Ix", UtilVmRead(VMCS_FIELD::kGuestGdtrBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest IdtrBase     = %016Ix", UtilVmRead(VMCS_FIELD::kGuestIdtrBase));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Dr7          = %016Ix", UtilVmRead(VMCS_FIELD::kGuestDr7));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Rsp          = %016Ix", UtilVmRead(VMCS_FIELD::kGuestRsp));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Rip          = %016Ix", UtilVmRead(VMCS_FIELD::kGuestRip));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest Rflags       = %016Ix", UtilVmRead(VMCS_FIELD::kGuestRflags));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SysenterEsp  = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSysenterEsp));
	MYHYPERPLATFORM_LOG_DEBUG_SAFE("Guest SysenterEip  = %016Ix", UtilVmRead(VMCS_FIELD::kGuestSysenterEip));
}

// ���� VM's EIP/RIP ����һ��ָ��
_Use_decl_annotations_ static void VmmAdjustGuestInstructionPointer(GUEST_CONTEXT* GuestContext)
{
	// ��ȡ��ǰ�������� VM-exit ָ��ĳ���
	const auto ExitInstructionLength = UtilVmRead(VMCS_FIELD::kVmExitInstructionLen);
	UtilVmWrite(VMCS_FIELD::kGuestRip, GuestContext->Ip + ExitInstructionLength);		// �޸� EIP/RIP

	// ��� TF ��־λ�����ע���ж� #DB
	if (GuestContext->Flag.fields.tf)
	{
		VmmInjectInterruption(INTERRUPTION_TYPE::kHardwareException, INTERRUPTION_VECTOR::kDebugException, false, 0);
		UtilVmWrite(VMCS_FIELD::kVmEntryInstructionLen, ExitInstructionLength);
	}
}

// ��ͻ���ע��һ���ж� - 4.4.3.3 P309 ��VM-entry֮ǰִ��
_Use_decl_annotations_ static void VmmInjectInterruption(INTERRUPTION_TYPE InterruptionType, INTERRUPTION_VECTOR InterruptionVector, bool DeliverErrorCode, ULONG32 ErrorCode)
{
	// http://blog.csdn.net/u013358112/article/details/74530455 ����
	VM_ENTRY_INTERRUPTION_INFORMATION_FIELD VmEntryIntrruptionInformationField = { 0 };
	VmEntryIntrruptionInformationField.fields.Valid = true;
	VmEntryIntrruptionInformationField.fields.InterruptionType = static_cast<ULONG32>(InterruptionType);
	VmEntryIntrruptionInformationField.fields.Vector = static_cast<ULONG32>(InterruptionVector);
	VmEntryIntrruptionInformationField.fields.DeliverErrorType = DeliverErrorCode;

	UtilVmWrite(VMCS_FIELD::kVmEntryIntrInfoField, VmEntryIntrruptionInformationField.all);

	// ����ַ��쳣�� - ��ErrorCodeд��VM-entry
	if (VmEntryIntrruptionInformationField.fields.DeliverErrorType)
		UtilVmWrite(VMCS_FIELD::kVmEntryExceptionErrorCode, ErrorCode);
}

// Triple ���µ� VM-exit
_Use_decl_annotations_ static void VmmHandleTripleFault(GUEST_CONTEXT* GuestContext)
{
	VmmDumpGuestState();
	MYHYPERPLATFORM_COMMON_BUG_CHECK(HYPERPLATFORM_BUG_CHECK::kTripleFaultVmExit, reinterpret_cast<ULONG_PTR>(GuestContext), GuestContext->Ip, 0);
}

// Guest ���� CPUID
_Use_decl_annotations_ static void VmmHandleCpuid(GUEST_CONTEXT* GuestContext)
{
	// �ӹ� VM ���� CPUID
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();
	unsigned int CpuInfo[4] = { 0 };
	const auto FunctionId = static_cast<int>(GuestContext->GpRegister->ax);		// ��ȡ�ͻ��˵��� CPUID ʱ������� ax
	const auto SubFunctionId = static_cast<int>(GuestContext->GpRegister->cx);

	__cpuidex(reinterpret_cast<int*>(CpuInfo), FunctionId, SubFunctionId);
	if (FunctionId == 1)
	{
		// ��ʾ VMM ����ʹ�� HypervisorPresent bit
		CPU_FEATURES_ECX CpuFeatruesEcx = { static_cast<ULONG_PTR>(CpuInfo[2]) };
		CpuFeatruesEcx.fields.not_used = true;
		CpuInfo[2] = static_cast<int>(CpuFeatruesEcx.all);
	}
	else if (FunctionId == HyperVCpuidInterface)
		CpuInfo[0] = 'AazZ';		// ��ѯ HyperplatForm �Ƿ����

	// д��ִ�н��
	GuestContext->GpRegister->ax = CpuInfo[0];
	GuestContext->GpRegister->bx = CpuInfo[1];
	GuestContext->GpRegister->cx = CpuInfo[2];
	GuestContext->GpRegister->dx = CpuInfo[3];
	// ���� VM eip/rip
	VmmAdjustGuestInstructionPointer(GuestContext);
}

// ˢ��CPU���û���
_Use_decl_annotations_ static void VmmHandleInvalidateInternalCaches(GUEST_CONTEXT* GuestContext)
{
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();
	AsmInvalidateInternalCaches();
	VmmAdjustGuestInstructionPointer(GuestContext);
}

// ˢ��һ��ҳ����ת�������еļ�¼
_Use_decl_annotations_ static void VmmHandleInvalidateTlbEntry(GUEST_CONTEXT* GuestContext)
{
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();

	const auto InvalidateAddress = reinterpret_cast<void*>(UtilVmRead(VMCS_FIELD::kExitQualification));
	__invlpg(InvalidateAddress);
	// ִ��ˢ��
	UtilInvvpidIndividualAddress(static_cast<USHORT>(KeGetCurrentProcessorNumberEx(nullptr) + 1), InvalidateAddress);
	VmmAdjustGuestInstructionPointer(GuestContext);
}

// ִ�� RDTSC
_Use_decl_annotations_ static void VmmHandleRdtsc(GUEST_CONTEXT* GuestContext)
{
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();
	ULARGE_INTEGER Tsc = { 0 };
	Tsc.QuadPart = __rdtsc();		// �õ�������ʱ���

	GuestContext->GpRegister->dx = Tsc.HighPart;
	GuestContext->GpRegister->ax = Tsc.LowPart;

	VmmAdjustGuestInstructionPointer(GuestContext);
}

// VM ���Է��� CRx
_Use_decl_annotations_ static void VmmHandleCrAccess(GUEST_CONTEXT* GuestContext)
{
	MYHYPERPLATFORM_PERFORMANCE_MEASURE_THIS_SCOPE();

	const CR_ACCESS_QUALIFICATION ExitQualification = { UtilVmRead(VMCS_FIELD::kExitQualification) };

	const auto RegisterUsed = VmmSelectRegister(ExitQualification.fields.GpRegister, GuestContext);
}

// ѡ��һ���Ĵ���
_Use_decl_annotations_ static ULONG_PTR* VmmSelectRegister(ULONG Index, GUEST_CONTEXT* GuestContext)
{
	ULONG_PTR* RegisterUsed = nullptr;

	switch (Index)
	{
		case 0: 
			RegisterUsed = &GuestContext->GpRegister->ax; break;
		case 1: 
			RegisterUsed = &GuestContext->GpRegister->cx; break;
		case 2: 
			RegisterUsed = &GuestContext->GpRegister->dx; break;
		case 3: 
			RegisterUsed = &GuestContext->GpRegister->bx; break;
		case 4: 
			RegisterUsed = &GuestContext->GpRegister->sp; break;
		case 5: 
			RegisterUsed = &GuestContext->GpRegister->bp; break;
		case 6: 
			RegisterUsed = &GuestContext->GpRegister->si; break;
		case 7: 
			RegisterUsed = &GuestContext->GpRegister->di; break;
#if defined(_AMD64_)
		case 8: 
			RegisterUsed = &GuestContext->GpRegister->r8; break;
		case 9: 
			RegisterUsed = &GuestContext->GpRegister->r9; break;
		case 10: 
			RegisterUsed = &GuestContext->GpRegister->r10; break;
		case 11: 
			RegisterUsed = &GuestContext->GpRegister->r11; break;
		case 12: 
			RegisterUsed = &GuestContext->GpRegister->r12; break;
		case 13: 
			RegisterUsed = &GuestContext->GpRegister->r13; break;
		case 14: 
			RegisterUsed = &GuestContext->GpRegister->r14; break;
		case 15: 
			RegisterUsed = &GuestContext->GpRegister->r15; break;
#endif
		default: 
			MYHYPERPLATFORM_COMMON_DBG_BREAK(); break;
	}

	return RegisterUsed;
}

EXTERN_C_END



