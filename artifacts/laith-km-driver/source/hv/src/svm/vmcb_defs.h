#pragma once
#include <ntddk.h>

//
// ─── AMD VMCB layout (AMD-V Architecture: APM Vol. 2 §15.16) ─────────────────
//

#pragma pack(push, 1)

// ── TLB control ──────────────────────────────────────────────────────────────
enum class TlbControl : UINT8 {
    DoNotFlush            = 0,
    FlushEntireTlb        = 1,
    FlushGuestTlb         = 3,
    FlushGuestNonGlobal   = 7,
};

// ── VMCB clean bits (for selective state caching) ─────────────────────────────
union VmcbClean {
    UINT32 Value;
    struct {
        UINT32 I            : 1;   // intercept config
        UINT32 Iopm         : 1;
        UINT32 Asid         : 1;
        UINT32 Tpr          : 1;
        UINT32 Np           : 1;   // nested paging
        UINT32 Cr           : 1;
        UINT32 Dr           : 1;
        UINT32 Dt           : 1;
        UINT32 Seg          : 1;
        UINT32 Cr2          : 1;
        UINT32 Lbr          : 1;
        UINT32 Avic         : 1;
        UINT32 Cet          : 1;
        UINT32 Reserved     : 19;
    };
};

// ── VMCB control area (0x000–0x3FF) ──────────────────────────────────────────
struct VmcbControl {
    UINT16 InterceptCR;             // 0x000  CR read/write intercept mask
    UINT16 InterceptCRW;            // 0x002
    UINT16 InterceptDR;             // 0x004
    UINT16 InterceptDRW;            // 0x006
    UINT32 InterceptException;      // 0x008  exception bitmap
    UINT32 InterceptMisc1;          // 0x00C  misc intercept bits 1
    UINT32 InterceptMisc2;          // 0x010  misc intercept bits 2
    UINT32 InterceptMisc3;          // 0x014
    UINT8  Pad0[0x24];              // 0x018–0x03B
    UINT16 Pause_FiltCount;         // 0x03C
    UINT16 Pause_FiltThresh;        // 0x03E
    UINT64 IoPmBasePa;              // 0x040
    UINT64 MsrPmBasePa;             // 0x048
    UINT64 TscOffset;               // 0x050
    UINT32 Asid;                    // 0x058
    TlbControl TlbCtrl;             // 0x05C
    UINT8  Pad1[0x3];               // 0x05D
    UINT64 VirtualIntr;             // 0x060
    UINT64 InterruptShadow;         // 0x068
    UINT64 ExitCode;                // 0x070  exit reason
    UINT64 ExitInfo1;               // 0x078  first exit info
    UINT64 ExitInfo2;               // 0x080  second exit info (faulting GPA for #NPF)
    UINT64 ExitIntInfo;             // 0x088
    UINT64 NpEnable;                // 0x090  nested paging enable (bit 0)
    UINT64 AvicApicBar;             // 0x098
    UINT64 GhcbPa;                  // 0x0A0
    UINT64 EventInj;                // 0x0A8
    UINT64 NCr3;                    // 0x0B0  nested CR3
    UINT64 LbrVirt;                 // 0x0B8
    VmcbClean VmcbCleanBits;        // 0x0C0
    UINT32 Pad2;                    // 0x0C4
    UINT64 NextRip;                 // 0x0C8  next sequential RIP (filled by hw for sync exits)
    UINT8  InsnLen;                 // 0x0D0  number of bytes in InsnBytes
    UINT8  InsnBytes[15];           // 0x0D1  bytes of faulting instruction (may be zeroed on #NPF)
    UINT8  Pad3[0x320];             // pad to 0x400
};

static_assert(sizeof(VmcbControl) == 0x400, "VmcbControl must be 0x400 bytes");

// ── Segment descriptor (for VMCB save-state) ─────────────────────────────────
struct VmcbSeg {
    UINT16 Selector;
    UINT16 Attrib;   // packed: type(3:0) S(4) DPL(6:5) P(7) AVL(8) L(9) D/B(10) G(11)
    UINT32 Limit;
    UINT64 Base;
};

