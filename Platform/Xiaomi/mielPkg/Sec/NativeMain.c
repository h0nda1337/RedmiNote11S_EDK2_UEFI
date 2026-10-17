/** @file
  Original MIEL SEC startup using generic EDK2 core services.
  Experimental: build/inspection only until a physical session validates it.
  SPDX-License-Identifier: BSD-2-Clause
**/
#include <PiPei.h>
#include <Pi/PiBootMode.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <Guid/MielBootInfo.h>
#include <MielSecEntry.h>
#include <MielNativeProfile.h>
#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/MielDebugLog.h>
#include <Library/HobLib.h>
#include <Library/PrePiLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/MielFdtLib.h>
#include <Library/MielFrameBufferLib.h>
#include <Library/MielMemoryPlanLib.h>
#include <Library/MielLabReset.h>

VOID EFIAPI ProcessLibraryConstructorList (VOID);

typedef struct {
  CONST MIEL_SEC_ENTRY_CONTEXT *Entry;
  MIEL_BOOT_HANDOFF Handoff;
  MIEL_EARLY_MEMORY_PLAN Plan;
  UINT32 PsciVersion;
} BOOT_STATE;

STATIC BOOLEAN mResetReady;
STATIC BOOLEAN mMarkerReady;
STATIC MIEL_FB_CONTEXT mMarker;
STATIC UINT64 mStopHoldTicks;
STATIC UINT64 mMarkerVramBytes;

STATIC UINT64 BigNumber (CONST UINT8 *P, UINTN Bytes)
{
  UINT64 Value = 0;
  for (UINTN I = 0; I < Bytes; I++) Value = (Value << 8) | P[I];
  return Value;
}

STATIC VOID Stage (UINTN Number, EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color)
{
  if (mMarkerReady)
    MielFbBlt (&mMarker, &Color, sizeof (Color), EfiBltVideoFill,
               0, 0, 0, 160 + Number * 32, MIEL_NATIVE_FB_WIDTH, 24, 0);
}

STATIC VOID Stop (VOID)
{
  Stage (3, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0, 0, 255, 0});
  if (mMarkerReady && mStopHoldTicks != 0) {
    UINT64 Start = ArmReadCntPct ();
    while (ArmReadCntPct () - Start < mStopHoldTicks) CpuPause ();
  }
  if (mResetReady) {
    MielSelectBootloaderReset ();
    ArmCallSmc0 (ARM_SMC_ID_PSCI_SYSTEM_RESET, NULL, NULL, NULL);
  }
  CpuDeadLoop ();
}

STATIC VOID EntryProbe (CONST MIEL_SEC_ENTRY_CONTEXT *Entry)
{
  // Each build retains only its selected marker. Packaging rejects other modes.
  if (FeaturePcdGet (PcdNativeMmuProbe)) {
    STATIC CONST volatile CHAR8 Identity[] = "MIEL_SEC_MMU_PROBE_ONLY_V1";
    if (Identity[0] != 'M') Stop ();
  } else {
    STATIC CONST volatile CHAR8 Identity[] = "MIEL_SEC_PROBE_ONLY_V1";
    if (Identity[0] != 'M') Stop ();
  }
  if (Entry->CounterFrequency > 1000000000ULL) Stop ();
  // Fastboot's scanned-out buffer may differ from the normal boot logo's.
  // Mark each complete surface inside the LK-declared VRAM; the observer's
  // color identifies the actual scanned-out buffer without display MMIO guesses.
  CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL Colors[3] = {
    {0, 255, 0, 0}, {255, 0, 0, 0}, {255, 0, 255, 0}
  };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL White = {255, 255, 255, 0};
  MIEL_FB_LAYOUT Layout = {MIEL_NATIVE_FB_WIDTH, MIEL_NATIVE_FB_HEIGHT, MIEL_NATIVE_FB_STRIDE,
                           MIEL_NATIVE_FB_RED_SHIFT, MIEL_NATIVE_FB_BLUE_SHIFT, TRUE};
  UINT64 Surface = (UINT64)MIEL_NATIVE_FB_STRIDE * MIEL_NATIVE_FB_HEIGHT;
  for (UINTN Index = 0; Index < 3; Index++) {
    UINT64 Offset = Index * (UINT64)MIEL_NATIVE_FB_BUFFER_BYTES;
    MIEL_FB_CONTEXT Probe;
    if (Offset > mMarkerVramBytes || Surface > mMarkerVramBytes - Offset) continue;
    if (!MielFbInitialize (&Probe, (VOID *)(UINTN)(MIEL_NATIVE_FB_RESERVATION_BASE + Offset),
                           (UINTN)Surface, &Layout)) Stop ();
    MielFbBlt (&Probe, (VOID *)&Colors[Index], sizeof (Colors[Index]), EfiBltVideoFill,
               0, 0, 0, 160, MIEL_NATIVE_FB_WIDTH, 24, 0);
    MielFbBlt (&Probe, &White, sizeof (White), EfiBltVideoFill,
               0, 0, 0, 192, MIEL_NATIVE_FB_WIDTH, 24, 0);
  }
  Stage (FeaturePcdGet (PcdNativeMmuProbe) ? 2 : 1,
         (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){255, 255, 255, 0});
  UINT64 Start = ArmReadCntPct ();
  UINT64 Ticks = Entry->CounterFrequency * 10;
  while (ArmReadCntPct () - Start < Ticks) CpuPause ();
  // This request is a test of PSCI, not a proven fallback for a CPU exception.
  MielSelectBootloaderReset ();
  ArmCallSmc0 (ARM_SMC_ID_PSCI_SYSTEM_RESET, NULL, NULL, NULL);
  Stop ();
  for (;;) CpuDeadLoop ();
}

