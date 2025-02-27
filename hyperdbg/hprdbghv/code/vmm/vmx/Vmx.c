/**
 * @file Vmx.c
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief VMX Instructions and VMX Related Functions
 * @details
 * @version 0.1
 * @date 2020-04-11
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

/**
 * @brief Check whether VMX Feature is supported or not
 *
 * @return BOOLEAN Returns true if vmx is supported or false if it's not supported
 */
BOOLEAN
VmxCheckVmxSupport()
{
    CPUID                         Data              = {0};
    IA32_FEATURE_CONTROL_REGISTER FeatureControlMsr = {0};

    //
    // Gets Processor Info and Feature Bits
    //
    __cpuid((int *)&Data, 1);

    //
    // Check For VMX Bit CPUID.ECX[5]
    //
    if (!_bittest((const LONG *)&Data.ecx, 5))
    {
        //
        // returns FALSE if vmx is not supported
        //
        return FALSE;
    }

    FeatureControlMsr.AsUInt = __readmsr(IA32_FEATURE_CONTROL);

    //
    // Commented because of https://stackoverflow.com/questions/34900224/
    // and https://github.com/HyperDbg/HyperDbg/issues/24
    // the problem is when writing to IA32_FEATURE_CONTROL MSR, the lock bit
    // of this MSR Is not set to 0 on most computers, if the user enabled VT-X
    // from the BIOS the VMXON will be already set so checking lock bit and
    // then writing to EnableVmxon again is not meaningful since its already
    // there
    //

    //
    // if (FeatureControlMsr.Fields.Lock == 0)
    // {
    //     FeatureControlMsr.Fields.Lock        = TRUE;
    //     FeatureControlMsr.Fields.EnableVmxon = TRUE;
    //     __writemsr(IA32_FEATURE_CONTROL, FeatureControlMsr.Flags);
    // }

    if (FeatureControlMsr.EnableVmxOutsideSmx == FALSE)
    {
        LogError("Err, you should enable vt-x from BIOS");
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Check current execution mode (vmx-root and non-root)
 *
 * @return BOOLEAN Returns true if the execution is on vmx-root, otherwise false
 */
BOOLEAN
VmxGetCurrentExecutionMode()
{
    if (g_GuestState)
    {
        ULONG                   CurrentCore    = KeGetCurrentProcessorIndex();
        VIRTUAL_MACHINE_STATE * CurrentVmState = &g_GuestState[CurrentCore];

        return CurrentVmState->IsOnVmxRootMode ? VmxExecutionModeRoot : VmxExecutionModeNonRoot;
    }
    else
    {
        //
        // The structure for guest state is not initialized, thus, we're in VMX non-root
        //
        return VmxExecutionModeNonRoot;
    }
}

/**
 * @brief Check if the VMX is launched or not
 *
 * @return BOOLEAN Returns true if it's launched, otherwise false
 */
BOOLEAN
VmxGetCurrentLaunchState()
{
    ULONG                   CurrentCore    = KeGetCurrentProcessorIndex();
    VIRTUAL_MACHINE_STATE * CurrentVmState = &g_GuestState[CurrentCore];

    return CurrentVmState->HasLaunched;
}

/**
 * @brief Initialize Vmx operation
 *
 * @return BOOLEAN Returns true if vmx initialized successfully
 */
BOOLEAN
VmxInitialize()
{
    ULONG LogicalProcessorsCount;

    //
    // ****** Start Virtualizing Current System ******
    //

    //
    // Initiating EPTP and VMX
    //
    if (!VmxPerformVirtualizationOnAllCores())
    {
        //
        // there was error somewhere in initializing
        //
        return FALSE;
    }

    LogicalProcessorsCount = KeQueryActiveProcessorCount(0);

    for (size_t ProcessorID = 0; ProcessorID < LogicalProcessorsCount; ProcessorID++)
    {
        //
        // *** Launching VM for Test (in the all logical processor) ***
        //

        VIRTUAL_MACHINE_STATE * GuestState = &g_GuestState[ProcessorID];

        //
        // Allocating VMM Stack
        //
        if (!VmxAllocateVmmStack(GuestState))
        {
            //
            // Some error in allocating Vmm Stack
            //
            return FALSE;
        }

        //
        // Allocating MSR Bit
        //
        if (!VmxAllocateMsrBitmap(GuestState))
        {
            //
            // Some error in allocating Msr Bitmaps
            //
            return FALSE;
        }

        //
        // Allocating I/O Bit
        //
        if (!VmxAllocateIoBitmaps(GuestState))
        {
            //
            // Some error in allocating I/O Bitmaps
            //
            return FALSE;
        }
    }

    //
    // Create a bitmap of the MSRs that cause #GP
    //
    g_MsrBitmapInvalidMsrs = VmxAllocateInvalidMsrBimap();

    if (g_MsrBitmapInvalidMsrs == NULL)
    {
        return FALSE;
    }

    //
    // As we want to support more than 32 processor (64 logical-core)
    // we let windows execute our routine for us
    //
    KeGenericCallDpc(DpcRoutineInitializeGuest, 0x0);

    //
    // Check if everything is ok then return true otherwise false
    //
    if (AsmVmxVmcall(VMCALL_TEST, 0x22, 0x333, 0x4444) == STATUS_SUCCESS)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/**
 * @brief Initialize essential VMX Operation tasks
 *
 * @return BOOLEAN Returns true if vmx is successfully initialized
 */
BOOLEAN
VmxPerformVirtualizationOnAllCores()
{
    int       ProcessorCount;
    KAFFINITY AffinityMask;

    if (!VmxCheckVmxSupport())
    {
        LogError("Err, VMX is not supported in this machine");
        return FALSE;
    }

    PAGED_CODE();

    //
    // Allocate	global variable to hold Ept State
    //
    g_EptState = ExAllocatePoolWithTag(NonPagedPool, sizeof(EPT_STATE), POOLTAG);

    if (!g_EptState)
    {
        LogError("Err, insufficient memory");
        return FALSE;
    }

    //
    // Zero memory
    //
    RtlZeroMemory(g_EptState, sizeof(EPT_STATE));

    //
    // Initialize the list of hooked pages detail
    //
    InitializeListHead(&g_EptState->HookedPagesList);

    //
    // Check whether EPT is supported or not
    //
    if (!EptCheckFeatures())
    {
        LogError("Err, your processor doesn't support all EPT features");
        return FALSE;
    }
    else
    {
        //
        // Our processor supports EPT, now let's build MTRR
        //
        LogDebugInfo("Your processor supports all EPT features");

        //
        // Build MTRR Map
        //
        if (!EptBuildMtrrMap())
        {
            LogError("Err, could not build MTRR memory map");
            return FALSE;
        }

        LogDebugInfo("MTRR memory map built successfully");
    }

    //
    // Initialize Pool Manager
    //
    if (!PoolManagerInitialize())
    {
        LogError("Err, could not initialize pool manager");
        return FALSE;
    }

    if (!EptLogicalProcessorInitialize())
    {
        //
        // There were some errors in EptLogicalProcessorInitialize
        //
        return FALSE;
    }

    //
    // Broadcast to run vmx-specific task to vitualize cores
    //
    BroadcastVmxVirtualizationAllCores();

    //
    // Everything is ok, let's return true
    //
    return TRUE;
}

/**
 * @brief Allocates Vmx regions for all logical cores (Vmxon region and Vmcs region)
 *
 * @return BOOLEAN
 */
BOOLEAN
VmxPerformVirtualizationOnSpecificCore()
{
    ULONG                   CurrentProcessorNumber = KeGetCurrentProcessorNumber();
    VIRTUAL_MACHINE_STATE * VCpu                   = &g_GuestState[CurrentProcessorNumber];

    LogDebugInfo("Allocating vmx regions for logical core %d", CurrentProcessorNumber);

    //
    // Enabling VMX Operation
    //
    AsmEnableVmxOperation();

    //
    // Fix Cr4 and Cr0 bits during VMX operation
    //
    VmxFixCr4AndCr0Bits();

    LogDebugInfo("VMX-Operation enabled successfully");

    if (!VmxAllocateVmxonRegion(VCpu))
    {
        LogError("Err, allocating memory for vmxon region was not successfull");
        return FALSE;
    }
    if (!VmxAllocateVmcsRegion(VCpu))
    {
        LogError("Err, allocating memory for vmcs region was not successfull");
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Fix values for cr0 and cr4 bits
 * @details The Cr4 And Cr0 Bits During VMX Operation Preventing Them From Any Change
 * (https://revers.engineering/day-2-entering-vmx-operation/)
 *
 * @return VOID
 */
VOID
VmxFixCr4AndCr0Bits()
{
    CR_FIXED CrFixed = {0};
    CR4      Cr4     = {0};
    CR0      Cr0     = {0};

    //
    // Fix Cr0
    //
    CrFixed.Flags = __readmsr(IA32_VMX_CR0_FIXED0);
    Cr0.AsUInt    = __readcr0();
    Cr0.AsUInt |= CrFixed.Fields.Low;
    CrFixed.Flags = __readmsr(IA32_VMX_CR0_FIXED1);
    Cr0.AsUInt &= CrFixed.Fields.Low;
    __writecr0(Cr0.AsUInt);

    //
    // Fix Cr4
    //
    CrFixed.Flags = __readmsr(IA32_VMX_CR4_FIXED0);
    Cr4.AsUInt    = __readcr4();
    Cr4.AsUInt |= CrFixed.Fields.Low;
    CrFixed.Flags = __readmsr(IA32_VMX_CR4_FIXED1);
    Cr4.AsUInt &= CrFixed.Fields.Low;
    __writecr4(Cr4.AsUInt);
}

/**
 * @brief It can deterministically check whether the caller is on vmx-root mode
 * or not
 *
 * @return BOOLEAN Returns true if current operation mode is vmx-root and false
 * if current operation mode is vmx non-root
 */
BOOLEAN
VmxCheckIsOnVmxRoot()
{
    UINT64 VmcsLink = 0;

    __try
    {
        if (!__vmx_vmread(VMCS_GUEST_VMCS_LINK_POINTER, &VmcsLink))
        {
            if (VmcsLink != 0)
            {
                return TRUE;
            }
        }
    }
    __except (1)
    {
    }

    return FALSE;
}

/**
 * @brief Initialize VMX Operation
 *
 * @param GuestStack Guest stack for the this core (VMCS_GUEST_RSP)
 * @return BOOLEAN This function won't return true as when Vmlaunch is executed the
   rest of the function never executes but returning FALSE is an indication of error
 */
BOOLEAN
VmxVirtualizeCurrentSystem(PVOID GuestStack)
{
    UINT64                  ErrorCode   = 0;
    ULONG                   ProcessorID = KeGetCurrentProcessorNumber();
    VIRTUAL_MACHINE_STATE * VCpu        = &g_GuestState[ProcessorID];

    LogDebugInfo("Virtualizing current system (logical core : 0x%x)", ProcessorID);

    //
    // Clear the VMCS State
    //
    if (!VmxClearVmcsState(VCpu))
    {
        LogError("Err, failed to clear vmcs");
        return FALSE;
    }

    //
    // Load VMCS (Set the Current VMCS)
    //
    if (!VmxLoadVmcs(VCpu))
    {
        LogError("Err, failed to load vmcs");
        return FALSE;
    }

    LogDebugInfo("Setting up VMCS for current logical core");

    VmxSetupVmcs(VCpu, GuestStack);

    LogDebugInfo("Executing VMLAUNCH on logical core %d", ProcessorID);

    //
    // Setting the state to indicate current core is currently virtualized
    //

    VCpu->HasLaunched = TRUE;

    __vmx_vmlaunch();

    //
    // ******** if Vmlaunch succeed will never be here ! ********
    //

    //
    // If failed, then indicate that current core is not currently virtualized
    //
    VCpu->HasLaunched = FALSE;

    //
    // Read error code firstly
    //
    __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &ErrorCode);

    LogError("Err, unable to execute VMLAUNCH, status : 0x%llx", ErrorCode);

    //
    // Then Execute Vmxoff
    //
    __vmx_off();
    LogError("Err, VMXOFF Executed Successfully but it was because of an error");

    return FALSE;
}

/**
 * @brief Broadcast to terminate VMX on all logical cores
 *
 * @return BOOLEAN Returns true if vmxoff successfully executed in vmcall or otherwise
 * returns false
 */
BOOLEAN
VmxTerminate()
{
    NTSTATUS                Status           = STATUS_SUCCESS;
    ULONG                   CurrentCoreIndex = KeGetCurrentProcessorNumber();
    VIRTUAL_MACHINE_STATE * VCpu             = &g_GuestState[CurrentCoreIndex];

    //
    // Execute Vmcall to to turn off vmx from Vmx root mode
    //
    Status = AsmVmxVmcall(VMCALL_VMXOFF, NULL, NULL, NULL);

    if (Status == STATUS_SUCCESS)
    {
        LogDebugInfo("VMX terminated on logical core %d\n", CurrentCoreIndex);

        //
        // Free the destination memory
        //
        MmFreeContiguousMemory(VCpu->VmxonRegionVirtualAddress);
        MmFreeContiguousMemory(VCpu->VmcsRegionVirtualAddress);
        ExFreePoolWithTag(VCpu->VmmStack, POOLTAG);
        ExFreePoolWithTag(VCpu->MsrBitmapVirtualAddress, POOLTAG);
        ExFreePoolWithTag(VCpu->IoBitmapVirtualAddressA, POOLTAG);
        ExFreePoolWithTag(VCpu->IoBitmapVirtualAddressB, POOLTAG);

        return TRUE;
    }

    return FALSE;
}

/**
 * @brief Implementation of VMPTRST instruction
 *
 * @return VOID
 */
VOID
VmxVmptrst()
{
    PHYSICAL_ADDRESS VmcsPhysicalAddr;
    VmcsPhysicalAddr.QuadPart = 0;
    __vmx_vmptrst((unsigned __int64 *)&VmcsPhysicalAddr);

    LogDebugInfo("VMPTRST result : %llx", VmcsPhysicalAddr);
}

/**
 * @brief Clearing Vmcs status using vmclear instruction
 *
 * @param VCpu
 * @return BOOLEAN If vmclear execution was successful it returns true
 * otherwise and if there was error with vmclear then it returns false
 */
_Use_decl_annotations_
BOOLEAN
VmxClearVmcsState(VIRTUAL_MACHINE_STATE * VCpu)
{
    UINT8 VmclearStatus;

    //
    // Clear the state of the VMCS to inactive
    //
    VmclearStatus = __vmx_vmclear(&VCpu->VmcsRegionPhysicalAddress);

    LogDebugInfo("VMCS VMCLEAR status : 0x%x", VmclearStatus);

    if (VmclearStatus)
    {
        //
        // Otherwise terminate the VMX
        //
        LogDebugInfo("VMCS failed to clear, status : 0x%x", VmclearStatus);
        __vmx_off();
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Implementation of VMPTRLD instruction
 *
 * @param VCpu
 * @return BOOLEAN If vmptrld was unsuccessful then it returns false otherwise
 * it returns false
 */
_Use_decl_annotations_
BOOLEAN
VmxLoadVmcs(VIRTUAL_MACHINE_STATE * VCpu)
{
    int VmptrldStatus;

    VmptrldStatus = __vmx_vmptrld(&VCpu->VmcsRegionPhysicalAddress);
    if (VmptrldStatus)
    {
        LogDebugInfo("VMCS failed to load, status : 0x%x", VmptrldStatus);
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Create and Configure a Vmcs Layout
 *
 * @param VCpu
 * @param GuestStack
 * @return BOOLEAN
 */
_Use_decl_annotations_
BOOLEAN
VmxSetupVmcs(VIRTUAL_MACHINE_STATE * VCpu, PVOID GuestStack)
{
    ULONG                   CpuBasedVmExecControls;
    ULONG                   SecondaryProcBasedVmExecControls;
    PVOID                   HostRsp;
    UINT64                  GdtBase         = 0;
    VMX_SEGMENT_SELECTOR    SegmentSelector = {0};
    IA32_VMX_BASIC_REGISTER VmxBasicMsr     = {0};

    //
    // Reading IA32_VMX_BASIC_MSR
    //
    VmxBasicMsr.AsUInt = __readmsr(IA32_VMX_BASIC);

    __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, AsmGetEs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, AsmGetCs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, AsmGetSs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, AsmGetDs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, AsmGetFs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, AsmGetGs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, AsmGetTr() & 0xF8);

    //
    // Setting the link pointer to the required value for 4KB VMCS
    //
    __vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, ~0ULL);

    __vmx_vmwrite(VMCS_GUEST_DEBUGCTL, __readmsr(IA32_DEBUGCTL) & 0xFFFFFFFF);
    __vmx_vmwrite(VMCS_GUEST_DEBUGCTL_HIGH, __readmsr(IA32_DEBUGCTL) >> 32);

    //
    // ******* Time-stamp counter offset *******
    //
    __vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0);

    __vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK, 0);
    __vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, 0);

    __vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT, 0);
    __vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT, 0);

    __vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT, 0);
    __vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0);

    GdtBase = AsmGetGdtBase();

    HvFillGuestSelectorData((PVOID)GdtBase, ES, AsmGetEs());
    HvFillGuestSelectorData((PVOID)GdtBase, CS, AsmGetCs());
    HvFillGuestSelectorData((PVOID)GdtBase, SS, AsmGetSs());
    HvFillGuestSelectorData((PVOID)GdtBase, DS, AsmGetDs());
    HvFillGuestSelectorData((PVOID)GdtBase, FS, AsmGetFs());
    HvFillGuestSelectorData((PVOID)GdtBase, GS, AsmGetGs());
    HvFillGuestSelectorData((PVOID)GdtBase, LDTR, AsmGetLdtr());
    HvFillGuestSelectorData((PVOID)GdtBase, TR, AsmGetTr());

    __vmx_vmwrite(VMCS_GUEST_FS_BASE, __readmsr(IA32_FS_BASE));
    __vmx_vmwrite(VMCS_GUEST_GS_BASE, __readmsr(IA32_GS_BASE));

    CpuBasedVmExecControls = HvAdjustControls(CPU_BASED_ACTIVATE_IO_BITMAP | CPU_BASED_ACTIVATE_MSR_BITMAP | CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
                                              VmxBasicMsr.VmxControls ? IA32_VMX_TRUE_PROCBASED_CTLS : IA32_VMX_PROCBASED_CTLS);

    __vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, CpuBasedVmExecControls);

    LogDebugInfo("CPU Based VM Exec Controls (Based on %s) : 0x%x",
                 VmxBasicMsr.VmxControls ? "IA32_VMX_TRUE_PROCBASED_CTLS" : "IA32_VMX_PROCBASED_CTLS",
                 CpuBasedVmExecControls);

    SecondaryProcBasedVmExecControls = HvAdjustControls(CPU_BASED_CTL2_RDTSCP | CPU_BASED_CTL2_ENABLE_EPT | CPU_BASED_CTL2_ENABLE_INVPCID | CPU_BASED_CTL2_ENABLE_XSAVE_XRSTORS | CPU_BASED_CTL2_ENABLE_VPID,
                                                        IA32_VMX_PROCBASED_CTLS2);

    __vmx_vmwrite(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, SecondaryProcBasedVmExecControls);

    LogDebugInfo("Secondary Proc Based VM Exec Controls (IA32_VMX_PROCBASED_CTLS2) : 0x%x", SecondaryProcBasedVmExecControls);

    __vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, HvAdjustControls(0, VmxBasicMsr.VmxControls ? IA32_VMX_TRUE_PINBASED_CTLS : IA32_VMX_PINBASED_CTLS));

    __vmx_vmwrite(VMCS_CTRL_PRIMARY_VMEXIT_CONTROLS, HvAdjustControls(VM_EXIT_HOST_ADDR_SPACE_SIZE, VmxBasicMsr.VmxControls ? IA32_VMX_TRUE_EXIT_CTLS : IA32_VMX_EXIT_CTLS));

    __vmx_vmwrite(VMCS_CTRL_VMENTRY_CONTROLS, HvAdjustControls(VM_ENTRY_IA32E_MODE, VmxBasicMsr.VmxControls ? IA32_VMX_TRUE_ENTRY_CTLS : IA32_VMX_ENTRY_CTLS));

    __vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0);
    __vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0);

    __vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, 0);
    __vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, 0);

    __vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
    __vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());
    __vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());

    __vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    __vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
    __vmx_vmwrite(VMCS_HOST_CR4, __readcr4());

    //
    // Because we may be executing in an arbitrary user-mode, process as part
    // of the DPC interrupt we execute in We have to save Cr3, for VMCS_HOST_CR3
    //

    __vmx_vmwrite(VMCS_HOST_CR3, LayoutGetSystemDirectoryTableBase());

    __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, AsmGetGdtBase());
    __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, AsmGetIdtBase());

    __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, AsmGetGdtLimit());
    __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, AsmGetIdtLimit());

    __vmx_vmwrite(VMCS_GUEST_RFLAGS, AsmGetRflags());

    __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));

    VmxGetSegmentDescriptor((PUCHAR)AsmGetGdtBase(), AsmGetTr(), &SegmentSelector);
    __vmx_vmwrite(VMCS_HOST_TR_BASE, SegmentSelector.Base);

    __vmx_vmwrite(VMCS_HOST_FS_BASE, __readmsr(IA32_FS_BASE));
    __vmx_vmwrite(VMCS_HOST_GS_BASE, __readmsr(IA32_GS_BASE));

    __vmx_vmwrite(VMCS_HOST_GDTR_BASE, AsmGetGdtBase());
    __vmx_vmwrite(VMCS_HOST_IDTR_BASE, AsmGetIdtBase());

    __vmx_vmwrite(VMCS_HOST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));

    //
    // Set MSR Bitmaps
    //
    __vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, VCpu->MsrBitmapPhysicalAddress);

    //
    // Set I/O Bitmaps
    //
    __vmx_vmwrite(VMCS_CTRL_IO_BITMAP_A_ADDRESS, VCpu->IoBitmapPhysicalAddressA);
    __vmx_vmwrite(VMCS_CTRL_IO_BITMAP_B_ADDRESS, VCpu->IoBitmapPhysicalAddressB);

    //
    // Set up EPT
    //
    __vmx_vmwrite(VMCS_CTRL_EPT_POINTER, g_EptState->EptPointer.AsUInt);

    //
    // Set up VPID

    //
    // For all processors, we will use a VPID = 1. This allows the processor to separate caching
    //  of EPT structures away from the regular OS page translation tables in the TLB.
    //
    __vmx_vmwrite(VIRTUAL_PROCESSOR_ID, VPID_TAG);

    //
    // setup guest rsp
    //
    __vmx_vmwrite(VMCS_GUEST_RSP, (UINT64)GuestStack);

    //
    // setup guest rip
    //
    __vmx_vmwrite(VMCS_GUEST_RIP, (UINT64)AsmVmxRestoreState);

    //
    // Stack should be aligned to 16 because we wanna save XMM and FPU registers and those instructions
    // needs alignment to 16
    //
    HostRsp = (UINT64)VCpu->VmmStack + VMM_STACK_SIZE - 1;
    HostRsp = ((PVOID)((ULONG_PTR)(HostRsp) & ~(16 - 1)));
    __vmx_vmwrite(VMCS_HOST_RSP, HostRsp);
    __vmx_vmwrite(VMCS_HOST_RIP, (UINT64)AsmVmexitHandler);

    return TRUE;
}

