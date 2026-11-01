; hv_hook_asm.asm  --  Intel VMX hyperjack entry stub (MASM x64 / ML64)
;
; NOTE: compiled with /Cx (case-sensitive) -- set in vcxproj MASM AdditionalOptions.
;
; When HvHook::Initialize() installs the 14-byte JMP at hvix64's VM-exit handler
; entry, the CPU jumps here on every VM exit instead.
;
; Flow:
;   VM exit → [JMP14 at hvix64 entry] → VmxHyperjackEntry
;       → saves all host GPRs
;       → calls VmxHyperjackDispatch(GpRegs*)   [C++ in exit_handler.cpp]
;       → restores all host GPRs
;       → push [g_VmxTrampolineTarget] + RET    (= jump to trampoline)
;   Trampoline:
;       → executes original 14 bytes from hvix64 entry
;       → JMP to hvix64_entry + 14              (continues normally)
;   hvix64:
;       → runs epilogue (STIMER, scheduler, VMRESUME)
;
; Register contract with hvix64:
;   We restore ALL host GPRs to exactly the values Hyper-V left them before
;   the VM exit, so hvix64's entry code sees a clean host state.
;   The trampoline target jump uses push+ret so it doesn't clobber any GPR.

EXTERN VmxHyperjackDispatch:PROC
EXTERN g_VmxTrampolineTarget:QWORD

.CODE

; ─────────────────────────────────────────────────────────────────────────────
; VmxHyperjackEntry
;   Intercepted VM-exit entry point (replaces hvix64's HOST_RIP).
;   Saves all 15 host GPRs, calls VmxHyperjackDispatch(GpRegs* hostRegs),
;   restores all 15 GPRs, then JMPs to the trampoline.
;
;   Stack on entry: RSP = Hyper-V's HOST_RSP (no extra words pushed by the CPU
;   because a VM exit sets RSP = HOST_RSP and jumps to HOST_RIP directly).
; ─────────────────────────────────────────────────────────────────────────────
VmxHyperjackEntry PROC PUBLIC

    ; ── Save all host GPRs ─────────────────────────────────────────────────
    ; Push order: RAX first → R15 last → RSP now points to GpRegs.R15
    ; This matches the GpRegs struct layout (R15 at offset 0, RAX at offset 0x70).
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

    ; ── Call C++ dispatch ──────────────────────────────────────────────────
    ; RCX = GpRegs* (RSP after 15 pushes)
    mov     rcx, rsp
    sub     rsp, 20h        ; 32-byte shadow space (Microsoft x64 ABI)
    call    VmxHyperjackDispatch
    add     rsp, 20h

    ; ── Restore all host GPRs ──────────────────────────────────────────────
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

    ; ── Jump to trampoline without clobbering any register ─────────────────
    ; Technique: push the trampoline VA from the global pointer variable, then
    ; RET "returns" to it.  RSP is unchanged net (push+ret cancel out), and
    ; no GPR is touched.
    ;
    ; g_VmxTrampolineTarget is a void* (QWORD) set by hv_hook.cpp to point at
    ; the trampoline buffer: [14 orig bytes] + [FF25 00000000 hvix64_entry+14].
    push    QWORD PTR [g_VmxTrampolineTarget]
    ret

VmxHyperjackEntry ENDP

END