STATIC BOOLEAN RegisterMatches (CONST MIEL_FDT_VIEW *View, CONST CHAR8 *Path,
                                UINT64 Base, UINT64 MinimumSize, UINTN Offset)
{
  CONST VOID *Value;
  UINT32 Bytes;
  if (MielFdtProperty (View, Path, "reg", &Value, &Bytes) != EFI_SUCCESS ||
      Offset > Bytes || Bytes - Offset < 16) return FALSE;
  CONST UINT8 *Reg = (CONST UINT8 *)Value + Offset;
  return (BOOLEAN)(BigNumber (Reg, 8) == Base && BigNumber (Reg + 8, 8) >= MinimumSize);
}

STATIC BOOLEAN StartBootloaderMarker (CONST MIEL_FDT_VIEW *View)
{
  CONST VOID *Value;
  UINT32 Bytes;
  EFI_STATUS Status = MielFdtProperty (View, "/chosen", "atag,videolfb", &Value, &Bytes);
  if (Status == EFI_NOT_FOUND)
    Status = MielFdtProperty (View, "/chosen@0", "atag,videolfb", &Value, &Bytes);
  if (Status != EFI_SUCCESS || Bytes < 20) return FALSE;
  CONST UINT8 *Tag = Value;
  UINT64 Base = 0, Size = 0, Found = 0;
  for (UINTN I = 0; I < 8; I++) Base |= (UINT64)Tag[I] << (I * 8);
  for (UINTN I = 0; I < 4; I++) {
    Found |= (UINT64)Tag[8 + I] << (I * 8);
    Size |= (UINT64)Tag[16 + I] << (I * 8);
  }
  // The current LK handoff must declare the exact VRAM allocation independently
  // observed in both bootloader captures. Only that display allocation is touched.
  // This short probe allocates no heap and does not claim a complete RAM map.
  if (!Found || Base != MIEL_NATIVE_FB_RESERVATION_BASE || Size > MIEL_NATIVE_FB_RESERVATION_BYTES)
    return FALSE;
  UINT64 Offset = MIEL_NATIVE_FB_BASE - Base;
  UINT64 Surface = (UINT64)MIEL_NATIVE_FB_STRIDE * MIEL_NATIVE_FB_HEIGHT;
  if (Offset > Size || Surface > Size - Offset) return FALSE;
  MIEL_FB_LAYOUT Layout = {MIEL_NATIVE_FB_WIDTH, MIEL_NATIVE_FB_HEIGHT, MIEL_NATIVE_FB_STRIDE,
                           MIEL_NATIVE_FB_RED_SHIFT, MIEL_NATIVE_FB_BLUE_SHIFT, TRUE};
  mMarkerVramBytes = Size;
  return MielFbInitialize (&mMarker, (VOID *)(UINTN)MIEL_NATIVE_FB_BASE, (UINTN)Surface, &Layout);
}

STATIC VOID MapRegion (ARM_MEMORY_REGION_DESCRIPTOR *Map, UINTN *Count,
                       MIEL_MEMORY_RANGE R, ARM_MEMORY_REGION_ATTRIBUTES Attributes)
{
  Map[*Count].PhysicalBase = R.Base;
  Map[*Count].VirtualBase = R.Base;
  Map[*Count].Length = R.Length;
  Map[*Count].Attributes = Attributes;
  (*Count)++;
}