/**
 * @brief Resume VM using VMRESUME instruction
 *
 * @return VOID
 */
VOID
VmxVmresume()
{
    UINT64 ErrorCode = 0;

    __vmx_vmresume();

    //
    // if VMRESUME succeed will never be here !
    //

    __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &ErrorCode);
    __vmx_off();

    //
    // It's such a bad error because we don't where to go !
    // prefer to break
    //
    LogError("Err,  in executing VMRESUME , status : 0x%llx", ErrorCode);
}

/**
 * @brief VMFUNC instruction
 * @details Should be executed in VMX NON-root
 *
 * @param EptpIndex
 * @param Function
 *
 * @return UINT64
 */
UINT64
VmxVmfunc(UINT32 EptpIndex, UINT32 Function)
{
    //
    // *** To be executed in VMX non-root ***
    //

    //
    // Description from : https://users.cs.utah.edu/~aburtsev/lls-sem/index.php?n=Main.VMFUNCNotes
    //
    // VMFUNC is a new Intel primitive that allows to change an EPT page table underneath a VT-x VM without exiting into the hypervisor
    // Effectively, it's a page table switch in hardware and thus it allows one to build a fast context switch
    //

    //
    // Each VT-x virtual machine is configured with a Virtual Machine Control Structure (VMCS)
    //  This is a page of memory in which the VMM writes configuration data for things like how interrupts are handled,
    //  initial control register values during guest entry, and a whole bunch of other things
    //
    // One of those other things is a pointer to a page of candidate EPT pointers. These are pointers to different EPT
    //  page table hierarchies, each one giving a possibly different physical -> machine mapping. The VMM sets up this page
    //  of EPT pointers and has to also turn on a couple other settings in the VMCS to fully enable EPT switching via VMFUNC
    //
    // In non-root operation (inside a VM) code running in any privilege level can switch EPT hierarchies through the following
    //  steps:
    //
    // Storing 0 in %rax (EPT switching is VMFUNC 0)
    // Storing the index into the candidate EPT table in %rcx
    // Invoking the VMFUNC instruction
    // The processor will switch EPTs. Invoking VMFUNC will not cause a VM Exit
    //
    // All of this is detailed in the Intel SDM Volume 3, 25.5.5.3 "EPT Switching"
    //
    // It is worth noting that this will not change/save values in control registers (e.g. %cr3), general purpose registers,
    //  and so on. It's up to the code and VMM to set things up so it all works gracefully
    //

    //
    // Are TLBs flushed ? No, unless VPIDs are not being used(which I, Charlie, would say is rare)
    // See Intel SDM Volume 3, 25.5.5.3 "EPT Switching" and 28.3.3.1 "Operations that Invalidate Cached Mappings"
    //

    return AsmVmfunc(EptpIndex, Function);
}

