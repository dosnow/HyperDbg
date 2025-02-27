/**
 * @file Configuration.c
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief Configuration interface for hypervisor events
 * @details
 *
 * @version 0.2
 * @date 2023-01-26
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

/**
 * @brief routines for debugging threads (enable mov-to-cr3 exiting)
 *
 * @return VOID
 */
VOID
ConfigureEnableMovToCr3ExitingOnAllProcessors()
{
    //
    // Indicate that the future #PFs should or should not be checked with user debugger
    //
    g_CheckPageFaultsAndMov2Cr3VmexitsWithUserDebugger = TRUE;

    BroadcastEnableMovToCr3ExitingOnAllProcessors();
}

/**
 * @brief routines for initializing Mode-based execution hooks
 * @param RevServiceRequest
 *
 * @return VOID
 */
VOID
ConfigureInitializeReversingMachineOnAllProcessors(PREVERSING_MACHINE_RECONSTRUCT_MEMORY_REQUEST RevServiceRequest)
{
    ReversingMachineInitialize(RevServiceRequest);
}

/**
 * @brief routines for initializing Mode-based execution hooks
 *
 * @return VOID
 */
VOID
ConfigureModeBasedExecHookUninitializeOnAllProcessors()
{
    ModeBasedExecHookUninitialize();
}

/**
 * @brief routines for initializing dirty logging mechanism
 *
 * @return VOID
 */
VOID
ConfigureDirtyLoggingInitializeOnAllProcessors()
{
    DirtyLoggingInitialize();
}

/**
 * @brief routines for uninitializing dirty logging mechanism
 *
 * @return VOID
 */
VOID
ConfigureDirtyLoggingUninitializeOnAllProcessors()
{
    DirtyLoggingUninitialize();
}

/**
 * @brief routines for debugging threads (disable mov-to-cr3 exiting)
 *
 * @return VOID
 */
VOID
ConfigureDisableMovToCr3ExitingOnAllProcessors()
{
    //
    // Indicate that the future #PFs should or should not be checked with user debugger
    //
    g_CheckPageFaultsAndMov2Cr3VmexitsWithUserDebugger = FALSE;

    BroadcastDisableMovToCr3ExitingOnAllProcessors();
}

/**
 * @brief routines for enabling syscall hooks on all cores
 * @param SyscallHookType
 *
 * @return VOID
 */
VOID
ConfigureEnableEferSyscallEventsOnAllProcessors(DEBUGGER_EVENT_SYSCALL_SYSRET_TYPE SyscallHookType)
{
    if (SyscallHookType == DEBUGGER_EVENT_SYSCALL_SYSRET_HANDLE_ALL_UD)
    {
        g_IsUnsafeSyscallOrSysretHandling = TRUE;
    }
    else if (SyscallHookType == DEBUGGER_EVENT_SYSCALL_SYSRET_SAFE_ACCESS_MEMORY)
    {
        g_IsUnsafeSyscallOrSysretHandling = FALSE;
    }

    BroadcastEnableEferSyscallEventsOnAllProcessors();
}

/**
 * @brief routines for disabling syscall hooks on all cores
 *
 * @return VOID
 */
VOID
ConfigureDisableEferSyscallEventsOnAllProcessors()
{
    BroadcastDisableEferSyscallEventsOnAllProcessors();
}

/**
 * @brief Remove single hook from the hooked pages list and invalidate TLB
 * @details Should be called from vmx non-root
 *
 * @param VirtualAddress Virtual address to unhook
 * @param PhysAddress Physical address to unhook (optional)
 * @param ProcessId The process id of target process
 * @details in unhooking for some hooks only physical address is availables
 *
 * @return BOOLEAN If unhook was successful it returns true or if it was not successful returns false
 */
BOOLEAN
ConfigureEptHookUnHookSingleAddress(UINT64 VirtualAddress, UINT64 PhysAddress, UINT32 ProcessId)
{
    return EptHookUnHookSingleAddress(VirtualAddress, PhysAddress, ProcessId);
}

/**
 * @brief This function allocates a buffer in VMX Non Root Mode and then invokes a VMCALL to set the hook
 *
 * @details this command uses hidden breakpoints (0xcc) to hook, THIS FUNCTION SHOULD BE CALLED WHEN THE
 * VMLAUNCH ALREADY EXECUTED, it is because, broadcasting to enable exception bitmap for breakpoint is not
 * clear here, if we want to broadcast to enable exception bitmaps on all cores when vmlaunch is not executed
 * then that's ok but a user might call this function when we didn't configure the vmcs, it's a problem! we
 * can solve it by giving a hint to vmcs configure function to make it ok for future configuration but that
 * sounds stupid, I think it's better to not support this feature. Btw, debugger won't use this function in
 * the above mentioned method, so we won't have any problem with this :)
 *
 * @param TargetAddress The address of function or memory address to be hooked
 * @param ProcessId The process id to translate based on that process's cr3
 * @return BOOLEAN Returns true if the hook was successful or false if there was an error
 */
BOOLEAN
ConfigureEptHook(PVOID TargetAddress, UINT32 ProcessId)
{
    return EptHook(TargetAddress, ProcessId);
}

/**
 * @brief This function allocates a buffer in VMX Non Root Mode and then invokes a VMCALL to set the hook
 * @details this command uses hidden detours, this NOT be called from vmx-root mode
 *
 *
 * @param TargetAddress The address of function or memory address to be hooked
 * @param HookFunction The function that will be called when hook triggered
 * @param ProcessId The process id to translate based on that process's cr3
 * @param SetHookForRead Hook READ Access
 * @param SetHookForWrite Hook WRITE Access
 * @param SetHookForExec Hook EXECUTE Access
 * @param EptHiddenHook2 epthook2 style hook
 * @return BOOLEAN Returns true if the hook was successful or false if there was an error
 */
