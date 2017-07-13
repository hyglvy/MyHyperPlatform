#pragma once

#include <fltKernel.h>

// ������ִ�VMM(�������س���)�������ǵ�ǩ��ͨ��CPUID ʹ����� FunctionCode ֤�����ǵĴ���
static const ULONG32 HyperVCpuidInterface = 0x40000002;

/// See: CONTROL REGISTERS
union CR4 
{
	ULONG_PTR all;
	struct 
	{
		unsigned vme : 1;         //!< [0] Virtual Mode Extensions
		unsigned pvi : 1;         //!< [1] Protected-Mode Virtual Interrupts
		unsigned tsd : 1;         //!< [2] Time Stamp Disable
		unsigned de : 1;          //!< [3] Debugging Extensions
		unsigned pse : 1;         //!< [4] Page Size Extensions
		unsigned pae : 1;         //!< [5] Physical Address Extension
		unsigned mce : 1;         //!< [6] Machine-Check Enable
		unsigned pge : 1;         //!< [7] Page Global Enable
		unsigned pce : 1;         //!< [8] Performance-Monitoring Counter Enable
		unsigned osfxsr : 1;      //!< [9] OS Support for FXSAVE/FXRSTOR
		unsigned osxmmexcpt : 1;  //!< [10] OS Support for Unmasked SIMD Exceptions
		unsigned reserved1 : 2;   //!< [11:12]
		unsigned vmxe : 1;        //!< [13] Virtual Machine Extensions Enabled
		unsigned smxe : 1;        //!< [14] SMX-Enable Bit
		unsigned reserved2 : 2;   //!< [15:16]
		unsigned pcide : 1;       //!< [17] PCID Enable
		unsigned osxsave : 1;  //!< [18] XSAVE and Processor Extended States-Enable
		unsigned reserved3 : 1;  //!< [19]
		unsigned smep : 1;  //!< [20] Supervisor Mode Execution Protection Enable
		unsigned smap : 1;  //!< [21] Supervisor Mode Access Protection Enable
	} fields;
};

/// See: BASIC VMX INFORMATION
union IA32_VMX_BASIC_MSR {
	unsigned __int64 all;
	struct {
		unsigned revision_identifier : 31;    //!< [0:30]
		unsigned reserved1 : 1;               //!< [31]
		unsigned region_size : 12;            //!< [32:43]
		unsigned region_clear : 1;            //!< [44]
		unsigned reserved2 : 3;               //!< [45:47]
		unsigned supported_ia64 : 1;          //!< [48]
		unsigned supported_dual_moniter : 1;  //!< [49]
		unsigned memory_type : 4;             //!< [50:53]
		unsigned vm_exit_report : 1;          //!< [54]
		unsigned vmx_capability_hint : 1;     //!< [55]
		unsigned reserved3 : 8;               //!< [56:63]
	} fields;
};
static_assert(sizeof(IA32_VMX_BASIC_MSR) == 8, "Size check");