// ── VMCB save-state area (0x400–0xBFF) ───────────────────────────────────────
struct VmcbSaveState {
    VmcbSeg Es;             // 0x400
    VmcbSeg Cs;             // 0x410
    VmcbSeg Ss;             // 0x420
    VmcbSeg Ds;             // 0x430
    VmcbSeg Fs;             // 0x440
    VmcbSeg Gs;             // 0x450
    VmcbSeg Gdtr;           // 0x460
    VmcbSeg Ldtr;           // 0x470
    VmcbSeg Idtr;           // 0x480
    VmcbSeg Tr;             // 0x490
    UINT8   Pad0[0x2B];     // 0x4A0
    UINT8   Cpl;            // 0x4CB
    UINT32  Pad1;           // 0x4CC
    UINT64  Efer;           // 0x4D0
    UINT8   Pad2[0x70];     // 0x4D8
    UINT64  Cr4;            // 0x548
    UINT64  Cr3;            // 0x550
    UINT64  Cr0;            // 0x558
    UINT64  Dr7;            // 0x560
    UINT64  Dr6;            // 0x568
    UINT64  Rflags;         // 0x570
    UINT64  Rip;            // 0x578
    UINT8   Pad3[0x58];     // 0x580
    UINT64  Rsp;            // 0x5D8
    UINT8   Pad4[0x18];     // 0x5E0
    UINT64  Rax;            // 0x5F8
    UINT64  Star;           // 0x600
    UINT64  LStar;          // 0x608
    UINT64  CStar;          // 0x610
    UINT64  SfMask;         // 0x618
    UINT64  KernelGsBase;   // 0x620
    UINT64  SysenterCs;     // 0x628
    UINT64  SysenterEsp;    // 0x630
    UINT64  SysenterEip;    // 0x638
    UINT64  Cr2;            // 0x640
    UINT8   Pad5[0x20];     // 0x648
    UINT64  GPat;           // 0x668
    UINT64  DbgCtl;         // 0x670
    UINT64  BrFrom;         // 0x678
    UINT64  BrTo;           // 0x680
    UINT64  LastExFrom;     // 0x688
    UINT64  LastExTo;       // 0x690
};

// ── Full VMCB (control + save-state, exactly 2 pages) ────────────────────────
struct Vmcb {
    VmcbControl  Control;      // 0x000
    VmcbSaveState Save;        // 0x400
};

static_assert(sizeof(Vmcb) <= 0x1000, "Vmcb must fit in one page");

#pragma pack(pop)

// ── SVM exit codes ────────────────────────────────────────────────────────────
#define SVM_EXIT_CR0_READ           0x000u
#define SVM_EXIT_CR3_READ           0x003u
#define SVM_EXIT_CR4_READ           0x004u
#define SVM_EXIT_CR8_READ           0x008u
#define SVM_EXIT_DR_READ            0x020u
#define SVM_EXIT_EXCP_BASE          0x040u
#define SVM_EXIT_INTR               0x060u
#define SVM_EXIT_NMI                0x061u
#define SVM_EXIT_SMI                0x062u
#define SVM_EXIT_INIT               0x063u
#define SVM_EXIT_VINTR              0x064u
#define SVM_EXIT_CR0_SEL_WRITE      0x065u
#define SVM_EXIT_IDTR_READ          0x066u
#define SVM_EXIT_GDTR_READ          0x067u
#define SVM_EXIT_LDTR_READ          0x068u
#define SVM_EXIT_TR_READ            0x069u
#define SVM_EXIT_RDTSC              0x06Eu
#define SVM_EXIT_RDPMC              0x06Fu
#define SVM_EXIT_PUSHF              0x070u
#define SVM_EXIT_POPF               0x071u
#define SVM_EXIT_CPUID              0x072u
#define SVM_EXIT_RSM                0x073u
#define SVM_EXIT_IRET               0x074u
#define SVM_EXIT_SWINT              0x075u
#define SVM_EXIT_INVD               0x076u
#define SVM_EXIT_PAUSE              0x077u
#define SVM_EXIT_HLT                0x078u
#define SVM_EXIT_INVLPG             0x079u
#define SVM_EXIT_INVLPGA            0x07Au
#define SVM_EXIT_IOIO               0x07Bu
#define SVM_EXIT_MSR                0x07Cu
#define SVM_EXIT_TASKSWITCH         0x07Du
#define SVM_EXIT_FERR_FREEZE        0x07Eu
#define SVM_EXIT_SHUTDOWN           0x07Fu
#define SVM_EXIT_VMRUN              0x080u
#define SVM_EXIT_VMMCALL            0x081u
#define SVM_EXIT_VMLOAD             0x082u
#define SVM_EXIT_VMSAVE             0x083u
#define SVM_EXIT_STGI               0x084u
#define SVM_EXIT_CLGI               0x085u
#define SVM_EXIT_SKINIT             0x086u
#define SVM_EXIT_RDTSCP             0x087u
#define SVM_EXIT_ICEBP              0x088u
#define SVM_EXIT_WBINVD             0x089u
#define SVM_EXIT_MONITOR            0x08Au
#define SVM_EXIT_MWAIT              0x08Bu
#define SVM_EXIT_MWAIT_ARMED        0x08Cu
#define SVM_EXIT_XSETBV             0x08Du
#define SVM_EXIT_RDPRU              0x08Eu
#define SVM_EXIT_EFER_WRITE_TRAP    0x08Fu
#define SVM_EXIT_NPF                0x400u   // Nested Page Fault (#NPF)
#define SVM_EXIT_AVIC_INCOMPLETE    0x401u
#define SVM_EXIT_AVIC_UNACCEL       0x402u
#define SVM_EXIT_INVALID            0xFFFFFFFFFFFFFFFFull

// NpEnable bits
#define SVM_NP_ENABLE_NP            (1ull << 0)

// MSR permission map bits
#define SVM_MSR_PM_READ             0x01
#define SVM_MSR_PM_WRITE            0x02

// MSRs
#define MSR_EFER                    0xC0000080
#define EFER_SVME                   (1ull << 12)
#define MSR_VM_HSAVE_PA             0xC0010117