/**
 * @brief Prepare and execute Vmxoff instruction
 * @param VCpu The virtual processor's state
 *
 * @return VOID
 */
VOID
VmxVmxoff(VIRTUAL_MACHINE_STATE * VCpu)
{
    UINT64 GuestRSP              = 0; // Save a pointer to guest rsp for times that we want to return to previous guest stateS
    UINT64 GuestRIP              = 0; // Save a pointer to guest rip for times that we want to return to previous guest state
    UINT64 GuestCr3              = 0;
    UINT64 ExitInstructionLength = 0;

    //
    // According to SimpleVisor :
    //  	Our callback routine may have interrupted an arbitrary user process,
    //  	and therefore not a thread running with a system-wide page directory.
    //  	Therefore if we return back to the original caller after turning off
    //  	VMX, it will keep our current "host" CR3 value which we set on entry
    //  	to the PML4 of the SYSTEM process. We want to return back with the
    //  	correct value of the "guest" CR3, so that the currently executing
    //  	process continues to run with its expected address space mappings.
    //

    __vmx_vmread(VMCS_GUEST_CR3, &GuestCr3);
    __writecr3(GuestCr3);

    //
    // Read guest rsp and rip
    //
    __vmx_vmread(VMCS_GUEST_RIP, &GuestRIP);
    __vmx_vmread(VMCS_GUEST_RSP, &GuestRSP);

    //
    // Read instruction length
    //
    __vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH, &ExitInstructionLength);
    GuestRIP += ExitInstructionLength;

    //
    // Set the previous register states
    //
    VCpu->VmxoffState.GuestRip = GuestRIP;
    VCpu->VmxoffState.GuestRsp = GuestRSP;

    //
    // Notify the Vmexit handler that VMX already turned off
    //
    VCpu->VmxoffState.IsVmxoffExecuted = TRUE;

    //
    // Restore the previous FS, GS , GDTR and IDTR register as patchguard might find the modified
    //
    HvRestoreRegisters();

    //
    // Before using vmxoff, you first need to use vmclear on any VMCSes that you want to be able to use again.
    // See sections 24.1 and 24.11 of the SDM.
    //
    VmxClearVmcsState(VCpu);

    //
    // Execute Vmxoff
    //
    __vmx_off();

    //
    // Indicate the current core is not currently virtualized
    //
    VCpu->HasLaunched = FALSE;

    //
    // Now that VMX is OFF, we have to unset vmx-enable bit on cr4
    //
    __writecr4(__readcr4() & (~X86_CR4_VMXE));
}

