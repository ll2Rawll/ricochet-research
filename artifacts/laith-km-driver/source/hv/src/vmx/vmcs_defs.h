#pragma once
#include <ntddk.h>

//
// ─── VMCS field encodings (Intel SDM Vol. 3C Appendix B) ─────────────────────
//

// ── 16-bit Guest-state fields ────────────────────────────────────────────────
#define VMCS_GUEST_ES_SEL           0x0800
#define VMCS_GUEST_CS_SEL           0x0802
#define VMCS_GUEST_SS_SEL           0x0804
#define VMCS_GUEST_DS_SEL           0x0806
#define VMCS_GUEST_FS_SEL           0x0808
#define VMCS_GUEST_GS_SEL           0x080A
#define VMCS_GUEST_LDTR_SEL         0x080C
#define VMCS_GUEST_TR_SEL           0x080E

// ── 16-bit Host-state fields ─────────────────────────────────────────────────
#define VMCS_HOST_ES_SEL            0x0C00
#define VMCS_HOST_CS_SEL            0x0C02
#define VMCS_HOST_SS_SEL            0x0C04
#define VMCS_HOST_DS_SEL            0x0C06
#define VMCS_HOST_FS_SEL            0x0C08
#define VMCS_HOST_GS_SEL            0x0C0A
#define VMCS_HOST_TR_SEL            0x0C0C

// ── 64-bit Control fields ─────────────────────────────────────────────────────
#define VMCS_CTRL_IO_BITMAP_A       0x2000
#define VMCS_CTRL_IO_BITMAP_B       0x2002
#define VMCS_CTRL_MSR_BITMAP        0x2004
#define VMCS_CTRL_VMEXIT_MSR_STORE  0x2006
#define VMCS_CTRL_VMEXIT_MSR_LOAD   0x2008
#define VMCS_CTRL_VMENTRY_MSR_LOAD  0x200A
#define VMCS_CTRL_TSC_OFFSET        0x2010
#define VMCS_CTRL_VIRT_APIC_ADDR    0x2012
#define VMCS_CTRL_APIC_ACCESS_ADDR  0x2014
#define VMCS_CTRL_EPTP              0x201A

// ── 64-bit Guest-state fields ─────────────────────────────────────────────────
#define VMCS_GUEST_VMCS_LINK_PTR    0x2800
#define VMCS_GUEST_DEBUGCTL         0x2802
#define VMCS_GUEST_PAT              0x2804
#define VMCS_GUEST_EFER             0x2806

// ── 64-bit Host-state fields ──────────────────────────────────────────────────
#define VMCS_HOST_PAT               0x2C00
#define VMCS_HOST_EFER              0x2C02

// ── 32-bit Control fields ─────────────────────────────────────────────────────
#define VMCS_CTRL_PIN_EXEC          0x4000
#define VMCS_CTRL_PROC_EXEC         0x4002
#define VMCS_CTRL_EXCEPTION_BITMAP  0x4004
#define VMCS_CTRL_PF_ERR_MASK       0x4006
#define VMCS_CTRL_PF_ERR_MATCH      0x4008
#define VMCS_CTRL_CR3_TARGET_COUNT  0x400A
#define VMCS_CTRL_EXIT_CTLS         0x400C
#define VMCS_CTRL_ENTRY_CTLS        0x4012
#define VMCS_CTRL_SECONDARY_EXEC    0x401E

// ── 32-bit Read-only data fields ──────────────────────────────────────────────
#define VMCS_RO_INSN_ERROR          0x4400
#define VMCS_RO_EXIT_REASON         0x4402
#define VMCS_RO_EXIT_INTR_INFO      0x4404
#define VMCS_RO_EXIT_INTR_ERR       0x4406
#define VMCS_RO_IDT_VECT_INFO       0x4408
#define VMCS_RO_IDT_VECT_ERR        0x440A
#define VMCS_RO_INSN_LENGTH         0x440C
#define VMCS_RO_INSN_INFO           0x440E