/// See: Feature Information Returned in the ECX Register - intel �ֲ� 776
union CPU_FEATURES_ECX {
	ULONG32 all;
	struct {
		ULONG32 sse3 : 1;       //!< [0] Streaming SIMD Extensions 3 (SSE3)
		ULONG32 pclmulqdq : 1;  //!< [1] PCLMULQDQ
		ULONG32 dtes64 : 1;     //!< [2] 64-bit DS Area
		ULONG32 monitor : 1;    //!< [3] MONITOR/WAIT
		ULONG32 ds_cpl : 1;     //!< [4] CPL qualified Debug Store
		ULONG32 vmx : 1;        //!< [5] Virtual Machine Technology
		ULONG32 smx : 1;        //!< [6] Safer Mode Extensions
		ULONG32 est : 1;        //!< [7] Enhanced Intel Speedstep Technology
		ULONG32 tm2 : 1;        //!< [8] Thermal monitor 2
		ULONG32 ssse3 : 1;      //!< [9] Supplemental Streaming SIMD Extensions 3
		ULONG32 cid : 1;        //!< [10] L1 context ID
		ULONG32 sdbg : 1;       //!< [11] IA32_DEBUG_INTERFACE MSR
		ULONG32 fma : 1;        //!< [12] FMA extensions using YMM state
		ULONG32 cx16 : 1;       //!< [13] CMPXCHG16B
		ULONG32 xtpr : 1;       //!< [14] xTPR Update Control
		ULONG32 pdcm : 1;       //!< [15] Performance/Debug capability MSR
		ULONG32 reserved : 1;   //!< [16] Reserved
		ULONG32 pcid : 1;       //!< [17] Process-context identifiers
		ULONG32 dca : 1;        //!< [18] prefetch from a memory mapped device
		ULONG32 sse4_1 : 1;     //!< [19] SSE4.1
		ULONG32 sse4_2 : 1;     //!< [20] SSE4.2
		ULONG32 x2_apic : 1;    //!< [21] x2APIC feature
		ULONG32 movbe : 1;      //!< [22] MOVBE instruction
		ULONG32 popcnt : 1;     //!< [23] POPCNT instruction
		ULONG32 reserved3 : 1;  //!< [24] one-shot operation using a TSC deadline
		ULONG32 aes : 1;        //!< [25] AESNI instruction
		ULONG32 xsave : 1;      //!< [26] XSAVE/XRSTOR feature
		ULONG32 osxsave : 1;    //!< [27] enable XSETBV/XGETBV instructions
		ULONG32 avx : 1;        //!< [28] AVX instruction extensions
		ULONG32 f16c : 1;       //!< [29] 16-bit floating-point conversion
		ULONG32 rdrand : 1;     //!< [30] RDRAND instruction
		ULONG32 not_used : 1;   //!< [31] Always 0 (a.k.a. HypervisorPresent)
	} fields;
};
static_assert(sizeof(CPU_FEATURES_ECX) == 4, "Size check");

/// See: MODEL-SPECIFIC REGISTERS (MSRS)
enum class MSR : unsigned int {
	kIa32ApicBase = 0x01B,

	kIa32FeatureControl = 0x03A,

	kIa32SysenterCs = 0x174,
	kIa32SysenterEsp = 0x175,
	kIa32SysenterEip = 0x176,

	kIa32Debugctl = 0x1D9,

	kIa32MtrrCap = 0xFE,
	kIa32MtrrDefType = 0x2FF,
	kIa32MtrrPhysBaseN = 0x200,
	kIa32MtrrPhysMaskN = 0x201,
	kIa32MtrrFix64k00000 = 0x250,
	kIa32MtrrFix16k80000 = 0x258,
	kIa32MtrrFix16kA0000 = 0x259,
	kIa32MtrrFix4kC0000 = 0x268,
	kIa32MtrrFix4kC8000 = 0x269,
	kIa32MtrrFix4kD0000 = 0x26A,
	kIa32MtrrFix4kD8000 = 0x26B,
	kIa32MtrrFix4kE0000 = 0x26C,
	kIa32MtrrFix4kE8000 = 0x26D,
	kIa32MtrrFix4kF0000 = 0x26E,
	kIa32MtrrFix4kF8000 = 0x26F,

	kIa32VmxBasic = 0x480,
	kIa32VmxPinbasedCtls = 0x481,
	kIa32VmxProcBasedCtls = 0x482,
	kIa32VmxExitCtls = 0x483,
	kIa32VmxEntryCtls = 0x484,
	kIa32VmxMisc = 0x485,
	kIa32VmxCr0Fixed0 = 0x486,
	kIa32VmxCr0Fixed1 = 0x487,
	kIa32VmxCr4Fixed0 = 0x488,
	kIa32VmxCr4Fixed1 = 0x489,
	kIa32VmxVmcsEnum = 0x48A,
	kIa32VmxProcBasedCtls2 = 0x48B,
	kIa32VmxEptVpidCap = 0x48C,
	kIa32VmxTruePinbasedCtls = 0x48D,
	kIa32VmxTrueProcBasedCtls = 0x48E,
	kIa32VmxTrueExitCtls = 0x48F,
	kIa32VmxTrueEntryCtls = 0x490,
	kIa32VmxVmfunc = 0x491,

	kIa32Efer = 0xC0000080,
	kIa32Star = 0xC0000081,
	kIa32Lstar = 0xC0000082,

	kIa32Fmask = 0xC0000084,