/**
 * @brief Get the RIP of guest (VMCS_GUEST_RIP) in the case of return from VMXOFF
 *
 * @return UINT64 Returns the stack pointer, to change in the case of Vmxoff
 */
UINT64
VmxReturnStackPointerForVmxoff()
{
    return g_GuestState[KeGetCurrentProcessorNumber()].VmxoffState.GuestRsp;
}

/**
 * @brief Get the RIP of guest (VMCS_GUEST_RIP) in the case of return from VMXOFF
 *
 * @return UINT64 Returns the instruction pointer, to change in the case of Vmxoff
 */
UINT64
VmxReturnInstructionPointerForVmxoff()
{
    return g_GuestState[KeGetCurrentProcessorNumber()].VmxoffState.GuestRip;
}

/**
 * @brief Terminate Vmx on all logical cores
 *
 * @return VOID
 */
VOID
VmxPerformTermination()
{
    LogDebugInfo("Terminating VMX ...\n");

    //
    // ******* Terminating Vmx *******
    //

    //
    // Unhide (disable and de-allocate) transparent-mode
    //
    TransparentUnhideDebugger();

    //
    // Remove All the hooks if any
    //
    EptHookUnHookAll();

    //
    // Broadcast to terminate Vmx
    //
    KeGenericCallDpc(DpcRoutineTerminateGuest, 0x0);

    //
    // ****** De-allocatee global variables ******
    //

    //
    // Free the buffer related to MSRs that cause #GP
    //
    ExFreePoolWithTag(g_MsrBitmapInvalidMsrs, POOLTAG);
    g_MsrBitmapInvalidMsrs = NULL;

    //
    // Free Identity Page Table
    //
    MmFreeContiguousMemory(g_EptState->EptPageTable);

    //
    // Free Identity Page Table for MBEC hooks
    //
    if (g_EptState->ModeBasedEptPageTable != NULL)
    {
        MmFreeContiguousMemory(g_EptState->ModeBasedEptPageTable);
    }

    //
    // Free Identity Page Table for execute-only hooks
    //
    if (g_EptState->ExecuteOnlyEptPageTable != NULL)
    {
        MmFreeContiguousMemory(g_EptState->ExecuteOnlyEptPageTable);
    }

    //
    // Free EptState
    //
    ExFreePoolWithTag(g_EptState, POOLTAG);
    g_EptState = NULL;

    //
    // Free the Pool manager
    //
    PoolManagerUninitialize();

    //
    // Uninitialize memory mapper
    //
    MemoryMapperUninitialize();

    //
    // Free g_GuestState
    //
    GlobalGuestStateFreeMemory();

    LogDebugInfo("VMX operation turned off successfully");
}

