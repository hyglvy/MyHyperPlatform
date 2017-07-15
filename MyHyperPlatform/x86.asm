.686P
.model flat, stdcall
.mmx
.xmm

; export functions to use in asm
extern UtilDumpGpRegisters@8 : PROC



; macro
; ��������ͨ�üĴ����ͱ�־�Ĵ��� - �����мĴ���ֵ����ջ��
ASM_DUMP_REGISTER  MACRO
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