	kIa32FsBase = 0xC0000100,
	kIa32GsBase = 0xC0000101,
	kIa32KernelGsBase = 0xC0000102,
	kIa32TscAux = 0xC0000103,
};

// MemoryType ������VMCS�б�����ʹ�õ�PAT�ڴ����ͺ���ص���������
enum class MemoryType : unsigned __int8
{
	kUncacheable = 0,
	kWriteCombining = 1,
	kWriteThrough = 4,
	kWriteProtected = 5,
	kWriteBack = 6,
	kUncached = 7
};

/// See: ARCHITECTURAL MSRS
union IA32_FEATURE_CONTROL_MSR
{
	unsigned __int64 all;
	struct {
		unsigned lock : 1;                  //!< [0]
		unsigned enable_smx : 1;            //!< [1]
		unsigned enable_vmxon : 1;          //!< [2]
		unsigned reserved1 : 5;             //!< [3:7]
		unsigned enable_local_senter : 7;   //!< [8:14]
		unsigned enable_global_senter : 1;  //!< [15]
		unsigned reserved2 : 16;            //!<
		unsigned reserved3 : 32;            //!< [16:63]
	} fields;
};
static_assert(sizeof(IA32_FEATURE_CONTROL_MSR) == 8, "Size check");

/// See: VPID AND EPT CAPABILITIES
union IA32_VMX_EPT_VPID_CAP {
	unsigned __int64 all;
	struct {
		unsigned support_execute_only_pages : 1;                        //!< [0]
		unsigned reserved1 : 5;                                         //!< [1:5]
		unsigned support_page_walk_length4 : 1;                         //!< [6]		�Ƿ�֧��4��ҳ��
		unsigned reserved2 : 1;                                         //!< [7]
		unsigned support_uncacheble_memory_type : 1;                    //!< [8]
		unsigned reserved3 : 5;                                         //!< [9:13]
		unsigned support_write_back_memory_type : 1;                    //!< [14]
		unsigned reserved4 : 1;                                         //!< [15]
		unsigned support_pde_2mb_pages : 1;                             //!< [16]
		unsigned support_pdpte_1_gb_pages : 1;                          //!< [17]
		unsigned reserved5 : 2;                                         //!< [18:19]
		unsigned support_invept : 1;                                    //!< [20]
		unsigned support_accessed_and_dirty_flag : 1;                   //!< [21]
		unsigned reserved6 : 3;                                         //!< [22:24]
		unsigned support_single_context_invept : 1;                     //!< [25]
		unsigned support_all_context_invept : 1;                        //!< [26]
		unsigned reserved7 : 5;                                         //!< [27:31]
		unsigned support_invvpid : 1;                                   //!< [32]
		unsigned reserved8 : 7;                                         //!< [33:39]
		unsigned support_individual_address_invvpid : 1;                //!< [40]
		unsigned support_single_context_invvpid : 1;                    //!< [41]
		unsigned support_all_context_invvpid : 1;                       //!< [42]
		unsigned support_single_context_retaining_globals_invvpid : 1;  //!< [43]
		unsigned reserved9 : 20;                                        //!< [44:63]
	} fields;
};
static_assert(sizeof(IA32_VMX_EPT_VPID_CAP) == 8, "Size check");

/// See: IA32_MTRRCAP Register
// MTRR ���� - ȷ��ϵͳ�ڴ�һ�������ڴ������
// MTRR ��������96���ڴ淶Χ�������ڴ�Ķ��塣��������һϵ�е�MSRs����Щ�Ĵ����ֱ�ȥ˵��MSR�����а���������ڴ�ľ������͡�
// �ڴ������Ѿ������� 5 ����
union IA32_MTRR_CAPABILITIES_MSR {
	ULONG64 all;
	struct {
		ULONG64 variable_range_count : 8;   //<! [0:7] VCNT ��ʾ8������ ָʾ8��MTRRs�ķ�Χ��
		ULONG64 fixed_range_supported : 1;  //<! [8]   ������1���̶�MTRRs�ķ�Χ
		ULONG64 reserved : 1;               //<! [9]   
		ULONG64 write_combining : 1;        //<! [10]  �Ƿ�֧��WC����
		ULONG64 smrr : 1;                   //<! [11]
	} fields;
};
static_assert(sizeof(IA32_MTRR_CAPABILITIES_MSR) == 8, "Size check");