/**
 * @brief implementation of vmx-root mode compatible strlen
 * @param S
 *
 * @return UINT32 If 0x0 indicates an error, otherwise length of the
 * string
 */
UINT32
VmxCompatibleStrlen(const CHAR * S)
{
    CHAR     Temp  = NULL;
    UINT32   Count = 0;
    UINT64   AlignedAddress;
    CR3_TYPE GuestCr3;
    CR3_TYPE OriginalCr3;

    AlignedAddress = (UINT64)PAGE_ALIGN((UINT64)S);

    //
    // Find the current process cr3
    //
    GuestCr3.Flags = LayoutGetCurrentProcessCr3().Flags;

    //
    // Move to new cr3
    //
    OriginalCr3.Flags = __readcr3();
    __writecr3(GuestCr3.Flags);

    //
    // First check
    //
    if (!CheckAccessValidityAndSafety(AlignedAddress, sizeof(CHAR)))
    {
        //
        // Error
        //

        //
        // Move back to original cr3
        //
        __writecr3(OriginalCr3.Flags);
        return 0;
    }

    while (TRUE)
    {
        /*
        Temp = *S;
        */
        MemoryMapperReadMemorySafe(S, &Temp, sizeof(CHAR));

        if (Temp != '\0')
        {
            Count++;
            S++;
        }
        else
        {
            //
            // Move back to original cr3
            //
            __writecr3(OriginalCr3.Flags);
            return Count;
        }

        if (!((UINT64)S & (PAGE_SIZE - 1)))
        {
            if (!CheckAccessValidityAndSafety((UINT64)S, sizeof(CHAR)))
            {
                //
                // Error
                //

                //
                // Move back to original cr3
                //
                __writecr3(OriginalCr3.Flags);
                return 0;
            }
        }
    }

    //
    // Move back to original cr3
    //
    __writecr3(OriginalCr3.Flags);
}

