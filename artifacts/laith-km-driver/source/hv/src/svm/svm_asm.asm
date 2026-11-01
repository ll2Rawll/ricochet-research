; svm_asm.asm  --  AMD SVM ASM stubs (MASM x64 / ML64)
;
; Exports:
;   SvmLaunchAndCapture(VmcbPhys)  - saves guest RSP/RIP into VMCB, VMRUN
;   SvmGuestEntry                  - resume point after successful VMRUN
;   SvmHostEntryPoint              - VMEXIT dispatcher loop
;   SvmOff                         - disable SVM (clear EFER.SVME)

EXTERN SvmExitDispatch:PROC

PUBLIC SvmLaunchAndCapture
PUBLIC SvmGuestEntry
PUBLIC SvmHostEntryPoint
PUBLIC SvmOff

.CODE

; --------------------------------------------------------------------------
; SvmLaunchAndCapture(VmcbPhys:QWORD)
;   RCX = VMCB physical address
;
;   The C caller (SvmLaunchVcpu) has already filled VMCB.Save.Rip with the
;   address of SvmGuestEntry and VMCB.Save.Rsp with the current RSP before
;   calling us.  All we do is put the VMCB phys into RAX and VMRUN.
;
;   On successful VMRUN the CPU enters guest mode.  The guest's first
;   instruction is SvmGuestEntry which returns 0 to the C caller.
;   On failure VMRUN exits immediately with ExitCode = SVM_EXIT_INVALID.
;
; Returns 0 on success (from SvmGuestEntry), non-zero on VMRUN failure.
; --------------------------------------------------------------------------
SvmLaunchAndCapture PROC
    mov     rax, rcx        ; RAX = VMCB physical address (VMRUN operand)
    vmrun   rax             ; enter guest

    ; If we get here VMRUN failed or immediately exited.
    ; Return 1 so C caller can inspect VMCB.Control.ExitCode.
    mov     rax, 1
    ret
SvmLaunchAndCapture ENDP


; --------------------------------------------------------------------------
; SvmGuestEntry
;   C caller set VMCB.Save.Rip = &SvmGuestEntry before calling
;   SvmLaunchAndCapture.  After successful VMRUN the guest executes here.
;   Return 0 to signal success.
; --------------------------------------------------------------------------
SvmGuestEntry PROC
    xor     rax, rax
    ret
SvmGuestEntry ENDP


; --------------------------------------------------------------------------
; SvmHostEntryPoint
;   Called (via longjmp / function pointer) from the C dispatcher after
;   each VMEXIT.  Saves all guest GPRs, calls SvmExitDispatch(GpRegs*),
;   then either loops back to VMRUN or returns to shut down.
;
;   RCX on first call = VMCB physical address (preserved in R12).
; --------------------------------------------------------------------------
SvmHostEntryPoint PROC
    ; Save VMCB physical address (passed in RCX) onto the stack.
    push    rcx

SvmRunLoop:
    ; Push all guest GPRs in GpRegs layout (R15..RAX = 15 registers)
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    mov     rcx, rsp        ; RCX = GpRegs*
    sub     rsp, 20h        ; 32 bytes shadow space
    call    SvmExitDispatch
    add     rsp, 20h

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax

    test    rax, rax
    jnz     SvmShutdown     ; non-zero = stop

    ; Load saved VMCB physical address from the stack slot (at [rsp])
    mov     rax, [rsp]
    vmrun   rax             ; run guest, exit brings us back to SvmRunLoop
    jmp     SvmRunLoop

SvmShutdown:
    add     rsp, 8          ; clean up the saved VMCB phys slot from stack
    ret

SvmHostEntryPoint ENDP


; --------------------------------------------------------------------------
; SvmOff
;   Clears EFER.SVME (bit 12) to disable SVM on the current CPU.
; --------------------------------------------------------------------------
SvmOff PROC
    mov     rcx, 0C0000080h ; MSR_EFER
    rdmsr
    ; Clear bit 12 (SVME).  Use a complement literal to avoid the NOT operator.
    and     eax, 0FFFFEFFFh ; same as: and eax, NOT 1000h
    wrmsr
    ret
SvmOff ENDP

END
