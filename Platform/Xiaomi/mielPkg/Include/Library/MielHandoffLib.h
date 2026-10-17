/** @file
  Original read-only interpretation of the MT6781 boot handoff.
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef MIEL_HANDOFF_LIB_H
#define MIEL_HANDOFF_LIB_H

#include <Uefi.h>
#include <Library/MielPlatformLib.h>

#define MIEL_MAX_RAM_BANKS 8
#define MIEL_MAX_BOOT_RESERVATIONS 64
#define MIEL_RESERVATION_NO_MAP 1U
#define MIEL_RESERVATION_MEMRESERVE 2U
#define MIEL_RESERVATION_BOOT_DATA 4U

typedef struct {
  MIEL_MEMORY_RANGE Ram[MIEL_MAX_RAM_BANKS];
  MIEL_MEMORY_RANGE Reserved[MIEL_MAX_BOOT_RESERVATIONS];
  UINT32 ReservedFlags[MIEL_MAX_BOOT_RESERVATIONS];
  UINTN RamCount;
  UINTN ReservedCount;
  UINT32 DynamicReservationCount;
  UINT32 UnresolvedReservationCount;
  UINT32 EmptyReservationNodeCount;
  BOOLEAN FdtPhysicalAddressRecorded;
  BOOLEAN PsciSmcDeclared;
  BOOLEAN NativeFramebufferPresent;
  BOOLEAN NativePanelFound;
  BOOLEAN NativeFramebufferCoveredByReservation;
  BOOLEAN SplitFramebufferPresent;
  BOOLEAN FramebufferDeclarationsDisagree;
  MIEL_MEMORY_RANGE NativeFramebuffer;
  MIEL_MEMORY_RANGE SplitFramebuffer;
} MIEL_BOOT_HANDOFF;

/**
  Interpret MT6781 RAM, fixed/dynamic reservations, initrd, framebuffer tags and
  declared PSCI interface without accessing hardware or allocating any memory.

  PhysicalFdtBase zero denotes offline inspection of a file. A nonzero value
  is checked against declared RAM and protects that range from later allocation.
  Dynamic and unresolved reservations are counted, not silently allocated or
  discarded. The result is not a complete UEFI memory map or proof of a live
  display/PSCI call. Failure leaves Handoff unchanged.
**/
EFI_STATUS EFIAPI MielInspectHandoff (
  IN CONST VOID *Fdt, IN UINTN Bytes, IN UINT64 PhysicalFdtBase,
  OUT MIEL_BOOT_HANDOFF *Handoff
  );

#endif