/**
 * @brief implementation of vmx-root mode compatible wcslen
 * @param S
 *
 * @return UINT32 If 0x0 indicates an error, otherwise length of the
 * string
 */
UINT32
VmxCompatibleWcslen(const wchar_t * S)
{
    wchar_t  Temp  = NULL;
    UINT32   Count = 0;
    UINT64   AlignedAddress;
    CR3_TYPE GuestCr3;
    CR3_TYPE OriginalCr3;

    AlignedAddress = (UINT64)PAGE_ALIGN((UINT64)S);

    //
    // Find the current process cr3
    //
    GuestCr3.Flags = LayoutGetCurrentProcessCr3().Flags;

    //
    // Move to new cr3
    //
    OriginalCr3.Flags = __readcr3();
    __writecr3(GuestCr3.Flags);

    AlignedAddress = (UINT64)PAGE_ALIGN((UINT64)S);

    //
    // First check
    //
    if (!CheckAccessValidityAndSafety(AlignedAddress, sizeof(wchar_t)))
    {
        //
        // Error
        //

        //
        // Move back to original cr3
        //
        __writecr3(OriginalCr3.Flags);
        return 0;
    }

    while (TRUE)
    {
        /*
        Temp = *S;
        */
        MemoryMapperReadMemorySafe(S, &Temp, sizeof(wchar_t));

        if (Temp != '\0\0')
        {
            Count++;
            S++;
        }
        else
        {
            //
            // Move back to original cr3
            //
            __writecr3(OriginalCr3.Flags);
            return Count;
        }

        if (!((UINT64)S & (PAGE_SIZE - 1)))
        {
            if (!CheckAccessValidityAndSafety((UINT64)S, sizeof(wchar_t)))
            {
                //
                // Error
                //

                //
                // Move back to original cr3
                //
                __writecr3(OriginalCr3.Flags);
                return 0;
            }
        }
    }

    //
    // Move back to original cr3
    //
    __writecr3(OriginalCr3.Flags);
}