// ── 32-bit Guest-state fields ─────────────────────────────────────────────────
#define VMCS_GUEST_ES_LIMIT         0x4800
#define VMCS_GUEST_CS_LIMIT         0x4802
#define VMCS_GUEST_SS_LIMIT         0x4804
#define VMCS_GUEST_DS_LIMIT         0x4806
#define VMCS_GUEST_FS_LIMIT         0x4808
#define VMCS_GUEST_GS_LIMIT         0x480A
#define VMCS_GUEST_LDTR_LIMIT       0x480C
#define VMCS_GUEST_TR_LIMIT         0x480E
#define VMCS_GUEST_GDTR_LIMIT       0x4810
#define VMCS_GUEST_IDTR_LIMIT       0x4812
#define VMCS_GUEST_ES_AR            0x4814
#define VMCS_GUEST_CS_AR            0x4816
#define VMCS_GUEST_SS_AR            0x4818
#define VMCS_GUEST_DS_AR            0x481A
#define VMCS_GUEST_FS_AR            0x481C
#define VMCS_GUEST_GS_AR            0x481E
#define VMCS_GUEST_LDTR_AR          0x4820
#define VMCS_GUEST_TR_AR            0x4822
#define VMCS_GUEST_INTERRUPTIBILITY 0x4824
#define VMCS_GUEST_ACTIVITY_STATE   0x4826
#define VMCS_GUEST_SYSENTER_CS      0x482A

// ── 32-bit Host-state fields ──────────────────────────────────────────────────
#define VMCS_HOST_SYSENTER_CS       0x4C00

// ── Natural-width Read-only ───────────────────────────────────────────────────
#define VMCS_RO_EXIT_QUAL           0x6400
#define VMCS_RO_GUEST_LINEAR_ADDR   0x640A
#define VMCS_RO_GUEST_PHYS_ADDR     0x2400   // 64-bit

// ── Natural-width Guest-state ─────────────────────────────────────────────────
#define VMCS_GUEST_CR0              0x6800
#define VMCS_GUEST_CR3              0x6802
#define VMCS_GUEST_CR4              0x6804
#define VMCS_GUEST_ES_BASE          0x6806
#define VMCS_GUEST_CS_BASE          0x6808
#define VMCS_GUEST_SS_BASE          0x680A
#define VMCS_GUEST_DS_BASE          0x680C
#define VMCS_GUEST_FS_BASE          0x680E
#define VMCS_GUEST_GS_BASE          0x6810
#define VMCS_GUEST_LDTR_BASE        0x6812
#define VMCS_GUEST_TR_BASE          0x6814
#define VMCS_GUEST_GDTR_BASE        0x6816
#define VMCS_GUEST_IDTR_BASE        0x6818
#define VMCS_GUEST_DR7              0x681A
#define VMCS_GUEST_RSP              0x681C
#define VMCS_GUEST_RIP              0x681E
#define VMCS_GUEST_RFLAGS           0x6820
#define VMCS_GUEST_SYSENTER_ESP     0x6824
#define VMCS_GUEST_SYSENTER_EIP     0x6826

// ── Natural-width Host-state ──────────────────────────────────────────────────
#define VMCS_HOST_CR0               0x6C00
#define VMCS_HOST_CR3               0x6C02
#define VMCS_HOST_CR4               0x6C04
#define VMCS_HOST_FS_BASE           0x6C06
#define VMCS_HOST_GS_BASE           0x6C08
#define VMCS_HOST_TR_BASE           0x6C0A
#define VMCS_HOST_GDTR_BASE         0x6C0C
#define VMCS_HOST_IDTR_BASE         0x6C0E
#define VMCS_HOST_SYSENTER_ESP      0x6C10
#define VMCS_HOST_SYSENTER_EIP      0x6C12
#define VMCS_HOST_RSP               0x6C14
#define VMCS_HOST_RIP               0x6C16

// ── VMX exit reasons ─────────────────────────────────────────────────────────
#define VMX_EXIT_EXCEPTION_NMI      0
#define VMX_EXIT_EXTERNAL_IRQ       1
#define VMX_EXIT_TRIPLE_FAULT       2
#define VMX_EXIT_CPUID              10
#define VMX_EXIT_HLT                12
#define VMX_EXIT_INVD               13
#define VMX_EXIT_VMCALL             18
#define VMX_EXIT_CR_ACCESS          28
#define VMX_EXIT_IO                 30
#define VMX_EXIT_RDMSR              31
#define VMX_EXIT_WRMSR              32
#define VMX_EXIT_PAUSE              40
#define VMX_EXIT_EPT_VIOLATION      48
#define VMX_EXIT_EPT_MISCONFIG      49
#define VMX_EXIT_XSETBV             55

