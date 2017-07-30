#pragma once

#include "ia32_type.h"

EXTERN_C_START

// ��VM��ʼ����һ���װ
// @param VmInitializationRoutine ����VMX-mode����ں���
// @param Context Context����
// @return true ������ִ�гɹ�
bool _stdcall AsmInitializeVm(_In_ void(*VmInitializationRoutine)(_In_ ULONG_PTR, _In_ ULONG_PTR, _In_opt_ void*), _In_opt_ void* Context);

// ���� ����Ȩ�� λ
// @param SegmentSelector ��Ҫ�õ�����Ȩ�޵Ķ�ѡ���
// @return ����Ȩ��λ
ULONG_PTR __stdcall AsmLoadAccessRightsByte(_In_ ULONG_PTR SegmentSelector);

// GDT ��д����
void __stdcall AsmReadGDT(_Out_ GDTR* Gdtr);
void __stdcall AsmWriteGDT(_In_ GDTR* Gdtr);

// ��ȡ SGDT
inline void __lgdt(_In_ void* gdtr)
{
	AsmWriteGDT(static_cast<GDTR*>(gdtr));
}


//  �������VMXģʽ
// @param VmsSupportPhysicalAddress  64λ�� VMXON �����ַ
// @return Equivalent to #VmxStatus
inline unsigned char __vmx_on(_In_ unsigned __int64 *VmsSupportPhysicalAddress) 
{
	// 2.6.6.1 VMXON ָ��
	FLAG_REGISTER FlagRegister = {};
	PHYSICAL_ADDRESS PhysicalAddress = {};
	PhysicalAddress.QuadPart = *VmsSupportPhysicalAddress;
	
	__asm 
	{
		push PhysicalAddress.HighPart
		push PhysicalAddress.LowPart

		// _emit ָ�����Ӳ����д�� F3 0F C7 34 24
		_emit  0xF3
		_emit  0x0F
		_emit  0xC7
		_emit  0x34
		_emit  0x24  // VMXON [ESP] | ������������push������

		pushfd
		pop FlagRegister.all
		add esp, 8
	}

	// �жϿ����Ƿ�ɹ�
	if (FlagRegister.fields.cf) 
		return 2;
	
	if (FlagRegister.fields.zf) 
		return 1;
	
	return 0;
}

// ��ʼ�� VMCS ���� ������ VMCS ���� launch state ֵΪ clear 
// @param VmcsPhysicalAddress VMCS �����ַ
// @return VMX_STATUS
inline unsigned  char __vmx_vmclear(_In_ unsigned __int64* VmcsPhysicalAddress)
{
	FLAG_REGISTER FlagRegister = { 0 };
	PHYSICAL_ADDRESS PhysicalAddress = { 0 };

	PhysicalAddress.QuadPart = *VmcsPhysicalAddress;

	_asm
	{
		push PhysicalAddress.HighPart;
		push PhysicalAddress.LowPart;

		// �ƺ�����ֱ��д���ָ��
		_emit 0x66
		_emit 0x0F
		_emit 0xc7
		_emit 0x34
		_emit 0x24  // VMCLEAR [ESP]

		// ȡ�� EFlag ��ֵ �����Լ��ı���
		pushfd		
		pop FlagRegister.all

		add esp, 8
	}

	if (FlagRegister.fields.cf)
		return 2;

	if (FlagRegister.fields.zf)
		return 1;

	return 0;
}

// 2.6.5.1 VMCS ����ָ�� - VMPTRLD ָ��
//  ����һ��64λ�������ַ����Ϊ��ǰ current-VMCS pointer 
//  ���� VMXON VMPRTLD VMCLEAR ����ָ���ʹ�� �ڲ�ά���� VMCS ָ��
inline unsigned char __vmx_vmptrld(_In_ unsigned __int64* VmcsPhysicalAddress)
{
	FLAG_REGISTER FlagRegitser = { 0 };
	PHYSICAL_ADDRESS PhysicalAddress = { 0 };

	PhysicalAddress.QuadPart = *VmcsPhysicalAddress;

	_asm
	{
		push PhysicalAddress.HighPart;
		push PhysicalAddress.LowPart;

		_emit 0x0F;
		_emit 0xC7;
		_emit 0x34;
		_emit 0x24;	// VMPTRLD [esp]

		pushfd
		pop FlagRegitser.all

		add esp, 8
	}

	if (FlagRegitser.fields.cf)
		return 2;

	if (FlagRegitser.fields.zf)
		return 1;

	return 0;
}