STATIC VOID EFIAPI ContinueBoot (VOID *Context, VOID *Unused)
{
  BOOT_STATE *State = Context;
  CONST MIEL_EARLY_MEMORY_PLAN *Plan = &State->Plan;
  UINT64 StackBase = Plan->Pool.Base + Plan->Pool.Length - MIEL_NATIVE_STACK_BYTES;
  EFI_HOB_HANDOFF_INFO_TABLE *Hobs;
  EFI_RESOURCE_ATTRIBUTE_TYPE Attributes = EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED | EFI_RESOURCE_ATTRIBUTE_TESTED |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE | EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE;
  ARM_MEMORY_REGION_DESCRIPTOR Map[MIEL_MAX_BOOT_RESERVATIONS + 8];
  UINTN MapCount = 0, Index;
  EFI_STATUS Status;
  MIEL_BOOT_INFO Info;
  (VOID)Unused;
  Hobs = HobConstructor ((VOID *)(UINTN)Plan->Pool.Base, (UINTN)Plan->Pool.Length,
                         (VOID *)(UINTN)Plan->Pool.Base, (VOID *)(UINTN)MIEL_NATIVE_DEBUG_BASE);
  if (Hobs == NULL || PrePeiSetHobList (Hobs) != EFI_SUCCESS) Stop ();
  BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, Attributes, Plan->Pool.Base, Plan->Pool.Length);
  for (Index = 0; Index < Plan->ProtectedCount; Index++) {
    MIEL_MEMORY_RANGE R = Plan->Protected[Index];
    EFI_MEMORY_TYPE Type = Plan->ProtectedType[Index];
    BuildResourceDescriptorHob (Type == EfiReservedMemoryType ? EFI_RESOURCE_MEMORY_RESERVED :
                                EFI_RESOURCE_SYSTEM_MEMORY, Attributes, R.Base, R.Length);
    if (Type != EfiReservedMemoryType) BuildMemoryAllocationHob (R.Base, R.Length, Type);
  }
  BuildStackHob (StackBase, MIEL_NATIVE_STACK_BYTES);
  BuildMemoryAllocationHob (MIEL_NATIVE_DEBUG_BASE, 4096, EfiBootServicesData);
  BuildCpuHob ((UINT8)ArmGetPhysicalAddressBits (), 0);
  SetBootMode (BOOT_WITH_FULL_CONFIGURATION);
  BuildFvHob (MIEL_NATIVE_FV_BASE, MIEL_NATIVE_FV_BYTES);
  ProcessLibraryConstructorList ();
  ZeroMem (Map, sizeof (Map));
  MapRegion (Map, &MapCount, Plan->Image, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK);
  MapRegion (Map, &MapCount, Plan->Pool, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK);
  for (Index = 0; Index < Plan->ProtectedCount; Index++)
    if (Plan->ProtectedType[Index] == EfiBootServicesData)
      MapRegion (Map, &MapCount, Plan->Protected[Index], ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK);
  MapRegion (Map, &MapCount, State->Handoff.NativeFramebuffer, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED);
  MapRegion (Map, &MapCount, (MIEL_MEMORY_RANGE){MIEL_NATIVE_GICD_BASE, 0x40000}, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE);
  MapRegion (Map, &MapCount, (MIEL_MEMORY_RANGE){MIEL_NATIVE_GICR_BASE, 0x200000}, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE);
  MapRegion (Map, &MapCount, (MIEL_MEMORY_RANGE){MIEL_NATIVE_WDT_BASE, 0x1000}, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE);
  if (State->Entry->ExceptionLevel == 2) {
    ArmWriteHcr ((1ULL << 31) | (1ULL << 27));
    ArmWriteCntHctl (3);
  }
  Status = ArmConfigureMmu (Map, NULL, NULL);
  if (Status != EFI_SUCCESS) Stop ();
  ArmEnableInstructionCache (); ArmEnableDataCache ();
  if ((ArmReadSctlr () & 0x1005U) != 0x1005U) Stop ();
  Stage (1, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){255, 0, 0, 0});
  // Observe HOB construction, stack switch and MMU before attempting DXE.
  if (FeaturePcdGet (PcdNativeMmuProbe)) EntryProbe (State->Entry);
  ZeroMem (&Info, sizeof (Info));
  Info.Signature = MIEL_BOOT_INFO_SIGNATURE; Info.Revision = MIEL_BOOT_INFO_REVISION;
  Info.InitialEl = State->Entry->ExceptionLevel; Info.CounterFrequency = State->Entry->CounterFrequency;
  Info.FramebufferBase = MIEL_NATIVE_FB_BASE;
  Info.FramebufferBytes = (UINT64)MIEL_NATIVE_FB_STRIDE * MIEL_NATIVE_FB_HEIGHT;
  Info.Width = MIEL_NATIVE_FB_WIDTH; Info.Height = MIEL_NATIVE_FB_HEIGHT; Info.Stride = MIEL_NATIVE_FB_STRIDE;
  Info.RedShift = MIEL_NATIVE_FB_RED_SHIFT;
  Info.BlueShift = MIEL_NATIVE_FB_BLUE_SHIFT; Info.OpaqueAlpha = TRUE;
  Info.InitialSctlr = State->Entry->Sctlr; Info.PsciVersion = State->PsciVersion;
  Info.Flags = MIEL_BOOT_MEMORY_READY | MIEL_BOOT_EXPECTED_DISPLAY_PROFILE | MIEL_BOOT_WATCHDOG_STOPPED;
  if (BuildGuidDataHob (&gMielBootInfoHobGuid, &Info, sizeof (Info)) == NULL) Stop ();
  Stage (2, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){255, 255, 255, 0});
  LoadDxeCoreFromFv (NULL, 0);
  Stop ();
}

