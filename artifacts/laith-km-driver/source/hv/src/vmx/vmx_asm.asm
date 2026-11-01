; vmx_asm.asm  --  VMX ASM stubs + CPU segment-selector helpers (MASM x64 / ML64)
;
; NOTE: compiled with /Cx (case-sensitive) — set in vcxproj MASM AdditionalOptions.
;
; x64 MSVC does not support inline __asm.  All code that needs to read segment
; selectors (CS, SS, DS, ES, FS, GS, TR, LDTR) or store GDTR/IDTR must call
; these small helpers instead.

EXTERN VmxExitDispatch:PROC

VMCS_GUEST_RSP  EQU 0681Ch
VMCS_GUEST_RIP  EQU 0681Eh

.CODE

; --------------------------------------------------------------------------
; VmxLaunchAndCapture
;   Writes current RSP + VmxGuestEntry as guest RSP/RIP into the VMCS,
;   then executes VMLAUNCH.
;   Returns 0 on success (via VmxGuestEntry) or RFLAGS on failure.
; --------------------------------------------------------------------------
VmxLaunchAndCapture PROC PUBLIC
    mov     rax, rsp
    mov     rdx, VMCS_GUEST_RSP
    vmwrite rdx, rax

    lea     rax, VmxGuestEntry
    mov     rdx, VMCS_GUEST_RIP
    vmwrite rdx, rax

    vmlaunch

    pushfq
    pop     rax
    ret
VmxLaunchAndCapture ENDP


; --------------------------------------------------------------------------
; VmxGuestEntry
;   Guest resume point after VMLAUNCH succeeds. Returns 0.
; --------------------------------------------------------------------------
VmxGuestEntry PROC PUBLIC
    xor     rax, rax
    ret
VmxGuestEntry ENDP


; --------------------------------------------------------------------------
; VmxHostEntryPoint
;   CPU jumps here on every VM-exit (HOST_RIP).
;   Saves guest GPRs, calls VmxExitDispatch(GpRegs*).
;   Returns 0 -> VMRESUME, non-zero -> shutdown path.
; --------------------------------------------------------------------------
VmxHostEntryPoint PROC PUBLIC
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

    mov     rcx, rsp
    sub     rsp, 20h
    call    VmxExitDispatch
    add     rsp, 20h

    test    rax, rax
    jnz     ShutdownPath        ; single-colon local label -- no :: needed

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
    vmresume
    jmp     VmxHostEntryPoint

ShutdownPath:
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
    ret

VmxHostEntryPoint ENDP


; --------------------------------------------------------------------------
; AsmVmxOff
; --------------------------------------------------------------------------
AsmVmxOff PROC PUBLIC
    vmxoff
    ret
AsmVmxOff ENDP

; ============================================================================
; Segment-selector reader helpers
;   Return value is in RAX (low 16 bits = selector).
;   Called from C++ vmx.cpp / svm.cpp to replace inline __asm blocks.
; ============================================================================

AsmReadCs PROC PUBLIC
    mov     ax, cs
    ret
AsmReadCs ENDP

AsmReadSs PROC PUBLIC
    mov     ax, ss
    ret
AsmReadSs ENDP

AsmReadDs PROC PUBLIC
    mov     ax, ds
    ret
AsmReadDs ENDP

AsmReadEs PROC PUBLIC
    mov     ax, es
    ret
AsmReadEs ENDP

AsmReadFs PROC PUBLIC
    mov     ax, fs
    ret
AsmReadFs ENDP

AsmReadGs PROC PUBLIC
    mov     ax, gs
    ret
AsmReadGs ENDP

AsmReadTr PROC PUBLIC
    str     ax
    ret
AsmReadTr ENDP

AsmReadLdtr PROC PUBLIC
    sldt    ax
    ret
AsmReadLdtr ENDP

AsmReadEflags PROC PUBLIC
    pushfq
    pop     rax
    ret
AsmReadEflags ENDP

; AsmSgdt(void* buf) -- stores 10-byte GDTR at [RCX]
AsmSgdt PROC PUBLIC
    sgdt    fword ptr [rcx]
    ret
AsmSgdt ENDP

; AsmSidt(void* buf) -- stores 10-byte IDTR at [RCX]
AsmSidt PROC PUBLIC
    sidt    fword ptr [rcx]
    ret
AsmSidt ENDP

; AsmInvept(type, desc) -- wrapper for INVEPT instruction
AsmInvept PROC PUBLIC
    invept  rcx, oword ptr [rdx]
    ret
AsmInvept ENDP

; AsmInvvpid(type, desc) -- wrapper for INVVPID instruction
AsmInvvpid PROC PUBLIC
    invvpid rcx, oword ptr [rdx]
    ret
AsmInvvpid ENDP

END