/// See: IA32_MTRR_DEF_TYPE MSR - �趨����MTRRs�����������ڴ������Ĭ������
union IA32_MTRR_DEFAULT_TYPE_MSR {
	ULONG64 all;
	struct {
		ULONG64 default_mtemory_type : 8;  //<! [0:7] Ĭ���ڴ����� - ���ͱ����8�ֽڵ�
		ULONG64 reserved : 2;              //<! [8:9] 
		ULONG64 fixed_mtrrs_enabled : 1;   //<! [10]  FE - �̶���ΧMTRRs enabled
		ULONG64 mtrrs_enabled : 1;         //<! [11]  E - MTRRs�Ƿ����á�����һλΪ0ʱ������MTRRs�����ã�����UC�ڴ��������������е������ڴ档
	} fields;
};
static_assert(sizeof(IA32_MTRR_DEFAULT_TYPE_MSR) == 8, "Size check");
// ���������ṹ��������� - MTRRs һ���д���MTRs fixed �� variable��fixed��������һ��ȷ����Χ�ڵ��ڴ����ͣ�variable����һ�οɱ䷶Χ���ڴ����͡�fixed���������ȼ�����variable.
//  ���һ���ڴ棬�������̶�����Ҳ�����ɱ���������ô������Ĭ�����͡�

/// See: Fixed Range MTRRs
// FixedMemoryRanges�ܹ���11��64λ��FixedRangeMsr�Ĵ���������ӳ��ġ���Щ�Ĵ������Ǳ���Ϊ8bits��������������Ӧ���ڴ�ε��ڴ����͡�
// IA32_MTRR_FIX64K_00000 - ӳ��� 0H ~ 7FFFFH ��512Kbyte�ĵ�ַ��Χ���˷�Χ����Ϊ8��64Kbyte�����䡣
// IA32_MTRR_FIX16K_80000 IA32_MTRR_FIX16K_A0000 ӳ��2��128Kbyte�ĵ�ַ��Χ�� 0x80000 ~ 0xBFFFF �˶˱�����Ϊ16��16Kbyte�������䣬ÿ���Ĵ�����8����Χ��(һ���Ĵ���64λ��Ҫ��8λ��ָ��һ���ڴ����ͣ�����һ���Ĵ���ֻ������8��)
// IA32_MTRR_FIX4K_C0000 IA32_MTRR_FIX4K_F8000 ӳ��8��32Kbyte�ĵ�ַ��Χ���˷�Χ��Ϊ64��4kb������ (��ͬԭ��
union IA32_MTRR_FIXED_RANGE_MSR {
	ULONG64 all;
	struct {
		UCHAR types[8];
	} fields;
};
static_assert(sizeof(IA32_MTRR_FIXED_RANGE_MSR) == 8, "Size check");

/// See: IA32_MTRR_PHYSBASEn and IA32_MTRR_PHYSMASKn Variable-Range Register
// Variable-Range �����ַ�Ĵ����ǳɶԳ��ֵ� һ����������ַ һ��������Χ����
union IA32_MTRR_PHYSICAL_BASE_MSR
{
	ULONG64 all;
	struct {
		ULONG64 type : 8;        //!< [0:7]
		ULONG64 reserved : 4;    //!< [8:11]
		ULONG64 phys_base : 36;  //!< [12:MAXPHYADDR]
	} fields;
};
static_assert(sizeof(IA32_MTRR_PHYSICAL_BASE_MSR) == 8, "Size check");

/// See: IA32_MTRR_PHYSBASEn and IA32_MTRR_PHYSMASKn Variable-Range Register
union IA32_MTRR_PHYSICAL_MASK_MSR 
{
	ULONG64 all;
	struct {
		ULONG64 reserved : 11;   //!< [0:10]
		ULONG64 valid : 1;       //!< [11]				�Ƿ�����ʹ��
		ULONG64 phys_mask : 36;  //!< [12:MAXPHYADDR]	PhysicalBase & PhysicalMask = PhysicalMask & AddressWithinRange
	} fields;
};
static_assert(sizeof(IA32_MTRR_PHYSICAL_MASK_MSR) == 8, "Size check");