// ��ȡ GDT
inline void __sgdt(_Out_ void* Gdtr)
{
	AsmReadGDT(static_cast<GDTR*>(Gdtr));
}

// д�� GDT
inline void __igdt(_In_ void* Gdtr)
{
	AsmWriteGDT(static_cast<GDTR*>(Gdtr));
}

// д���ض�ֵ����ǰVMCS�ض�������
inline unsigned char __vmx_vmwrite(_In_ size_t Field, _In_ size_t FieldValue)
{
	FLAG_REGISTER Flags = { 0 };
	__asm
	{
		pushad
		push FieldValue
		mov eax, Field

		_emit 0x0F
		_emit 0x79
		_emit 0x04
		_emit 0x24	// VMWRITE EAX, [ESP]

		pushfd
		pop Flags

		add esp, 4
		popad
	}

	if (Flags.fields.cf)
		return 2;

	if (Flags.fields.zf)
		return 1;

	return 0;
}

// Reads a specified field from the current VMCS
// @param Field  The VMCS field to read
// @param FieldValue  A pointer to the location to store the value read from the VMCS field specified by the Field parameter
// @return Equivalent to #VmxStatus
inline unsigned char __vmx_vmread(_In_ size_t Field, _Out_ size_t *FieldValue) 
{
	FLAG_REGISTER Flags = { 0 };

	__asm 
	{
		pushad
		mov eax, Field

		_emit 0x0F
		_emit 0x78
		_emit 0xC3  // VMREAD  EBX, EAX

		pushfd
		pop Flags.all

		mov eax, FieldValue
		mov[eax], ebx
		popad
	}

	if (Flags.fields.cf) 
		return 2;
	
	if (Flags.fields.zf) 
		return 1;
	
	return 0;
}

// Places the calling application in VMX non-root operation state (VM enter)
// @return Equivalent to #VmxStatus
inline unsigned char __vmx_vmlaunch() 
{
	FLAG_REGISTER Flags = { 0 };

	__asm 
	{
		_emit 0x0f
		_emit 0x01
		_emit 0xc2  // VMLAUNCH

		pushfd
		pop Flags.all
	}

	if (Flags.fields.cf)
		return 2;
	
	if (Flags.fields.zf)
		return 1;
	
	return 0;
}

// д�� CR2
void __stdcall AsmWriteCR2(_In_ ULONG_PTR Cr2Value);

// ˢ�� EPT ת������ - ִ�� INVEPT ָ��
// @param InveptType INVEPTָ��ִ������
// @param INV_EPT_DESCRIPTOR ������
unsigned char __stdcall AsmInvept(_In_ INV_EPT_TYPE InveptType, _In_ const INV_EPT_DESCRIPTOR* InveptDescriptor);

// ˢ�� LineAddress -> HPA ת������
// @param InvVpidType ת������
// @param InvVpidDescriptor  ת��������
unsigned char __stdcall AsmInvvpid(_In_ INV_VPID_TYPE InvVpidType, _In_ const INV_VPID_DESCRIPTOR* InvVpidDescriptor);

USHORT __stdcall AsmReadES();

USHORT __stdcall AsmReadCS();

USHORT __stdcall AsmReadSS();

USHORT __stdcall AsmReadDS();

USHORT __stdcall AsmReadFS();

USHORT __stdcall AsmReadGS();

USHORT __stdcall AsmReadLDTR();

USHORT __stdcall AsmReadTR();

// VMM ��� - ������ VM-exit ���к���
void __stdcall AsmVmmEntryPoint();

// ִ�� VMCALL
// @param HypercallNumber һ��HypercallNumber
// @param Context VMCALL ������ 
unsigned char __stdcall AsmVmxCall(_In_ ULONG_PTR HypercallNumber, _In_opt_ void* Context);

// ˢ�� CPU ���û���
void __stdcall AsmInvalidateInternalCaches();

EXTERN_C_END