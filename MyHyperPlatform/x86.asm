.686P
.model flat, stdcall
.mmx
.xmm

; export functions to use in asm
extern VmmVmExitHandler@4	  : PROC
extern VmmVmxFailureHandler@4 : PROC
extern UtilDumpGpRegisters@8  : PROC


.const	; ����ֵ�趨
VMX_OK					 EQU 0
VMX_ERROR_WITH_STATUS    EQU 1
VMX_ERROR_WITHOUT_STATUS EQU 2

; macro
; ��������ͨ�üĴ����ͱ�־�Ĵ��� - �����мĴ���ֵ����ջ��
ASM_DUMP_REGISTERS  MACRO
	pushfd			    ; pushn EFlags
	pushad				; -4 * 8	Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, and EDI
			
	mov ecx, esp		;
	mov edx, esp
	add edx, 4 * 9	    ; esp ��ԭ

	push ecx
	push edx
	
	; ����Ĵ�����ֵ
	call UtilDumpGpRegisters@8	; UtilDumpGpRegisters(all_regs, stack_pointer);
		
	popad
	popfd
ENDM

.code
; ���⻯��ʼ������
; bool __stdcall AsmInitializeVm(_In_ void (*vm_initialization_routine)(_In_ ULONG_PTR, _In_ ULONG_PTR,  _In_opt_ void *), _In_opt_ void *context);
AsmInitializeVm PROC VmInitializationRoutine, Context

	pushfd
	pushad

	mov ecx, esp		; esp

	; ����VmInitializationRoutine 
	push Context
	push AsmResumeVm
	push ecx
	call VmInitializationRoutine	;  VmInitializationRoutine(esp, AsmResumeVm, Context)

	popad
	popfd

	xor eax, eax
	ret

AsmResumeVm:
	nop
	popad
	popfd

	ASM_DUMP_REGISTERS

	xor eax, eax
	inc eax
	ret

AsmInitializeVm	ENDP

; unsigned char __stdcall AsmVmxCall(_In_ ULONG_PTR HypercallNumber, _In_opt_ void* Context);
AsmVmxCall PROC HypercallNumber, Context
	mov ecx, HypercallNumber
	mov edx, Context
	vmcall ; vmcall HypercallNumber, Context

	jz ErrorWithCode
	jc ErrorWithOutCode

	xor eax, eax
	ret

ErrorWithCode:
	mov eax, VMX_ERROR_WITH_STATUS
	ret

ErrorWithOutCode:
	mov eax, VMX_ERROR_WITHOUT_STATUS
	ret
AsmVmxCall ENDP

;	unsigned char __stdcall AsmInvept(_In_ INV_EPT_TYPE InveptType, _In_ const INV_EPT_DESCRIPTOR* InveptDescriptor);
AsmInvept PROC InveptType, InveptDescriptor
	mov ecx, InveptType
	mov edx, InveptDescriptor

	; invept ecx, dword ptr [edx]
	db 66h, 0fh, 38h, 80h, 0ah
	jz ErrorWithCode		; �ж��Ƿ�ɹ� ZF CF
	jc ErrorWithOutCode
	xor eax, eax		
	ret

ErrorWithOutCode:
	mov eax, VMX_ERROR_WITHOUT_STATUS
	ret

ErrorWithCode:
	mov eax, VMX_ERROR_WITHOUT_STATUS
	ret
AsmInvept ENDP

; unsigned char __stdcall AsmInvvpit(_In_ INV_VPID_TYPE InvvpidType, _In_ const INV_VPID_DESCRIPTOR* InvVpidDescriptor);
AsmInvvpid PROC InvVpidType, InvVpidDescriptor
	mov ecx, InvVpidType
	mov edx, InvVpidDescriptor

	; invvpid ecx, dword ptr [edx]
	db  66h, 0fh, 38h, 81h, 0ah
	jz ErrorWithCode
	jc ErrorWithOutCode
	xor eax, eax
	ret

ErrorWithOutCode:
	mov eax, VMX_ERROR_WITHOUT_STATUS
	ret

ErrorWithCode:
	mov eax, VMX_ERROR_WITH_STATUS
	ret

AsmInvvpid ENDP	

; void __stdcall AsmReadGDT(_Out_ GDTR* Gdtr);
AsmWriteGDT PROC Gdtr
    mov ecx, Gdtr
    lgdt fword ptr [ecx]
    ret
AsmWriteGDT ENDP