/**
 * @brief Get Segment Descriptor
 *
 * @param SegmentSelector
 * @param Selector
 * @param GdtBase
 * @return BOOLEAN
 */
_Use_decl_annotations_
BOOLEAN
VmxGetSegmentDescriptor(PUCHAR GdtBase, UINT16 Selector, PVMX_SEGMENT_SELECTOR SegmentSelector)
{
    SEGMENT_DESCRIPTOR_32 * DescriptorTable32;
    SEGMENT_DESCRIPTOR_32 * Descriptor32;
    SEGMENT_SELECTOR        SegSelector = {.AsUInt = Selector};

    if (!SegmentSelector)
        return FALSE;

#define SELECTOR_TABLE_LDT 0x1
#define SELECTOR_TABLE_GDT 0x0

    //
    // Ignore LDT
    //
    if ((Selector == 0x0) || (SegSelector.Table != SELECTOR_TABLE_GDT))
    {
        return FALSE;
    }

    DescriptorTable32 = (SEGMENT_DESCRIPTOR_32 *)(GdtBase);
    Descriptor32      = &DescriptorTable32[SegSelector.Index];

    SegmentSelector->Selector = Selector;
    SegmentSelector->Limit    = __segmentlimit(Selector);
    SegmentSelector->Base     = (Descriptor32->BaseAddressLow | Descriptor32->BaseAddressMiddle << 16 | Descriptor32->BaseAddressHigh << 24);

    SegmentSelector->Attributes.AsUInt = (AsmGetAccessRights(Selector) >> 8);

    if (SegSelector.Table == 0 && SegSelector.Index == 0)
    {
        SegmentSelector->Attributes.Unusable = TRUE;
    }

    if ((Descriptor32->Type == SEGMENT_DESCRIPTOR_TYPE_TSS_BUSY) || (Descriptor32->Type == SEGMENT_DESCRIPTOR_TYPE_CALL_GATE))
    {
        //
        // this is a TSS or callgate etc, save the base high part
        //

        UINT64 SegmentLimitHigh;
        SegmentLimitHigh      = (*(UINT64 *)((PUCHAR)Descriptor32 + 8));
        SegmentSelector->Base = (SegmentSelector->Base & 0xffffffff) | (SegmentLimitHigh << 32);
    }

    if (SegmentSelector->Attributes.Granularity)
    {
        //
        // 4096-bit granularity is enabled for this segment, scale the limit
        //
        SegmentSelector->Limit = (SegmentSelector->Limit << 12) + 0xfff;
    }

    return TRUE;
}
