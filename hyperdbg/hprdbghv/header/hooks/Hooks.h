/**
 * @file Hooks.h
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief Hook headers
 * @details
 * @version 0.1
 * @date 2020-04-11
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#pragma once

//////////////////////////////////////////////////
//				   Syscall Hook					//
//////////////////////////////////////////////////

/**
 * @brief As we have just on sysret in all the Windows,
 * we use the following variable to hold its address
 * this way, we're not force to check for the instruction
 * so we remove the memory access to check for sysret
 * in this case.
 *
 */

/**
 * @brief Check for instruction sysret and syscall
 *
 */
#define IS_SYSRET_INSTRUCTION(Code)   \
    (*((PUINT8)(Code) + 0) == 0x48 && \
     *((PUINT8)(Code) + 1) == 0x0F && \
     *((PUINT8)(Code) + 2) == 0x07)
#define IS_SYSCALL_INSTRUCTION(Code)  \
    (*((PUINT8)(Code) + 0) == 0x0F && \
     *((PUINT8)(Code) + 1) == 0x05)

/**
 * @brief Special signatures
 *
 */
#define IMAGE_DOS_SIGNATURE    0x5A4D     // MZ
#define IMAGE_OS2_SIGNATURE    0x454E     // NE
#define IMAGE_OS2_SIGNATURE_LE 0x454C     // LE
#define IMAGE_VXD_SIGNATURE    0x454C     // LE
#define IMAGE_NT_SIGNATURE     0x00004550 // PE00

//////////////////////////////////////////////////
//				   Structure					//
//////////////////////////////////////////////////

/**
 * @brief SSDT structure
 *
 */
typedef struct _SSDTStruct
{
    LONG * pServiceTable;
    PVOID  pCounterTable;
#ifdef _WIN64
    UINT64 NumberOfServices;
#else
    ULONG NumberOfServices;
#endif
    PCHAR pArgumentTable;
} SSDTStruct, *PSSDTStruct;

/**
 * @brief Details of detours style EPT hooks
 *
 */
typedef struct _HIDDEN_HOOKS_DETOUR_DETAILS
{
    LIST_ENTRY OtherHooksList;
    PVOID      HookedFunctionAddress;
    PVOID      ReturnAddress;
} HIDDEN_HOOKS_DETOUR_DETAILS, *PHIDDEN_HOOKS_DETOUR_DETAILS;

/**
 * @brief Module entry
 *
 */
typedef struct _SYSTEM_MODULE_ENTRY
{
    HANDLE Section;
    PVOID  MappedBase;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    UINT16 LoadOrderIndex;
    UINT16 InitOrderIndex;
    UINT16 LoadCount;
    UINT16 OffsetToFileName;
    UCHAR  FullPathName[256];
} SYSTEM_MODULE_ENTRY, *PSYSTEM_MODULE_ENTRY;

/**
 * @brief System Information for modules
 *
 */
typedef struct _SYSTEM_MODULE_INFORMATION
{
    ULONG               Count;
    SYSTEM_MODULE_ENTRY Module[0];

} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

/**
 * @brief System information class
 *
 */
typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemModuleInformation         = 11,
    SystemKernelDebuggerInformation = 35
} SYSTEM_INFORMATION_CLASS,
    *PSYSTEM_INFORMATION_CLASS;

typedef NTSTATUS(NTAPI * ZWQUERYSYSTEMINFORMATION)(
    IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
    OUT PVOID                   SystemInformation,
    IN ULONG                    SystemInformationLength,
    OUT PULONG ReturnLength     OPTIONAL);

NTSTATUS(*NtCreateFileOrig)
(
    PHANDLE            FileHandle,
    ACCESS_MASK        DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK   IoStatusBlock,
    PLARGE_INTEGER     AllocationSize,
    ULONG              FileAttributes,
    ULONG              ShareAccess,
    ULONG              CreateDisposition,
    ULONG              CreateOptions,
    PVOID              EaBuffer,
    ULONG              EaLength);

//////////////////////////////////////////////////
//		     Syscall Hooks Functions			//
//////////////////////////////////////////////////

VOID
SyscallHookTest();

VOID
SyscallHookConfigureEFER(VIRTUAL_MACHINE_STATE * VCpu, BOOLEAN EnableEFERSyscallHook);

BOOLEAN
SyscallHookHandleUD(_Inout_ VIRTUAL_MACHINE_STATE * VCpu);

BOOLEAN
SyscallHookEmulateSYSRET(_Inout_ VIRTUAL_MACHINE_STATE * VCpu);

BOOLEAN
SyscallHookEmulateSYSCALL(_Inout_ VIRTUAL_MACHINE_STATE * VCpu);

//////////////////////////////////////////////////
//		    	 Hidden Hooks Test				//
//////////////////////////////////////////////////

PVOID(*ExAllocatePoolWithTagOrig)
(
    POOL_TYPE PoolType,
    SIZE_T    NumberOfBytes,
    ULONG     Tag);

/**
 * @brief Maximum number of supported execution trampoline
 *
 */
#define MAX_EXEC_TRAMPOLINE_SIZE 100

// ----------------------------------------------------------------------

/**
 * @brief Hook in VMX Root Mode with hidden breakpoints (A pre-allocated buffer should be available)
 *
 * @param TargetAddress
 * @param ProcessCr3
 * @return BOOLEAN
 */
BOOLEAN
EptHookPerformPageHook(PVOID TargetAddress, CR3_TYPE ProcessCr3);

/**
 * @brief Hook in VMX Root Mode with hidden detours and monitor
 * (A pre-allocated buffer should be available)
 *
 * @param TargetAddress
 * @param HookFunction
 * @param ProcessCr3
 * @param UnsetRead
 * @param UnsetWrite
 * @param EptHiddenHook
 * @param UnsetExecute
 * @return BOOLEAN
 */