BOOLEAN
ConfigureEptHook2(PVOID   TargetAddress,
                  PVOID   HookFunction,
                  UINT32  ProcessId,
                  BOOLEAN SetHookForRead,
                  BOOLEAN SetHookForWrite,
                  BOOLEAN SetHookForExec,
                  BOOLEAN EptHiddenHook2)
{
    return EptHook2(TargetAddress, HookFunction, ProcessId, SetHookForRead, SetHookForWrite, SetHookForExec, EptHiddenHook2);
}

/**
 * @brief Change PML EPT state for execution (execute)
 * @detail should be called from VMX-root
 *
 * @param CoreId Current Core ID
 * @param PhysicalAddress Target physical address
 * @param IsUnset Is unsetting bit or setting bit
 *
 * @return BOOLEAN
 */
BOOLEAN
ConfigureEptHookModifyInstructionFetchState(UINT32  CoreId,
                                            PVOID   PhysicalAddress,
                                            BOOLEAN IsUnset)
{
    return EptHookModifyInstructionFetchState(&g_GuestState[CoreId], PhysicalAddress, IsUnset);
}

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
ConfigureEptHookModifyPageReadState(UINT32  CoreId,
                                    PVOID   PhysicalAddress,
                                    BOOLEAN IsUnset)
{
    return EptHookModifyPageReadState(&g_GuestState[CoreId], PhysicalAddress, IsUnset);
}

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
ConfigureEptHookModifyPageWriteState(UINT32  CoreId,
                                     PVOID   PhysicalAddress,
                                     BOOLEAN IsUnset)
{
    return EptHookModifyPageWriteState(&g_GuestState[CoreId], PhysicalAddress, IsUnset);
}

/**
 * @brief routines for enabling EFER syscall hooks on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 * @param SyscallHookType Type of hook
 *
 * @return VOID
 */
VOID
ConfigureEnableEferSyscallHookOnSingleCore(UINT32 TargetCoreId, DEBUGGER_EVENT_SYSCALL_SYSRET_TYPE SyscallHookType)
{
    if (SyscallHookType == DEBUGGER_EVENT_SYSCALL_SYSRET_HANDLE_ALL_UD)
    {
        g_IsUnsafeSyscallOrSysretHandling = TRUE;
    }
    else if (SyscallHookType == DEBUGGER_EVENT_SYSCALL_SYSRET_SAFE_ACCESS_MEMORY)
    {
        g_IsUnsafeSyscallOrSysretHandling = FALSE;
    }

    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformEnableEferSyscallHookOnSingleCore, NULL);
}

/**
 * @brief set external interrupt exiting on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 *
 * @return VOID
 */
VOID
ConfigureSetExternalInterruptExitingOnSingleCore(UINT32 TargetCoreId)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformSetExternalInterruptExitingOnSingleCore, NULL);
}

/**
 * @brief enable RDTSC exiting on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 *
 * @return VOID
 */
VOID
ConfigureEnableRdtscExitingOnSingleCore(UINT32 TargetCoreId)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformEnableRdtscExitingOnSingleCore, NULL);
}

/**
 * @brief enable RDPMC exiting on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 *
 * @return VOID
 */
VOID
ConfigureEnableRdpmcExitingOnSingleCore(UINT32 TargetCoreId)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformEnableRdpmcExitingOnSingleCore, NULL);
}

/**
 * @brief enable mov 2 debug register exiting on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 *
 * @return VOID
 */
VOID
ConfigureEnableMovToDebugRegistersExitingOnSingleCore(UINT32 TargetCoreId)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformEnableMovToDebugRegistersExiting, NULL);
}

/**
 * @brief set exception bitmap on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 * @param BitMask The bit mask of exception bitmap
 *
 * @return VOID
 */
VOID
ConfigureSetExceptionBitmapOnSingleCore(UINT32 TargetCoreId, UINT32 BitMask)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformSetExceptionBitmapOnSingleCore, BitMask);
}

/**
 * @brief enable mov 2 control register on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 * @param BroadcastingOption The optional broadcasting fields
 *
 * @return VOID
 */
VOID
ConfigureEnableMovToControlRegisterExitingOnSingleCore(UINT32 TargetCoreId, DEBUGGER_BROADCASTING_OPTIONS * BroadcastingOption)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformEnableMovToControlRegisterExiting, &BroadcastingOption);
}

/**
 * @brief change the mask of msr bitmaps for write on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 * @param MsrMask The ECX in MSR (mask)
 *
 * @return VOID
 */
VOID
ConfigureChangeMsrBitmapWriteOnSingleCore(UINT32 TargetCoreId, UINT64 MsrMask)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformChangeMsrBitmapWriteOnSingleCore, MsrMask);
}

/**
 * @brief change the mask of msr bitmaps for read on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 * @param MsrMask The ECX in MSR (mask)
 *
 * @return VOID
 */
VOID
ConfigureChangeMsrBitmapReadOnSingleCore(UINT32 TargetCoreId, UINT64 MsrMask)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformChangeMsrBitmapReadOnSingleCore, MsrMask);
}

/**
 * @brief change I/O port bitmap on a single core
 *
 * @param TargetCoreId The target core's ID (to just run on this core)
 * @param Port Target port in I/O bitmap
 *
 * @return VOID
 */
VOID
ConfigureChangeIoBitmapOnSingleCore(UINT32 TargetCoreId, UINT64 Port)
{
    DpcRoutineRunTaskOnSingleCore(TargetCoreId, DpcRoutinePerformChangeIoBitmapOnSingleCore, Port);
}