; void __stdcall AsmWriteGDT(_In_ GDTR* Gdtr)
AsmReadGDT PROC Gdtr
    mov ecx, Gdtr
    sgdt [ecx]
    ret
AsmReadGDT ENDP

; �Ĵ�����д����

; USHORT __stdcall AsmReadES();
AsmReadES PROC
	mov ax, es
	ret
AsmReadES ENDP

; USHORT __stdcall AsmReadCS();
AsmReadCS PROC
	mov ax, cs
	ret
AsmReadCS ENDP

; USHORT __stdcall AsmReadSS();
AsmReadSS PROC
	mov ax, ss
	ret
AsmReadSS ENDP

; USHORT __stdcall AsmReadDS();
AsmReadDS PROC
	mov ax, ds
	ret
AsmReadDS ENDP

; USHORT __stdcall AsmReadFS();
AsmReadFS PROC
	mov ax, fs
	ret
AsmReadFS ENDP

; USHORT __stdcall AsmReadGS();
AsmReadGS PROC
    mov ax, gs
    ret
AsmReadGS ENDP

; USHORT __stdcall AsmReadLDTR();
; intel �ֲ� P1748
AsmReadLDTR PROC
    sldt ax
    ret
AsmReadLDTR ENDP

; USHORT __stdcall AsmReadTR();
AsmReadTR PROC
    str ax
    ret
AsmReadTR ENDP

; ULONG_PTR __stdcall AsmLoadAccessRightsByte(_In_ ULONG_PTR SegmentSelector);
AsmLoadAccessRightsByte PROC SegmentSelector
	mov ecx, SegmentSelector
	lar eax, ecx			; intel �ֲ� P1085 - ���ݶ������� ���ؼ���Ȩ��λ
	ret
AsmLoadAccessRightsByte ENDP

; void __stdcall AsmVmmEntryPoint();
AsmVmmEntryPoint PROC
	pushad	; esp - 4 * 8
	mov eax, esp

	; ���� volatile XMM �Ĵ���
	sub esp, 68h	; +8 ���� 
	mov ecx, cr0
	mov edx, ecx	; ����ԭ��CR0 
	and cl, 0f1h	; ��� MP, EM TS bit (����Ĵ�������Ȩ��)
	mov cr0, ecx
	; ���渡��Ĵ��� ???
	movaps xmmword ptr [esp +  0h], xmm0
    movaps xmmword ptr [esp + 10h], xmm1
    movaps xmmword ptr [esp + 20h], xmm2
    movaps xmmword ptr [esp + 30h], xmm3
    movaps xmmword ptr [esp + 40h], xmm4
    movaps xmmword ptr [esp + 50h], xmm5
	mov cr0, edx	; ��ԭ CR0 ֵ

	push eax	; esp ֵ
	call VmmVmExitHandler@4	; VmmVmExitHandler(GuestContext);

	; �ظ� XMM �Ĵ���
	mov ecx, cr0
	mov edx, ecx
	and cl, 0f1h	; ��շ��ʿ���λ
	mov cr0, ecx            
	; ȡ�� XMM �Ĵ���ֵ
    movaps xmm0, xmmword ptr [esp +  0h]
    movaps xmm1, xmmword ptr [esp + 10h]
    movaps xmm2, xmmword ptr [esp + 20h]
    movaps xmm3, xmmword ptr [esp + 30h]
    movaps xmm4, xmmword ptr [esp + 40h]
    movaps xmm5, xmmword ptr [esp + 50h]
    mov cr0, edx            
    add esp, 68h     

	; ��� VmmVmExitHandler ����ֵ
	test al, al
	jz ExitVm

	popad
	vmresume
	jmp VmxError

ExitVm:
	; ִ�� vmxoff ָ�� �ر����⻯
	popad
	vmxoff	; ִ����ɺ�
	;   eax = Guest's eflags
    ;   edx = Guest's esp
    ;   ecx = Guest's eip for the next instruction 
	; ��� ZF CF
	jz VmxError	
	jc VmxError

	push eax
	popfd			; eflags = guest's eflags
	mov esp, edx	; esp = guest's esp
	push ecx		; ecx = guest's eip for the next instruction
	ret				; ִ����һ��ָ��

VmxError:
	; ���������ԭ��
	pushfd
	pushad
	mov ecx, esp
	push ecx		; stack: eflags, common reg, esp 
	call VmmVmxFailureHandler@4 ; VmmVmxFailurehandler(AllRegs);
	int 3	; �ն�
AsmVmmEntryPoint ENDP

END