//
// Hyper-V uses its own internal exit-reason space layered on top of VMX.
// When hyperjacking (routing our exits back through hvix64 for STIMER
// accounting), fake the exit reason to this value so hvix64 treats it
// as a no-op PAUSE and runs its epilogue (clock balancing, idle wakeup).
// Without this on modern Win11, you get CLOCK_WATCHDOG_TIMEOUT (0x101).
//
#define HV_SYNTHETIC_EXIT_PAUSE     0x7Bu

// ── Pin-based execution control bits ─────────────────────────────────────────
#define PIN_NMI_EXITING             (1u << 3)
#define PIN_VIRTUAL_NMIS            (1u << 5)

// ── Processor-based execution control bits ────────────────────────────────────
#define PROC_INTERRUPT_WINDOW       (1u << 2)
#define PROC_HLT_EXITING            (1u << 7)
#define PROC_CR3_LOAD_EXITING       (1u << 15)
#define PROC_CR3_STORE_EXITING      (1u << 16)
#define PROC_CR8_LOAD_EXITING       (1u << 19)
#define PROC_CR8_STORE_EXITING      (1u << 20)
#define PROC_USE_MSR_BITMAP         (1u << 28)
#define PROC_ACTIVATE_SECONDARY     (1u << 31)

// ── Secondary execution control bits ─────────────────────────────────────────
#define SECONDARY_ENABLE_EPT        (1u << 1)
#define SECONDARY_ENABLE_RDTSCP     (1u << 3)
#define SECONDARY_ENABLE_VPID       (1u << 5)
#define SECONDARY_UNRESTRICTED      (1u << 7)
#define SECONDARY_ENABLE_XSAVES     (1u << 20)

// ── VM-exit control bits ──────────────────────────────────────────────────────
#define EXIT_SAVE_DEBUG_CTLS        (1u << 2)
#define EXIT_HOST_ADDR_SPACE_64     (1u << 9)
#define EXIT_SAVE_EFER              (1u << 20)
#define EXIT_LOAD_EFER              (1u << 21)

// ── VM-entry control bits ─────────────────────────────────────────────────────
#define ENTRY_LOAD_DEBUG_CTLS       (1u << 2)
#define ENTRY_IA32E_GUEST           (1u << 9)
#define ENTRY_LOAD_EFER             (1u << 15)

// ── MSR indices needed for setup ─────────────────────────────────────────────
#define IA32_VMX_BASIC              0x480
#define IA32_VMX_PINBASED_CTLS      0x481
#define IA32_VMX_PROCBASED_CTLS     0x482
#define IA32_VMX_EXIT_CTLS          0x483
#define IA32_VMX_ENTRY_CTLS         0x484
#define IA32_VMX_MISC               0x485
#define IA32_VMX_CR0_FIXED0         0x486
#define IA32_VMX_CR0_FIXED1         0x487
#define IA32_VMX_CR4_FIXED0         0x488
#define IA32_VMX_CR4_FIXED1         0x489
#define IA32_VMX_PROCBASED_CTLS2    0x48B
#define IA32_VMX_TRUE_PINBASED      0x48D
#define IA32_VMX_TRUE_PROCBASED     0x48E
#define IA32_VMX_TRUE_EXIT_CTLS     0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS    0x490
#define IA32_FEATURE_CONTROL        0x3A
#define IA32_SYSENTER_CS            0x174
#define IA32_SYSENTER_ESP           0x175
#define IA32_SYSENTER_EIP           0x176
#define IA32_GS_BASE                0xC0000101
#define IA32_FS_BASE                0xC0000100
#define IA32_DEBUGCTL               0x1D9
#define IA32_PAT                    0x277
#define IA32_EFER                   0xC0000080

// ── EPT pointer format ────────────────────────────────────────────────────────
// bits [2:0] = memory type for EPT paging structures (WB = 6)
// bits [5:3] = page-walk length minus 1 (4-level = 3)
// bits [51:12] = PML4 PFN
#define EPT_MEMORY_TYPE_WB          6u
#define EPT_PAGEWALK_4              3u
#define EPT_ENABLE_DIRTY_FLAG       (1ull << 6)