VOID EFIAPI MielSecMain (IN CONST MIEL_SEC_ENTRY_CONTEXT *Entry)
{
  // Linked core routines may use SIMD even before the memory map exists.
  ArmEnableVFP ();
  BOOT_STATE State;
  MIEL_FDT_VIEW View;
  CONST VOID *Value;
  UINT32 Bytes;
  ARM_SMC_ARGS Args;
  UINT64 Fdt = Entry->Fdt;
  EFI_STATUS Status;
  if (FeaturePcdGet (PcdNativeEntryProbe) && FeaturePcdGet (PcdNativeMmuProbe)) Stop ();
  if ((Entry->ExceptionLevel != 1 && Entry->ExceptionLevel != 2) || Entry->CounterFrequency == 0 ||
      Fdt < MIEL_NATIVE_DRAM_BASE || Fdt > MIEL_NATIVE_DRAM_END - 40 || (Fdt & 7)) Stop ();
  if (Entry->CounterFrequency > 1000000000ULL) Stop ();
  mStopHoldTicks = Entry->CounterFrequency * 30;
  UINT64 Total = BigNumber ((CONST UINT8 *)(UINTN)Fdt + 4, 4);
  if (Total > 2 * 1024 * 1024 || Total > MIEL_NATIVE_DRAM_END - Fdt ||
      !MielFdtOpen ((CONST VOID *)(UINTN)Fdt, (UINTN)Total, &View)) Stop ();
  if (MielFdtProperty (&View, "/", "compatible", &Value, &Bytes) != EFI_SUCCESS ||
      Bytes != sizeof ("mediatek,MT6781") || CompareMem (Value, "mediatek,MT6781", Bytes)) Stop ();
  // Validate the declared PSCI binding before making the first SMC call.
  if (MielFdtProperty (&View, "/psci", "compatible", &Value, &Bytes) != EFI_SUCCESS ||
      Bytes != sizeof ("arm,psci-1.0") ||
      (CompareMem (Value, "arm,psci-1.0", Bytes) && CompareMem (Value, "arm,psci-0.2", Bytes))) Stop ();
  if (MielFdtProperty (&View, "/psci", "method", &Value, &Bytes) != EFI_SUCCESS ||
      Bytes != 4 || CompareMem (Value, "smc", 4)) Stop ();
  ZeroMem (&Args, sizeof (Args)); Args.Arg0 = ARM_SMC_ID_PSCI_VERSION; ArmCallSmc (&Args);
  State.PsciVersion = (UINT32)Args.Arg0;
  if ((State.PsciVersion & 0x80000000U) || State.PsciVersion < 2) Stop ();
  if (!RegisterMatches (&View, "/toprgu@10007000", MIEL_NATIVE_WDT_BASE, 0x1000, 0)) Stop ();
  mResetReady = TRUE;
  // MT6781 watchdog mode key and enable bit; driver reference documented separately.
  volatile UINT32 *Mode = (volatile UINT32 *)(UINTN)MIEL_NATIVE_WDT_BASE;
  *Mode = ((*Mode & 0x00ffffffU) & ~1U) | 0x22000000U;
  MemoryFence ();
  if (*Mode & 1U) Stop ();
  if (!StartBootloaderMarker (&View)) Stop ();
  mMarkerReady = TRUE;
  Stage (0, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0, 255, 0, 0});
  if (FeaturePcdGet (PcdNativeEntryProbe)) EntryProbe (Entry);
  if (!MielDebugStartEarly ()) Stop ();
  DEBUG ((DEBUG_INFO, "EL %u FDT %Lx PSCI %x\n", (UINT32)Entry->ExceptionLevel, Fdt, State.PsciVersion));
  if (!RegisterMatches (&View, "/interrupt-controller", MIEL_NATIVE_GICD_BASE, 0x40000, 0) ||
      !RegisterMatches (&View, "/interrupt-controller", MIEL_NATIVE_GICR_BASE, 0x200000, 16)) Stop ();
  DEBUG ((DEBUG_INFO, "GIC DECLARATIONS OK\n"));
  State.Entry = Entry;
  Status = MielInspectHandoff ((CONST VOID *)(UINTN)Fdt, (UINTN)Total, Fdt, &State.Handoff);
  DEBUG ((DEBUG_INFO, "HANDOFF: %r\n", Status));
  if (Status != EFI_SUCCESS) Stop ();
  DEBUG ((DEBUG_INFO, "RAM %u RESERVED %u DYNAMIC %u UNRESOLVED %u\n", (UINT32)State.Handoff.RamCount,
          (UINT32)State.Handoff.ReservedCount, State.Handoff.DynamicReservationCount,
          State.Handoff.UnresolvedReservationCount));
  for (UINTN I = 0; I < State.Handoff.RamCount; I++)
    DEBUG ((DEBUG_INFO, "RAM %u: %Lx + %Lx\n", (UINT32)I, State.Handoff.Ram[I].Base, State.Handoff.Ram[I].Length));
  Stage (1, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){255, 0, 0, 0});
  Status = MielPlanEarlyMemory (&State.Handoff,
    (MIEL_MEMORY_RANGE){MIEL_NATIVE_FD_BASE, MIEL_NATIVE_FD_BYTES},
    (MIEL_MEMORY_RANGE){MIEL_NATIVE_POOL_BASE, MIEL_NATIVE_POOL_BYTES}, &State.Plan);
  DEBUG ((DEBUG_INFO, "MEMORY PLAN: %r\n", Status));
  if (Status != EFI_SUCCESS || !State.Handoff.NativePanelFound ||
      !State.Handoff.NativeFramebufferCoveredByReservation ||
      State.Handoff.NativeFramebuffer.Base != MIEL_NATIVE_FB_RESERVATION_BASE) Stop ();
  MIEL_FB_LAYOUT Layout = {MIEL_NATIVE_FB_WIDTH, MIEL_NATIVE_FB_HEIGHT, MIEL_NATIVE_FB_STRIDE,
                           MIEL_NATIVE_FB_RED_SHIFT, MIEL_NATIVE_FB_BLUE_SHIFT, TRUE};
  // LK's active overlay is the second buffer, not the allocation's first byte.
  // Only access it after proving that the complete visible surface is reserved.
  UINT64 VisibleBytes = (UINT64)MIEL_NATIVE_FB_STRIDE * MIEL_NATIVE_FB_HEIGHT;
  UINT64 VisibleOffset = MIEL_NATIVE_FB_BASE - MIEL_NATIVE_FB_RESERVATION_BASE;
  if (VisibleOffset > State.Handoff.NativeFramebuffer.Length ||
      VisibleBytes > State.Handoff.NativeFramebuffer.Length - VisibleOffset) Stop ();
  if (!MielFbInitialize (&mMarker, (VOID *)(UINTN)MIEL_NATIVE_FB_BASE,
                         (UINTN)VisibleBytes, &Layout)) Stop ();
  mMarkerReady = TRUE;
  Stage (0, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0, 255, 0, 0});
  if (!MielDebugStart ()) Stop ();
  DEBUG ((DEBUG_INFO, "SEC handoff validated; EL %u, counter %Lu\n",
          (UINT32)Entry->ExceptionLevel, Entry->CounterFrequency));
  SwitchStack (ContinueBoot, &State, NULL,
               (VOID *)(UINTN)(State.Plan.Pool.Base + State.Plan.Pool.Length));
  Stop ();
}