BOOLEAN
EptHookPerformPageHook2(PVOID    TargetAddress,
                        PVOID    HookFunction,
                        CR3_TYPE ProcessCr3,
                        BOOLEAN  UnsetRead,
                        BOOLEAN  UnsetWrite,
                        BOOLEAN  UnsetExecute,
                        BOOLEAN  EptHiddenHook);

/**
 * @brief Hook in VMX Non Root Mode (hidden breakpoint)
 *
 * @param TargetAddress
 * @param ProcessId
 * @return BOOLEAN
 */
BOOLEAN
EptHook(PVOID TargetAddress, UINT32 ProcessId);

/**
 * @brief Hook in VMX Non Root Mode (hidden detours)
 *
 * @param TargetAddress
 * @param HookFunction
 * @param ProcessId
 * @param SetHookForRead
 * @param SetHookForWrite
 * @param SetHookForExec
 * @param EptHiddenHook2
 *
 * @return BOOLEAN
 */
BOOLEAN
EptHook2(PVOID   TargetAddress,
         PVOID   HookFunction,
         UINT32  ProcessId,
         BOOLEAN SetHookForRead,
         BOOLEAN SetHookForWrite,
         BOOLEAN SetHookForExec,
         BOOLEAN EptHiddenHook2);

/**
 * @brief Handle hooked pages in Vmx-root mode
 *
 * @param VCpu
 * @param HookedEntryDetails
 * @param ViolationQualification
 * @param PhysicalAddress
 * @param LastContext
 * @param IgnoreReadOrWriteOrExec
 * @param IsExecViolation
 * @param IsTriggeringPostEventAllowed
 * @return BOOLEAN
 */
BOOLEAN
EptHookHandleHookedPage(VIRTUAL_MACHINE_STATE *              VCpu,
                        EPT_HOOKED_PAGE_DETAIL *             HookedEntryDetails,
                        VMX_EXIT_QUALIFICATION_EPT_VIOLATION ViolationQualification,
                        SIZE_T                               PhysicalAddress,
                        EPT_HOOKS_CONTEXT *                  LastContext,
                        BOOLEAN *                            IgnoreReadOrWriteOrExec,
                        BOOLEAN *                            IsExecViolation,
                        BOOLEAN *                            IsTriggeringPostEventAllowed);

/**
 * @brief Remove a special hook from the hooked pages lists
 *
 * @param PhysicalAddress
 * @return BOOLEAN
 */
BOOLEAN
EptHookRestoreSingleHookToOriginalEntry(SIZE_T PhysicalAddress);

/**
 * @brief Remove all hooks from the hooked pages lists (Should be called in vmx-root)
 *
 * @return VOID
 */
VOID
EptHookRestoreAllHooksToOriginalEntry();

/**
 * @brief Remove all hooks from the hooked pages lists
 *
 * @return VOID
 */
VOID
EptHookUnHookAll();

/**
 * @brief Remove single hook from the hooked pages list and invalidate TLB
 *
 * @param VirtualAddress
 * @param PhysAddress
 * @param ProcessId
 * @return BOOLEAN
 */
BOOLEAN
EptHookUnHookSingleAddress(UINT64 VirtualAddress, UINT64 PhysAddress, UINT32 ProcessId);

/**
 * @brief get the length of active EPT hooks (!epthook and !epthook2)
 *
 * @param
 *
 * @return UINT32
 */
UINT32
EptHookGetCountOfEpthooks(BOOLEAN IsEptHook2);

/**
 * @brief Remove an entry from g_EptHook2sDetourListHead
 *
 * @param Address
 * @return BOOLEAN
 */
BOOLEAN
EptHookRemoveEntryAndFreePoolFromEptHook2sDetourList(UINT64 Address);

/**
 * @brief routines to generally handle breakpoint hit for detour
 * @param Regs
 * @param CalledFrom
 *
 * @return PVOID
 */
PVOID
EptHook2GeneralDetourEventHandler(PGUEST_REGS Regs, PVOID CalledFrom);

/**
 * @brief Allocate (reserve) extra pages for storing details of page hooks
 * @param Count
 *
 * @return VOID
 */
VOID
EptHookAllocateExtraHookingPages(UINT32 Count);

/**
 * @brief Change PML EPT state for execution (execute)
 * @detail should be called from VMX-root
 *
 * @param VCpu The virtual processor's state
 * @param PhysicalAddress Target physical address
 * @param IsUnset Is unsetting bit or setting bit
 *
 * @return BOOLEAN
 */
BOOLEAN
EptHookModifyInstructionFetchState(VIRTUAL_MACHINE_STATE * VCpu,
                                   PVOID                   PhysicalAddress,
                                   BOOLEAN                 IsUnset);

/**
 * @brief Change PML EPT state for read
 * @detail should be called from VMX-root
 *
 * @param VCpu The virtual processor's state
 * @param PhysicalAddress Target physical address
 * @param IsUnset Is unsetting bit or setting bit
 *
 * @return BOOLEAN
 */
BOOLEAN
EptHookModifyPageReadState(VIRTUAL_MACHINE_STATE * VCpu,
                           PVOID                   PhysicalAddress,
                           BOOLEAN                 IsUnset);

/**
 * @brief Change PML EPT state for write
 * @detail should be called from VMX-root
 *
 * @param VCpu The virtual processor's state
 * @param PhysicalAddress Target physical address
 * @param IsUnset Is unsetting bit or setting bit
 *
 * @return BOOLEAN
 */
BOOLEAN
EptHookModifyPageWriteState(VIRTUAL_MACHINE_STATE * VCpu,
                            PVOID                   PhysicalAddress,
                            BOOLEAN                 IsUnset);
